"""Builds FlightSim/instructions.pdf: components, architecture and parameters of FlightSim and HHMARL_3D_NEW."""
import sys
from xml.sax.saxutils import escape

from reportlab.graphics.shapes import Drawing, Group, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.pdfmetrics import registerFontFamily
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (CondPageBreak, ListFlowable, ListItem, Paragraph, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

OUT = sys.argv[1] if len(sys.argv) > 1 else "instructions.pdf"

FONTS = r"C:\Windows\Fonts"
pdfmetrics.registerFont(TTFont("UI", rf"{FONTS}\segoeui.ttf"))
pdfmetrics.registerFont(TTFont("UI-Bold", rf"{FONTS}\segoeuib.ttf"))
pdfmetrics.registerFont(TTFont("UI-Italic", rf"{FONTS}\segoeuii.ttf"))
pdfmetrics.registerFont(TTFont("UI-Semibold", rf"{FONTS}\seguisb.ttf"))
pdfmetrics.registerFont(TTFont("Mono", rf"{FONTS}\consola.ttf"))
pdfmetrics.registerFont(TTFont("Mono-Bold", rf"{FONTS}\consolab.ttf"))
registerFontFamily("UI", normal="UI", bold="UI-Bold", italic="UI-Italic", boldItalic="UI-Bold")
registerFontFamily("Mono", normal="Mono", bold="Mono-Bold", italic="Mono", boldItalic="Mono-Bold")

INK = colors.HexColor("#1D2733")
MUTED = colors.HexColor("#5A6675")
ACCENT = colors.HexColor("#1F4E79")
RULE = colors.HexColor("#C9D3DE")
HEAD_BG = colors.HexColor("#E8EEF5")
ZEBRA = colors.HexColor("#F7F9FB")
HH_FILL, HH_LINE, HH_TEXT = colors.HexColor("#F1F7EF"), colors.HexColor("#7FA77A"), colors.HexColor("#2E5A2A")
FS_FILL, FS_LINE, FS_TEXT = colors.HexColor("#EEF3FA"), colors.HexColor("#7B9CC9"), colors.HexColor("#22487A")
BR_FILL, BR_LINE, BR_TEXT = colors.HexColor("#FFF6E5"), colors.HexColor("#D19A35"), colors.HexColor("#7A5212")

PAGE_W, PAGE_H = A4
MARGIN = 18 * mm
CONTENT_W = PAGE_W - 2 * MARGIN

body = ParagraphStyle("body", fontName="UI", fontSize=9, leading=12.4, textColor=INK, spaceAfter=4)
small = ParagraphStyle("small", parent=body, fontSize=8, leading=10.4, textColor=MUTED, spaceAfter=2)
cell = ParagraphStyle("cell", parent=body, fontSize=8, leading=10.2, spaceAfter=0)
cell_head = ParagraphStyle("cell_head", parent=cell, fontName="UI-Semibold", textColor=ACCENT)
h1 = ParagraphStyle("h1", fontName="UI-Bold", fontSize=14, leading=18, textColor=ACCENT, spaceBefore=10, spaceAfter=5)
h2 = ParagraphStyle("h2", fontName="UI-Semibold", fontSize=10.5, leading=14, textColor=INK, spaceBefore=8, spaceAfter=3)
title = ParagraphStyle("title", fontName="UI-Bold", fontSize=21, leading=26, textColor=INK, alignment=TA_LEFT)
subtitle = ParagraphStyle("subtitle", fontName="UI", fontSize=12, leading=16, textColor=ACCENT, spaceAfter=2)


def c(text):
    """Code in the monospace font."""
    return f'<font face="Mono">{escape(text)}</font>'


def q(text):
    """Quoted string in the monospace font, e.g. "python" (Python 3.11 f-strings cannot contain the quotes)."""
    return c('"' + text + '"')


def win(path):
    """Windows path from a path with slashes (Python 3.11 f-strings cannot contain backslashes)."""
    return path.replace("/", "\\")


JSBSIM_CPP = c('JSBSIM_STATES = "c++"')
TEMPLATES_EXAMPLE = c('{"R": "SepecatJaguarRedBig"}')
OVERRIDES_EXAMPLE = c('{"battle_field_center": [lon, lat, 0.0]}')
KWARGS_FLIGHTSIM = c('kwargs["flightsim"]')
RUN_NAME_DEFAULT = c('run_name ""')


def p(text, style=body):
    return Paragraph(text, style)


def bullets(items, style=body):
    return ListFlowable([ListItem(p(t, style), leftIndent=12) for t in items], bulletType="bullet", start="\u2022",
                        leftIndent=12, bulletFontName="UI", bulletFontSize=9)


def table(header, rows, widths, keep=True):
    """Table of paragraphs with a header row; widths as fractions of the content width."""
    data = [[p(h, cell_head) for h in header]] + [[p(v, cell) for v in row] for row in rows]
    t = Table(data, colWidths=[w * CONTENT_W for w in widths], repeatRows=1, hAlign="LEFT")
    style = [
        ("BACKGROUND", (0, 0), (-1, 0), HEAD_BG),
        ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACCENT),
        ("LINEBELOW", (0, 1), (-1, -1), 0.3, RULE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 2.6),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3.2),
    ]
    style += [("BACKGROUND", (0, r), (-1, r), ZEBRA) for r in range(2, len(data), 2)]
    t.setStyle(TableStyle(style))
    return t


def section(text):
    return [CondPageBreak(60 * mm), p(text, h1)]


def subsection(text, *flowables):
    """Heading followed by its flowables, starting on a new page if less than 45 mm are left; tables split."""
    return [CondPageBreak(45 * mm), p(text, h2), *flowables]


# --- architecture diagram ----------------------------------------------------------------------------------------

def fit_font(text, font, size, width, min_size=5.5):
    while size > min_size and pdfmetrics.stringWidth(text, font, size) > width:
        size -= 0.1
    return size


def box(g, x, y, w, h, title_text, lines, fill, line, text_color, title_size=7.8, line_size=6.9):
    g.add(Rect(x, y, w, h, rx=3, ry=3, fillColor=fill, strokeColor=line, strokeWidth=0.8))
    top = y + h - 11
    g.add(String(x + 5, top, title_text, fontName="UI-Bold", fontSize=fit_font(title_text, "UI-Bold", title_size, w - 10),
                 fillColor=text_color))
    for i, text in enumerate(lines):
        g.add(String(x + 5, top - 10 - 9 * i, text, fontName="UI", fontSize=fit_font(text, "UI", line_size, w - 10),
                     fillColor=INK))


def arrow(g, points, color=colors.HexColor("#4A5562"), dashed=False, both=False):
    """Polyline with an arrow head at the end (and at the start if both)."""
    dash = [3, 2] if dashed else None
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        g.add(Line(x0, y0, x1, y1, strokeColor=color, strokeWidth=0.9, strokeDashArray=dash))

    def head(tip, prev):
        (tx, ty), (px, py) = tip, prev
        dx, dy = tx - px, ty - py
        length = max((dx * dx + dy * dy) ** 0.5, 1e-6)
        ux, uy = dx / length, dy / length
        size, half = 5.0, 2.4
        bx, by = tx - ux * size, ty - uy * size
        g.add(Polygon([tx, ty, bx - uy * half, by + ux * half, bx + uy * half, by - ux * half],
                      fillColor=color, strokeColor=color, strokeWidth=0.3))

    head(points[-1], points[-2])
    if both:
        head(points[0], points[1])


def architecture_diagram():
    W, H = CONTENT_W, 292
    d = Drawing(W, H)
    g = Group()
    panel_w = 0.335 * W
    gap = (W - 2 * panel_w)
    right_x = W - panel_w
    row_h, row_gap, top_pad = 33, 10, 24
    rows = [H - top_pad - row_h - i * (row_h + row_gap) for i in range(6)]
    inner_w = panel_w - 14

    # Panels
    for x, fill, line, color, name, sub in ((0, HH_FILL, HH_LINE, HH_TEXT, "HHMARL_3D_NEW", "Python"),
                                            (right_x, FS_FILL, FS_LINE, FS_TEXT, "FlightSim", "C++ / Skybolt")):
        g.add(Rect(x, 2, panel_w, H - 4, rx=5, ry=5, fillColor=fill, strokeColor=line, strokeWidth=0.9))
        g.add(String(x + 7, H - 16, name, fontName="UI-Bold", fontSize=9.5, fillColor=color))
        g.add(String(x + 7 + pdfmetrics.stringWidth(name, "UI-Bold", 9.5) + 5, H - 16, sub, fontName="UI",
                     fontSize=8, fillColor=MUTED))

    left = [
        ("test.py \u00b7 train.py", ["inference and training loops", "settings: test.py, args.py"]),
        ("algorithm/", ["actor and critic networks", "PPO \u00b7 SPO \u00b7 MARS"]),
        ("env/env.py", ["AircombatMARL \u2192 AircraftEnv", "or AircraftEnvHier (commander)"]),
        ("env/missions.py \u00b7 env/models.py", ["trainable tasks, opponents,", "HumanModel (joysticks, keyboard)"]),
        ("env/sim.py", ["AircraftSim: one JSBSim 1.3.0", "per aircraft at 75 Hz, CannonSim"]),
        ("env/configs/*.yaml", ["base, hier and level configs:", "aircraft, missions, rewards"]),
    ]
    right = [
        ("skybolt (Python module)", ["PythonBindings.cpp: sb.FlightSim", "aircraft, JSBSim, keyboard, joysticks"]),
        ("FlightSimVisuals.h \u00b7 FlightSimRecording.h", ["overview camera, trails, tracers, minimap,", "WEZ, labels, MP4 video (ffmpeg)"]),
        ("SkyboltEngine", ["entity templates, sim \u2192 visual", "binding, cameras, input"]),
        ("SkyboltSim", ["world, entities, SimStepper", "(1/dynamicsRate), FdmComponent"]),
        ("JsbSim plugin", ["JsbSimAircraftComponent", "JSBSim 1.3.0 (templates *Jsb*)"]),
        ("SkyboltVis", ["OpenSceneGraph: terrain, sky,", "clouds, ocean, shadows"]),
    ]
    for y, (name, lines) in zip(rows, left):
        box(g, 7, y, inner_w, row_h, name, lines, colors.white, HH_LINE, HH_TEXT)
    for y, (name, lines) in zip(rows, right):
        box(g, right_x + 7, y, inner_w, row_h, name, lines, colors.white, FS_LINE, FS_TEXT)

    # Bridge between env/env.py (row 2) and env/sim.py (row 4)
    bx, bw = panel_w + 16, gap - 32
    by, bt = rows[4], rows[2] + row_h
    g.add(Rect(bx, by, bw, bt - by, rx=4, ry=4, fillColor=BR_FILL, strokeColor=BR_LINE, strokeWidth=0.9))
    texts = [("rendering/flightsim.py", "UI-Bold", 7.6, BR_TEXT), ("", "UI", 4, INK),
             ("FlightSimBridge", "UI-Semibold", 7.4, INK),
             ("hooks: reset, before and", "UI", 6.9, INK), ("after every JSBSim step", "UI", 6.9, INK),
             ("states \u2192 FlightSim (python)", "UI", 6.9, INK), ("joysticks, keys \u2192 human aircraft", "UI", 6.9, INK),
             ("tracers, real-time pacing, video", "UI", 6.9, INK), ("", "UI", 4, INK),
             ("FlightSimFDM", "UI-Semibold", 7.4, INK), ("JSBSim of FlightSim (c++)", "UI", 6.9, INK)]
    ty = bt - 12
    for text, font, size, color in texts:
        if text:
            g.add(String(bx + 6, ty, text, fontName=font, fontSize=fit_font(text, font, size, bw - 12), fillColor=color))
        ty -= size + 3.2

    mid = lambda y: y + row_h / 2  # noqa: E731
    arrow(g, [(7 + inner_w, mid(rows[2])), (bx, mid(rows[2]))])
    arrow(g, [(7 + inner_w, mid(rows[4])), (bx, mid(rows[4]))], dashed=True)
    jx = bx + bw + (right_x + 7 - bx - bw) / 2
    arrow(g, [(bx + bw, mid(rows[2]) + 4), (jx, mid(rows[2]) + 4), (jx, mid(rows[0])), (right_x + 7, mid(rows[0]))],
          both=True)
    arrow(g, [(bx + bw, mid(rows[4])), (right_x + 7, mid(rows[4]))], dashed=True)
    d.add(g)
    return d


# --- content -----------------------------------------------------------------------------------------------------

story = []
story += [
    p("FlightSim + HHMARL_3D_NEW", title),
    p("Components, architecture and parameters", subtitle),
    p("Status 30 October 2026 \u00b7 FlightSim Python API version 10 \u00b7 JSBSim 1.3.0 at 75 Hz", small),
    Spacer(1, 6),
]

story += section("1  Overview")
story.append(bullets([
    f"<b>HHMARL_3D_NEW</b>: multi-agent air combat environment in Python on JSBSim, with its own on-policy MARL "
    f"training (MAPPO, MASPO, MARS). Training always runs here, without FlightSim.",
    f"<b>FlightSim</b>: a modified Skybolt (C++, OpenSceneGraph) with the Python module {c('skybolt')}. It is used "
    f"for inference only: realistic rendering, video recording, optionally the C++ JSBSim, and a human pilot with "
    f"joysticks or the keyboard.",
    f"The repositories are separate clones side by side ({c('Projects/FlightSim')}, {c('Projects/HHMARL_3D_NEW')}). "
    f"HHMARL imports FlightSim only when it is used.",
    "The HHMARL env always computes observations, rewards and terminations. FlightSim displays the aircraft, reads "
    "the joysticks and the keyboard or simulates the flight dynamics.",
]))

story += section("2  Architecture")
story.append(architecture_diagram())
story.append(p("Solid arrows: always used with FlightSim. Dashed: only with "
               f"{JSBSIM_CPP}, where AircraftSim calls the JSBSim of FlightSim.", small))
story += subsection("One env step with FlightSim", ListFlowable([
    ListItem(p("The policies act. Each mission turns its action into JSBSim flight controls (aileron, elevator, "
               "rudder, throttle) and fires the cannon.")),
    ListItem(p(f"{c('sim_steps')} (10) JSBSim steps of 1/{c('sim_freq')} s (1/75 s) follow. Before each step, "
               "FlightSimBridge hides aircraft that were shot down or crashed, writes the controls of the human "
               f"(joysticks or keyboard) and, in c++ mode, advances the JSBSim of FlightSim ({c('step(1)')}). After each step, it "
               f"copies the states of the Python JSBSim into FlightSim (python mode, {c('step(1)')}), shows tracers "
               f"while a cannon fires and renders in real time ({c('speed')}). While recording, every frame of the "
               "video is rendered, even if the simulation then runs slower than real time.")),
    ListItem(p("The env computes observations, rewards and terminations, as without FlightSim.")),
], bulletType="1", bulletFormat="%s.", leftIndent=14, bulletFontName="UI-Semibold", bulletFontSize=8.5))
story.append(p(f"Commander ({c('hier')}): one env step assigns a low-level policy (cannon, track or escape) to each "
               f"aircraft and runs it for {c('hl_sim_steps')} low-level steps. With FlightSim, all aircraft advance "
               "in lockstep (all set their controls, then all step together)."))
story += subsection("JSBSim modes", table(
    ["JSBSIM_STATES", "Flight dynamics", "FlightSim templates"],
    [[c('"python"') + " (default)", "jsbsim Python package of HHMARL; FlightSim only displays the states",
      f"{c('SepecatJaguarBlue')}, {c('SepecatJaguarRed')}"],
     [c('"c++"'), f"JSBSim plugin of FlightSim; {c('FlightSimFDM')} replaces {c('jsbsim.FGFDMExec')} in AircraftSim",
      f"{c('SepecatJaguarJsbBlue')}, {c('SepecatJaguarJsbRed')} (JSBSim model {c('f16')} only)"]],
    [0.2, 0.47, 0.33]))
story.append(Spacer(1, 3))
story.append(p("Both modes use JSBSim 1.3.0 at 75 Hz and give identical trajectories; "
               f"{c('Tools/Compare_JSBSim_Python_Cpp.py')} checks this.", small))

story += section("3  FlightSim")
story += subsection("3.1  Components", table(
    ["Component", "Where", "Role"],
    [["skybolt Python module", c("src/Skybolt/SkyboltPythonBindings/") + "<br/>" + c("PythonBindings.cpp"),
      f"{c('sb.FlightSim')}: window, aircraft, JSBSim access, cameras, keyboard, labels"],
     ["Joysticks", c("src/Skybolt/SkyboltPythonBindings/") + "<br/>" + c("FlightSimJoysticks.h"),
      f"joysticks, throttles and pedals of the manual aircraft ({c('Tools/joysticks.json')}), read with SDL 2.32.10"],
     ["Visuals", c("src/Skybolt/SkyboltPythonBindings/") + "<br/>" + c("FlightSimVisuals.h"),
      "overview camera (F3), trails, cannon tracers, minimap (key M), weapon engagement zones (key Z)"],
     ["Recording", c("src/Skybolt/SkyboltPythonBindings/") + "<br/>" + c("FlightSimRecording.h"),
      "MP4 video of the window view (key R), encoded by ffmpeg in a background thread"],
     ["SkyboltSim", c("src/Skybolt/SkyboltSim"),
      f"world, entities and components; {c('SimStepper')} (fixed steps of 1/dynamicsRate); "
      f"{c('FdmComponent')} (flight dynamics interface)"],
     ["JSBSim plugin", c("src/Skybolt/SkyboltEnginePlugins/JsbSim"),
      f"JSBSim 1.3.0 for templates with a {c('jsbSimAircraft')} component"],
     ["SkyboltEngine", c("src/Skybolt/SkyboltEngine"),
      "engine root, entity templates, sim-to-visual binding, camera controllers, keyboard and mouse"],
     ["SkyboltVis", c("src/Skybolt/SkyboltVis"), "OpenSceneGraph rendering: planet, terrain, sky, clouds, ocean, shadows"],
     ["Assets", c("Assets/"), f"templates ({c('Movers/Entities')}), models, F-16 of the manual aircraft ({c('Movers/JSBSimManual')}), "
                              f"shaders ({c('Core')}), planet and map "
                              f"tiles ({c('Globe')}, {c('mapbox_*_ch')})"],
     ["Dependencies", f"{c('conanfile.py')}, {c('Conan/Recipes')}", "Conan 2 packages, incl. the JSBSim 1.3.0 recipe and SDL 2.32.10 (joysticks)"],
     ["Path setup", c("flightsim_env.py"),
      f"{c('import flightsim_env')} before {c('import skybolt')}: finds the build, sets library and asset paths "
      f"and {c('FLIGHTSIM_FFMPEG')}, reads {c('Tools/mapbox_token.txt')}, sets {c('FLIGHTSIM_JOYSTICKS')}, selects the dedicated GPU "
      "(Windows: graphics preference of python.exe; WSL2, Linux: environment variables of Mesa and NVIDIA)"],
     ["Examples", f"{c('Test_FlightSim_Windows.py')}<br/>{c('Test_FlightSim_Linux.py')}",
      "manual flight (joysticks or keyboard) of a JSBSim aircraft next to an aircraft moved from Python"]],
    [0.15, 0.38, 0.47]))

story += subsection("3.2  Python API: sb.FlightSim", table(
    ["Constructor argument", "Default", "Meaning"],
    [[c("x, y, width, height"), c("0, 0, 1080, 720"), "window position and size in pixels"],
     [c("manual"), c("False"), f"the joysticks (3.3) or the keyboard fly the aircraft of {c('set_manual_aircraft(id)')}"],
     [c("multiwindow"), c("False"), "second window with a free camera"],
     [c("dynamicsRate"), c("75.0"), "simulation steps per second"],
     [c("headless"), c("False"), "no window, e.g. for comparisons (not with manual or multiwindow)"]],
    [0.24, 0.2, 0.56]))
story.append(Spacer(1, 5))
story.append(table(
    ["Area", "Calls and properties"],
    [["Aircraft", f"{c('add_aircraft(id, template, position, rpy, velocityNed)')} \u00b7 "
                  f"{c('set_aircraft_state(id, position, rpy, velocityNed=None)')} \u00b7 {c('remove_aircraft(id)')} "
                  f"\u00b7 {c('set_aircraft_visible(id, visible)')} \u00b7 {c('set_dynamics_enabled(id, enabled)')} "
                  f"\u00b7 {c('get_aircraft_ids()')} \u00b7 {c('get_aircraft(id)')}"],
     ["Units", f"{c('position')}: {c('sb.LatLonAlt(lat, lon, alt)')}, geodetic radians, m above sea level \u00b7 "
               f"{c('rpy')}: {c('sb.Vector3')} roll, pitch, yaw in radians (north-east-down) \u00b7 "
               f"{c('velocityNed')}: north, east, down in m/s"],
     ["JSBSim", f"{c('get_property(id, name)')} \u00b7 {c('get_properties(id, names)')} \u00b7 "
                f"{c('set_property(id, name, value)')} \u00b7 {c('query_property_catalog(id)')} \u00b7 "
                f"{c('run_ic(id)')} \u00b7 {c('get_num_engines(id)')} \u00b7 {c('init_engine_running(id, engine)')} "
                f"\u00b7 {c('get_steady_state(id)')} \u00b7 {c('set_external_control(id, enabled)')}: flight "
                "controls only from the properties, not from the joysticks or the keyboard"],
     ["Simulation", f"{c('step(substeps=1)')} \u00b7 {c('render()')}: {c('False')} once the window is closed or Esc "
                    f"is pressed \u00b7 {c('frame(dt)')}: steps for {c('dt')} seconds and renders \u00b7 "
                    f"{c('sim_time')} \u00b7 {c('dynamics_step_size')}"],
     ["Window", f"{c('vsync')} (key V, on: render() waits for the refresh of the monitor) \u00b7 "
                f"{c('fullscreen')} (key B; a change of the window size stops a recording)"],
     ["Camera", f"{c('select_camera_view(view)')} with {q('Cockpit')}, {q('Follow')}, {q('Overview')} or "
                f"{q('Free')} \u00b7 {c('set_camera_target(id)')}"],
     ["Labels, trails", f"{c('set_aircraft_label(id, text)')} \u00b7 {c('set_aircraft_color(id, (r, g, b))')} "
                        f"(linear RGB) \u00b7 {c('labels_visible')} \u00b7 {c('trails_visible')} \u00b7 "
                        f"{c('trail_length')} (m, 30000) \u00b7 {c('clear_trails()')} (also the paths on the minimap) \u00b7 "
                        f"{c('set_aircraft_throttle(id, throttle)')}: nozzles of the camera target, without JSBSim"],
     ["Minimap", f"{c('minimap_visible')} (key M, on) · {c('minimap_range')} (m, 20000: outer ring, farther aircraft "
                 f"at the ring with their distance) · {c('minimap_heading_up')} (key N; default: with {c('manual')}): "
                 "map of the aircraft around the camera target in the bottom right corner, arrows in their colors with "
                 "their labels and distances (3D) to the camera target, the paths of the other "
                 "aircraft in the last 30 s as dots; in the free view (F4) the camera and its field of view"],
     ["WEZ", f"{c('set_aircraft_team(id, team)')}: aircraft of different teams are opponents (none without a team) · "
             f"{c('wez_visible')} (key Z, on) · {c('wez_range')} (m, 3500) · {c('wez_angle')} (degrees, 10: half angle "
             "of the cone around the velocity): in the cockpit and follow views (F1, F2), a light green circle ahead of the "
             "camera target while an opponent is in its weapon engagement zone, and a red beam like a flashlight from each "
             "opponent that has it in its zone, most opaque at the opponent (or at the edge of the window in its direction if "
             "it is out of view) and fading out towards the camera target (a red border around the window for an opponent "
             "nearly behind the camera)"],
     ["Cannon", f"{c('fire_tracer(id, speed=1000.0, lifetime=1.0)')}: dark tracer line \u00b7 {c('clear_tracers()')}"],
     ["Recording", f"{c('start_recording(path=None)')}: returns the video file \u00b7 {c('stop_recording()')} \u00b7 "
                   f"{c('recording')} \u00b7 {c('recording_frame_due')} \u00b7 {c('recording_directory')} "
                   f"({q('recordings')}) \u00b7 {c('recording_fps')} (30), see 3.6"],
     ["Manual flight", f"{c('set_manual_aircraft(id)')} \u00b7 {c('get_manual_controls()')}: aileron, elevator, "
                       f"rudder, throttle \u00b7 {c('consume_fire_requests()')}: presses of the fire button or Space since the "
                       f"last call \u00b7 {c('fire_pressed')}: held"],
     ["Low level", f"{c('engineRoot')} \u00b7 {c('world')} \u00b7 {c('visRoot')} \u00b7 {c('window')} \u00b7 "
                   f"{c('camera')} \u00b7 {c('sb.captureScreenshot(app.visRoot)')}"]],
    [0.14, 0.86]))

story += subsection("3.3  Keyboard, mouse and joysticks", table(
    ["Key", "Action"],
    [["F1 \u00b7 F2 \u00b7 F3 \u00b7 F4", "cockpit \u00b7 follow (external) \u00b7 overview of all aircraft \u00b7 free camera"],
     ["Tab", "next aircraft as camera target"],
     ["L \u00b7 T \u00b7 R", "labels \u00b7 trails \u00b7 start/stop recording a video"],
     ["M \u00b7 N", "minimap \u00b7 minimap north-up/heading-up (shown at the top of the window)"],
     ["Z", "weapon engagement zones in the cockpit and follow views on/off (shown at the top of the window)"],
     ["V \u00b7 B", "vsync on/off (shown at the top of the window and in the help) \u00b7 full screen on/off"],
     ["H \u00b7 Esc", "help \u00b7 exit"],
     ["Mouse, wheel", "rotate the camera; in the overview, rotate around the aircraft and zoom with the wheel"],
     ["A/D \u00b7 W/S", "overview: rotate \u00b7 zoom (unless they fly the manual aircraft); free camera: move at 2 km/s "
                     "(Shift held: 10 times faster, Ctrl held: 10 times slower)"],
     ["Manual flight", "without joysticks: Up/Down pitch down/up \u00b7 Left/Right roll \u00b7 A/D rudder \u00b7 "
                       "W/S throttle \u00b7 Space cannon"],
     ["Joysticks", f"the devices of {c('Tools/joysticks.json')} fly the manual aircraft while they are connected, "
                   "each control otherwise with its keys: stick (roll, pitch), throttle lever, rudder pedals, "
                   "trigger (fires while held). Device names and axis and button numbers as shown by "
                   f"{c('Tools/joysticks.py')}, optionally {c('invert')} and {c('deadzone')}; read when "
                   "FlightSim starts, so changes need no rebuild"]],
    [0.2, 0.8]))

story += subsection("3.4  Setup, tools and settings", table(
    ["Item", "Parameters and use"],
    [[c("setup_windows.bat"), f"full setup; {c('-BuildDir <folder>')}, {c('-PythonExe <python.exe>')}"],
     [c("setup_linux.sh"), f"full setup; {c('--build-dir <folder>')}, {c('--python <python3.11>')}, {c('--jobs N')}"],
     [f"{c('Tools/update_pythonbindings_windows.bat')}<br/>{c('Tools/update_pythonbindings_linux.sh')}",
      f"rebuild after C++ changes (Linux: {c('--jobs N')}). Run the full setup instead after adding or removing "
      "source files or changing dependencies."],
     [c("Tools/Compare_JSBSim_Python_Cpp.py"),
      f"flies an F-16 in both JSBSims for 120 s at 75 Hz ({c('SECONDS')}, {c('RATE')}) and prints the differences"],
     [c("Tools/Tile_Map_Downloader.py"),
      f"downloads (or unzips from {c('Assets')}) the offline map tiles of Switzerland; {c('--zoom 15 16')} for "
      f"more detail (needs a Mapbox token); area: {c('CENTER_LATITUDE')}, {c('CENTER_LONGITUDE')} in the script"],
     [c("Tools/mapbox_token.txt"), "optional Mapbox access token (Git stores only the comments, filter set up by the setup). "
                                   "Without it, only the offline map tiles are shown; with it, the missing tiles are "
                                   "loaded from Mapbox"],
     [c("Tools/joysticks.json"), "device and axis or button of each flight control of the manual aircraft: roll, pitch, "
                                "throttle, rudder, fire (3.3)"],
     [c("Tools/joysticks.py"), "shows the names and the axes, buttons and hats of the connected joysticks, "
                              "throttles and pedals"],
     [c("Tools/Scale_OSG_Files.py"), f"scales a model, e.g. by {c('8.0')} for the enlarged *Big models; "
                                     f"{c('--to-osgt')} converts it to text"],
     [c("Tools/build_instructions_pdf.py"), f"builds this {c('instructions.pdf')} into the current folder "
                                            "(Windows fonts)"],
     ["Packages of .venv", f"installed by the setup: {c('conan')} 2.22.0, {c('jsbsim')} 1.3.0, {c('requests')}, "
                           f"{c('imageio-ffmpeg')} 0.6.0 (ffmpeg of the videos), {c('reportlab')} 5.1.0 (this PDF), "
                           f"{c('pygame-ce')} 2.5.8 ({c('Tools/joysticks.py')})"],
     ["Environment variables", f"{c('FLIGHTSIM_BUILD_DIR')}: build folder (default {c('../FlightSimBuild')}) \u00b7 "
                               f"{c('FLIGHTSIM_DIR')}: FlightSim folder for HHMARL (default {c('../FlightSim')}) \u00b7 "
                               f"{c('FLIGHTSIM_FFMPEG')}: ffmpeg of the videos (default: the one of imageio-ffmpeg, "
                               "otherwise ffmpeg on PATH) \u00b7 "
                               f"{c('FLIGHTSIM_MAPBOX_TOKEN')}: Mapbox token (default: the one of "
                               f"{c('Tools/mapbox_token.txt')}) \u00b7 "
                               f"{c('FLIGHTSIM_JOYSTICKS')}: joystick mapping (default {c('Tools/joysticks.json')}, "
                               "empty: keyboard only)"],
     [c("Settings.json"), f"Windows {c(win('%LOCALAPPDATA%/Skybolt'))}, Linux {c('~/Skybolt')}: {c('display')} "
                          f"({c('multiSampleCount')}, {c('vsync')}, {c('texturePoolSizeMB')}), {c('shadows')}, "
                          f"{c('clouds')}, {c('mouse.sensitivity')}, {c('tileApiKeys')} (keep empty, the Mapbox token is in "
                          f"{c('Tools/mapbox_token.txt')}), "
                          f"{c('jsb.rootDir')} (JSBSim aircraft data)"],
     [c("PlanetEarth.json"), f"{c('Assets/Globe/Entities')}: {c('maxLevel')} of the map tiles; raise it after "
                             f"downloading more zoom levels; {c('fallback')}: online Mapbox tiles, used with a token"],
     ["Templates", f"{c('Assets/Movers/Entities')}: {c('SepecatJaguarBlue/Red')} (moved from Python), "
                   f"{c('SepecatJaguarJsbBlue/Red')} (JSBSim {c('f16')}), {c('SepecatJaguarJsbBlue/RedManual')} (F-16 with "
                   f"corrected rudder for the manual aircraft, {c('aircraftDir')}), {c('SepecatJaguarBlueBig/RedBig')} "
                   "(enlarged models)"]],
    [0.38, 0.62]))

story += subsection("3.5  Visual constants (C++)", table(
    ["Element", f"Values in {c('FlightSimVisuals.h')} (rebuild after changing)"],
    [["Overview camera", "vertical field of view 0.6 rad, looks down at 12\u00b0, margin 1.15 around the aircraft, "
                         "at least 60 m away, zoom factor up to e<super>3</super>"],
     ["Trails", f"a point every 25 m, about 3 px wide, opacity 0.9 fading quadratically along {c('trail_length')}"],
     ["Tracers", "20 m dark line, about 4 px wide, starting 10 m ahead of the aircraft"],
     ["Nozzles", f"discs at the nozzle exits of the camera target ({c('nozzles')} of the entity template), linearly from "
                 f"black at idle to dark red (linear RGB 0.045, 0.0012, 0) at full throttle ({c('fcs/throttle-cmd-norm')})"],
     ["Minimap", f"{c('TacticalMinimap')}: black background (opacity 0.7) of 28 % of the window height, 2 % from the bottom "
                 "right corner; outer ring 25 % of the window height wide; arrows 2 % long (camera target 2.4 %), texts 1.6 % "
                 "in the aircraft colors mixed 45 % with white, beside the arrows where they cover the least of the other "
                 "symbols and texts; distances in steps of 10 m below 1 km, 0.1 km below 10 km; paths: a point every 0.5 s, dots 0.35 % "
                 "large every 0.75 % of the window height along them; free camera: light gray camera 2.25 % long with dashed "
                 "lines along its horizontal field of view to the outer ring"],
     ["WEZ", f"{c('WezIndicators')}: green circle (linear RGB 0.3, 0.9, 0.3) with a radius of 7.2 % of the window height, "
             "opacity 0.2 inside and 0.5 on its rim, ahead of the nose at the distance of the nearest opponent in the zone; red "
             "beams (linear RGB 1, 0.03, 0.02) like flashlights from the opponent on the screen (or the edge of the window in its "
             "direction if it is out of view) to the camera target (the center in the cockpit): opacity 0.5 at the source, falling "
             "with the power 1.5 to 0 at the camera target; half opacity 1.5 % of the window height from the axis at the source, "
             "widening at 12° to each side, with slightly sharp sides (sharpness 1.5); within 30° of the direction behind "
             "the camera they turn gradually into a red border around the window (opacity 0.5 at the edge); drawn below the "
             "labels and the minimap"]],
    [0.2, 0.8]))

story += subsection("3.6  Video recording", table(
    ["Aspect", "Behavior"],
    [["Start, stop", f"key R or {c('start_recording()')} / {c('stop_recording()')}; also stops when the window is "
                     f"closed or the {c('FlightSim')} is deleted; a red dot in the top left corner shows the recording"],
     ["Content", "the view of the window (selected camera with labels, trails, minimap, WEZ and cockpit HUD if shown), without the "
                 "help message and the red dot; size of the window (rounded down to even pixels)"],
     ["File", f"{c('<recording_directory>/flightsim_<date>_<time>.mp4')}, or the path given to "
              f"{c('start_recording(path)')}; a relative folder is relative to the current directory"],
     ["Frame rate", f"{c('recording_fps')} (30) frames per second of simulation time, each from the simulation step "
                    "closest to its time, so the video plays at simulation speed (at 75 Hz alternately 2 and 3 steps "
                    "apart)"],
     ["Skipped rendering", f"programs that skip frames to keep up with real time must still render when "
                           f"{c('recording_frame_due')} is {c('True')}, otherwise frames repeat (FlightSimBridge does)"],
     ["Encoding", f"H.264 (libx264, CRF 18, yuv420p, BT.709) by the ffmpeg of {c('FLIGHTSIM_FFMPEG')}; not with "
                  f"{c('headless')}"]],
    [0.2, 0.8]))

story += section("4  HHMARL_3D_NEW")
story += subsection("4.1  Components", table(
    ["Module", "Role"],
    [[c("train.py"), f"training; {c('process_args')} applies experiment-specific overrides"],
     [c("test.py"), "inference over many episodes (win and loss rates), with rendering"],
     [c("args.py"), "all arguments of training and inference (command-line flags)"],
     [c("algorithm/"), "learner (PPO, SPO, MARS), actor and critic networks, value normalization"],
     [c("env/env.py"), f"{c('AircombatMARL')} (interface of the learner) \u2192 {c('AircraftEnv')} (low level) or "
                       f"{c('AircraftEnvHier')} (commander)"],
     [c("env/sim.py"), f"{c('AircraftSim')} (one JSBSim per aircraft) and {c('CannonSim')}"],
     [c("env/missions.py"), "trainable tasks: track, cannon, escape, hier"],
     [c("env/models.py"), f"scripted and pre-trained opponents, {c('HumanModel')} (joysticks or keyboard via FlightSim)"],
     [f"{c('env/rewards.py')}, {c('env/terminations.py')}", "reward terms and episode ends"],
     [c("env/configs/"), f"{c('base.yaml')}, {c('hier.yaml')}, {c('levels/<task>_L<n>.yaml')} (deep-merged)"],
     [f"{c('env/base_actor/')}, {c('env/catalog.py')}", "scripted base actor and JSBSim property catalog from LAG"],
     [c("rendering/"), f"{c('render.py')}, {c('scene.py')}: PyGame renderer \u00b7 {c('flightsim.py')}: FlightSim bridge"],
     [c("runs/"), f"checkpoints and TensorBoard logs: {c('runs/<task>_<model>_<run_name>')}"],
     [c("recordings/"), "videos recorded with key R in the FlightSim window (ignored by git)"]],
    [0.3, 0.7]))

story += subsection("4.2  Inference settings (test.py)", table(
    ["Setting", "Default", "Meaning"],
    [[c("RENDER"), c("True"), "render while testing"],
     [c("RENDERER"), q("flightsim"), f"{q('pygame')} (simple) or {q('flightsim')}"],
     [c("RESTORE_PATH"), c("runs\\cannon_L4_f16"), "checkpoint file, or run folder (its latest checkpoint)"],
     [f"{c('TASK')}, {c('LEVEL')}", f"{q('cannon')}, {c('4')}",
      f"task and level of the policy; commander: {c('--hierarchy_level 2')}"],
     [c("INIT_STATES"), c("None"), f"per aircraft, relative to the battle field center: {c('north')}/{c('east')} "
                                   f"in m (or {c('lat')}/{c('lon')} in degrees), {c('alt')} in m, {c('speed')} in "
                                   f"m/s, {c('heading')}, {c('roll')}, {c('pitch')} in degrees; {c('None')}: config"],
     [c("JSBSIM_STATES"), q("python"), f"{q('python')} or {q('c++')}, see section 2"],
     [f"{c('MANUAL')}, {c('MANUAL_AGENT')}", f"{c('False')}, {q('B2')}",
      f"a human flies {c('MANUAL_AGENT')} with the joysticks or the keyboard: a copy of B1 if the config has no such aircraft (track, cannon, "
      "escape), otherwise it replaces the RL agent (hier: B3)"],
     [c("LOCATION"), c("(8.965318, 45.975305)"), "battle field center (longitude, latitude), here Lugano"],
     [f"{c('MINIMAP')}, {c('MINIMAP_RANGE')}", f"{c('True')}, {c('20000')}",
      "minimap of the aircraft around the camera target (3.2), and its range in m"],
     [c("WEZ"), c("True"), "weapon engagement zones of the cannons in the cockpit and follow views (3.2)"],
     [c("run_inference(num_episodes)"), c("500"), "number of episodes"]],
    [0.26, 0.22, 0.52]))

story += subsection("4.3  FlightSim bridge (env config \"flightsim\")", p(
    f"Options of {c('FlightSimBridge')} in {c('rendering/flightsim.py')}. {c('test.py')} passes {c('jsbsim_states')}, "
    f"{c('manual_agent')}, {c('minimap')}, {c('minimap_range')} and {c('wez')}; add further options to {KWARGS_FLIGHTSIM} there.", small), table(
    ["Option", "Default", "Meaning"],
    [[c("jsbsim_states"), q("python"), f"{q('python')} or {q('c++')}"],
     [c("manual_agent"), c("None"), f"ID of the aircraft flown by the human, e.g. {q('B2')}"],
     [c("window"), c("(10, 30, 1600, 900)"), "x, y, width, height of the window"],
     [c("speed"), c("1.0"), f"simulated seconds per real second; {c('None')}: as fast as possible"],
     [c("camera_view"), c("None"), f"{q('Cockpit')}, {q('Follow')}, {q('Overview')} or {q('Free')}; "
                                   f"{c('None')}: cockpit with a human, otherwise overview"],
     [c("trail_length"), c("30000.0"), "length of the trails in m"],
     [c("templates"), c("None"), f"templates per team, e.g. {TEMPLATES_EXAMPLE}; with c++, they "
                                 f"need a {c('jsbSimAircraft')} component"],
     [c("tracer_interval"), c("5"), "JSBSim steps between tracers while a cannon fires"],
     [c("headless"), c("False"), "no window (not with manual_agent)"],
     [c("recording_dir"), c("None"), f"folder of the videos of key R; {c('None')}: {c('recordings/')} of "
                                     "HHMARL_3D_NEW. While recording, every video frame is rendered (no frames are "
                                     "skipped), so a slow PC runs slower than real time"],
     [c("minimap"), c("True"), "minimap of the aircraft around the camera target (key M)"],
     [c("minimap_range"), c("20000.0"), "radius of the minimap in m"],
     [c("minimap_heading_up"), c("None"), f"{c('True')}: heading-up, {c('False')}: north-up (key N); {c('None')}: "
                                          "heading-up with a human, otherwise north-up"],
     [c("wez"), c("True"), f"weapon engagement zones (key Z) of the cannons ({c('CannonSim')}: {c('Rmax_wez')}, "
                           f"{c('omega_wez')}), with the teams of the env"],
     [c("TEAM_COLORS"), "B " + c("(0.05, 0.3, 1.0)") + "<br/>R " + c("(1.0, 0.06, 0.04)"),
      "module constant: colors of the labels, trails and minimap per team (linear RGB)"],
     [c("FLIGHTSIM_API_VERSION"), c("10"), "module constant: minimum version of the FlightSim build (10: weapon engagement zones)"]],
    [0.25, 0.22, 0.53]))

story += subsection("4.4  Environment (AircombatMARL(task, **kwargs))", table(
    ["Argument", "Values", "Meaning"],
    [[c("task"), f"{q('track/L4')}, {q('cannon/L1')}, {q('escape/L2')}, {q('hier')}",
      f"config: {c('base.yaml')} + level file or {c('hier.yaml')}"],
     [c("hierarchy_level"), f"{c('1')} or {c('2')}", "1: low-level policies, 2: commander"],
     [c("training_level"), "integer", "stage of the random initial states (set by train.py and test.py)"],
     [f"{c('rendering')}, {c('render_view')}, {c('render_samples')}", f"{c('False')}, {c('0')}, {c('4')}",
      "PyGame rendering, viewpoint, anti-aliasing samples"],
     [c("flightsim"), c("None"), f"options of 4.3; {c('None')}: without FlightSim"],
     [c("config_overrides"), c("None"), f"deep-merged into the config, e.g. {OVERRIDES_EXAMPLE}"],
     [c("reset(seed, options)"), c('{"init_states": ...}'), f"initial states as {c('INIT_STATES')}"]],
    [0.3, 0.27, 0.43]))

story += subsection("4.5  YAML configuration (env/configs)", table(
    ["Key", "Default", "Meaning"],
    [[c("sim_freq"), c("75"), "JSBSim steps per second"],
     [c("sim_steps"), c("10"), "JSBSim steps per env step (0.133 s)"],
     [c("hl_sim_steps"), f"{c('10')} (hier)", "low-level steps per commander step"],
     [c("horizon"), f"{c('600')}; hier {c('100')}", "env steps per episode"],
     [c("map_size"), f"{c('50')}; hier {c('80')}", "arena size in km; farther than map_size/2 from the center is a crash"],
     [f"{c('altitude_limit')}, {c('altitude_limit_max')}", f"{c('2500')}, {c('10000')}", "allowed altitudes in m"],
     [c("acceleration_limit_x/_y/_z"), c("9.0"), "overload limit in g"],
     [c("rnd_init"), c("true"), "random initial heading, position, altitude and speed"],
     [c("battle_field_center"), c("[0.0, 0.0, 0.0]"), f"longitude, latitude, altitude (test.py: {c('LOCATION')})"],
     [f"{c('num_rl_agents')}, {c('num_opp_agents')}", f"hier {c('3')}, {c('3')}", "team sizes of the commander config"],
     [c("opp_hier_strategy"), c("mixed"), f"commander opponents: {c('mixed')} (cannon/track/escape 40/40/20 %), "
                                         f"{c('aggressive')} (70/20/10 %), {c('random')}"],
     [c("sp_policy_name"), "L3 levels", f"policy of the self-play opponent, e.g. {c('track_L2_f16')}"],
     [c("aircraft.<ID>"), f"{c('B1')}, {c('R2')}, \u2026",
      f"first letter = team; {c('model')} ({c('f16')} or {c('A4')}), {c('mission')} (4.6), {c('num_missiles')} "
      f"(cannon shots, 10), {c('init_state')} ({c('ic_long_gc_deg')}, {c('ic_lat_geod_deg')}, {c('ic_h_sl_ft')}, "
      f"{c('ic_psi_true_deg')}, {c('ic_u_fps')}), {c('unlimited_fuel')} ({c('true')})"],
     [f"{c('<Reward>_scale')}, {c('<Reward>_potential')}", f"{c('1.0')}, {c('false')}",
      f"per reward term: {c('EventDrivenReward')}, {c('ShootReward')}, {c('ShootPenaltyReward')}, "
      f"{c('HeadingReward')}, {c('DistanceReward')}, {c('FleeReward')}; also {c('ShootReward_orientation_kill')}, "
      f"{c('ShootPenaltyReward_posture_punish')}, {c('HeadingReward_track')}, {c('HeadingReward_opposite')}"]],
    [0.3, 0.17, 0.53]))

story += subsection("4.6  Missions and interfaces", table(
    [f"Mission ({c('aircraft.<ID>.mission')})", "Behavior"],
    [[f"{c('task_track')}, {c('task_cannon')}, {c('task_escape')}",
      "trainable low-level skills: pursue, attack with the cannon, evade"],
     [c("task_hier"), f"trainable commander: picks {c('cannon_L4')}, {c('track_L4')} or {c('escape_L2')} per aircraft"],
     [f"{c('model_L1Maneuver')}, {c('model_L2Maneuver')}",
      f"scripted opponents (L1, L2): random turns and shots \u00b7 precise tracking and shooting; based on the LAG "
      f"base actor ({c('model_track')})"],
     [c("model_SPManeuver"), f"self-play opponent (L3) with the pre-trained policy {c('sp_policy_name')}"],
     [c("model_strategy"), "L4 opponent: mixed strategy over the pre-trained L3 (escape: L1) policies"],
     [c("model_hier"), f"commander opponent with {c('opp_hier_strategy')}"],
     [c("model_human"), f"human pilot with joysticks or the keyboard via FlightSim ({c('MANUAL')} in test.py); "
                       f"its F-16 has the corrected rudder of FlightSim's {c('Assets/Movers/JSBSimManual')}"]],
    [0.4, 0.6]))
story.append(Spacer(1, 5))
story.append(table(
    ["Interface", "Definition"],
    [["Low-level action", f"{c('MultiDiscrete([17, 11, 9, 9, 2])')}: aileron, elevator, rudder, throttle, shoot"],
     ["Low-level observation", "30 values in [-5, 5]: own position, velocity, body rates, attitude, cannon state and "
                               "ammunition; closest opponent: relative position, velocity and angles"],
     ["Commander action", f"{c('Discrete(3)')}: cannon, track, escape"],
     ["Cannon", f"range 3500 m, cone \u00b110\u00b0, hit probability 0.8, shots = {c('num_missiles')}; RL agents: a shot "
                f"fires until it hits or for 12 s, the next one after 16 s ({c('launch()')}); human: fires while the "
                f"button is held, unlimited ammunition ({c('fire()')})"]],
    [0.22, 0.78]))

story += subsection("4.7  Training arguments (args.py)", table(
    ["Group", f"Arguments and defaults, set as flags, e.g. {c('python train.py --task track --level 2')}"],
    [["Run", f"{c('task track')}, {c('level 1')}, {c('hierarchy_level 1')}, {RUN_NAME_DEFAULT}, "
             f"{c('restore_path None')}, {c('seed 1')}, {c('total_timesteps 5000000')}"],
     ["Algorithm", f"{c('algorithm ppo')} ({c('ppo')}, {c('spo')}, {c('mars')}), {c('trust_region_clip 0.25')}, "
                   f"{c('gamma 0.99')}, {c('td_lambda 0.92')}, {c('use_gae True')}, {c('entropy_coef 0.01')}, "
                   f"{c('entropy_decay None')}"],
     ["Networks", f"{c('shared_policies True')}, {c('centralized_critic True')}, {c('actor_hidden_dim 128')}, "
                  f"{c('critic_hidden_dim 128')}, {c('actor_num_layers 1')}, {c('critic_num_layers 1')}, "
                  f"{c('use_ReLU True')}, {c('normalize_input True')}, {c('init_orthogonal True')}"],
     ["Recurrence, attention", f"{c('rnn False')}, {c('rnn_num_layers 1')}, {c('data_chunk_length 10')}, "
                               f"{c('attention False')}, {c('attention_heads 2')} (rnn and attention exclude each other)"],
     ["Optimization", f"{c('optimizer Adam')}, {c('learning_rate_actor 3e-4')}, {c('learning_rate_critic 3e-4')}, "
                      f"{c('lr_decay None')}, {c('train_epochs 10')}, {c('batch_size 10000')}, "
                      f"{c('minibatch_size 5000')}, {c('clip_gradients 10')}"],
     ["Losses", f"{c('value_loss_coef 1.0')}, {c('clip_value_loss 1.0')}, {c('huber_coef 10.0')}, "
                f"{c('normalize_advantage True')}, {c('normalize_value True')}, {c('normalize_popart False')}, "
                f"{c('policy_mask True')}, {c('value_mask True')}"],
     ["Workers, hardware", f"{c('num_envs 20')}, {c('cuda True')}, {c('cuda_id 0')}, {c('torch_deterministic False')}, "
                           f"{c('torch_compile False')}, {c('avail_actions False')}, {c('log_every_step 100')}, "
                           f"{c('render_samples 4')}"]],
    [0.2, 0.8]))

story += section("5  Quick start")
story.append(table(
    ["Goal", "Command"],
    [["Set up FlightSim", f"{c('setup_windows.bat')} \u00b7 {c('./setup_linux.sh')}"],
     ["Try FlightSim", f"{c(win('.venv/Scripts/python Test_FlightSim_Windows.py'))} \u00b7 "
                       f"{c('.venv/bin/python Test_FlightSim_Linux.py')}"],
     ["After C++ changes", f"{c(win('Tools/update_pythonbindings_windows.bat'))} \u00b7 "
                           f"{c('./Tools/update_pythonbindings_linux.sh')}"],
     ["Train low level", f"{c('python train.py --task track --level 1')}, then {c('--level 2 --restore_path runs/track_L1_f16_<run_name>')}"],
     ["Train commander", c("python train.py --task hier --hierarchy_level 2")],
     ["Inference", f"set the settings in {c('test.py')}, then {c('python test.py')} (commander: "
                   f"{c('RESTORE_PATH')} of a hier run and {c('--hierarchy_level 2')})"],
     ["Record a video", f"press R in the FlightSim window to start and again to stop (or "
                        f"{c('app.start_recording()')}); MP4 files in {c('recordings/')}"],
     ["Update this PDF", c(win(".venv/Scripts/python Tools/build_instructions_pdf.py"))]],
    [0.2, 0.8]))
story.append(Spacer(1, 4))
story.append(p(f"The venv of HHMARL_3D_NEW needs Python 3.11, like the {c('skybolt')} module of FlightSim, and "
               f"{c('imageio-ffmpeg')} of its {c('requirements.txt')} for the videos.", small))


def on_page(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.5)
    canvas.line(MARGIN, 12 * mm, PAGE_W - MARGIN, 12 * mm)
    canvas.setFont("UI", 7.5)
    canvas.setFillColor(MUTED)
    canvas.drawString(MARGIN, 8.5 * mm, "FlightSim + HHMARL_3D_NEW \u00b7 components, architecture and parameters")
    canvas.drawRightString(PAGE_W - MARGIN, 8.5 * mm, f"{doc.page}")
    canvas.restoreState()


class Doc(SimpleDocTemplate):
    def afterFlowable(self, flowable):
        """PDF outline from the section headings."""
        if isinstance(flowable, Paragraph) and flowable.style.name in ("h1", "h2"):
            text = flowable.getPlainText()
            key = f"h{id(flowable)}"
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text, key, level=0 if flowable.style.name == "h1" else 1, closed=False)


doc = Doc(OUT, pagesize=A4, leftMargin=MARGIN, rightMargin=MARGIN, topMargin=16 * mm, bottomMargin=17 * mm,
          title="FlightSim + HHMARL_3D_NEW: components, architecture and parameters",
          subject="Components, architecture and settable parameters of FlightSim and HHMARL_3D_NEW")
doc.build(story, onFirstPage=on_page, onLaterPages=on_page)
print("written", OUT)
