// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  input_laser.cpp - private pipe input for Laser Mame
//
//============================================================

#include "input_module.h"
#include "modules/osdmodule.h"

#include "input_common.h"

#include "emu.h"
#include "interface/inputseq.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr int k_laser_axis_count = INPUT_MAX_AXIS;
constexpr int k_laser_relative_axis_count = 2;
constexpr uint32_t k_laser_ipc_magic = 0x8d4c5a21;
constexpr uint16_t k_laser_ipc_version = 1;
constexpr uint16_t k_laser_ipc_type_control = 2;
constexpr size_t k_laser_ipc_header_size = 32;
constexpr size_t k_laser_control_record_size = 24;
constexpr uint64_t k_laser_ipc_default_key = 0x6c61736572495043ULL;
constexpr uint64_t k_laser_ipc_header_mask_nonce = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t k_laser_ipc_control_mask_nonce = 0x165667b19e3779f9ULL;
constexpr uint8_t k_laser_control_key = 1;
constexpr uint8_t k_laser_control_axis = 2;
constexpr uint8_t k_laser_control_relative = 3;

std::array<std::uint8_t, ITEM_ID_MAXIMUM> s_keyboard_state = { };
std::array<std::int32_t, k_laser_axis_count> s_joystick_axis_state = { };
std::array<std::int32_t, k_laser_relative_axis_count> s_joystick_relative_axis_state = { };
std::array<std::int32_t, k_laser_relative_axis_count> s_joystick_relative_axis_pending = { };

struct laser_key_entry
{
	input_item_id item;
	char const *name;
};

constexpr laser_key_entry s_laser_keys[] =
{
	{ ITEM_ID_A, "A" },
	{ ITEM_ID_B, "B" },
	{ ITEM_ID_C, "C" },
	{ ITEM_ID_D, "D" },
	{ ITEM_ID_E, "E" },
	{ ITEM_ID_F, "F" },
	{ ITEM_ID_G, "G" },
	{ ITEM_ID_H, "H" },
	{ ITEM_ID_I, "I" },
	{ ITEM_ID_J, "J" },
	{ ITEM_ID_K, "K" },
	{ ITEM_ID_L, "L" },
	{ ITEM_ID_M, "M" },
	{ ITEM_ID_N, "N" },
	{ ITEM_ID_O, "O" },
	{ ITEM_ID_P, "P" },
	{ ITEM_ID_Q, "Q" },
	{ ITEM_ID_R, "R" },
	{ ITEM_ID_S, "S" },
	{ ITEM_ID_T, "T" },
	{ ITEM_ID_U, "U" },
	{ ITEM_ID_V, "V" },
	{ ITEM_ID_W, "W" },
	{ ITEM_ID_X, "X" },
	{ ITEM_ID_Y, "Y" },
	{ ITEM_ID_Z, "Z" },
	{ ITEM_ID_0, "0" },
	{ ITEM_ID_1, "1" },
	{ ITEM_ID_2, "2" },
	{ ITEM_ID_3, "3" },
	{ ITEM_ID_4, "4" },
	{ ITEM_ID_5, "5" },
	{ ITEM_ID_6, "6" },
	{ ITEM_ID_7, "7" },
	{ ITEM_ID_8, "8" },
	{ ITEM_ID_9, "9" },
	{ ITEM_ID_0_PAD, "KP 0" },
	{ ITEM_ID_1_PAD, "KP 1" },
	{ ITEM_ID_2_PAD, "KP 2" },
	{ ITEM_ID_3_PAD, "KP 3" },
	{ ITEM_ID_4_PAD, "KP 4" },
	{ ITEM_ID_5_PAD, "KP 5" },
	{ ITEM_ID_6_PAD, "KP 6" },
	{ ITEM_ID_7_PAD, "KP 7" },
	{ ITEM_ID_8_PAD, "KP 8" },
	{ ITEM_ID_9_PAD, "KP 9" },
	{ ITEM_ID_ESC, "ESCAPE" },
	{ ITEM_ID_TAB, "TAB" },
	{ ITEM_ID_ENTER, "RETURN" },
	{ ITEM_ID_SPACE, "SPACE" },
	{ ITEM_ID_BACKSPACE, "BACKSPACE" },
	{ ITEM_ID_DEL, "DELETE" },
	{ ITEM_ID_MINUS, "MINUS" },
	{ ITEM_ID_EQUALS, "EQUALS" },
	{ ITEM_ID_OPENBRACE, "LEFT BRACKET" },
	{ ITEM_ID_CLOSEBRACE, "RIGHT BRACKET" },
	{ ITEM_ID_BACKSLASH, "BACKSLASH" },
	{ ITEM_ID_COLON, "SEMICOLON" },
	{ ITEM_ID_QUOTE, "QUOTE" },
	{ ITEM_ID_TILDE, "GRAVE" },
	{ ITEM_ID_COMMA, "COMMA" },
	{ ITEM_ID_STOP, "PERIOD" },
	{ ITEM_ID_SLASH, "SLASH" },
	{ ITEM_ID_LSHIFT, "LEFT SHIFT" },
	{ ITEM_ID_RSHIFT, "RIGHT SHIFT" },
	{ ITEM_ID_LCONTROL, "LEFT CONTROL" },
	{ ITEM_ID_RCONTROL, "RIGHT CONTROL" },
	{ ITEM_ID_LALT, "LEFT OPTION" },
	{ ITEM_ID_RALT, "RIGHT OPTION" },
	{ ITEM_ID_F1, "F1" },
	{ ITEM_ID_F2, "F2" },
	{ ITEM_ID_F3, "F3" },
	{ ITEM_ID_F4, "F4" },
	{ ITEM_ID_F5, "F5" },
	{ ITEM_ID_F6, "F6" },
	{ ITEM_ID_F7, "F7" },
	{ ITEM_ID_F8, "F8" },
	{ ITEM_ID_F9, "F9" },
	{ ITEM_ID_F10, "F10" },
	{ ITEM_ID_F11, "F11" },
	{ ITEM_ID_F12, "F12" },
	{ ITEM_ID_INSERT, "INSERT" },
	{ ITEM_ID_HOME, "HOME" },
	{ ITEM_ID_END, "END" },
	{ ITEM_ID_PGUP, "PAGE UP" },
	{ ITEM_ID_PGDN, "PAGE DOWN" },
	{ ITEM_ID_UP, "UP" },
	{ ITEM_ID_DOWN, "DOWN" },
	{ ITEM_ID_LEFT, "LEFT" },
	{ ITEM_ID_RIGHT, "RIGHT" },
	{ ITEM_ID_INVALID, nullptr }
};

struct laser_key_alias
{
	char const *code;
	input_item_id item;
};

constexpr laser_key_alias s_laser_key_aliases[] =
{
	{ "ESC", ITEM_ID_ESC },
	{ "ESCAPE", ITEM_ID_ESC },
	{ "TAB", ITEM_ID_TAB },
	{ "ENTER", ITEM_ID_ENTER },
	{ "RETURN", ITEM_ID_ENTER },
	{ "SPACE", ITEM_ID_SPACE },
	{ "BACKSPACE", ITEM_ID_BACKSPACE },
	{ "DELETE", ITEM_ID_DEL },
	{ "MINUS", ITEM_ID_MINUS },
	{ "EQUALS", ITEM_ID_EQUALS },
	{ "COMMA", ITEM_ID_COMMA },
	{ "STOP", ITEM_ID_STOP },
	{ "PERIOD", ITEM_ID_STOP },
	{ "SLASH", ITEM_ID_SLASH },
	{ "0PAD", ITEM_ID_0_PAD },
	{ "1PAD", ITEM_ID_1_PAD },
	{ "2PAD", ITEM_ID_2_PAD },
	{ "3PAD", ITEM_ID_3_PAD },
	{ "4PAD", ITEM_ID_4_PAD },
	{ "5PAD", ITEM_ID_5_PAD },
	{ "6PAD", ITEM_ID_6_PAD },
	{ "7PAD", ITEM_ID_7_PAD },
	{ "8PAD", ITEM_ID_8_PAD },
	{ "9PAD", ITEM_ID_9_PAD },
	{ "KP0", ITEM_ID_0_PAD },
	{ "KP1", ITEM_ID_1_PAD },
	{ "KP2", ITEM_ID_2_PAD },
	{ "KP3", ITEM_ID_3_PAD },
	{ "KP4", ITEM_ID_4_PAD },
	{ "KP5", ITEM_ID_5_PAD },
	{ "KP6", ITEM_ID_6_PAD },
	{ "KP7", ITEM_ID_7_PAD },
	{ "KP8", ITEM_ID_8_PAD },
	{ "KP9", ITEM_ID_9_PAD },
	{ "LEFT", ITEM_ID_LEFT },
	{ "RIGHT", ITEM_ID_RIGHT },
	{ "UP", ITEM_ID_UP },
	{ "DOWN", ITEM_ID_DOWN },
	{ "LSHIFT", ITEM_ID_LSHIFT },
	{ "RSHIFT", ITEM_ID_RSHIFT },
	{ "LCONTROL", ITEM_ID_LCONTROL },
	{ "RCONTROL", ITEM_ID_RCONTROL },
	{ "LALT", ITEM_ID_LALT },
	{ "RALT", ITEM_ID_RALT },
	{ "LOPTION", ITEM_ID_LALT },
	{ "ROPTION", ITEM_ID_RALT },
	{ "F1", ITEM_ID_F1 },
	{ "F2", ITEM_ID_F2 },
	{ "F3", ITEM_ID_F3 },
	{ "F4", ITEM_ID_F4 },
	{ "F5", ITEM_ID_F5 },
	{ "F6", ITEM_ID_F6 },
	{ "F7", ITEM_ID_F7 },
	{ "F8", ITEM_ID_F8 },
	{ "F9", ITEM_ID_F9 },
	{ "F10", ITEM_ID_F10 },
	{ "F11", ITEM_ID_F11 },
	{ "F12", ITEM_ID_F12 },
	{ nullptr, ITEM_ID_INVALID }
};

bool laser_input_debug()
{
	char const *const env = std::getenv("LASER_MAME_INPUT_DEBUG");
	return env && *env && std::strcmp(env, "0");
}

uint16_t read_u16_le(uint8_t const *data)
{
	return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

uint32_t read_u32_le(uint8_t const *data)
{
	return uint32_t(data[0]) |
			(uint32_t(data[1]) << 8) |
			(uint32_t(data[2]) << 16) |
			(uint32_t(data[3]) << 24);
}

int32_t read_s32_le(uint8_t const *data)
{
	return int32_t(read_u32_le(data));
}

uint32_t checksum_bytes(uint8_t const *data, size_t size)
{
	uint32_t hash = 2166136261U;
	for (size_t index = 0; index < size; index++)
	{
		hash ^= data[index];
		hash *= 16777619U;
	}
	return hash;
}

uint64_t parse_ipc_key()
{
	char const *const text = std::getenv("LASER_MAME_IPC_KEY");
	if (!text || !*text)
		return k_laser_ipc_default_key;

	char *end = nullptr;
	uint64_t const parsed = std::strtoull(text, &end, 16);
	return (end != text) ? parsed : k_laser_ipc_default_key;
}

uint64_t next_mask_state(uint64_t state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

void mask_bytes(uint8_t *data, size_t size, uint64_t key, uint64_t nonce)
{
	uint64_t state = key ^ nonce;
	if (!state)
		state = k_laser_ipc_default_key ^ nonce;

	for (size_t index = 0; index < size; index++)
	{
		state = next_mask_state(state);
		data[index] ^= uint8_t(state >> 24);
	}
}

input_code laser_joystick_code(input_item_class itemclass, input_item_modifier modifier, input_item_id item)
{
	return input_code(DEVICE_CLASS_JOYSTICK, 0, itemclass, modifier, item);
}

input_item_id laser_key_to_item(char const *code)
{
	if (!code || !*code)
		return ITEM_ID_INVALID;

	if (!code[1])
	{
		if ((code[0] >= 'A') && (code[0] <= 'Z'))
			return input_item_id(ITEM_ID_A + (code[0] - 'A'));
		else if ((code[0] >= 'a') && (code[0] <= 'z'))
			return input_item_id(ITEM_ID_A + (code[0] - 'a'));
		else if ((code[0] >= '0') && (code[0] <= '9'))
			return input_item_id(ITEM_ID_0 + (code[0] - '0'));
	}

	for (laser_key_alias const *alias = s_laser_key_aliases; alias->code; alias++)
		if (!std::strcmp(code, alias->code))
			return alias->item;

	return ITEM_ID_INVALID;
}

void set_key_state(char const *code, int pressed)
{
	input_item_id const item = laser_key_to_item(code);
	if ((item > ITEM_ID_INVALID) && (item < ITEM_ID_MAXIMUM))
		s_keyboard_state[item] = pressed ? 0x80 : 0x00;
}

int laser_axis_to_index(char const *code)
{
	if (!code || !*code)
		return -1;

	struct axis_alias
	{
		char const *code;
		int axis;
	};

	constexpr axis_alias aliases[] =
	{
		{ "X", 0 },
		{ "Y", 1 },
		{ "Z", 2 },
		{ "RX", 3 },
		{ "RY", 4 },
		{ "RZ", 5 },
		{ "U", 3 },
		{ "V", 4 },
		{ "W", 5 },
		{ "SL1", 6 },
		{ "SL2", 7 },
		{ "SLIDER1", 6 },
		{ "SLIDER2", 7 },
		{ nullptr, -1 }
	};

	for (axis_alias const *alias = aliases; alias->code; alias++)
		if (!std::strcmp(code, alias->code))
			return alias->axis;

	return -1;
}

void set_axis_state(char const *code, int value)
{
	int const axis = laser_axis_to_index(code);
	if ((axis >= 0) && (axis < int(s_joystick_axis_state.size())))
	{
		s_joystick_axis_state[axis] = std::clamp(
				value,
				int(osd::input_device::ABSOLUTE_MIN),
				int(osd::input_device::ABSOLUTE_MAX));
	}
}

int laser_relative_axis_to_index(char const *code)
{
	if (!code || !*code)
		return -1;

	if (!std::strcmp(code, "X"))
		return 0;
	if (!std::strcmp(code, "Y"))
		return 1;

	return -1;
}

void add_relative_axis_delta(char const *code, int delta)
{
	int const axis = laser_relative_axis_to_index(code);
	if ((axis >= 0) && (axis < int(s_joystick_relative_axis_pending.size())))
		s_joystick_relative_axis_pending[axis] += delta * osd::input_device::RELATIVE_PER_PIXEL;
}

void reset_laser_input_state()
{
	s_keyboard_state.fill(0);
	s_joystick_axis_state.fill(0);
	s_joystick_relative_axis_state.fill(0);
	s_joystick_relative_axis_pending.fill(0);
}

class laser_input_receiver
{
public:
	~laser_input_receiver()
	{
		close_pipe();
	}

	void poll()
	{
		if (!is_open())
			open_pipe();
		if (!is_open())
			return;

		read_available();
		parse_frames();
	}

private:
	bool is_open() const
	{
#if defined(_WIN32)
		return (m_handle != nullptr) && (m_handle != INVALID_HANDLE_VALUE);
#else
		return m_fd >= 0;
#endif
	}

	void open_pipe()
	{
		if (m_open_attempted)
			return;
		m_open_attempted = true;
		m_debug = laser_input_debug();
		m_key = parse_ipc_key();

#if defined(_WIN32)
		char const *const value = std::getenv("LASER_MAME_CONTROL_HANDLE");
		if (!value || !*value)
			return;

		char *end = nullptr;
		unsigned long long const parsed = std::strtoull(value, &end, 16);
		if ((end == value) || !parsed)
			return;

		m_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parsed));
		if (!is_open())
			return;

		osd_printf_info("Laser Mame input pipe ready\n");
#else
		char const *const value = std::getenv("LASER_MAME_CONTROL_FD");
		if (!value || !*value)
			return;

		char *end = nullptr;
		long const parsed = std::strtol(value, &end, 10);
		if ((end == value) || (parsed < 0) || (parsed > std::numeric_limits<int>::max()))
			return;

		m_fd = int(parsed);
		int const flags = ::fcntl(m_fd, F_GETFL, 0);
		if (flags >= 0)
			::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

		osd_printf_info("Laser Mame input pipe ready\n");
#endif
	}

	void close_pipe()
	{
#if defined(_WIN32)
		if (is_open())
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

	void read_available()
	{
		std::array<uint8_t, 4096> temp = { };

#if defined(_WIN32)
		while (true)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(m_handle, nullptr, 0, nullptr, &available, nullptr))
			{
				osd_printf_warning("Laser Mame input pipe peek failed\n");
				close_pipe();
				return;
			}
			if (!available)
				return;

			DWORD bytes_read = 0;
			DWORD const to_read = std::min<DWORD>(available, DWORD(temp.size()));
			if (!ReadFile(m_handle, temp.data(), to_read, &bytes_read, nullptr))
			{
				osd_printf_warning("Laser Mame input pipe read failed\n");
				close_pipe();
				return;
			}
			if (!bytes_read)
				return;
			m_buffer.insert(m_buffer.end(), temp.begin(), temp.begin() + bytes_read);
		}
#else
		while (true)
		{
			ssize_t const bytes_read = ::read(m_fd, temp.data(), temp.size());
			if (bytes_read > 0)
			{
				m_buffer.insert(m_buffer.end(), temp.begin(), temp.begin() + bytes_read);
				continue;
			}
			if (bytes_read == 0)
			{
				close_pipe();
				return;
			}
			if (errno == EINTR)
				continue;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				return;

			osd_printf_warning("Laser Mame input pipe read failed: %s\n", std::strerror(errno));
			close_pipe();
			return;
		}
#endif
	}

	void parse_frames()
	{
		while (m_buffer.size() >= k_laser_ipc_header_size)
		{
			std::array<uint8_t, k_laser_ipc_header_size> header = { };
			std::copy_n(m_buffer.begin(), k_laser_ipc_header_size, header.begin());
			mask_bytes(header.data(), header.size(), m_key, k_laser_ipc_header_mask_nonce);

			uint32_t const magic = read_u32_le(header.data());
			uint16_t const version = read_u16_le(header.data() + 4);
			uint16_t const type = read_u16_le(header.data() + 6);
			uint32_t const frame_number = read_u32_le(header.data() + 8);
			uint32_t const payload_size = read_u32_le(header.data() + 12);
			uint32_t const record_count = read_u32_le(header.data() + 16);
			uint32_t const expected_checksum = read_u32_le(header.data() + 20);

			if ((magic != k_laser_ipc_magic) || (version != k_laser_ipc_version))
			{
				m_buffer.erase(m_buffer.begin());
				continue;
			}

			if (payload_size > 64 * 1024)
			{
				m_buffer.clear();
				return;
			}

			size_t const frame_size = k_laser_ipc_header_size + size_t(payload_size);
			if (m_buffer.size() < frame_size)
				return;

			std::vector<uint8_t> payload(
					m_buffer.begin() + k_laser_ipc_header_size,
					m_buffer.begin() + frame_size);
			m_buffer.erase(m_buffer.begin(), m_buffer.begin() + frame_size);

			if (type != k_laser_ipc_type_control)
				continue;

			mask_bytes(payload.data(), payload.size(), m_key, k_laser_ipc_control_mask_nonce ^ frame_number);
			if (checksum_bytes(payload.data(), payload.size()) != expected_checksum)
				continue;

			decode_control_payload(payload, record_count);
		}
	}

	void decode_control_payload(std::vector<uint8_t> const &payload, uint32_t record_count)
	{
		size_t const available_records = payload.size() / k_laser_control_record_size;
		size_t const records = std::min<size_t>(available_records, record_count);
		for (size_t index = 0; index < records; index++)
		{
			uint8_t const *const record = payload.data() + (index * k_laser_control_record_size);
			uint8_t const kind = record[0];
			uint8_t const code_length = std::min<uint8_t>(record[1], 16);
			int const value = read_s32_le(record + 4);
			char code[17] = { };
			std::memcpy(code, record + 8, code_length);

			switch (kind)
			{
			case k_laser_control_key:
				set_key_state(code, value ? 1 : 0);
				if (m_debug)
					osd_printf_info("Laser Mame input key %s %d\n", code, value ? 1 : 0);
				break;
			case k_laser_control_axis:
				set_axis_state(code, value);
				if (m_debug)
					osd_printf_info("Laser Mame input axis %s %d\n", code, value);
				break;
			case k_laser_control_relative:
				add_relative_axis_delta(code, value);
				if (m_debug)
					osd_printf_info("Laser Mame input relative %s %d\n", code, value);
				break;
			default:
				break;
			}
		}
	}

#if defined(_WIN32)
	HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
	int m_fd = -1;
#endif
	uint64_t m_key = k_laser_ipc_default_key;
	std::vector<uint8_t> m_buffer;
	bool m_open_attempted = false;
	bool m_debug = false;
};

laser_input_receiver &laser_receiver()
{
	static laser_input_receiver receiver;
	return receiver;
}

void flush_laser_relative_axes(bool relative_reset)
{
	if (!relative_reset)
	{
		return;
	}

	for (int axis = 0; axis < k_laser_relative_axis_count; axis++)
	{
		s_joystick_relative_axis_state[axis] = s_joystick_relative_axis_pending[axis];
		s_joystick_relative_axis_pending[axis] = 0;
	}
}

} // anonymous namespace

namespace osd {

namespace {

class laser_keyboard_device : public device_info
{
public:
	laser_keyboard_device(std::string &&name, std::string &&id, input_module &module)
		: device_info(std::move(name), std::move(id), module)
	{
	}

	virtual void poll(bool relative_reset) override
	{
		laser_receiver().poll();
		flush_laser_relative_axes(relative_reset);
	}

	virtual void reset() override
	{
		reset_laser_input_state();
	}

	virtual void configure(input_device &device) override
	{
		for (laser_key_entry const *entry = s_laser_keys; entry->item != ITEM_ID_INVALID; entry++)
		{
			device.add_item(
					entry->name,
					std::string_view(),
					entry->item,
					generic_button_get_state<std::uint8_t>,
					&s_keyboard_state[entry->item]);
		}
	}

};

class laser_joystick_device : public device_info
{
public:
	laser_joystick_device(std::string &&name, std::string &&id, input_module &module)
		: device_info(std::move(name), std::move(id), module)
	{
	}

	virtual void poll(bool relative_reset) override
	{
		laser_receiver().poll();
		flush_laser_relative_axes(relative_reset);
	}

	virtual void reset() override
	{
		reset_laser_input_state();
	}

	virtual void configure(input_device &device) override
	{
		std::array<input_item_id, k_laser_axis_count> axis_items = { };
		input_device::assignment_vector assignments;

		for (int axis = 0; axis < k_laser_axis_count; axis++)
		{
			input_item_id const item = input_item_id(ITEM_ID_XAXIS + axis);
			axis_items[axis] = device.add_item(
					default_axis_name[axis],
					std::string_view(),
					item,
					generic_axis_get_state<std::int32_t>,
					&s_joystick_axis_state[axis]);
		}

		input_item_id const relative_x_item = device.add_item(
				"Rel X",
				std::string_view(),
				ITEM_ID_ADD_RELATIVE1,
				generic_axis_get_state<std::int32_t>,
				&s_joystick_relative_axis_state[0]);
		input_item_id const relative_y_item = device.add_item(
				"Rel Y",
				std::string_view(),
				ITEM_ID_ADD_RELATIVE2,
				generic_axis_get_state<std::int32_t>,
				&s_joystick_relative_axis_state[1]);

		assignments.emplace_back(
				IPT_AD_STICK_X,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NONE, axis_items[0])));
		assignments.emplace_back(
				IPT_AD_STICK_Y,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NONE, axis_items[1])));
		assignments.emplace_back(
				IPT_AD_STICK_Z,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NONE, axis_items[2])));
		assignments.emplace_back(
				IPT_PADDLE,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NONE, axis_items[3])));
		assignments.emplace_back(
				IPT_PADDLE_V,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NONE, axis_items[4])));
		assignments.emplace_back(
				IPT_PEDAL,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NEG, axis_items[7])));
		assignments.emplace_back(
				IPT_PEDAL2,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_ABSOLUTE, ITEM_MODIFIER_NEG, axis_items[6])));
		assignments.emplace_back(
				IPT_DIAL,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_RELATIVE, ITEM_MODIFIER_NONE, relative_x_item)));
		assignments.emplace_back(
				IPT_DIAL_V,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_RELATIVE, ITEM_MODIFIER_NONE, relative_y_item)));
		assignments.emplace_back(
				IPT_TRACKBALL_X,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_RELATIVE, ITEM_MODIFIER_NONE, relative_x_item)));
		assignments.emplace_back(
				IPT_TRACKBALL_Y,
				SEQ_TYPE_STANDARD,
				input_seq(laser_joystick_code(ITEM_CLASS_RELATIVE, ITEM_MODIFIER_NONE, relative_y_item)));

		device.set_default_assignments(std::move(assignments));
	}
};

class laser_keyboard_module : public input_module_base
{
public:
	laser_keyboard_module()
		: input_module_base(OSD_KEYBOARDINPUT_PROVIDER, "laser")
	{
	}

	virtual void exit() override
	{
		m_devices.free_all_devices();
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_base::input_init(machine);

		auto devinfo = std::make_unique<laser_keyboard_device>("Laser Mame Keyboard", "Laser Mame Keyboard", *this);
		input_device &osddev = manager().add_device(DEVICE_CLASS_KEYBOARD, devinfo->name(), devinfo->id(), devinfo.get());
		devinfo->configure(osddev);
		m_devices.add_device(std::move(devinfo));
	}

	virtual void reset_devices() override
	{
		m_devices.reset_devices();
	}

private:
	virtual void poll(bool relative_reset) override
	{
		m_devices.poll_devices(relative_reset);
	}

	input_device_list<laser_keyboard_device> m_devices;
};

class laser_joystick_module : public input_module_base
{
public:
	laser_joystick_module()
		: input_module_base(OSD_JOYSTICKINPUT_PROVIDER, "laser")
	{
	}

	virtual void exit() override
	{
		m_devices.free_all_devices();
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_base::input_init(machine);

		auto devinfo = std::make_unique<laser_joystick_device>("Laser Mame Joystick", "Laser Mame Joystick", *this);
		input_device &osddev = manager().add_device(DEVICE_CLASS_JOYSTICK, devinfo->name(), devinfo->id(), devinfo.get());
		devinfo->configure(osddev);
		m_devices.add_device(std::move(devinfo));
	}

	virtual void reset_devices() override
	{
		m_devices.reset_devices();
	}

private:
	virtual void poll(bool relative_reset) override
	{
		m_devices.poll_devices(relative_reset);
	}

	input_device_list<laser_joystick_device> m_devices;
};

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(KEYBOARDINPUT_LASER, osd::laser_keyboard_module)
MODULE_DEFINITION(JOYSTICKINPUT_LASER, osd::laser_joystick_module)
