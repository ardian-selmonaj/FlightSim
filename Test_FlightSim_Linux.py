import flightsim_env  # sets up paths and shared libraries for skybolt, see flightsim_env.py
import skybolt as sb
import math

# sb.convertToOSGT("Assets/Movers/Models/SepecatJaguarBlue/SepecatJaguar_Fuselage.osgb")

#Lugano
LON = 8.965318
LAT = 45.975305

# Baldegg
# LON = 8.274841
# LAT = 47.182253

def lla(lat_deg, lon_deg, alt_m):
    return sb.LatLonAlt(math.radians(lat_deg), math.radians(lon_deg), float(alt_m))

def rpy(roll_deg, pitch_deg, yaw_deg):
    return sb.Vector3(math.radians(roll_deg), math.radians(pitch_deg), math.radians(yaw_deg))

app = sb.FlightSim(10, 30, 1080, 720, manual=True, multiwindow=False)

# Aircraft 1 is simulated by JSBSim (template with a jsbSimAircraft component) and flown with the keyboard.
# Its initial conditions are the position, orientation and velocity (north, east, down in m/s) given here.
app.add_aircraft(1, "SepecatJaguarJsbBlue", position=lla(LAT, LON, 3300), rpy=rpy(0, 0, 0), velocityNed=sb.Vector3(150, 0, 0))
app.set_manual_aircraft(1)

# Aircraft 2 has no flight dynamics and is moved from Python
app.add_aircraft(2, "SepecatJaguarRed", position=lla(LAT, LON, 3050), rpy=rpy(0, 0, 90))

# Labels and trails (the flown path) in team colors, toggled with L and T
app.set_aircraft_label(1, "B1")
app.set_aircraft_color(1, (0.05, 0.3, 1.0))
app.set_aircraft_label(2, "R2")
app.set_aircraft_color(2, (1.0, 0.06, 0.04))

dt = 1.0 / 60.0
i = 0

while True:
    app.set_aircraft_state(2, position=lla(LAT, LON, 3050 + i), rpy=rpy(0, 0, 90))

    # Space fires the cannon (shown as tracers)
    if app.consume_fire_requests():
        app.fire_tracer(1)

    if not app.frame(dt):
        break
    i += 1

    # if i == 75:
    #     app.remove_aircraft(2)
