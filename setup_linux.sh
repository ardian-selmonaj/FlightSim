#!/usr/bin/env bash
# Linux setup of FlightSim (Skybolt), the counterpart of setup_windows.bat for Debian and Ubuntu.
#
# Every step checks whether it is already done, so the script can be re-run at any time
# (e.g. to rebuild after changing the C++ code):
#   1. System packages: compiler, CMake, OpenGL and X11 development files
#      (installed with apt, asks for the sudo password if packages are missing)
#   2. Python 3.11 virtual environment .venv with conan, jsbsim, requests, imageio-ffmpeg, reportlab and pip, created with uv
#      (uv and Python 3.11 are installed for the current user if they are missing)
#   3. Compiler and CMake (a newer CMake is installed into .venv if the one of the system is too old)
#   4. Conan default profile
#   5. Conan build of FlightSim
#   6. JSBSim root directory in the Skybolt settings (~/Skybolt/Settings.json)
#   7. Check that "import skybolt" works
# Map tiles are downloaded separately with Tools/Tile_Map_Downloader.py.
#
# Usage: ./setup_linux.sh [--build-dir DIR] [--python PATH] [--jobs N]
#   --build-dir DIR  Build output folder. Defaults to FlightSimBuild next to the repository, where flightsim_env.py looks for it.
#   --python PATH    Python 3.11 for creating .venv, e.g. /usr/bin/python3.11 (needs the development files, e.g. python3.11-dev).
#                    Defaults to a Python 3.11 managed by uv.
#   --jobs N         Number of parallel compile jobs. Defaults to what the CPU cores and the memory allow.

if [ -z "${BASH_VERSION:-}" ]; then
    echo "Run this script with bash: bash setup_linux.sh" >&2
    exit 1
fi

set -Eeuo pipefail

PYTHON_VERSION="3.11"
CONAN_VERSION="2.22.0"
JSBSIM_VERSION="1.3.0"    # same as the JSBSim of the C++ build (conanfile.py) and of HHMARL_3D_NEW
IMAGEIO_FFMPEG_VERSION="0.6.0"  # its ffmpeg encodes the videos recorded with key R
REPORTLAB_VERSION="5.1.0"       # builds instructions.pdf with Tools/build_instructions_pdf.py
CMAKE_MIN_VERSION="3.26"  # cmake_minimum_required of CMakeLists.txt
GCC_MIN_VERSION="11"      # Skybolt uses C++20

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_BUILD_DIR="$(dirname "$REPO_DIR")/FlightSimBuild"
BUILD_DIR="$DEFAULT_BUILD_DIR"
PYTHON_EXE=""
JOBS=""
VENV_DIR="$REPO_DIR/.venv"
VENV_PYTHON="$VENV_DIR/bin/python"

# git and curl fetch sources and uv. libgl-dev is required by the conan recipe opengl/system and the X11
# development files by xorg/system (see the system_requirements of these recipes).
SYSTEM_PACKAGES=(
    build-essential cmake pkg-config git curl ca-certificates
    libgl-dev libgl1-mesa-dri libglx-mesa0 fontconfig
    libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev libxau-dev libxaw7-dev libxcomposite-dev
    libxcursor-dev libxdamage-dev libxdmcp-dev libxext-dev libxfixes-dev libxi-dev libxinerama-dev libxkbfile-dev
    libxmu-dev libxmuu-dev libxpm-dev libxrandr-dev libxrender-dev libxres-dev libxss-dev libxt-dev libxtst-dev
    libxv-dev libxxf86vm-dev libxcb-glx0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-xkb-dev
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev
    libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-dri3-dev uuid-dev libxcb-cursor-dev libxcb-dri2-0-dev
    libxcb-present-dev libxcb-composite0-dev libxcb-ewmh-dev libxcb-res0-dev
)

if [ -t 1 ]; then
    CYAN=$'\e[36m'; GREEN=$'\e[32m'; YELLOW=$'\e[33m'; RED=$'\e[31m'; RESET=$'\e[0m'
else
    CYAN=""; GREEN=""; YELLOW=""; RED=""; RESET=""
fi

trap 'echo; echo "${RED}Setup failed in line $LINENO: $BASH_COMMAND${RESET}" >&2' ERR

step() { echo; echo "${CYAN}==> $*${RESET}"; }
warn() { echo "${YELLOW}$*${RESET}"; }
fail() { echo; echo "${RED}Setup failed: $*${RESET}" >&2; exit 1; }

usage() { awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"; }

# True if version $1 is at least version $2
version_ge() { [ -n "$1" ] && [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]; }

is_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }

find_uv() {
    local candidate
    for candidate in "$(command -v uv || true)" "$HOME/.local/bin/uv" "$HOME/.cargo/bin/uv"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

cmake_version() { cmake --version 2>/dev/null | sed -n 's/^cmake version \([0-9.]*\).*/\1/p'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="${2:?--build-dir needs a folder}"; shift 2 ;;
        --build-dir=*) BUILD_DIR="${1#*=}"; shift ;;
        --python) PYTHON_EXE="${2:?--python needs the path of a Python $PYTHON_VERSION}"; shift 2 ;;
        --python=*) PYTHON_EXE="${1#*=}"; shift ;;
        --jobs) JOBS="${2:?--jobs needs a number}"; shift 2 ;;
        --jobs=*) JOBS="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

[ "$(uname -s)" = "Linux" ] || fail "setup_linux.sh is for Linux, use setup_windows.bat on Windows."
[[ -z "$JOBS" || "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail "--jobs needs a positive number, not '$JOBS'."
BUILD_DIR="$(realpath -m "$BUILD_DIR")"

# ---------------------------------------------------------------------------------------------
step "1/7 System packages"
if command -v apt-get >/dev/null && command -v dpkg-query >/dev/null; then
    packages=("${SYSTEM_PACKAGES[@]}")

    # xorg/system also accepts libxcb-util0-dev, the name on older releases
    xcb_util="libxcb-util-dev"
    if ! is_installed "$xcb_util" && { is_installed libxcb-util0-dev || ! apt-cache show "$xcb_util" >/dev/null 2>&1; }; then
        xcb_util="libxcb-util0-dev"
    fi
    packages+=("$xcb_util")

    missing=()
    for package in "${packages[@]}"; do
        is_installed "$package" || missing+=("$package")
    done

    if [ ${#missing[@]} -eq 0 ]; then
        echo "All required packages are installed"
    else
        echo "Installing ${missing[*]} ..."
        sudo_cmd=()
        if [ "$(id -u)" -ne 0 ]; then
            sudo_cmd=(sudo)
        fi
        "${sudo_cmd[@]}" apt-get update
        "${sudo_cmd[@]}" apt-get install -y "${missing[@]}"
    fi
else
    warn "apt is not available. Install the counterparts of these packages with the package manager of your distribution:"
    warn "${SYSTEM_PACKAGES[*]} libxcb-util-dev"
fi

# ---------------------------------------------------------------------------------------------
step "2/7 Python $PYTHON_VERSION virtual environment"
if ! UV="$(find_uv)"; then
    echo "uv not found, installing it into ~/.local/bin (PATH and shell profiles are not changed) ..."
    curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR="$HOME/.local/bin" UV_NO_MODIFY_PATH=1 sh
    UV="$(find_uv)" || fail "uv was installed, but $HOME/.local/bin/uv is not found."
fi
echo "uv -> $UV"

if [ ! -x "$VENV_PYTHON" ]; then
    echo "Creating $VENV_DIR"
    if [ -n "$PYTHON_EXE" ]; then
        "$UV" venv --python "$PYTHON_EXE" "$VENV_DIR"
    else
        # Python managed by uv: installed for the current user if missing, next to any other Python versions,
        # without changing the system Python or the default "python"
        "$UV" venv --python "$PYTHON_VERSION" --python-preference only-managed "$VENV_DIR"
    fi
fi

venv_version="$("$VENV_PYTHON" -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')" \
    || fail "$VENV_PYTHON does not work. Delete the .venv folder and run the setup again."
case "$venv_version" in
    "$PYTHON_VERSION".*) echo "$VENV_DIR uses Python $venv_version" ;;
    *) fail "$VENV_DIR uses Python $venv_version instead of $PYTHON_VERSION. Delete the .venv folder and run the setup again." ;;
esac

# The Python module is compiled against Python.h
"$VENV_PYTHON" -c 'import os, sys, sysconfig; sys.exit(not os.path.isfile(os.path.join(sysconfig.get_paths()["include"], "Python.h")))' \
    || fail "The Python of $VENV_DIR has no development files (Python.h). Install them (e.g. python$PYTHON_VERSION-dev), or delete the .venv folder and run the setup without --python to use a Python managed by uv."

# pip as well, so that further packages can be installed with ".venv/bin/python -m pip", as on Windows
"$UV" pip install --python "$VENV_PYTHON" "conan==$CONAN_VERSION" "jsbsim==$JSBSIM_VERSION" requests "imageio-ffmpeg==$IMAGEIO_FFMPEG_VERSION" "reportlab==$REPORTLAB_VERSION" pip

# Use the venv for the rest of the setup (conanfile.py calls "conan" itself)
export VIRTUAL_ENV="$VENV_DIR"
export PATH="$VENV_DIR/bin:$PATH"
hash -r

# ---------------------------------------------------------------------------------------------
step "3/7 Compiler and CMake"
command -v g++ >/dev/null || fail "g++ not found. Install it with: sudo apt install build-essential"
gcc_version="$(g++ -dumpfullversion -dumpversion)"
version_ge "$gcc_version" "$GCC_MIN_VERSION" || fail "g++ $gcc_version is too old, version $GCC_MIN_VERSION or newer is needed."
echo "g++ -> $(command -v g++) ($gcc_version)"

if ! version_ge "$(cmake_version)" "$CMAKE_MIN_VERSION"; then
    echo "CMake $(cmake_version) is older than $CMAKE_MIN_VERSION, installing a newer CMake into $VENV_DIR ..."
    "$UV" pip install --python "$VENV_PYTHON" "cmake>=$CMAKE_MIN_VERSION,<4"
    hash -r
fi
echo "cmake -> $(command -v cmake) ($(cmake_version))"

# ---------------------------------------------------------------------------------------------
step "4/7 Conan profile"
profile="$(conan config home)/profiles/default"
if [ -f "$profile" ]; then
    echo "Using existing profile $profile"
else
    conan profile detect
fi

# JSBSim needs C++17. The profile detected for a current GCC has gnu17, which is kept, so that the
# dependencies already built with it are reused.
cppstd="$(sed -n 's/^compiler\.cppstd *= *//p' "$profile")"
cppstd_args=()
case "$cppstd" in
    17|gnu17|20|gnu20|23|gnu23|26|gnu26) echo "compiler.cppstd=$cppstd" ;;
    *)
        cppstd_args=(-s:a "compiler.cppstd=gnu17")
        echo "The profile has compiler.cppstd=${cppstd:-<none>}, building with gnu17 (needed for JSBSim)"
        ;;
esac

# ---------------------------------------------------------------------------------------------
step "5/7 Building FlightSim into $BUILD_DIR (the first build compiles all dependencies and takes long)"
if [ -z "$JOBS" ]; then
    # Compiling the Python bindings needs a lot of memory, so one job per 2.5 GB of memory at most
    mem_kb="$(sed -n 's/^MemTotal: *\([0-9]*\) kB$/\1/p' /proc/meminfo)"
    JOBS=$(( mem_kb / 2500000 ))
    if [ "$JOBS" -gt "$(nproc)" ]; then JOBS="$(nproc)"; fi
    if [ "$JOBS" -lt 1 ]; then JOBS=1; fi
fi
echo "Compiling with $JOBS parallel jobs"

conan build "$REPO_DIR" \
    --output-folder="$BUILD_DIR" \
    --build=missing \
    ${cppstd_args[@]+"${cppstd_args[@]}"} \
    -o "openscenegraph-mr/*:shared=True" \
    -o enable_jsbsim=True \
    -o enable_python=True \
    -o enable_qt=False \
    -c "tools.build:jobs=$JOBS" \
    -c "tools.cmake.cmaketoolchain:extra_variables={'Python3_EXECUTABLE': '$VENV_PYTHON'}"  # build the module for the venv's Python

# ---------------------------------------------------------------------------------------------
step "6/7 Skybolt settings"
"$VENV_PYTHON" -I - <<'EOF'
import json
from pathlib import Path

import jsbsim

# Same location as getAppUserDataDirectory("Skybolt") in Skybolt
settings_file = Path.home() / "Skybolt" / "Settings.json"
settings = json.loads(settings_file.read_text()) if settings_file.is_file() else {}
root_dir = settings.get("jsb", {}).get("rootDir")
if root_dir and (Path(root_dir) / "aircraft").is_dir():
    print(f"Keeping JSBSim root directory {root_dir} from {settings_file}")
else:
    # The jsbsim Python package ships the aircraft, engine and systems data
    root_dir = jsbsim.get_default_root_dir()
    settings.setdefault("jsb", {})["rootDir"] = root_dir
    settings_file.parent.mkdir(parents=True, exist_ok=True)
    settings_file.write_text(json.dumps(settings, indent=4) + "\n")
    print(f"Set jsb.rootDir to {root_dir} in {settings_file}")
EOF

# ---------------------------------------------------------------------------------------------
step "7/7 Checking that the skybolt Python module loads"
(cd "$REPO_DIR" && FLIGHTSIM_BUILD_DIR="$BUILD_DIR" "$VENV_PYTHON" -c "import flightsim_env, skybolt; print('import skybolt: OK')")

echo
echo "${GREEN}Setup complete.${RESET}"
echo "Next steps, from $REPO_DIR :"
echo "  .venv/bin/python Tools/Tile_Map_Downloader.py   (map tiles of Switzerland)"
echo "  .venv/bin/python Test_FlightSim_Linux.py"
if [ "$BUILD_DIR" != "$DEFAULT_BUILD_DIR" ]; then
    warn "Set the environment variable FLIGHTSIM_BUILD_DIR=$BUILD_DIR so that flightsim_env.py finds the build."
fi
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    warn "No display found. The simulation needs a desktop session to open its window (under WSL2 it is provided by WSLg)."
fi
