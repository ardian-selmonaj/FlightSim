# FlightSim 

This is a modified version of the project called [Skybolt](https://github.com/Prograda/Skybolt/tree/master). It is used as MARL environment for realistic simulations and human-agent interactions.

## INSTALLING ON WINDOWS

1) install VS Build Tools and use c++ build tools. Make sure to have selected:
- C++ build tools workload
- MSVC v143
- Windows 10 or 11 SDK
- C++ CMake tools for Windows

2) Open "Developer Command Prompt for VS 2022" and type `cl` and `cmake --version` to check if both return something.

3) `pip install conan==2.22.0` and create a default profile: `conan profile detect --force`. For JSBSim, need to set `compiler.cppstd=17` (instead of 14) in conan profile, located under "user\\.conan2"

4) go to the directory of the local clone of this repository, e.g., "\user\FlightSim" and run `conan build . --output-folder=\user\FlightSimBuild --build==missing -o openscenegraph-mr/*:shared=True -o enable_jsbsim=True -o enable_python=True`.

5) install [JSBSSim](https://github.com/JSBSim-Team/jsbsim?tab=readme-ov-file#installation), go to Skybolt settings, located under "user\AppData\Local\Skybolt\settings.json" and provide the root directory of JSBSim as: 

```
    "jsb": {
        "rootDir": "C:/Users/thisuser/JSBSim"
    }
```

6) for realistic rendering, download the map tiles of Switzerland: [Terrain](https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQCWFoPOCBLaQ6p837Vod2coAbW9juV3RTQxCuAd-rjkjiA?e=TWTKNb) and [Satellite](https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQC3RLT8KjYKSKEZxr-s7Rm6AQ3ukCzpFAOT0MRr-jqHN5Y?e=oOqqbS). Of course, you can download any other Map tiles from Mapbox or any other source. Unzip and store the folders into `Assets`. The entity under `Assets\Globe\Entities\PlanetEarth.json` will look for these map tiles for rendering. 

7) run python script `Test_MultiAgent.py`. For more Python Bindings, need to extend `FlightSim\src\Skybolt\SkyboltPythonBindings\PythonBindings.cpp` appropriately. 


## INSTALLING ON LINUX

Instructions will follow


## RENDERING SCENE

![Scene](Assets/scene.png)