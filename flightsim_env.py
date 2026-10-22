"""
Prepares the environment on Windows and Linux so that the skybolt Python module can be imported.

Import it before skybolt:

    import flightsim_env
    import skybolt as sb

The build folder defaults to "FlightSimBuild" next to this repository (as created by setup_windows.bat or
setup_linux.sh) and can be changed with the environment variable FLIGHTSIM_BUILD_DIR.
"""
import ctypes, os, re, subprocess, sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent
SKYBOLT_DIR = Path(os.environ.get("FLIGHTSIM_BUILD_DIR", REPO_DIR.parent / "FlightSimBuild"))
ASSETS = REPO_DIR / "Assets"

if sys.platform == "win32":
    RELEASE = SKYBOLT_DIR / "bin" / "Release"
    PLUGIN = RELEASE / "plugins"
    RUN_ENV_FILE = SKYBOLT_DIR / "conanrunenv-release-x86_64.bat"
    SETUP_SCRIPT = "setup_windows.bat"
elif sys.platform.startswith("linux"):
    RELEASE = SKYBOLT_DIR / "lib"
    PLUGIN = RELEASE / "Release" / "plugins"
    RUN_ENV_FILE = SKYBOLT_DIR / "conanrunenv-release-x86_64.sh"
    SETUP_SCRIPT = "setup_linux.sh"
else:
    raise ImportError(f"FlightSim supports Windows and Linux, not '{sys.platform}'.")

if not RUN_ENV_FILE.is_file():
    raise ImportError(f"FlightSim build not found in '{SKYBOLT_DIR}'. Run {SETUP_SCRIPT} or set FLIGHTSIM_BUILD_DIR.")

# Set plugin path BEFORE importing skybolt
os.environ["SKYBOLT_PLUGINS_PATH"] = str(PLUGIN)

# Skybolt searches "Assets/" and "../Assets/" relative to the working directory. Add the repo's Assets
# so that scripts also run from other directories, but not twice (packages would be registered twice).
if not any((Path.cwd() / d).resolve() == ASSETS for d in ("Assets", "../Assets")):
    os.environ["SKYBOLT_ASSETS_PATH"] = os.pathsep.join(filter(None, [str(ASSETS), os.environ.get("SKYBOLT_ASSETS_PATH")]))

if sys.platform == "win32":
    # Run the bat and capture its environment
    cmd = f'cmd /c "call \"{RUN_ENV_FILE}\" && set"'
    env = subprocess.check_output(cmd, shell=True, text=True)

    for line in env.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ[k] = v

    # Register DLL search paths explicitly
    for p in os.environ.get("PATH", "").split(";"):
        if os.path.isdir(p):
            try:
                os.add_dll_directory(p)
            except OSError:
                pass
else:
    # Under WSL2, Mesa renders on the CPU (llvmpipe) unless its driver for the GPU is selected, which is about 25x faster
    if os.path.exists("/dev/dxg"):
        os.environ.setdefault("GALLIUM_DRIVER", "d3d12")

    # fontconfig built by conan does not find the configuration of the system by itself
    if os.path.isfile("/etc/fonts/fonts.conf"):
        os.environ.setdefault("FONTCONFIG_FILE", "/etc/fonts/fonts.conf")

    # The shared libraries of the build (OpenSceneGraph) are in the conan cache, but LD_LIBRARY_PATH is only read
    # when a process starts. Load them directly instead. They depend on each other, so repeat until all are loaded.
    lib_dirs = [p for paths in re.findall(r'export LD_LIBRARY_PATH="([^"]*)"', RUN_ENV_FILE.read_text())
                for p in paths.split(":") if p and not p.startswith("$")]
    pending = [os.path.join(d, f) for d in lib_dirs if os.path.isdir(d)
               for f in sorted(os.listdir(d)) if re.fullmatch(r"lib.+\.so\.\d+", f)]
    while pending:
        failed = []
        for lib in pending:
            try:
                ctypes.CDLL(lib, mode=os.RTLD_GLOBAL)
            except OSError:
                failed.append(lib)
        if len(failed) == len(pending):
            break  # if skybolt needs one of them, importing skybolt reports the missing library
        pending = failed

# Ensure skybolt module is found
sys.path.append(str(RELEASE))
