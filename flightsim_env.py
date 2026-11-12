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

MAPBOX_TOKEN_FILE = REPO_DIR / "Tools" / "mapbox_token.txt"
JOYSTICKS_FILE = REPO_DIR / "Tools" / "joysticks.json"
# JSBSim models of the aircraft flown by a human, e.g. the F-16 with the corrected rudder (see its README.md)
MANUAL_AIRCRAFT_DIR = ASSETS / "Movers" / "JSBSimManual"


def read_mapbox_token():
    """Returns the Mapbox access token of MAPBOX_TOKEN_FILE (the first line that is not empty and no # comment),
    or None if the file does not exist or contains no token."""
    if MAPBOX_TOKEN_FILE.is_file():
        for line in MAPBOX_TOKEN_FILE.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                return line
    return None


def use_dedicated_gpu():
    """Lets FlightSim render on the dedicated GPU of computers with two GPUs, e.g. laptops with an integrated Intel GPU and an
    NVIDIA GPU, whose integrated GPU otherwise renders the OpenGL of Python several times slower. Settings of the user are kept."""
    if sys.platform == "win32":
        # Windows renders on the integrated GPU unless the program has the graphics preference "High performance" (Settings >
        # System > Display > Graphics). It is set for the program of this process, the python.exe of the Python installation (the
        # python.exe of a venv only starts it), and applies to the windows that are opened afterwards, also by this process.
        import winreg
        buffer = ctypes.create_unicode_buffer(32768)
        if not ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer)):
            return
        program = buffer.value
        try:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\DirectX\UserGpuPreferences", 0,
                                    winreg.KEY_QUERY_VALUE | winreg.KEY_SET_VALUE) as key:
                try:
                    winreg.QueryValueEx(key, program)
                    return  # preference of the user
                except FileNotFoundError:
                    winreg.SetValueEx(key, program, 0, winreg.REG_SZ, "GpuPreference=2;")
            print(f"FlightSim: set the graphics preference of {program} to high performance (Windows Settings > System > "
                  "Display > Graphics), so that it renders on the dedicated GPU")
        except OSError as e:
            print(f"FlightSim: could not set the graphics preference of {program}: {e}")
    elif os.path.exists("/dev/dxg"):
        # Under WSL2, the d3d12 driver of Mesa renders on a GPU of Windows, by default the first one, the integrated GPU of laptops
        if os.path.exists("/usr/lib/wsl/lib/nvidia-smi"):
            os.environ.setdefault("MESA_D3D12_DEFAULT_ADAPTER_NAME", "NVIDIA")
    else:
        # Vendor IDs of the graphics cards by PCI address
        cards = {}
        for device in Path("/sys/class/drm").glob("card*/device"):
            if re.fullmatch(r"card\d+", device.parent.name):
                try:
                    cards[device.resolve().name] = (device / "vendor").read_text().strip()
                except OSError:
                    pass
        nvidia, intel = "0x10de", "0x8086"
        if len(cards) < 2:
            return
        if nvidia in cards.values() and os.path.isdir("/proc/driver/nvidia/gpus"):
            # PRIME render offload of the NVIDIA driver
            os.environ.setdefault("__NV_PRIME_RENDER_OFFLOAD", "1")
            os.environ.setdefault("__GLX_VENDOR_LIBRARY_NAME", "nvidia")
        elif intel in cards.values():
            # Mesa renders on the GPU of DRI_PRIME, here the first one that is not the integrated Intel GPU
            dedicated = sorted(address for address, vendor in cards.items() if vendor != intel)
            if dedicated:
                os.environ.setdefault("DRI_PRIME", "pci-" + re.sub(r"[:.]", "_", dedicated[0]))


def file_version(path):
    """Returns the file version of a Windows DLL or EXE as a tuple, e.g. (14, 44, 35211, 0), or None."""
    version_dll = ctypes.WinDLL("version")
    size = version_dll.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return None
    info = ctypes.create_string_buffer(size)
    value, length = ctypes.c_void_p(), ctypes.c_uint()
    if not (version_dll.GetFileVersionInfoW(str(path), 0, size, info)
            and version_dll.VerQueryValueW(info, "\\", ctypes.byref(value), ctypes.byref(length))):
        return None
    fixed = ctypes.cast(value, ctypes.POINTER(ctypes.c_uint32 * 4)).contents  # VS_FIXEDFILEINFO up to dwFileVersionLS
    return fixed[2] >> 16, fixed[2] & 0xFFFF, fixed[3] >> 16, fixed[3] & 0xFFFF


def check_cpp_runtime():
    """Warns if the Visual C++ runtime (msvcp140.dll) that the skybolt module will use is older than the Visual C++ that built
    it. Such a runtime is not compatible and crashes skybolt with an access violation (exit code 0xC0000005)."""
    import sysconfig
    module = RELEASE / ("skybolt" + sysconfig.get_config_var("EXT_SUFFIX"))
    if not module.is_file():
        return  # importing skybolt reports it
    # Linker version in the PE header of the module, which is the version of Visual C++ that built it, e.g. (14, 44)
    with open(module, "rb") as f:
        header = f.read(4096)
    pe = int.from_bytes(header[0x3C:0x40], "little")
    built_with = (header[pe + 26], header[pe + 27])

    # Loaded with the same search order as the module loads it (or the one already loaded), so the module gets this one
    runtime = ctypes.WinDLL("msvcp140.dll")
    kernel32 = ctypes.WinDLL("kernel32")
    kernel32.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
    buffer = ctypes.create_unicode_buffer(32768)
    if not kernel32.GetModuleFileNameW(runtime._handle, buffer, len(buffer)):
        return
    version = file_version(buffer.value)
    if version and version[:2] < built_with:
        print(f"FlightSim: WARNING: the Visual C++ runtime {buffer.value} (version {'.'.join(map(str, version))}) is older than "
              f"the Visual C++ {built_with[0]}.{built_with[1]} that built skybolt, which can then crash with an access "
              "violation (exit code 0xC0000005). Install the latest Visual C++ Redistributable "
              "(https://aka.ms/vs/17/release/vc_redist.x64.exe). If the runtime is not the one in C:\\Windows\\System32, "
              "another module imported before flightsim_env loaded it: import flightsim_env first.")


if not RUN_ENV_FILE.is_file():
    raise ImportError(f"FlightSim build not found in '{SKYBOLT_DIR}'. Run {SETUP_SCRIPT} or set FLIGHTSIM_BUILD_DIR.")

# Set plugin path BEFORE importing skybolt
os.environ["SKYBOLT_PLUGINS_PATH"] = str(PLUGIN)

# Skybolt searches "Assets/" and "../Assets/" relative to the working directory. Add the repo's Assets
# so that scripts also run from other directories, but not twice (packages would be registered twice).
if not any((Path.cwd() / d).resolve() == ASSETS for d in ("Assets", "../Assets")):
    os.environ["SKYBOLT_ASSETS_PATH"] = os.pathsep.join(filter(None, [str(ASSETS), os.environ.get("SKYBOLT_ASSETS_PATH")]))

use_dedicated_gpu()

if sys.platform == "win32":
    # Run the bat and capture its environment
    cmd = f'cmd /c "call \"{RUN_ENV_FILE}\" && set"'
    env = subprocess.check_output(cmd, shell=True, text=True)

    for line in env.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            os.environ[k] = v

    # Python loads the DLLs of extension modules from the folders registered with os.add_dll_directory and from System32, not
    # from PATH. Register the folders that the run environment of the build puts on PATH (OpenSceneGraph), but not the rest of
    # PATH: programs there often ship an older Visual C++ runtime (msvcp140.dll), which is searched before the one of Windows
    # and crashes skybolt with an access violation (exit code 0xC0000005) when it starts its threads.
    for paths in re.findall(r'set "PATH=([^"]*)"', RUN_ENV_FILE.read_text(encoding="utf-8")):
        for p in paths.split(";"):
            if p and "%" not in p and os.path.isdir(p):
                os.add_dll_directory(p)

    check_cpp_runtime()
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

# ffmpeg encodes the videos recorded with key R: the one of the Python package imageio-ffmpeg if it is installed, otherwise
# skybolt uses ffmpeg from PATH. Set FLIGHTSIM_FFMPEG to use another ffmpeg.
if not os.environ.get("FLIGHTSIM_FFMPEG"):
    try:
        import imageio_ffmpeg
        os.environ["FLIGHTSIM_FFMPEG"] = imageio_ffmpeg.get_ffmpeg_exe()
    except (ImportError, RuntimeError):
        pass

# The map tiles come from Assets (offline). With a Mapbox token, the tiles missing there are loaded from Mapbox,
# so that the whole earth is shown. The token is read from MAPBOX_TOKEN_FILE, unless FLIGHTSIM_MAPBOX_TOKEN is set.
if not os.environ.get("FLIGHTSIM_MAPBOX_TOKEN"):
    token = read_mapbox_token()
    if token:
        os.environ["FLIGHTSIM_MAPBOX_TOKEN"] = token

# The manual aircraft is flown with the joysticks, throttles and pedals of JOYSTICKS_FILE while they are connected, otherwise with
# the keyboard. Set FLIGHTSIM_JOYSTICKS to use another file, or to an empty string to fly with the keyboard only.
if "FLIGHTSIM_JOYSTICKS" not in os.environ and JOYSTICKS_FILE.is_file():
    os.environ["FLIGHTSIM_JOYSTICKS"] = str(JOYSTICKS_FILE)

# Ensure skybolt module is found
sys.path.append(str(RELEASE))
