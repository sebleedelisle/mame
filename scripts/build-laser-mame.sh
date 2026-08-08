#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
jobs="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)}"
debug="${LASER_MAME_DEBUG:-0}"
case "$(uname -s)" in
  Darwin)
    default_osd="mac"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    default_osd="windows"
    ;;
  *)
    default_osd="sdl"
    ;;
esac
osd="${LASER_MAME_OSD:-$default_osd}"

cd "$repo_root"

vector_sources=(
  src/mame/alliedleisure/aztarac.cpp
  src/mame/atari/asteroid.cpp
  src/mame/atari/bwidow.cpp
  src/mame/atari/bzone.cpp
  src/mame/atari/mhavoc.cpp
  src/mame/atari/quantum.cpp
  src/mame/atari/starwars.cpp
  src/mame/atari/tempest.cpp
  src/mame/atari/tomcat.cpp
  src/mame/cinematronics/cchasm.cpp
  src/mame/cinematronics/cinemat.cpp
  src/mame/exidy/vertigo.cpp
  src/mame/midway/omegrace.cpp
  src/mame/miltonbradley/vectrex.cpp
  src/mame/sega/segag80v.cpp
)

sources="$(IFS=,; echo "${vector_sources[*]}")"

make DEBUG="$debug" SUBTARGET=laser-mame OSD="$osd" REGENIE=1 \
  SOURCES="$sources" \
  -j"$jobs"

if [[ "$debug" == "0" ]]; then
  built_binary="laser_mame"
else
  built_binary="laser_mamed"
fi

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    cp -f "${built_binary}.exe" laser-mame.exe
    ;;
  *)
    ln -sf "$built_binary" laser-mame
    ;;
esac
