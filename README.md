# FlightSim 

This is a modified version of the project called [Skybolt](https://github.com/Prograda/Skybolt/tree/master). It is used as MARL environment for realistic simulations and human-agent interactions.

*TODO:* 

- Make it run on Linux
- Init location directly as instance creation of FlightSim(loc) for JSBSim, otherwise performance decrease!
- Read out state of JSBSim aircraft for Human-Agent Interaction --> partially done, see `test_flightsim.py` in [HHMARL3D](https://github.com/ardian-selmonaj/HHMARL_3D)
- Better to set init position, then orientation and velocity to let C++ enginge simulate future states instead of setting the attitude by Python-JSBSim.

## INSTALLING ON WINDOWS

1) install VS Build Tools and use c++ build tools. Make sure to have selected:
- C++ build tools workload
- MSVC v143
- Windows 10 or 11 SDK
- C++ CMake tools for Windows

2) Open "Developer Command Prompt for VS 2022" and type `cl` and `cmake --version` to check if both return something.

3) `pip install conan==2.22.0` and create a default profile: `conan profile detect --force`. For JSBSim, need to set `compiler.cppstd=17` (instead of 14) in conan profile, located under "user\\.conan2"

4) go to the directory of the local clone of this repository, e.g., "\user\FlightSim" and run 
```conan build . --output-folder=\user\FlightSimBuild --build=missing -o openscenegraph-mr/*:shared=True -o enable_jsbsim=True -o enable_python=True -o enable_qt=False```.

5) install [JSBSSim](https://github.com/JSBSim-Team/jsbsim?tab=readme-ov-file#installation), go to Skybolt settings, located under "user\AppData\Local\Skybolt\settings.json" and provide the root directory of JSBSim as: 

```
    "jsb": {
        "rootDir": "C:/Users/thisuser/JSBSim"
    }
```

6) for realistic rendering, download the map tiles of Switzerland: [Terrain](https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQCWFoPOCBLaQ6p837Vod2coAbW9juV3RTQxCuAd-rjkjiA?e=TWTKNb) and [Satellite](https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQC3RLT8KjYKSKEZxr-s7Rm6AQ3ukCzpFAOT0MRr-jqHN5Y?e=oOqqbS). Of course, you can download any other Map tiles from Mapbox or any other source. Unzip and store the folders into `Assets`. The entity under `Assets\Globe\Entities\PlanetEarth.json` will look for these map tiles for rendering. 

7) run python script `Test_MultiAgent.py`. For more Python Bindings, need to extend `FlightSim\src\Skybolt\SkyboltPythonBindings\PythonBindings.cpp` appropriately. 

*NOTE*: when pushing with git, set `git config --global core.autocrlf false` to use LF accross all platforms.


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