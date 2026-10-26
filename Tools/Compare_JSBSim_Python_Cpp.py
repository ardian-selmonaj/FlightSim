"""Compares the JSBSim of FlightSim (C++) with the jsbsim Python package.

Both simulate an F-16 from the same initial conditions with the same varying flight controls, initialized and
stepped like the aircraft of HHMARL_3D_NEW (env/sim.py), and the differences of the states are printed.
FlightSim runs without a window. Run it from the repository folder:
    .venv\\Scripts\\python Tools\\Compare_JSBSim_Python_Cpp.py     (Windows)
    .venv/bin/python Tools/Compare_JSBSim_Python_Cpp.py         (Linux)
"""
import math
import os
import sys
from pathlib import Path

import jsbsim

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import flightsim_env  # noqa: E402, F401  sets up paths and DLLs for skybolt
import skybolt as sb  # noqa: E402

RATE = 75.0      # JSBSim steps per second, as sim_freq of HHMARL_3D_NEW
SECONDS = 120.0  # simulated time

# Initial conditions as set by AircraftSim.reload() of HHMARL_3D_NEW: defaults first, then the initial state
INITIAL_CONDITIONS = {
    "ic/long-gc-deg": 0.0, "ic/lat-geod-deg": 0.0, "ic/h-sl-ft": 20000, "ic/psi-true-deg": 0.0,
    "ic/u-fps": 800.0, "ic/v-fps": 0.0, "ic/w-fps": 0.0,
    "ic/p-rad_sec": 0.0, "ic/q-rad_sec": 0.0, "ic/r-rad_sec": 0.0,
    "ic/roc-fpm": 0.0, "ic/terrain-elevation-ft": 0, "gear/gear-cmd-norm": 0.0,
    "propulsion/tank/contents-lbs": 1500,
}
INITIAL_STATE = {"ic/long-gc-deg": 8.965318, "ic/lat-geod-deg": 45.975305, "ic/h-sl-ft": 25000,
                 "ic/psi-true-deg": 30.0, "ic/u-fps": 800.0}

STATE = ["position/lat-geod-deg", "position/long-gc-deg", "position/h-sl-meters",
         "attitude/phi-deg", "attitude/theta-deg", "attitude/psi-deg",
         "velocities/v-north-fps", "velocities/v-east-fps", "velocities/v-down-fps"]


def controls(t):
    """Flight controls at time t: aileron, elevator, rudder, throttle"""
    return {
        "fcs/aileron-cmd-norm": 0.3 * math.sin(0.5 * t),
        "fcs/elevator-cmd-norm": -0.1 + 0.15 * math.sin(0.3 * t),
        "fcs/rudder-cmd-norm": 0.1 * math.sin(0.2 * t),
        "fcs/throttle-cmd-norm": 0.6 + 0.2 * math.sin(0.1 * t),
    }


def initialize(get_engine_count, init_engine, steady_state, set_property, run_ic):
    for name, value in {**INITIAL_CONDITIONS, **INITIAL_STATE}.items():
        set_property(name, value)
    if not run_ic():
        raise RuntimeError("JSBSim failed to initialize")
    for i in range(get_engine_count()):
        init_engine(i)
    steady_state()


def main():
    # Python
    fdm = jsbsim.FGFDMExec(os.path.dirname(jsbsim.__file__))
    fdm.set_debug_level(0)
    fdm.load_model("f16")
    fdm.set_dt(1.0 / RATE)
    propulsion = fdm.get_propulsion()
    initialize(propulsion.get_num_engines, lambda i: propulsion.get_engine(i).init_running(),
               propulsion.get_steady_state, fdm.set_property_value, fdm.run_ic)

    # C++
    app = sb.FlightSim(headless=True, dynamicsRate=RATE)
    app.add_aircraft(1, "SepecatJaguarJsbBlue", sb.LatLonAlt(0, 0, 0))
    app.set_external_control(1, True)
    initialize(lambda: app.get_num_engines(1), lambda i: app.init_engine_running(1, i), lambda: app.get_steady_state(1),
               lambda name, value: app.set_property(1, name, value), lambda: app.run_ic(1))

    print(f"jsbsim Python {jsbsim.__version__}, {RATE:g} Hz, {SECONDS:g} s")
    print(f"{'time':>6} {'position [m]':>13} {'altitude [m]':>13} {'attitude [deg]':>15} {'velocity [ft/s]':>16}")
    max_differences = [0.0, 0.0, 0.0, 0.0]
    for step in range(int(SECONDS * RATE)):
        t = step / RATE
        for name, value in controls(t).items():
            fdm.set_property_value(name, value)
            app.set_property(1, name, value)
        fdm.run()
        app.step(1)
        # Unlimited fuel, as in HHMARL_3D_NEW
        fdm.set_property_value("propulsion/tank/contents-lbs", 1500)
        app.set_property(1, "propulsion/tank/contents-lbs", 1500)

        python = [fdm.get_property_value(name) for name in STATE]
        cpp = app.get_properties(1, STATE)
        north = (cpp[0] - python[0]) * 111132.0
        east = (cpp[1] - python[1]) * 111320.0 * math.cos(math.radians(python[0]))
        differences = [
            math.hypot(north, east),
            abs(cpp[2] - python[2]),
            max(abs((c - p + 180) % 360 - 180) for c, p in zip(cpp[3:6], python[3:6])),
            max(abs(c - p) for c, p in zip(cpp[6:9], python[6:9])),
        ]
        max_differences = [max(m, d) for m, d in zip(max_differences, differences)]
        if (step + 1) % int(10 * RATE) == 0:
            print(f"{t + 1 / RATE:6.1f} {differences[0]:13.6f} {differences[1]:13.6f} {differences[2]:15.6f} {differences[3]:16.6f}")

    print(f"{'max':>6} {max_differences[0]:13.6f} {max_differences[1]:13.6f} {max_differences[2]:15.6f} {max_differences[3]:16.6f}")


if __name__ == "__main__":
    main()
