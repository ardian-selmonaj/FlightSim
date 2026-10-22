import argparse
import os
import math
import shutil
import tempfile
import zipfile
import requests
import time
from pathlib import Path

ASSETS_DIR = Path(__file__).resolve().parents[1] / "Assets"

# Tiles are stored as Assets/<package>/<tile_folder>/{z}/{x}/{y}.<form>, which is where
# Assets/Globe/Entities/PlanetEarth.json reads them from.
# zip_url: tiles of Switzerland that were already downloaded from Mapbox, packed as zip.
TILE_SETS = [
    {
        "data_type": "terrain-rgb",
        "form": "png",
        "package": "mapbox_terrain_ch",
        "tile_folder": "TerrainTilesCH",
        "zip_url": "https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQCWFoPOCBLaQ6p837Vod2coAbW9juV3RTQxCuAd-rjkjiA?e=TWTKNb",
    },
    {
        "data_type": "satellite",
        "form": "jpg",
        "package": "mapbox_satellite_ch",
        "tile_folder": "SatelliteTilesCH",
        "zip_url": "https://supsi-my.sharepoint.com/:u:/g/personal/ardian_selmonaj_supsi_ch/IQC3RLT8KjYKSKEZxr-s7Rm6AQ3ukCzpFAOT0MRr-jqHN5Y?e=oOqqbS",
    },
]

def lat_lon_to_tile(lat, lon, zoom):
    """
    Convert latitude/longitude to tile coordinates for a given zoom level.
    
    Args:
        lat: Latitude in degrees
        lon: Longitude in degrees
        zoom: Zoom level
    
    Returns:
        tuple: (x, y) tile coordinates
    """
    lat_rad = math.radians(lat)
    n = 2.0 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return (x, y)

def download_tile(z, x, y, data_type, form, access_token, base_path="mapbox_tiles", delay=0.005):
    """
    Download a single Mapbox terrain-rgb tile and save it in the hierarchical structure.
    
    Args:
        z: Zoom level
        x: Tile X coordinate
        y: Tile Y coordinate
        access_token: Mapbox access token
        base_path: Base directory for storing tiles
        delay: Delay between requests in seconds
    
    Returns:
        bool: True if successful, False otherwise
    """
    # Create the directory structure: base_path/z/x/
    tile_dir = Path(base_path) / str(z) / str(x)
    tile_dir.mkdir(parents=True, exist_ok=True)
    
    tile_path = tile_dir / f"{y}.{form}"
    
    # Skip if tile already exists
    if tile_path.exists():
        return True
    
    # Download the tile
    if form == "png":
        url = f"http://api.mapbox.com/v4/mapbox.{data_type}/{z}/{x}/{y}.{form}raw?access_token={access_token}"
    else:
        url = f"http://api.mapbox.com/v4/mapbox.{data_type}/{z}/{x}/{y}.{form}?access_token={access_token}"
    
    try:
        headers = {
            'User-Agent': 'Mapbox Terrain/Satellite Tile Loader (Educational Purpose)'
        }
        
        response = requests.get(url, headers=headers, timeout=10)
        
        if response.status_code == 200:
            with open(tile_path, 'wb') as f:
                f.write(response.content)
            # print(f"Downloaded: {z}/{x}/{y}.{form}")
            time.sleep(delay)  # Be respectful to servers
            return True
        else:
            print(f"Failed to download {z}/{x}/{y}.{form} - Status: {response.status_code}")
            return False
            
    except Exception as e:
        print(f"Error downloading {z}/{x}/{y}.{form}: {e}")
        return False

def download_tiles_for_location(center_lat, center_lon, 
                                access_token, data_type, form,
                                lat_range=2.0, lon_range=2.0, step=0.001,
                                zoom_levels=range(0, 14), 
                                base_path="mapbox_tiles"):
    """
    Download Mapbox terrain-rgb tiles for a location and surrounding area.
    Tiles that already exist in base_path are skipped.
    
    Args:
        center_lat: Center latitude
        center_lon: Center longitude
        access_token: Mapbox access token
        lat_range: Range in degrees to iterate around center latitude (+/-)
        lon_range: Range in degrees to iterate around center longitude (+/-)
        step: Step size in degrees for iteration
        zoom_levels: Range of zoom levels to download
        base_path: Base directory for storing tiles
    """
    print(f"Starting Mapbox {data_type} tile download for location: ({center_lat}, {center_lon})")
    print(f"Range: ±{lat_range}° lat, ±{lon_range}° lon, Step: {step}°")
    print(f"Zoom levels: {list(zoom_levels)}")
    print(f"Output directory: {base_path}")
    print("-" * 60)
    
    # Track unique tiles to avoid duplicates
    tiles_to_download = set()
    
    # Iterate over the location grid
    lat = center_lat - lat_range
    while lat <= center_lat + lat_range:
        lon = center_lon - lon_range
        while lon <= center_lon + lon_range:
            # For each zoom level
            for z in zoom_levels:
                x, y = lat_lon_to_tile(lat, lon, z)
                tiles_to_download.add((z, x, y))
            
            lon += step
        lat += step
    
    # Skip tiles that already exist
    existing_tiles = {(z, x, y) for (z, x, y) in tiles_to_download
                      if (Path(base_path) / str(z) / str(x) / f"{y}.{form}").exists()}
    tiles_to_download -= existing_tiles

    print(f"Tiles already existing: {len(existing_tiles)}")
    print(f"Total unique tiles to download: {len(tiles_to_download)}")
    print("-" * 60)

    if not tiles_to_download:
        print("All tiles already exist, nothing to download.\n")
        return
    
    # Download all unique tiles
    success_count = 0
    fail_count = 0
    
    for i, (z, x, y) in enumerate(sorted(tiles_to_download), 1):
        if download_tile(z, x, y, data_type, form, access_token, base_path):
            success_count += 1
        else:
            fail_count += 1
        
        if i % 1000 == 0:
            print(f"Progress: {i}/{len(tiles_to_download)} tiles processed")
    
    print("-" * 60)
    print(f"Download complete!")
    print(f"Successful: {success_count}")
    print(f"Failed: {fail_count}")
    print(f"Total: {len(tiles_to_download)}")
    print("\n")

def tile_dir(tile_set):
    """
    Directory in Assets that holds the {z}/{x}/{y} tiles of a tile set.
    """
    return ASSETS_DIR / tile_set["package"] / tile_set["tile_folder"]

def download_file(url, file_path, chunk_size=1024 * 1024):
    """
    Download a (large) file from a SharePoint/OneDrive share link.

    Args:
        url: Share link
        file_path: Path to store the file
        chunk_size: Size of the chunks written to disk

    Returns:
        bool: True if successful, False otherwise
    """
    # With download=1, the share link returns the file itself instead of the preview page
    url += ("&" if "?" in url else "?") + "download=1"

    try:
        with requests.get(url, stream=True, timeout=30) as response:
            content_type = response.headers.get("Content-Type", "")

            # An expired or restricted share link returns an HTML error page instead of the file
            if response.status_code != 200 or content_type.startswith("text/html"):
                print(f"Failed to download {url} - Status: {response.status_code}, Content-Type: {content_type}")
                return False

            total = int(response.headers.get("Content-Length", 0))
            done = 0
            with open(file_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=chunk_size):
                    f.write(chunk)
                    done += len(chunk)
                    print(f"\rDownloaded {done / 1e6:.0f} / {total / 1e6:.0f} MB", end="")
            print()
            return True

    except Exception as e:
        print(f"Error downloading {url}: {e}")
        return False

def find_tile_root(folder, tile_folder):
    """
    Find the folder holding the {z}/{x}/{y} tiles in an unzipped archive, i.e., the folder
    named tile_folder or the first folder that only contains zoom level folders.
    """
    for root, dirs, _ in os.walk(folder):
        if Path(root).name == tile_folder or (dirs and all(d.isdigit() for d in dirs)):
            return Path(root)
    return None

def download_switzerland_tiles(tile_set):
    """
    Download the zip with the tiles of Switzerland and unzip it into Assets.
    Skipped if the tiles already exist in Assets.

    Args:
        tile_set: Entry of TILE_SETS

    Returns:
        bool: True if the tiles are available in Assets, False otherwise
    """
    target = tile_dir(tile_set)

    # The zip contains all zoom levels starting at 0, so the 0/0/0 tile shows whether it was already unzipped
    if (target / "0" / "0" / f"0.{tile_set['form']}").exists():
        print(f"Tiles already exist in {target}, skipping download.\n")
        return True

    print(f"Downloading {tile_set['data_type']} tiles of Switzerland into {target}")
    ASSETS_DIR.mkdir(exist_ok=True)

    # Temporary folder inside Assets, so that the unzipped tiles can be moved instead of copied
    with tempfile.TemporaryDirectory(prefix="_download_", dir=ASSETS_DIR, ignore_cleanup_errors=True) as tmp:
        zip_path = Path(tmp) / f"{tile_set['package']}.zip"
        if not download_file(tile_set["zip_url"], zip_path) or not zipfile.is_zipfile(zip_path):
            print(f"The link may have expired. Download the zip manually (see README) and unzip it into {ASSETS_DIR}.\n")
            return False

        print("Unzipping ...")
        unzip_dir = Path(tmp) / "unzipped"
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(unzip_dir)
        zip_path.unlink()

        source = find_tile_root(unzip_dir, tile_set["tile_folder"])
        if source is None:
            print(f"No map tiles found in {zip_path.name}.\n")
            return False

        if target.exists():
            # e.g. created by an earlier download of further zoom levels from Mapbox
            shutil.copytree(source, target, dirs_exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(target))

    print(f"Tiles stored in {target}\n")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Download map tiles into Assets. By default, downloads and unzips the tiles of Switzerland "
                    "(skipped if they already exist). For more detail, further zoom levels can be downloaded from Mapbox.")
    parser.add_argument("--zoom", type=int, nargs="+", default=[],
                        help="additional zoom levels to download from Mapbox, e.g. --zoom 15 16")
    args = parser.parse_args()

    MAPBOX_ACCESS_TOKEN = "pk.eyJ1IjoiYXJkaWFuLXNlbG1vbmFqIiwiYSI6ImNtbDVmYjlyMDAwYXkzZXMybGV6bWh2enAifQ.kYwMo1PfJVT2bmSzMK9n8g"
    
    # center CH (near Luzern)
    CENTER_LATITUDE = 46.953096
    CENTER_LONGITUDE = 8.439162

    # Tiles of Switzerland from the zips
    for tile_set in TILE_SETS:
        download_switzerland_tiles(tile_set)

    # Further zoom levels from Mapbox, stored next to the tiles of Switzerland
    if args.zoom:
        for tile_set in TILE_SETS:
            download_tiles_for_location(
                center_lat=CENTER_LATITUDE,
                center_lon=CENTER_LONGITUDE,
                access_token=MAPBOX_ACCESS_TOKEN,
                data_type=tile_set["data_type"],
                form=tile_set["form"],
                lat_range=2,      # ±2 degrees latitude
                lon_range=2,      # ±2 degrees longitude
                step=0.001,         # 0.0002 degree steps --> up to level 13, 0.001 for level 14
                zoom_levels=sorted(args.zoom),
                base_path=tile_dir(tile_set)
            )
        print(f"To render the new zoom levels, raise 'maxLevel' in Assets/Globe/Entities/PlanetEarth.json to {max(args.zoom)}.")
