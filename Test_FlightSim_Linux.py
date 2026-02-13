########################## IMPORT 1 ############################

# import os, subprocess, sys

# SKYBOLT_DIR = os.path.expanduser("~/projects/FlightSimBuild")
# RELEASE = os.path.join(SKYBOLT_DIR, "lib")
# SH_FILE = os.path.join(SKYBOLT_DIR, "conanrunenv-release-x86_64.sh")
# PLUGIN = os.path.join(SKYBOLT_DIR, "lib", "Release", "plugins")

# # Check if we've already set up the environment
# if not os.environ.get("SKYBOLT_ENV_LOADED"):
#     # Set plugin path
#     os.environ["SKYBOLT_PLUGINS_PATH"] = PLUGIN
    
#     # Source the shell script and capture its environment
#     cmd = f'cd {SKYBOLT_DIR} && bash -c "source {SH_FILE} && env"'
#     env = subprocess.check_output(cmd, shell=True, text=True)
    
#     env_dict = {}
#     for line in env.splitlines():
#         if "=" in line:
#             k, v = line.split("=", 1)
#             env_dict[k] = v
    
#     # Mark that we've loaded the environment
#     env_dict["SKYBOLT_ENV_LOADED"] = "1"
#     env_dict["PYTHONPATH"] = RELEASE + ":" + env_dict.get("PYTHONPATH", "")
    
#     # Restart Python with the new environment
#     os.execve(sys.executable, [sys.executable] + sys.argv, env_dict)

# import skybolt as sb


########################## IMPORT 2 ############################
# import os, subprocess, sys

# SKYBOLT_DIR = os.path.expanduser("~/projects/FlightSimBuild")
# RELEASE = os.path.join(SKYBOLT_DIR, "lib")
# SH_FILE = os.path.join(SKYBOLT_DIR, "conanrunenv-release-x86_64.sh")
# PLUGIN = os.path.join(SKYBOLT_DIR, "lib", "Release", "plugins")

# if not os.environ.get("SKYBOLT_ENV_LOADED"):
#     cmd = f'cd {SKYBOLT_DIR} && bash -c "source {SH_FILE} && env"'
#     env = subprocess.check_output(cmd, shell=True, text=True)
    
#     env_dict = os.environ.copy()
    
#     for line in env.splitlines():
#         if "=" in line:
#             k, v = line.split("=", 1)
#             if k and v and not v.startswith("()"):
#                 env_dict[k] = v
    
#     env_dict["SKYBOLT_ENV_LOADED"] = "1"
    
#     if "PYTHONPATH" in env_dict:
#         env_dict["PYTHONPATH"] = f"{RELEASE}:{env_dict['PYTHONPATH']}"
#     else:
#         env_dict["PYTHONPATH"] = RELEASE
    
#     # DON'T set SKYBOLT_PLUGINS_PATH - let the C++ code use defaults
#     # env_dict["SKYBOLT_PLUGINS_PATH"] = PLUGIN  # <-- Remove this
    
#     os.execve(sys.executable, [sys.executable] + sys.argv, env_dict)

# import skybolt as sb


########################## IMPORT 3 ############################
import os, subprocess, sys

SKYBOLT_DIR = os.path.expanduser("~/projects/FlightSimBuild")
RELEASE = os.path.join(SKYBOLT_DIR, "lib")
SH_FILE = os.path.join(SKYBOLT_DIR, "conanrunenv-release-x86_64.sh")
PLUGIN = os.path.join(SKYBOLT_DIR, "lib", "Release", "plugins")

# Check if we've already set up the environment
if not os.environ.get("SKYBOLT_ENV_LOADED"):
    # Set plugin path
    os.environ["SKYBOLT_PLUGINS_PATH"] = PLUGIN
    
    # Source the shell script and capture its environment
    cmd = f'cd {SKYBOLT_DIR} && bash -c "source {SH_FILE} && env"'
    env = subprocess.check_output(cmd, shell=True, text=True)
    
    # Start with current environment to preserve system variables
    env_dict = os.environ.copy()
    
    for line in env.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            # Skip problematic or empty variables
            if k and v and not v.startswith("()"):  # Skip bash functions
                env_dict[k] = v
    
    # Mark that we've loaded the environment
    env_dict["SKYBOLT_ENV_LOADED"] = "1"
    
    # Add RELEASE to PYTHONPATH more carefully
    if "PYTHONPATH" in env_dict:
        env_dict["PYTHONPATH"] = f"{RELEASE}:{env_dict['PYTHONPATH']}"
    else:
        env_dict["PYTHONPATH"] = RELEASE
    
    # Ensure SKYBOLT_PLUGINS_PATH is set
    env_dict["SKYBOLT_PLUGINS_PATH"] = PLUGIN
    
    # Restart Python with the new environment
    os.execve(sys.executable, [sys.executable] + sys.argv, env_dict)

# If we get here, the environment is already set up
import skybolt as sb



# sb.convertToOSGT(r"C:\Users\ardian.selmonaj\Projects\SkyboltJSBSim\Assets\Movers\Models\SepecatJaguarBlue\SepecatJaguar_Fuselage.osgb")

######## in set_entity_attitude(), we could also provide Velocity, as Skybolt already includes this.
######## better to provide initial location at instance creation !
######## ---->> CURRENTLY CANNOT LOAD EARTH IN LINUX! 

app = sb.FlightSim(10, 30, 1080, 720, 
                        entityTypes={1:"SepecatJaguarBlue", 2:"SepecatJaguarRed"}, 
                        # entityTypes={}, 
                        manual = False,
                        multiwindow=False)


import math
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
