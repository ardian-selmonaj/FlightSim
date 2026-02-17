import os, subprocess, sys

SKYBOLT_DIR = r"C:\Users\ardian.selmonaj\Projects\FlightSimBuild"
RELEASE = SKYBOLT_DIR + r"\bin\Release"
BAT_FILE = SKYBOLT_DIR + r"\conanrunenv-release-x86_64.bat"
PLUGIN = RELEASE + r"\plugins"

# Set plugin path BEFORE importing skybolt
os.environ["SKYBOLT_PLUGINS_PATH"] = PLUGIN

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
sys.path.append(RELEASE)

import skybolt as sb


# sb.convertToOSGT(r"C:\Users\ardian.selmonaj\Projects\SkyboltJSBSim\Assets\Movers\Models\SepecatJaguarBlue\SepecatJaguar_Fuselage.osgb")
sb.scaleAndSaveModel(r"C:\Users\ardian.selmonaj\Projects\FlightSim\Assets\Movers\Models\SepecatJaguarRed\SepecatJaguar_Fuselage.ive", 8.0)

