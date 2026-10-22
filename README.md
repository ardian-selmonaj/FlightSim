# FlightSim 

This is a modified version of the project called [Skybolt](https://github.com/Prograda/Skybolt/tree/master). It is used as MARL environment for realistic simulations and human-agent interactions.

*TODO:* 

- Make it run on Linux
- Init location directly as instance creation of FlightSim(loc) for JSBSim, otherwise performance decrease!
- Read out state of JSBSim aircraft for Human-Agent Interaction --> partially done, see `test_flightsim.py` in [HHMARL3D](https://github.com/ardian-selmonaj/HHMARL_3D)
- Better to set init position, then orientation and velocity to let C++ enginge simulate future states instead of setting the attitude by Python-JSBSim.

## INSTALLING ON WINDOWS

1) In the local clone of this repository, run `setup_windows.bat` (double-click it or run it in a terminal). It does the complete setup. Each step is skipped if it is already done, so it can be re-run at any time, e.g., to rebuild after changing the C++ code:
- installs the VS 2022 Build Tools with the C++ build tools workload (MSVC v143, Windows SDK, C++ CMake tools for Windows) using `VSBuildTools.exe` of this repository (downloaded if missing). This step asks for administrator rights.
- loads the MSVC compiler environment, so the "Developer Command Prompt for VS 2022" is not needed
- creates the Python 3.11 virtual environment `.venv` with an installed Python 3.11. If there is none, Python 3.11.9 (the latest 3.11 with a Windows installer) is installed for the current user next to any other Python versions, without changing PATH, file associations or the default `python`. The venv gets `conan==2.22.0`, `jsbsim==1.3.3` and `requests`
- creates a default conan profile (if there is none) and builds the project with `compiler.cppstd=17` (needed for JSBSim) into `FlightSimBuild` next to the repository. The first build compiles all dependencies and takes long.
- sets the root directory of JSBSim in the Skybolt settings, located under "user\AppData\Local\Skybolt\Settings.json". The JSBSim aircraft data comes with the `jsbsim` Python package, so no separate JSBSim installation is needed. An existing valid `rootDir` is kept.
- checks that `import skybolt` works

- Options: `setup_windows.bat -BuildDir D:\FlightSimBuild` builds into another folder (then set the environment variable `FLIGHTSIM_BUILD_DIR` to that folder), `setup_windows.bat -PythonExe C:\path\to\python.exe` uses a Python 3.11 in a non-standard location.

2) for realistic rendering, run `.venv\Scripts\python Tools\Tile_Map_Downloader.py`. For Switzerland, the map tiles (zoom levels 0-14) are already available under the links [Terrain](https://drive.google.com/file/d/1lMife7PamdICDU4D7hoqI3tCQr1aqn9c/view?usp=sharing) and [Satellite](https://drive.google.com/file/d/1CQ3N1oqyjL1ly_6SO7m9wICEsS0f2zVm/view?usp=sharing), as zips of tiles that were already downloaded from Mapbox. The script downloads both zips and unzips them into `Assets\mapbox_terrain_ch` and `Assets\mapbox_satellite_ch`, where the entity `Assets\Globe\Entities\PlanetEarth.json` looks for the map tiles. Tiles that already exist in `Assets` are not downloaded again. If the download fails (e.g., the link expired), download the zips manually and unzip them into `Assets`.

- For increased depth (better details), further zoom levels need to be downloaded directly from Mapbox with the same script, e.g., `.venv\Scripts\python Tools\Tile_Map_Downloader.py --zoom 15 16`. Existing tiles are skipped. Afterwards, raise `maxLevel` in `PlanetEarth.json` accordingly. The area is set by `CENTER_LATITUDE`, `CENTER_LONGITUDE` and the ranges in the script. Note that the number of tiles grows about 4x per zoom level. Of course, you can use any other map tiles from Mapbox or any other source.

3) run `.venv\Scripts\python Test_FlightSim_Windows.py`. Python scripts using Skybolt only need `import flightsim_env` before `import skybolt` (see `flightsim_env.py`). For more Python Bindings, need to extend `FlightSim\src\Skybolt\SkyboltPythonBindings\PythonBindings.cpp` appropriately and re-run `setup_windows.bat`. 

*NOTE 1*: The repo and build folder need short paths, otherwise Windows build tools break if path exceeds 260 characters.

*NOTE 2*: when pushing with git, set `git config --global core.autocrlf false` to use LF accross all platforms.


## INSTALLING ON LINUX

*NOTE*: Currently, installation on Linux works. But, at time of development, running caused problems (buffer overflow). 

1) in Linux terminal, run: 

```
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip python3-venv pkg-config
```

Check to install Python 3.10 or 3.11. Check if all required components are installed:
```
g++ --version
cmake --version
python3 --version
```

2) create venv and install conan:
```
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install conan==2.22.0
```
and create a conan profile:
```
conan profile detect --force
```
which is usually stored under `~/.conan2/profiles/default`. Check that `compiler.cppstd=17` is set.

3) go to FlightSim directory and build the project:
```
conan build . \
  --output-folder=$HOME/FlightSimBuild \
  --build=missing \
  -o openscenegraph-mr/*:shared=True \
  -o enable_jsbsim=True \
  -o enable_python=True \
  - enable_qt = False
```

4) create a `settings.json` file in `~/Skybolt/Settings.json` with the most basic information:
```
{
    "clouds": {
        "enableTemporalUpscaling": true
    },
    "display": {
        "multiSampleCount": 4,
        "texturePoolSizeMB": 512,
        "vsync": true
    },
    "mouse": {
        "sensitivity": 1.0
    },
    "shadows": {
        "cascadeBoundingDistances": [
            0.02,
            20.0,
            70.0,
            250.0,
            7000
        ],
        "enabled": true,
        "textureSize": 2048
    },
    "tileApiKeys": {
        "bing": "",
        "mapbox": "pk.eyJ1IjoiYXJkaWFuLXNlbG1vbmFqIiwiYSI6ImNtbDVmYjlyMDAwYXkzZXMybGV6bWh2enAifQ.kYwMo1PfJVT2bmSzMK9n8g"
    },
    "jsb": {
        "rootDir": "None"
    }
}
```

5) Install JSBSim and provide the installation directory in `settings.json`. 


## RENDERING SCENE

![Scene](Assets/scene.png)