#!/usr/bin/env bash
# Updates the Python bindings (the skybolt module) after changing C++ code, e.g. in
# src/Skybolt/SkyboltPythonBindings/PythonBindings.cpp. Builds the existing FlightSim build again with CMake,
# which compiles only what changed, and checks that "import skybolt" works.
# The build folder is FlightSimBuild next to the repository, or the folder in FLIGHTSIM_BUILD_DIR.
# Run setup_linux.sh instead after adding or removing source files or changing dependencies (see README.md).
#
# Usage: Tools/update_pythonbindings_linux.sh [--jobs N]
#   --jobs N  Number of parallel compile jobs. Defaults to what the CPU cores and the memory allow.

if [ -z "${BASH_VERSION:-}" ]; then
    echo "Run this script with bash: bash Tools/update_pythonbindings_linux.sh" >&2
    exit 1
fi

set -Eeuo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(realpath -m "${FLIGHTSIM_BUILD_DIR:-$(dirname "$REPO_DIR")/FlightSimBuild}")"
VENV_PYTHON="$REPO_DIR/.venv/bin/python"
JOBS=""

if [ -t 1 ]; then
    CYAN=$'\e[36m'; GREEN=$'\e[32m'; RED=$'\e[31m'; RESET=$'\e[0m'
else
    CYAN=""; GREEN=""; RED=""; RESET=""
fi

trap 'echo; echo "${RED}Update failed in line $LINENO: $BASH_COMMAND${RESET}" >&2' ERR

step() { echo; echo "${CYAN}==> $*${RESET}"; }
fail() { echo; echo "${RED}Update failed: $*${RESET}" >&2; exit 1; }

usage() { awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs) JOBS="${2:?--jobs needs a number}"; shift 2 ;;
        --jobs=*) JOBS="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -z "$JOBS" || "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail "--jobs needs a positive number, not '$JOBS'."

# The cmake that configured the build, which is the one in .venv if setup_linux.sh installed a newer CMake there
cache="$BUILD_DIR/CMakeCache.txt"
[ -f "$cache" ] || fail "No FlightSim build found in $BUILD_DIR. Run setup_linux.sh first, or set FLIGHTSIM_BUILD_DIR."
cmake_exe="$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$cache")"
[ -x "$cmake_exe" ] || fail "$cmake_exe of the build not found. Run setup_linux.sh."
[ -x "$VENV_PYTHON" ] || fail "$VENV_PYTHON not found. Run setup_linux.sh first."

if [ -z "$JOBS" ]; then
    # Same as setup_linux.sh: compiling the Python bindings needs a lot of memory, so one job per 2.5 GB of memory at most
    mem_kb="$(sed -n 's/^MemTotal: *\([0-9]*\) kB$/\1/p' /proc/meminfo)"
    JOBS=$(( mem_kb / 2500000 ))
    if [ "$JOBS" -gt "$(nproc)" ]; then JOBS="$(nproc)"; fi
    if [ "$JOBS" -lt 1 ]; then JOBS=1; fi
fi

step "Building into $BUILD_DIR with $JOBS parallel jobs, only what changed is compiled again"
"$cmake_exe" --build "$BUILD_DIR" --config Release --parallel "$JOBS"

step "Checking that the skybolt Python module loads"
(cd "$REPO_DIR" && FLIGHTSIM_BUILD_DIR="$BUILD_DIR" "$VENV_PYTHON" -c "import flightsim_env, skybolt; print('import skybolt: OK')")

echo
echo "${GREEN}Python bindings updated.${RESET}"
