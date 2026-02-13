import os, subprocess, sys

SKYBOLT_DIR = r"C:\Users\ardian.selmonaj\Projects\SkyboltJSBSimBuild"
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
import math

# sb.convertToOSGT(r"C:\Users\ardian.selmonaj\Projects\SkyboltJSBSim\Assets\Movers\Models\SepecatJaguarBlue\SepecatJaguar_Fuselage.osgb")

######## in set_entity_attitude(), we could also provide Velocity, as Skybolt already includes this.
######## better to provide initial location at instance creation !

app = sb.FlightSim(10, 30, 1080, 720, 
                        entityTypes={1:"SepecatJaguarJsbBlue", 2:"SepecatJaguarRed"}, 
                        # entityTypes={1:"SepecatJaguarJsbBlue"}, 
                        manual = True,
                        multiwindow=False)

dt = 1.0 / 60.0
i = 0
rocket_idx = None

#Lugano
LON = 8.965318 
LAT = 45.975305

# Baldegg
# LON = 8.274841
# LAT = 47.182253 

while True:

    # if i <75:
    if i < 1:
        lla = [LAT, LON, 3300] #Lugano
        lla = sb.LatLonAlt(math.radians(lla[0]), math.radians(lla[1]), float(lla[2]))
        rpy = [0,0,0]
        rpy = sb.Vector3(math.radians(rpy[0]), math.radians(rpy[1]), math.radians(rpy[2]))

        app.set_entity_attitude(1, position=lla, rpy=rpy)

    lla = [LAT, LON, 3050+i] #Lugano
    lla = sb.LatLonAlt(math.radians(lla[0]), math.radians(lla[1]), float(lla[2]))
    rpy = [0,0,90]
    rpy = sb.Vector3(math.radians(rpy[0]), math.radians(rpy[1]), math.radians(rpy[2]))
    app.set_entity_attitude(2, position=lla, rpy=rpy)

    if not app.frame(dt):
        break
    i+=1

    # print(f"iter {i}")
    # time.sleep(0.1)

    # if i == 20:
    #     rocket_idx = app.fire_rocket(1)

    # if i == 75:
    #     app.remove_entity(1)

    # if i > 22:
    #     lla = [LAT+i*0.00001, LON, 3000] #Lugano
    #     lla = sb.LatLonAlt(math.radians(lla[0]), math.radians(lla[1]), float(lla[2]))
    #     rpy = [0,0,0]
    #     rpy = sb.Vector3(math.radians(rpy[0]), math.radians(rpy[1]), math.radians(rpy[2]))
    #     app.set_entity_attitude(rocket_idx, position=lla, rpy=rpy)
