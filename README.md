# FlightSim 

This is a modified version of the project called [Skybolt](https://github.com/Prograda/Skybolt/tree/master). It is used as MARL environment for realistic simulations and human-agent interactions.

The MARL air combat environment and its pre-trained policies are in [HHMARL_3D_NEW](https://github.com/ardian-selmonaj/HHMARL_3D_NEW), which uses FlightSim for rendering, as JSBSim simulator and for human-agent interaction (see [Using FlightSim from Python](#using-flightsim-from-python)).

*TODO:* 

- Human-Agent Interaction with dedicated joysticks, rudder and pedals (for now, only the keyboard)


## INSTALLING ON WINDOWS

1) In the local clone of this repository, run `setup_windows.bat` (double-click it or run it in a terminal). It does the complete setup. Each step is skipped if it is already done, so it can be re-run at any time. After changing C++ code, the faster `Tools\update_pythonbindings_windows.bat` is usually enough, see [Updating the Python bindings](#updating-the-python-bindings):
- installs the VS 2022 Build Tools with the C++ build tools workload (MSVC v143, Windows SDK, C++ CMake tools for Windows) using `Tools\VSBuildTools.exe` of this repository (downloaded if missing). This step asks for administrator rights.
- loads the MSVC compiler environment, so the "Developer Command Prompt for VS 2022" is not needed
- creates the Python 3.11 virtual environment `.venv` with an installed Python 3.11. If there is none, Python 3.11.9 (the latest 3.11 with a Windows installer) is installed for the current user next to any other Python versions, without changing PATH, file associations or the default `python`. The venv gets `conan==2.22.0`, `jsbsim==1.3.0`, `requests`, `imageio-ffmpeg==0.6.0` (its ffmpeg encodes the videos recorded with key R) and `reportlab==5.1.0` (for `Tools\build_instructions_pdf.py`)
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
- creates the Python 3.11 virtual environment `.venv` with [uv](https://docs.astral.sh/uv/), which is installed into `~/.local/bin` if it is missing. If there is no Python 3.11 managed by uv, uv installs one for the current user next to any other Python versions, without changing the system Python or the default `python`. The venv gets `conan==2.22.0`, `jsbsim==1.3.0`, `requests`, `imageio-ffmpeg==0.6.0` (its ffmpeg encodes the videos recorded with key R), `reportlab==5.1.0` (for `Tools/build_instructions_pdf.py`) and `pip`
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
- `add_aircraft(id, template, position, rpy, velocityNed)` adds an aircraft of an entity template of `Assets/Movers/Entities` at its initial position (`sb.LatLonAlt` with geodetic latitude and longitude in radians, altitude in meters above sea level), roll, pitch and yaw (radians, relative to north-east-down) and velocity (north, east, down in m/s). Templates without flight dynamics, e.g. `SepecatJaguarBlue`, are moved from Python with `set_aircraft_state(id, position, rpy, velocityNed)`. Templates with a `jsbSimAircraft` component, e.g. `SepecatJaguarJsbBlue` (JSBSim model `f16`), are simulated by JSBSim, starting at the given state.
- JSBSim aircraft: `get_property(id, name)`, `get_properties(id, names)`, `set_property(id, name, value)` and `query_property_catalog(id)` access the JSBSim properties, e.g. `fcs/aileron-cmd-norm`. Setting the `ic/...` properties and calling `run_ic(id)`, `init_engine_running(id, engine)` and `get_steady_state(id)` initializes the aircraft as the `jsbsim` Python package does. By default, the flight controls come from the keyboard (manual aircraft); after `set_external_control(id, True)` only from the properties.
- `step(n)` advances the simulation by `n` steps and `render()` renders a frame (`False` once the window is closed or escape is pressed). `frame(dt)` does both for a time `dt`.
- `manual=True`: the keyboard flies the aircraft of `set_manual_aircraft(id)`, whose controls are also returned by `get_manual_controls()`. `consume_fire_requests()` returns how often space was pressed.
- `set_camera_target(id)`, `select_camera_view("Cockpit" | "Follow" | "Overview" | "Free")`, `set_aircraft_visible(id, visible)`, `set_dynamics_enabled(id, enabled)` and `remove_aircraft(id)` change the view and the aircraft, `fire_tracer(id)` shows a cannon shot as a short dark line (`fire_rocket` is the old name). The overview shows all visible aircraft from slightly above, from behind the camera target towards the others.
- `set_aircraft_label(id, label)` shows a label above the aircraft, e.g. its name, `set_aircraft_color(id, (r, g, b))` sets the (linear) color of the label and of the trail, the flown path drawn behind the aircraft. `labels_visible`, `trails_visible`, `trail_length` (m, default 30 km) and `clear_trails()` control them.
- `start_recording(path=None)` records the view of the window into an MP4 video until `stop_recording()`, the window is closed or the `FlightSim` is deleted. Key R starts and stops recording as well, and a red dot in the top left corner shows that it records. The video shows the view as in the window (the selected camera, with the labels, trails and cockpit HUD if they are shown), but not the help message and the red dot. Without a path, the video is a new file named after the date and time in `recording_directory` (default `recordings`, relative to the current directory). It has `recording_fps` (default 30) frames per second of simulation time, each taken from the simulation step closest to its time, so it plays at the speed of the simulation, however fast it is rendered (at 75 Hz, the frames are alternately 2 and 3 steps apart). Programs that skip frames to keep up with real time should render those for which `recording_frame_due` is `True`, otherwise frames repeat in the video. The ffmpeg that encodes the video is the one of the Python package `imageio-ffmpeg` (`flightsim_env.py` sets the environment variable `FLIGHTSIM_FFMPEG` to it), otherwise ffmpeg from PATH.

Keys: F1 cockpit, F2 follow, F3 overview of all aircraft (the mouse rotates around them and its wheel zooms, without `manual` also A/D and W/S), F4 free camera, Tab next aircraft, L labels, T trails, R start/stop recording a video, H help, Esc exit. With `manual=True`: Up/Down pitch down/up, Left/Right roll, A/D rudder, W/S throttle, Space fire.

The JSBSim of FlightSim is the same version as the `jsbsim` Python package (1.3.0), simulated at the same rate. `.venv\Scripts\python Tools\Compare_JSBSim_Python_Cpp.py` (Linux: `.venv/bin/python Tools/Compare_JSBSim_Python_Cpp.py`) flies an F-16 with both and prints the differences.

HHMARL_3D_NEW uses FlightSim when cloned next to this repository (or at the folder in the environment variable `FLIGHTSIM_DIR`), see its README.


## RENDERING SCENE

![Scene](Assets/scene.png)