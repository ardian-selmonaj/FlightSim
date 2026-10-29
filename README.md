# FlightSim 

This is a modified version of the project called [Skybolt](https://github.com/Prograda/Skybolt/tree/master). It is used as MARL environment for realistic simulations and human-agent interactions.

The MARL air combat environment and its pre-trained policies are in [HHMARL_3D_NEW](https://github.com/ardian-selmonaj/HHMARL_3D_NEW), which uses FlightSim for rendering, as JSBSim simulator and for human-agent interaction with the keyboard or with joysticks, throttles and pedals (see [Using FlightSim from Python](#using-flightsim-from-python) and [Joysticks, throttles and pedals](#joysticks-throttles-and-pedals)).

*TODO:*

- The manual aircraft flies an F-16 with a corrected rudder (`Assets/Movers/JSBSimManual`, see its `README.md`), while the RL agents of HHMARL_3D_NEW fly the F-16 of JSBSim 1.3.0 that their policies were trained with, whose rudder barely deflects. For the same dynamics, the policies may have to be trained again with the corrected F-16.
- The cannon of the manual aircraft fires exactly while the fire button is held, with unlimited ammunition, see `CannonSim.fire()` of HHMARL_3D_NEW. The cannon of the RL agents behaves differently (`CannonSim.launch()`): a shot keeps firing until it hits or for up to 90 env steps (12 s), the next shot follows after 120 env steps at the earliest, and they have `num_missiles` shots.


## QUICK START

Clone this repository into a short path, e.g. `C:\Users\<name>\Projects\FlightSim` (Windows build tools fail with paths longer than 260 characters), and run these commands in the clone. The details are in the sections below.

| Step | Windows | Linux (Ubuntu, also under WSL2) |
|---|---|---|
| 1. Set up and build: build tools, Python 3.11 venv `.venv`, dependencies and FlightSim into `FlightSimBuild` next to the clone. The first build takes long. | `setup_windows.bat` | `./setup_linux.sh` |
| 2. Offline map tiles of Switzerland, for realistic terrain | `.venv\Scripts\python Tools\Tile_Map_Downloader.py` | `.venv/bin/python Tools/Tile_Map_Downloader.py` |
| 3. Try it: fly an F-16 with the keyboard or joysticks, next to a second aircraft | `.venv\Scripts\python Test_FlightSim_Windows.py` | `.venv/bin/python Test_FlightSim_Linux.py` |
| After changing C++ code | `Tools\update_pythonbindings_windows.bat` | `./Tools/update_pythonbindings_linux.sh` |

- In the FlightSim window, H shows the keys: F1 to F4 views, Tab next aircraft, M minimap, R video recording, Esc exit.
- After pulling changes that add or remove C++ files or change `conanfile.py` (e.g. a new Conan package), run step 1 again instead of the update script, see [Updating the Python bindings](#updating-the-python-bindings).
- Own Python scripts need `import flightsim_env` before `import skybolt`, see [Using FlightSim from Python](#using-flightsim-from-python).
- With HHMARL_3D_NEW: clone it next to this repository (`Projects/FlightSim` and `Projects/HHMARL_3D_NEW`), set `RENDERER = "flightsim"` and the other FlightSim settings in its `test.py` and run it with its own Python 3.11 venv, see its README.
- `instructions.pdf` gives an overview of the components, the architecture and all parameters of both repositories.


## INSTALLING ON WINDOWS

1) In the local clone of this repository, run `setup_windows.bat` (double-click it or run it in a terminal). It does the complete setup. Each step is skipped if it is already done, so it can be re-run at any time. After changing C++ code, the faster `Tools\update_pythonbindings_windows.bat` is usually enough, see [Updating the Python bindings](#updating-the-python-bindings):
- installs the VS 2022 Build Tools with the C++ build tools workload (MSVC v143, Windows SDK, C++ CMake tools for Windows) using `Tools\VSBuildTools.exe` of this repository (downloaded if missing). This step asks for administrator rights.
- loads the MSVC compiler environment, so the "Developer Command Prompt for VS 2022" is not needed
- creates the Python 3.11 virtual environment `.venv` with an installed Python 3.11. If there is none, Python 3.11.9 (the latest 3.11 with a Windows installer) is installed for the current user next to any other Python versions, without changing PATH, file associations or the default `python`. The venv gets `conan==2.22.0`, `jsbsim==1.3.0`, `requests`, `imageio-ffmpeg==0.6.0` (its ffmpeg encodes the videos recorded with key R), `reportlab==5.1.0` (for `Tools\build_instructions_pdf.py`) and `pygame-ce==2.5.8` (for `Tools\joysticks.py`, see [Joysticks, throttles and pedals](#joysticks-throttles-and-pedals))
- creates a default conan profile (if there is none) and builds the project with `compiler.cppstd=17` (needed for JSBSim) into `FlightSimBuild` next to the repository. The first build compiles all dependencies and takes long. JSBSim 1.3.0, the version of the `jsbsim` Python package, is built from source by the recipe in `Conan/Recipes/jsbsim`.
- sets the root directory of JSBSim in the Skybolt settings, located under "user\AppData\Local\Skybolt\Settings.json". The JSBSim aircraft data comes with the `jsbsim` Python package, so no separate JSBSim installation is needed. An existing valid `rootDir` is kept.
- checks that `import skybolt` works

- Options: `setup_windows.bat -BuildDir D:\FlightSimBuild` builds into another folder (then set the environment variable `FLIGHTSIM_BUILD_DIR` to that folder), `setup_windows.bat -PythonExe C:\path\to\python.exe` uses a Python 3.11 in a non-standard location.

2) for realistic rendering, run `.venv\Scripts\python Tools\Tile_Map_Downloader.py`. For Switzerland, the map tiles (zoom levels 0-14) are already available under the links [Terrain](https://drive.google.com/file/d/1lMife7PamdICDU4D7hoqI3tCQr1aqn9c/view?usp=sharing) and [Satellite](https://drive.google.com/file/d/1CQ3N1oqyjL1ly_6SO7m9wICEsS0f2zVm/view?usp=sharing), as zips of tiles that were already downloaded from Mapbox. The script downloads both zips and unzips them into `Assets\mapbox_terrain_ch` and `Assets\mapbox_satellite_ch`, where the entity `Assets\Globe\Entities\PlanetEarth.json` looks for the map tiles. Tiles that already exist in `Assets` are not downloaded again. If the download fails (e.g., the link expired), download the zips manually into `Assets` (as `Assets\mapbox_terrain_ch.zip` and `Assets\mapbox_satellite_ch.zip`) and run the script again, which then unzips them instead of downloading them.

- FlightSim is meant to run offline with these tiles, so it shows the earth in detail only within Switzerland. To show any other area, paste a [Mapbox access token](https://account.mapbox.com/access-tokens/) into `Tools\mapbox_token.txt` (empty lines and lines starting with `#` are ignored). The file is part of the repository, but Git only stores its comments: the Git filter `mapbox-token` (`.gitattributes`) removes all other lines when the file is added, so the token stays in the local clone and is never committed or pushed. Git does not run filter commands defined by a repository, so `setup_windows.bat` and `setup_linux.sh` set up the filter in each clone; run the setup before pasting a token into a new clone. After pasting the token, `git status` may list the file as modified, although `git diff` shows no change; `git add Tools\mapbox_token.txt` updates this without storing the token. When the file has a token, `flightsim_env.py` passes it to FlightSim (environment variable `FLIGHTSIM_MAPBOX_TOKEN`, which can also be set directly). FlightSim then still takes the tiles from `Assets` where they exist, and loads only the missing ones from Mapbox (the `fallback` tile sources in `PlanetEarth.json`), which needs an internet connection. Tiles loaded from Mapbox are cached in the Skybolt cache folder (`%LOCALAPPDATA%\Skybolt\Cache`, Linux `~/Skybolt/Cache`). This also applies to HHMARL_3D_NEW, which uses FlightSim through `flightsim_env.py`. Without a token, FlightSim only uses the offline tiles. The `tileApiKeys` in the Skybolt settings (see step 1) are not needed and stay empty.

- For increased depth (better details), further zoom levels need to be downloaded directly from Mapbox with the same script, e.g., `.venv\Scripts\python Tools\Tile_Map_Downloader.py --zoom 15 16`, which needs the token in `Tools\mapbox_token.txt`. Existing tiles are skipped. Afterwards, raise `maxLevel` in `PlanetEarth.json` accordingly. The area is set by `CENTER_LATITUDE`, `CENTER_LONGITUDE` and the ranges in the script. Note that the number of tiles grows about 4x per zoom level. Of course, you can use any other map tiles from Mapbox or any other source.

3) run `.venv\Scripts\python Test_FlightSim_Windows.py`. Python scripts using Skybolt only need `import flightsim_env` before `import skybolt` (see `flightsim_env.py`). For more Python Bindings, need to extend `FlightSim\src\Skybolt\SkyboltPythonBindings\PythonBindings.cpp` appropriately and run `Tools\update_pythonbindings_windows.bat` (see [Updating the Python bindings](#updating-the-python-bindings)).

*NOTE*: The repo and build folder need short paths, otherwise Windows build tools break if path exceeds 260 characters.


## INSTALLING ON LINUX

Tested with Ubuntu 24.04 under WSL2. The setup installs the system packages with `apt`, i.e., it is meant for Debian and Ubuntu.

1) In the local clone of this repository, run `./setup_linux.sh`. It does the complete setup. Each step is skipped if it is already done, so it can be re-run at any time. After changing C++ code, the faster `./Tools/update_pythonbindings_linux.sh` is usually enough, see [Updating the Python bindings](#updating-the-python-bindings):
- installs the missing system packages with `apt`: compiler, CMake, OpenGL and X11 development files. This step asks for the `sudo` password if packages are missing.
- creates the Python 3.11 virtual environment `.venv` with [uv](https://docs.astral.sh/uv/), which is installed into `~/.local/bin` if it is missing. If there is no Python 3.11 managed by uv, uv installs one for the current user next to any other Python versions, without changing the system Python or the default `python`. The venv gets `conan==2.22.0`, `jsbsim==1.3.0`, `requests`, `imageio-ffmpeg==0.6.0` (its ffmpeg encodes the videos recorded with key R), `reportlab==5.1.0` (for `Tools/build_instructions_pdf.py`), `pygame-ce==2.5.8` (for `Tools/joysticks.py`) and `pip`
- checks the compiler (GCC 11 or newer) and CMake. If the CMake of the system is older than 3.26 (e.g., on Ubuntu 22.04), a newer CMake is installed into `.venv`
- creates a default conan profile (if there is none) and builds the project into `FlightSimBuild` next to the repository. The first build compiles all dependencies and takes long.
- sets the root directory of JSBSim in the Skybolt settings, located under `~/Skybolt/Settings.json`. The JSBSim aircraft data comes with the `jsbsim` Python package, so no separate JSBSim installation is needed. An existing valid `rootDir` is kept.
- checks that `import skybolt` works

- Options: `./setup_linux.sh --build-dir ~/FlightSimBuild` builds into another folder (then set the environment variable `FLIGHTSIM_BUILD_DIR` to that folder), `./setup_linux.sh --python /usr/bin/python3.11` creates `.venv` with a specific Python 3.11 (it needs the development files, e.g., `python3.11-dev`), `./setup_linux.sh --jobs 4` sets the number of parallel compile jobs (by default, it depends on the CPU cores and the memory).

2) for realistic rendering, run `.venv/bin/python Tools/Tile_Map_Downloader.py`, as described in step 2 of the Windows installation. Under WSL2, the map tiles can also be copied from a Windows clone, e.g., `cp -r /mnt/c/<path to clone>/Assets/mapbox_* Assets/` (reading them directly from `/mnt/c` is slow).

3) run `.venv/bin/python Test_FlightSim_Linux.py`. As on Windows, Python scripts using Skybolt only need `import flightsim_env` before `import skybolt` (see `flightsim_env.py`). On Linux, it loads the shared libraries of the build, so `LD_LIBRARY_PATH` does not need to be set. For more Python Bindings, need to extend `FlightSim/src/Skybolt/SkyboltPythonBindings/PythonBindings.cpp` appropriately and run `./Tools/update_pythonbindings_linux.sh` (see [Updating the Python bindings](#updating-the-python-bindings)).

*NOTE 1*: A desktop session is needed to open the window. Under WSL2, it is provided by WSLg. There, Mesa renders on the CPU unless its driver for the GPU is selected, which `flightsim_env.py` does (`GALLIUM_DRIVER=d3d12`). Otherwise, the simulation is about 25x slower.

*NOTE 2*: In contrast to Windows, the build does not set `compiler.cppstd=17`. The conan profile detected for GCC has `gnu17`, which JSBSim accepts, and changing it would rebuild all dependencies.


## UPDATING THE PYTHON BINDINGS

After changing C++ code, e.g., extending `src/Skybolt/SkyboltPythonBindings/PythonBindings.cpp`, the Python bindings are updated in one step, without running the whole setup again:
- Windows: double-click `Tools\update_pythonbindings_windows.bat`. Close all Python programs that use `skybolt` before, because Windows locks the module file `skybolt.cp311-win_amd64.pyd` while it is loaded, so the build cannot replace it.
- Linux: run `./Tools/update_pythonbindings_linux.sh`. `--jobs N` sets the number of parallel compile jobs (by default, it depends on the CPU cores and the memory, as in the setup).

The script builds the existing `FlightSimBuild` (or the folder in `FLIGHTSIM_BUILD_DIR`) again with CMake and checks that `import skybolt` works. Only what changed is compiled again: the Python bindings, and also the other Skybolt libraries and the plugins (e.g., JSBSim) if their code was changed. Changes to `CMakeLists.txt` files are picked up as well.

Re-run the whole setup (`setup_windows.bat` or `./setup_linux.sh`) instead if:
- C++ source files were added, removed or renamed. CMake collects the source files only when the setup configures the build, so the update script does not see them.
- `conanfile.py`, the conan options or the dependencies were changed
- the build folder or `.venv` does not exist yet, e.g., on a new machine or after deleting `FlightSimBuild` (the update script reports this)


## USING FLIGHTSIM FROM PYTHON

`sb.FlightSim` (see `src/Skybolt/SkyboltPythonBindings/PythonBindings.cpp`) shows aircraft over the earth, identified by integer IDs chosen in Python. `Test_FlightSim_Windows.py` and `Test_FlightSim_Linux.py` show the usage.
- `sb.FlightSim(x, y, width, height, manual=False, multiwindow=False, dynamicsRate=75.0, headless=False)` opens the window (none with `headless=True`). The simulation advances in steps of `1/dynamicsRate` seconds.
- `add_aircraft(id, template, position, rpy, velocityNed)` adds an aircraft of an entity template of `Assets/Movers/Entities` at its initial position (`sb.LatLonAlt` with geodetic latitude and longitude in radians, altitude in meters above sea level), roll, pitch and yaw (radians, relative to north-east-down) and velocity (north, east, down in m/s). Templates without flight dynamics, e.g. `SepecatJaguarBlue`, are moved from Python with `set_aircraft_state(id, position, rpy, velocityNed)`. Templates with a `jsbSimAircraft` component, e.g. `SepecatJaguarJsbBlue` (JSBSim model `f16`), are simulated by JSBSim, starting at the given state. `SepecatJaguarJsbBlueManual` and `SepecatJaguarJsbRedManual` are meant for the manual aircraft: their F-16 is the one of `Assets/Movers/JSBSimManual`, whose rudder is corrected (see its `README.md`), loaded with `"aircraftDir": "JSBSimManual"` of the `jsbSimAircraft` component (a folder of the asset packages with the JSBSim models, whose engines and systems come from the JSBSim root directory).
- JSBSim aircraft: `get_property(id, name)`, `get_properties(id, names)`, `set_property(id, name, value)` and `query_property_catalog(id)` access the JSBSim properties, e.g. `fcs/aileron-cmd-norm`. Setting the `ic/...` properties and calling `run_ic(id)`, `init_engine_running(id, engine)` and `get_steady_state(id)` initializes the aircraft as the `jsbsim` Python package does. By default, the flight controls come from the joysticks or the keyboard (manual aircraft); after `set_external_control(id, True)` only from the properties.
- `step(n)` advances the simulation by `n` steps and `render()` renders a frame (`False` once the window is closed or escape is pressed). `frame(dt)` does both for a time `dt`.
- `manual=True`: the joysticks of `Tools/joysticks.json` (see [Joysticks, throttles and pedals](#joysticks-throttles-and-pedals)) or the keyboard fly the aircraft of `set_manual_aircraft(id)`, whose controls are also returned by `get_manual_controls()`. `consume_fire_requests()` returns how often the fire button of the joystick, or without it space, was pressed since the last call, and `fire_pressed` is `True` while it is held.
- `set_camera_target(id)`, `select_camera_view("Cockpit" | "Follow" | "Overview" | "Free")`, `set_aircraft_visible(id, visible)`, `set_dynamics_enabled(id, enabled)` and `remove_aircraft(id)` change the view and the aircraft, `fire_tracer(id)` shows a cannon shot as a short dark line (`fire_rocket` is the old name). The overview shows all visible aircraft from slightly above, from behind the camera target towards the others.
- `set_aircraft_label(id, label)` shows a label above the aircraft, e.g. its name, `set_aircraft_color(id, (r, g, b))` sets the (linear) color of the label and of the trail, the flown path drawn behind the aircraft. `labels_visible`, `trails_visible`, `trail_length` (m, default 30 km) and `clear_trails()` control them. The engine nozzles of the camera target glow with its throttle lever, from black at idle to dark red at full throttle: discs at the nozzle exits of the entity template (`"nozzles": [{"positionRelBody": [x, y, z], "radius": r}, ...]` next to `"components"`, in m in the body axes, set for the Sepecat Jaguar templates). Aircraft with JSBSim show their `fcs/throttle-cmd-norm` (the F-16 has afterburner above 0.5), the others the throttle of `set_aircraft_throttle(id, throttle)` in [0, 1].
- `minimap_visible` (key M, on by default) shows a map of the visible aircraft around the camera target in the bottom right corner of the window, about a quarter of its height. Each aircraft is an arrow in its color pointing in its heading, with its label and its distance (in 3D) to the camera target, e.g. `7.3 km`. Small dots behind the other aircraft show their paths of the last 30 s (simulation time); `clear_trails()` clears them as well. In the free view (F4), a light gray camera shows where the camera is (on the outer ring if it is farther away) and dashed lines its field of view, which makes it easier to turn the camera towards the aircraft. The outer ring is at `minimap_range` (m, default 20 km, shown in the bottom left corner), the dashed ring at half of it. Aircraft farther away are shown by a triangle on the outer ring in their direction, with their distance. With `minimap_heading_up` (key N, shown at the top of the window), the heading of the camera target points up, otherwise north. By default it is heading-up with `manual=True`, otherwise north-up. Ticks every 30 degrees show the directions, N marks north, and the label of the camera target is in the bottom right corner. Videos show the minimap like the labels.
- `start_recording(path=None)` records the view of the window into an MP4 video until `stop_recording()`, the window is closed or the `FlightSim` is deleted. Key R starts and stops recording as well, and a red dot in the top left corner shows that it records. The video shows the view as in the window (the selected camera, with the labels, trails, minimap and cockpit HUD if they are shown), but not the help message and the red dot. Without a path, the video is a new file named after the date and time in `recording_directory` (default `recordings`, relative to the current directory). It has `recording_fps` (default 30) frames per second of simulation time, each taken from the simulation step closest to its time, so it plays at the speed of the simulation, however fast it is rendered (at 75 Hz, the frames are alternately 2 and 3 steps apart). Programs that skip frames to keep up with real time should render those for which `recording_frame_due` is `True`, otherwise frames repeat in the video. The ffmpeg that encodes the video is the one of the Python package `imageio-ffmpeg` (`flightsim_env.py` sets the environment variable `FLIGHTSIM_FFMPEG` to it), otherwise ffmpeg from PATH.
- `vsync` (key V, on by default) waits for the refreshes of the monitor when a frame is shown, so `render()` returns at the next refresh. Without it, `render()` returns as soon as the frame is drawn, e.g. for programs that keep real time themselves, like FlightSimBridge of HHMARL_3D_NEW. Programs that rely on vsync to run in real time, e.g. `Test_FlightSim_*.py` with `frame(1/60)`, then run faster. A change is shown at the top of the window for two seconds (not in videos), and the help message shows the current mode. `fullscreen` (key B) shows the window without its border over the whole monitor, or again at its previous position and size. A change of the window size stops a recording.

Keys: F1 cockpit, F2 follow, F3 overview of all aircraft (the mouse rotates around them and its wheel zooms, also A/D and W/S unless they fly the manual aircraft), F4 free camera (W/S and A/D move it at 2 km/s, holding Shift 10 times faster, holding Ctrl 10 times slower), Tab next aircraft, L labels, T trails, M minimap, N minimap north-up/heading-up, R start/stop recording a video, V vsync on/off, B full screen on/off, H help, Esc exit. With `manual=True`, the flight controls without connected joysticks: Up/Down pitch down/up, Left/Right roll, A/D rudder, W/S throttle, Space fire.

The JSBSim of FlightSim is the same version as the `jsbsim` Python package (1.3.0), simulated at the same rate. `.venv\Scripts\python Tools\Compare_JSBSim_Python_Cpp.py` (Linux: `.venv/bin/python Tools/Compare_JSBSim_Python_Cpp.py`) flies an F-16 with both and prints the differences.

HHMARL_3D_NEW uses FlightSim when cloned next to this repository (or at the folder in the environment variable `FLIGHTSIM_DIR`), see its README.


## JOYSTICKS, THROTTLES AND PEDALS

With `manual=True` (e.g. `MANUAL` in `test.py` of HHMARL_3D_NEW), the joysticks, throttles and pedals of `Tools/joysticks.json` fly the manual aircraft, e.g. a Thrustmaster HOTAS Warthog with the TPR pedals. The file maps each flight control to an axis or button of a device, which is identified by its name:

| Control | Values of the axis | `Tools/joysticks.json` |
|---|---|---|
| `roll` | -1 left to 1 right | axis 0 of `Joystick - HOTAS Warthog` |
| `pitch` | -1 forward (nose down) to 1 back (nose up) | axis 1 of `Joystick - HOTAS Warthog` |
| `throttle` | -1 idle to 1 full | axis 2 of `Throttle - HOTAS Warthog`, inverted, because it shows 1 at idle (a lever, axis 3 is the other one) |
| `rudder` | -1 left to 1 right | axis 2 of `TPR T.Pendular Rudder` (axes 0 and 1 are the toe brakes) |
| `fire` | (button) | button 0 of `Joystick - HOTAS Warthog` (trigger): the cannon fires while it is held |

- Each control comes from its device while the device is connected, otherwise from its keys (see the keys above). So without the devices, the keyboard flies the aircraft as before. With all devices, the keyboard and the mouse only control the views, labels, trails, recording and help, and A/D and W/S move the camera in the overview and the free view as without `manual`. Devices can be plugged in and unplugged while FlightSim runs, and they are also read while its window does not have the focus. FlightSim prints where the controls come from when it starts and when devices are plugged in or unplugged, and the help message (key H) shows it.
- `.venv\Scripts\python Tools\joysticks.py` (Linux: `.venv/bin/python Tools/joysticks.py`) shows the names of the connected devices and the numbers and values of their axes, buttons and hats while they are moved. FlightSim reads the devices with SDL 2.32.10 (Conan package `sdl`), the same SDL as pygame-ce 2.5.8 of `Tools/joysticks.py`, so it has the same device names and numbers. The names come from the operating system, so on Linux they can differ from the ones in `Tools/joysticks.json`.
- Optional settings of an axis: `"invert": true` reverses it, e.g. if the throttle shows 1 at idle, and `"deadzone": 0.05` (not for `throttle`) sets the values within 5% of the center to zero. Controls that are not in the file come from the keyboard. Keys starting with `_` are comments.
- The file is read when FlightSim starts, so changing it needs no rebuild. `flightsim_env.py` sets the environment variable `FLIGHTSIM_JOYSTICKS` to `Tools/joysticks.json`. Set it before to another file, or to an empty string to fly with the keyboard only.
- The rudder of the F-16 of JSBSim 1.3.0 barely deflects (about 5 of 30 degrees with full pedal), because the yaw damper of its flight control system resets the rudder every step. The manual aircraft therefore flies a corrected copy (`Assets/Movers/JSBSimManual`, see its `README.md` and the TODO above): the templates `SepecatJaguarJsb*Manual` in FlightSim, and HHMARL_3D_NEW for its manual agent in both JSBSim modes.


## GPU

FlightSim renders with OpenGL on the GPU that the operating system chooses for the Python process. Computers with two GPUs, e.g. laptops with an integrated Intel GPU and an NVIDIA GPU, often choose the integrated one, which renders FlightSim more than 10 times slower (e.g. 8 instead of 100 frames per second). `flightsim_env.py` therefore selects the dedicated GPU, and keeps the settings of the user:
- Windows: the graphics preference "High performance" of Windows (Settings > System > Display > Graphics) is set for the `python.exe` that runs FlightSim (the one of the Python installation, which the `python.exe` of a venv starts), unless it has a preference already. FlightSim prints when it sets it. To render on another GPU, choose another preference there.
- WSL2: the d3d12 driver of Mesa renders on the NVIDIA GPU of Windows if there is one (environment variable `MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA`), instead of the first GPU of Windows.
- Linux: with the NVIDIA driver and another GPU, the PRIME render offload of NVIDIA (`__NV_PRIME_RENDER_OFFLOAD=1`, `__GLX_VENDOR_LIBRARY_NAME=nvidia`); with an Intel GPU and another GPU, the other one (`DRI_PRIME`). The environment variables are only set if they are not set already.


## RENDERING SCENE

![Scene](Assets/scene.png)