# JSBSim aircraft of the manual aircraft

JSBSim models for the aircraft flown by a human (`FlightSim(manual=True)`), loaded instead of the ones of the JSBSim root directory:
by the entity templates `SepecatJaguarJsbBlueManual` and `SepecatJaguarJsbRedManual` (`"aircraftDir": "JSBSimManual"` of the
`jsbSimAircraft` component), and by HHMARL_3D_NEW for its manual agent (`flightsim_env.MANUAL_AIRCRAFT_DIR`).

`f16` is the F-16 of JSBSim 1.3.0 (`aircraft/f16` of the `jsbsim` Python package) with one change in the yaw channel of its
flight control system: the PID `fcs/yaw-load-pid` no longer writes its output into `fcs/rudder-pos-norm`. The rudder actuator
`fcs/rudder-position` starts every step from `fcs/rudder-pos-norm`, so in the original model the PID output reset the rudder to
small deflections every step: full pedal deflects the rudder by about 5 of 30 degrees, which gives about 1 degree of sideslip
and almost no yaw. With the change, full pedal deflects it by up to 30 degrees, with 4 to 7 degrees of sideslip at 130 to 250 m/s.

The RL agents of HHMARL_3D_NEW still use the original F-16, with which their policies were trained. For the same dynamics as the
human, the policies have to be trained again with this model (see the TODO in the README of FlightSim).
