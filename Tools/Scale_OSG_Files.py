r"""
Scales an OSG model (e.g. .ive, .osgb) and saves it next to the input as <name>_scaled.osgt and <name>_scaled.ive.
With --to-osgt, the model is only converted to the text format <name>.osgt, e.g. to inspect it.

Examples (from the repository root):
    .venv\Scripts\python Tools\Scale_OSG_Files.py Assets\Movers\Models\SepecatJaguarRed\SepecatJaguar_Fuselage.ive 8.0
    .venv\Scripts\python Tools\Scale_OSG_Files.py Assets\Movers\Models\SepecatJaguarBlue\SepecatJaguar_Fuselage.osgb --to-osgt
"""
import argparse, sys
from pathlib import Path

parser = argparse.ArgumentParser(description="Scale an OSG model or convert it to .osgt.")
parser.add_argument("model", help="path of the model file")
parser.add_argument("scale", type=float, nargs="?", help="uniform scale factor, e.g. 8.0")
parser.add_argument("--to-osgt", action="store_true", help="only convert the model to .osgt, without scaling")
args = parser.parse_args()
if args.scale is None and not args.to_osgt:
    parser.error("provide a scale factor or --to-osgt")

# flightsim_env.py is in the repository root
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import flightsim_env  # sets up paths and DLLs for skybolt, see flightsim_env.py
import skybolt as sb

model = str(Path(args.model).resolve())
if args.to_osgt:
    ok = sb.convertToOSGT(model)
else:
    ok = sb.scaleAndSaveModel(model, args.scale)
sys.exit(0 if ok else 1)
