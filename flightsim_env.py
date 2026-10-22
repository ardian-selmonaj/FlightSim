"""
Prepares the environment on Windows so that the skybolt Python module can be imported.

Import it before skybolt:

    import flightsim_env
    import skybolt as sb

The build folder defaults to "FlightSimBuild" next to this repository (as created by setup_windows.bat)
and can be changed with the environment variable FLIGHTSIM_BUILD_DIR.
"""
import os, subprocess, sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent
SKYBOLT_DIR = Path(os.environ.get("FLIGHTSIM_BUILD_DIR", REPO_DIR.parent / "FlightSimBuild"))
RELEASE = SKYBOLT_DIR / "bin" / "Release"
BAT_FILE = SKYBOLT_DIR / "conanrunenv-release-x86_64.bat"
PLUGIN = RELEASE / "plugins"
ASSETS = REPO_DIR / "Assets"

if not BAT_FILE.is_file():
    raise ImportError(f"FlightSim build not found in '{SKYBOLT_DIR}'. Run setup_windows.bat or set FLIGHTSIM_BUILD_DIR.")

# Set plugin path BEFORE importing skybolt
os.environ["SKYBOLT_PLUGINS_PATH"] = str(PLUGIN)

# Skybolt searches "Assets/" and "../Assets/" relative to the working directory. Add the repo's Assets
# so that scripts also run from other directories, but not twice (packages would be registered twice).
if not any((Path.cwd() / d).resolve() == ASSETS for d in ("Assets", "../Assets")):
    os.environ["SKYBOLT_ASSETS_PATH"] = os.pathsep.join(filter(None, [str(ASSETS), os.environ.get("SKYBOLT_ASSETS_PATH")]))

# Run the bat and capture its environment
cmd = f'cmd /c "call \"{BAT_FILE}\" && set"'
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

# Ensure skybolt module is found
sys.path.append(str(RELEASE))
