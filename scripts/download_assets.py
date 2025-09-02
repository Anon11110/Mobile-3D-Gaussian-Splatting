#!/usr/bin/env python3
"""
Download and extract large asset files that are too big for Git.
This script downloads the flowers_1.ply file from NVIDIA's servers.
"""

import os
import sys
import urllib.request
import zipfile
import shutil
from pathlib import Path

def download_file(url, dest_path):
    """Download a file with progress indicator."""
    print(f"Downloading {url}...")
    
    def download_progress(block_num, block_size, total_size):
        downloaded = block_num * block_size
        percent = min(downloaded * 100 / total_size, 100)
        mb_downloaded = downloaded / 1024 / 1024
        mb_total = total_size / 1024 / 1024
        sys.stdout.write(f'\rProgress: {percent:.1f}% ({mb_downloaded:.1f}/{mb_total:.1f} MB)')
        sys.stdout.flush()
    
    try:
        urllib.request.urlretrieve(url, dest_path, reporthook=download_progress)
        print("\nDownload complete!")
        return True
    except Exception as e:
        print(f"\nError downloading file: {e}")
        return False

def extract_zip(zip_path, extract_to):
    """Extract a zip file."""
    print(f"Extracting {zip_path}...")
    try:
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(extract_to)
        print("Extraction complete!")
        return True
    except Exception as e:
        print(f"Error extracting file: {e}")
        return False

def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    assets_dir = project_root / "assets" / "splats"
    flowers_dir = assets_dir / "flowers_1"
    ply_file = flowers_dir / "flowers_1.ply"
    license_file = flowers_dir / "license_cc_by_4.0.html"

    if ply_file.exists():
        print(f"PLY file already exists at {ply_file}")
        print("Skipping download.")
        return 0

    assets_dir.mkdir(parents=True, exist_ok=True)

    url = "http://developer.download.nvidia.com/ProGraphics/nvpro-samples/flowers_1.zip"

    temp_zip = assets_dir / "flowers_1.zip"
    
    print("=" * 60)
    print("Downloading flowers_1.ply asset file")
    print("=" * 60)
    print(f"Source: {url}")
    print(f"Destination: {flowers_dir}")
    print()

    if not download_file(url, str(temp_zip)):
        return 1

    if not extract_zip(str(temp_zip), str(assets_dir)):
        return 1

    try:
        os.remove(temp_zip)
        print(f"Cleaned up temporary file: {temp_zip}")
    except Exception as e:
        print(f"Warning: Could not delete temporary file: {e}")
    
    if ply_file.exists() and license_file.exists():
        print()
        print("=" * 60)
        print("SUCCESS: Asset files downloaded and extracted!")
        print("=" * 60)
        print(f"PLY file: {ply_file}")
        print(f"License: {license_file}")

        size_mb = ply_file.stat().st_size / 1024 / 1024
        print(f"File size: {size_mb:.2f} MB")
        
        return 0
    else:
        print("ERROR: Expected files not found after extraction!")
        return 1

if __name__ == "__main__":
    sys.exit(main())