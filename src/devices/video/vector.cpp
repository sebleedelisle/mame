// license:BSD-3-Clause
// copyright-holders:Brad Oliver,Aaron Giles,Bernd Wiebelt,Allard van der Bas
/******************************************************************************
 *
 * vector.cpp
 *
 *        anti-alias code by Andrew Caldwell
 *        (still more to add)
 *
 * Vector Team
 *
 *        Brad Oliver
 *        Aaron Giles
 *        Bernd Wiebelt
 *        Allard van der Bas
 *        Al Kossow (VECSIM)
 *        Hedley Rainnie (VECSIM)
 *        Eric Smith (VECSIM)
 *        Neil Bradley (technical advice)
 *        Andrew Caldwell (anti-aliasing)
 *
 **************************************************************************** */

#include "emu.h"
#include "vector.h"

#include "emuopts.h"
#include "render.h"
#include "screen.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif


#define VECTOR_WIDTH_DENOM 512

// 20000 is needed for mhavoc (see MT 06668) 10000 is enough for other games
#define MAX_POINTS 20000

namespace {

constexpr u32 LASER_IPC_MAGIC = 0x8d4c5a21;
constexpr u16 LASER_IPC_VERSION = 1;
constexpr u16 LASER_IPC_TYPE_VECTOR_FRAME = 1;
constexpr u32 LASER_IPC_HEADER_SIZE = 32;
constexpr u32 LASER_VECTOR_RECORD_SIZE = 24;
constexpr u64 LASER_IPC_DEFAULT_KEY = 0x6c61736572495043ULL;
constexpr u64 LASER_IPC_HEADER_MASK_NONCE = 0x9e3779b97f4a7c15ULL;
constexpr u64 LASER_IPC_VECTOR_MASK_NONCE = 0xc2b2ae3d27d4eb4fULL;

void append_u8(std::vector<u8> &data, u8 value)
{
	data.push_back(value);
}

void append_u16_le(std::vector<u8> &data, u16 value)
{
	data.push_back(u8(value & 0xff));
	data.push_back(u8((value >> 8) & 0xff));
}

void append_u32_le(std::vector<u8> &data, u32 value)
{
	data.push_back(u8(value & 0xff));
	data.push_back(u8((value >> 8) & 0xff));
	data.push_back(u8((value >> 16) & 0xff));
	data.push_back(u8((value >> 24) & 0xff));
}

void append_float_le(std::vector<u8> &data, float value)
{
	u32 bits;
	std::memcpy(&bits, &value, sizeof(bits));
	append_u32_le(data, bits);
}

u32 checksum_bytes(const std::vector<u8> &data)
{
	u32 hash = 2166136261U;
	for (u8 byte : data)
	{
		hash ^= byte;
		hash *= 16777619U;
	}
	return hash;
}

u64 parse_ipc_key()
{
	char const *const text = std::getenv("LASER_MAME_IPC_KEY");
	if (!text || !*text)
		return LASER_IPC_DEFAULT_KEY;

	char *end = nullptr;
	u64 const parsed = std::strtoull(text, &end, 16);
	return (end != text) ? parsed : LASER_IPC_DEFAULT_KEY;
}

u64 next_mask_state(u64 state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

void mask_bytes(std::vector<u8> &data, u64 key, u64 nonce)
{
	u64 state = key ^ nonce;
	if (!state)
		state = LASER_IPC_DEFAULT_KEY ^ nonce;

	for (u8 &byte : data)
	{
		state = next_mask_state(state);
		byte ^= u8(state >> 24);
	}
}

} // anonymous namespace

class laser_vector_pipe_sender
{
public:
	laser_vector_pipe_sender(vector_device &device)
		: m_device(device)
		, m_key(parse_ipc_key())
	{
		if (!open_pipe())
			return;

		m_payload.reserve(64 * 1024);
		m_header.reserve(LASER_IPC_HEADER_SIZE);
		m_enabled = true;
		osd_printf_verbose("[%s] Laser vector pipe output enabled.\n", m_device.tag());
	}

	~laser_vector_pipe_sender()
	{
		close_pipe();
	}

	bool enabled() const
	{
		return m_enabled;
	}

	void begin_frame(float physical_aspect)
	{
		m_payload.clear();
		m_record_count = 0;
		m_physical_aspect = std::clamp(physical_aspect, 0.01f, 100.0f);
	}

	void line(float x0, float y0, float x1, float y1, rgb_t color, int intensity)
	{
		if (!m_enabled)
			return;

		append_float_le(m_payload, x0);
		append_float_le(m_payload, y0);
		append_float_le(m_payload, x1);
		append_float_le(m_payload, y1);
		append_u32_le(m_payload, color & 0x00ffffff);
		append_u8(m_payload, u8(std::clamp(intensity, 0, 255)));
		append_u8(m_payload, 0);
		append_u16_le(m_payload, 0);
		m_record_count++;
	}

	void end_frame()
	{
		if (!m_enabled)
			return;

		if (m_payload.size() > std::numeric_limits<u32>::max())
		{
			disable_after_error("Laser vector frame is too large; disabling vector pipe output.");
			return;
		}

		m_header.clear();
		append_u32_le(m_header, LASER_IPC_MAGIC);
		append_u16_le(m_header, LASER_IPC_VERSION);
		append_u16_le(m_header, LASER_IPC_TYPE_VECTOR_FRAME);
		append_u32_le(m_header, m_frame_number);
		append_u32_le(m_header, u32(m_payload.size()));
		append_u32_le(m_header, m_record_count);
		append_u32_le(m_header, checksum_bytes(m_payload));
		append_float_le(m_header, m_physical_aspect);
		append_u32_le(m_header, 0);

		std::vector<u8> masked_payload = m_payload;
		mask_bytes(m_header, m_key, LASER_IPC_HEADER_MASK_NONCE);
		mask_bytes(masked_payload, m_key, LASER_IPC_VECTOR_MASK_NONCE ^ m_frame_number);

		if (!write_all(m_header.data(), m_header.size()) ||
				(!masked_payload.empty() && !write_all(masked_payload.data(), masked_payload.size())))
		{
			disable_after_error("Laser vector pipe write failed; disabling vector pipe output.");
			return;
		}

		m_frame_number++;
	}

private:
	void disable_after_error(char const *message)
	{
		if (!m_error_logged)
		{
			osd_printf_error("[%s] %s\n", m_device.tag(), message);
			m_error_logged = true;
		}
		m_enabled = false;
	}

	bool open_pipe()
	{
#if defined(_WIN32)
		char const *const value = std::getenv("LASER_MAME_VECTOR_HANDLE");
		if (!value || !*value)
			return false;

		char *end = nullptr;
		unsigned long long const parsed = std::strtoull(value, &end, 16);
		if ((end == value) || !parsed)
			return false;

		m_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parsed));
		return (m_handle != nullptr) && (m_handle != INVALID_HANDLE_VALUE);
#else
		char const *const value = std::getenv("LASER_MAME_VECTOR_FD");
		if (!value || !*value)
			return false;

		char *end = nullptr;
		long const parsed = std::strtol(value, &end, 10);
		if ((end == value) || (parsed < 0) || (parsed > std::numeric_limits<int>::max()))
			return false;

		std::signal(SIGPIPE, SIG_IGN);
		m_fd = int(parsed);
		return true;
#endif
	}

	void close_pipe()
	{
#if defined(_WIN32)
		if ((m_handle != nullptr) && (m_handle != INVALID_HANDLE_VALUE))
		{
			CloseHandle(m_handle);
			m_handle = INVALID_HANDLE_VALUE;
		}
#else
		if (m_fd >= 0)
		{
			::close(m_fd);
			m_fd = -1;
		}
#endif
	}

	bool write_all(u8 const *data, size_t size)
	{
#if defined(_WIN32)
		while (size > 0)
		{
			DWORD const chunk = DWORD(std::min<size_t>(size, std::numeric_limits<DWORD>::max()));
			DWORD written = 0;
			if (!WriteFile(m_handle, data, chunk, &written, nullptr) || !written)
				return false;
			data += written;
			size -= written;
		}
		return true;
#else
		while (size > 0)
		{
			size_t const chunk = std::min<size_t>(size, size_t(std::numeric_limits<ssize_t>::max()));
			ssize_t const written = ::write(m_fd, data, chunk);
			if (written < 0)
			{
				if (errno == EINTR)
					continue;
				return false;
			}
			if (!written)
				return false;
			data += written;
			size -= size_t(written);
		}
		return true;
#endif
	}

	vector_device &m_device;
	std::vector<u8> m_payload;
	std::vector<u8> m_header;
	u64 m_key;
	u32 m_frame_number = 0;
	u32 m_record_count = 0;
	float m_physical_aspect = 1.0f;
	bool m_enabled = false;
	bool m_error_logged = false;
#if defined(_WIN32)
	HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
	int m_fd = -1;
#endif
};


float vector_options::s_flicker = 0.0f;
float vector_options::s_beam_width_min = 0.0f;
float vector_options::s_beam_width_max = 0.0f;
float vector_options::s_beam_dot_size = 0.0f;
float vector_options::s_beam_intensity_weight = 0.0f;

void vector_options::init(emu_options &options)
{
	s_beam_width_min = options.beam_width_min();
	s_beam_width_max = options.beam_width_max();
	s_beam_dot_size = options.beam_dot_size();
	s_beam_intensity_weight = options.beam_intensity_weight();
	s_flicker = options.flicker();
}

// device type definition
DEFINE_DEVICE_TYPE(VECTOR, vector_device, "vector_device", "VECTOR")

vector_device::vector_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
		: device_t(mconfig, VECTOR, tag, owner, clock),
			device_video_interface(mconfig, *this),
			m_vector_list(nullptr),
			m_laser_vector_pipe(nullptr),
			m_min_intensity(255),
			m_max_intensity(0)
{
}

vector_device::~vector_device() = default;

void vector_device::device_start()
{
	vector_options::init(machine().options());

	m_vector_index = 0;

	/* allocate memory for tables */
	m_vector_list = std::make_unique<point[]>(MAX_POINTS);

	m_laser_vector_pipe = std::make_unique<laser_vector_pipe_sender>(*this);
}

void vector_device::device_stop()
{
	m_laser_vector_pipe.reset();
}


//-------------------------------------------------
//  subscribe for frame-begin notifications
//-------------------------------------------------

util::notifier_subscription vector_device::add_frame_begin_notifier(frame_begin_delegate &&n)
{
	return m_frame_begin_notifier.subscribe(std::move(n));
}


//-------------------------------------------------
//  subscribe for frame-end notifications
//-------------------------------------------------

util::notifier_subscription vector_device::add_frame_end_notifier(frame_end_delegate &&n)
{
	return m_frame_end_notifier.subscribe(std::move(n));
}


//-------------------------------------------------
//  subscribe for hidden-move notifications
//-------------------------------------------------

util::notifier_subscription vector_device::add_move_notifier(move_delegate &&n)
{
	return m_move_notifier.subscribe(std::move(n));
}


//-------------------------------------------------
//  subscribe for visible-line notifications
//-------------------------------------------------

util::notifier_subscription vector_device::add_line_notifier(line_delegate &&n)
{
	return m_line_notifier.subscribe(std::move(n));
}


//-------------------------------------------------
// www.dinodini.wordpress.com/2010/04/05/normalized-tunable-sigmoid-functions/
//-------------------------------------------------

float vector_device::normalized_sigmoid(float n, float k)
{
	// valid for n and k in range of -1.0 and 1.0
	return (n - n * k) / (k - fabs(n) * 2.0f * k + 1.0f);
}


//-------------------------------------------------
// Adds a line end point to the vertices list. The vector processor emulation
// needs to call this.
//-------------------------------------------------

void vector_device::add_point(int x, int y, rgb_t color, int intensity)
{
	point *newpoint;

	intensity = std::clamp(intensity, 0, 255);

	m_min_intensity = intensity > 0 ? std::min(m_min_intensity, intensity) : m_min_intensity;
	m_max_intensity = intensity > 0 ? std::max(m_max_intensity, intensity) : m_max_intensity;

	if (vector_options::s_flicker && (intensity > 0))
	{
		float random = float(machine().rand() & 255) / 255.0f; // random value between 0.0 and 1.0

		intensity -= int(intensity * random * vector_options::s_flicker);

		intensity = std::clamp(intensity, 0, 255);
	}

	newpoint = &m_vector_list[m_vector_index];
	newpoint->x = x;
	newpoint->y = y;
	newpoint->col = color;
	newpoint->intensity = intensity;

	m_vector_index++;
	if (m_vector_index >= MAX_POINTS)
	{
		m_vector_index--;
		logerror("*** Warning! Vector list overflow!\n");
	}
}


//-------------------------------------------------
// The vector CPU creates a new display list. We save the old display list,
// but only once per refresh.
//-------------------------------------------------

void vector_device::clear_list()
{
	m_vector_index = 0;
}

//-------------------------------------------------
// Update the screen container with queued vectors.
//-------------------------------------------------

uint32_t vector_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	uint32_t flags = PRIMFLAG_ANTIALIAS(1) | PRIMFLAG_BLENDMODE(BLENDMODE_ADD) | PRIMFLAG_VECTOR(1);
	const rectangle &visarea = screen.visible_area();
	float xscale = 1.0f / (65536 * visarea.width());
	float yscale = 1.0f / (65536 * visarea.height());
	float xoffs = (float)visarea.min_x;
	float yoffs = (float)visarea.min_y;

	point *curpoint;
	int lastx = 0;
	int lasty = 0;

	curpoint = m_vector_list.get();

	screen.container().empty();
	screen.container().add_rect(0.0f, 0.0f, 1.0f, 1.0f, rgb_t(0xff,0x00,0x00,0x00), PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA) | PRIMFLAG_VECTORBUF(1));

	if (m_laser_vector_pipe && m_laser_vector_pipe->enabled())
	{
		const std::pair<unsigned, unsigned> physical_aspect = screen.physical_aspect();
		m_laser_vector_pipe->begin_frame(float(physical_aspect.first) / float(physical_aspect.second));
	}

	m_frame_begin_notifier();

	for (int i = 0; i < m_vector_index; i++)
	{
		render_bounds coords;

		float intensity = (float)curpoint->intensity / 255.0f;
		float intensity_weight = normalized_sigmoid(intensity, vector_options::s_beam_intensity_weight);

		// check for static intensity
		float beam_width = m_min_intensity == m_max_intensity
			? vector_options::s_beam_width_min
			: vector_options::s_beam_width_min + intensity_weight * (vector_options::s_beam_width_max - vector_options::s_beam_width_min);

		// normalize width
		beam_width *= 1.0f / (float)VECTOR_WIDTH_DENOM;

		// apply point scale for points
		if (lastx == curpoint->x && lasty == curpoint->y)
			beam_width *= vector_options::s_beam_dot_size;

		coords.x0 = (float(lastx) - xoffs) * xscale;
		coords.y0 = (float(lasty) - yoffs) * yscale;
		coords.x1 = (float(curpoint->x) - xoffs) * xscale;
		coords.y1 = (float(curpoint->y) - yoffs) * yscale;

		if (curpoint->intensity != 0)
		{
			screen.container().add_line(
					coords.x0, coords.y0, coords.x1, coords.y1,
					beam_width,
					(curpoint->intensity << 24) | (curpoint->col & 0xffffff),
					flags);
			if (m_laser_vector_pipe && m_laser_vector_pipe->enabled())
				m_laser_vector_pipe->line(coords.x0, coords.y0, coords.x1, coords.y1, curpoint->col, curpoint->intensity);
			m_line_notifier(lastx, lasty, curpoint->x, curpoint->y, curpoint->col, curpoint->intensity, visarea.width(), visarea.height());
		}
		else
		{
			m_move_notifier(curpoint->x, curpoint->y, curpoint->col, visarea.width(), visarea.height());
		}

		lastx = curpoint->x;
		lasty = curpoint->y;

		curpoint++;
	}

	m_frame_end_notifier();

	if (m_laser_vector_pipe && m_laser_vector_pipe->enabled())
		m_laser_vector_pipe->end_frame();

	return 0;
}
