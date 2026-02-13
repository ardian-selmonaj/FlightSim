import os
import math
import requests
import time
from pathlib import Path

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
    print(f"Starting Mapbox terrain-rgb tile download for location: ({center_lat}, {center_lon})")
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
    
    print(f"Total unique tiles to download: {len(tiles_to_download)}")
    print("-" * 60)
    
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

if __name__ == "__main__":
    MAPBOX_ACCESS_TOKEN = "pk.eyJ1IjoiYXJkaWFuLXNlbG1vbmFqIiwiYSI6ImNtbDVmYjlyMDAwYXkzZXMybGV6bWh2enAifQ.kYwMo1PfJVT2bmSzMK9n8g"
    
    # center CH (near Luzern)
    CENTER_LATITUDE = 46.953096
    CENTER_LONGITUDE = 8.439162

    datas = ["terrain-rgb", "satellite"]
    # datas = ["satellite"]
    formats = ["png", "jpg"]
    # formats = ["jpg"]

    for data, form in zip(datas, formats):

        cwd = os.getcwd()
        terrain = os.path.join(cwd, f"mapbox_{data}_ch")
        
        # Download tiles
        download_tiles_for_location(
            center_lat=CENTER_LATITUDE,
            center_lon=CENTER_LONGITUDE,
            access_token=MAPBOX_ACCESS_TOKEN,
            data_type=data,
            form=form,
            lat_range=2,      # ±2 degrees latitude
            lon_range=2,      # ±2 degrees longitude
            step=0.001,         # 0.0002 degree steps --> up to level 13, 0.001 for level 14
            zoom_levels=range(14, 15),  # Zoom levels 1 to 13 --> 14
            base_path=terrain
        )