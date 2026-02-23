#!/usr/bin/env python3
"""
Download and extract large asset files that are too big for Git.

Supports downloading from:
- NVIDIA servers (flowers_1)
- Hugging Face datasets (train, drjohnson, playroom, truck, garden, bicycle, kitchen)
"""

import os
import sys
import urllib.request
import zipfile
import shutil
import argparse
from pathlib import Path

# Asset registry - defines all downloadable assets
ASSETS = {
    # NVIDIA assets (zip format)
    "flowers_1": {
        "source": "nvidia",
        "url": "http://developer.download.nvidia.com/ProGraphics/nvpro-samples/flowers_1.zip",
        "type": "zip",
        "output_dir": "flowers_1",
        "output_file": "flowers_1.ply",
        "description": "Flowers scene from NVIDIA"
    },
    # Hugging Face assets (direct PLY download)
    "train_7000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/train/point_cloud/iteration_7000/point_cloud.ply",
        "output_dir": "train/point_cloud/iteration_7000",
        "output_file": "point_cloud.ply",
        "description": "Train scene iteration 7000"
    },
    "train_30000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/train/point_cloud/iteration_30000/point_cloud.ply",
        "output_dir": "train/point_cloud/iteration_30000",
        "output_file": "point_cloud.ply",
        "description": "Train scene iteration 30000"
    },
    "drjohnson_7000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/drjohnson/point_cloud/iteration_7000/point_cloud.ply",
        "output_dir": "drjohnson/point_cloud/iteration_7000",
        "output_file": "point_cloud.ply",
        "description": "Dr. Johnson scene iteration 7000"
    },
    "drjohnson_30000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/drjohnson/point_cloud/iteration_30000/point_cloud.ply",
        "output_dir": "drjohnson/point_cloud/iteration_30000",
        "output_file": "point_cloud.ply",
        "description": "Dr. Johnson scene iteration 30000"
    },
    "playroom_7000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/playroom/point_cloud/iteration_7000/point_cloud.ply",
        "output_dir": "playroom/point_cloud/iteration_7000",
        "output_file": "point_cloud.ply",
        "description": "Playroom scene iteration 7000"
    },
    "playroom_30000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/playroom/point_cloud/iteration_30000/point_cloud.ply",
        "output_dir": "playroom/point_cloud/iteration_30000",
        "output_file": "point_cloud.ply",
        "description": "Playroom scene iteration 30000"
    },
    "truck_7000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/truck/point_cloud/iteration_7000/point_cloud.ply",
        "output_dir": "truck/point_cloud/iteration_7000",
        "output_file": "point_cloud.ply",
        "description": "Truck scene iteration 7000"
    },
    "truck_30000": {
        "source": "huggingface",
        "dataset": "Voxel51/gaussian_splatting",
        "path": "FO_dataset/truck/point_cloud/iteration_30000/point_cloud.ply",
        "output_dir": "truck/point_cloud/iteration_30000",
        "output_file": "point_cloud.ply",
        "description": "Truck scene iteration 30000"
    },
    "garden": {
        "source": "huggingface",
        "dataset": "dylanebert/3dgs",
        "path": "garden/garden-7k.splat",
        "output_dir": "garden",
        "output_file": "garden-7k.splat",
        "description": "Garden scene"
    },
    "bicycle_7000": {
        "source": "huggingface",
        "dataset": "dylanebert/3dgs",
        "path": "bicycle/point_cloud/iteration_7000/point_cloud.ply",
        "output_dir": "bicycle/point_cloud/iteration_7000",
        "output_file": "point_cloud.ply",
        "description": "Bicycle scene iteration 7000"
    },
    "kitchen": {
        "source": "huggingface",
        "dataset": "dylanebert/3dgs",
        "path": "kitchen/kitchen-7k.splat",
        "output_dir": "kitchen",
        "output_file": "kitchen-7k.splat",
        "description": "Kitchen scene"
    },
}

# Default assets to download when no specific assets are requested
DEFAULT_ASSETS = [
    "flowers_1", "train_30000", "garden",
    "bicycle_7000", "kitchen",
]


def download_file(url, dest_path, description=""):
    """Download a file with progress indicator."""
    print(f"Downloading {description or url}...")

    def download_progress(block_num, block_size, total_size):
        if total_size > 0:
            downloaded = block_num * block_size
            percent = min(downloaded * 100 / total_size, 100)
            mb_downloaded = downloaded / 1024 / 1024
            mb_total = total_size / 1024 / 1024
            sys.stdout.write(f'\rProgress: {percent:.1f}% ({mb_downloaded:.1f}/{mb_total:.1f} MB)')
        else:
            downloaded = block_num * block_size
            mb_downloaded = downloaded / 1024 / 1024
            sys.stdout.write(f'\rDownloaded: {mb_downloaded:.1f} MB')
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


def get_huggingface_url(dataset, path):
    """Construct Hugging Face download URL."""
    return f"https://huggingface.co/datasets/{dataset}/resolve/main/{path}"


def download_nvidia_asset(asset_info, assets_dir):
    """Download and extract an NVIDIA zip asset."""
    output_dir = assets_dir / asset_info["output_dir"]
    output_file = output_dir / asset_info["output_file"]

    if output_file.exists():
        print(f"Asset already exists: {output_file}")
        return True

    output_dir.mkdir(parents=True, exist_ok=True)
    temp_zip = assets_dir / f"{asset_info['output_dir']}.zip"

    if not download_file(asset_info["url"], str(temp_zip), asset_info.get("description", "")):
        return False

    if not extract_zip(str(temp_zip), str(assets_dir)):
        return False

    # Clean up temp zip
    try:
        os.remove(temp_zip)
        print(f"Cleaned up temporary file: {temp_zip}")
    except Exception as e:
        print(f"Warning: Could not delete temporary file: {e}")

    if output_file.exists():
        size_mb = output_file.stat().st_size / 1024 / 1024
        print(f"Asset ready: {output_file} ({size_mb:.2f} MB)")
        return True
    else:
        print(f"ERROR: Expected file not found after extraction: {output_file}")
        return False


def download_huggingface_asset(asset_info, assets_dir):
    """Download a PLY file from Hugging Face."""
    output_dir = assets_dir / asset_info["output_dir"]
    output_file = output_dir / asset_info["output_file"]

    if output_file.exists():
        print(f"Asset already exists: {output_file}")
        return True

    output_dir.mkdir(parents=True, exist_ok=True)

    url = get_huggingface_url(asset_info["dataset"], asset_info["path"])

    if not download_file(url, str(output_file), asset_info.get("description", "")):
        return False

    if output_file.exists():
        size_mb = output_file.stat().st_size / 1024 / 1024
        print(f"Asset ready: {output_file} ({size_mb:.2f} MB)")
        return True
    else:
        print(f"ERROR: Download failed - file not found: {output_file}")
        return False


def download_asset(asset_name, assets_dir):
    """Download a single asset by name."""
    if asset_name not in ASSETS:
        print(f"ERROR: Unknown asset '{asset_name}'")
        print(f"Available assets: {', '.join(ASSETS.keys())}")
        return False

    asset_info = ASSETS[asset_name]
    print(f"\n{'=' * 60}")
    print(f"Downloading: {asset_name}")
    print(f"Description: {asset_info.get('description', 'N/A')}")
    print(f"{'=' * 60}")

    if asset_info["source"] == "nvidia":
        return download_nvidia_asset(asset_info, assets_dir)
    elif asset_info["source"] == "huggingface":
        return download_huggingface_asset(asset_info, assets_dir)
    else:
        print(f"ERROR: Unknown source type '{asset_info['source']}'")
        return False


def list_assets():
    """Print available assets."""
    print("\nAvailable assets:")
    print("-" * 70)
    for name, info in ASSETS.items():
        default_marker = " [DEFAULT]" if name in DEFAULT_ASSETS else ""
        print(f"  {name:<20} - {info.get('description', 'N/A')}{default_marker}")
    print("-" * 70)
    print(f"\nDefault assets: {', '.join(DEFAULT_ASSETS)}")


def main():
    parser = argparse.ArgumentParser(
        description="Download 3D Gaussian Splatting assets",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python download_assets.py                    # Download default assets
  python download_assets.py --list             # List available assets
  python download_assets.py flowers_1 train_7000  # Download specific assets
  python download_assets.py --all              # Download all assets
        """
    )
    parser.add_argument(
        "assets",
        nargs="*",
        help="Specific assets to download (default: flowers_1, train_7000, train_30000)"
    )
    parser.add_argument(
        "--list", "-l",
        action="store_true",
        help="List available assets"
    )
    parser.add_argument(
        "--all", "-a",
        action="store_true",
        help="Download all available assets"
    )

    args = parser.parse_args()

    if args.list:
        list_assets()
        return 0

    # Determine project paths
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    assets_dir = project_root / "assets" / "splats"

    # Create assets directory
    assets_dir.mkdir(parents=True, exist_ok=True)

    # Determine which assets to download
    if args.all:
        assets_to_download = list(ASSETS.keys())
    elif args.assets:
        assets_to_download = args.assets
    else:
        assets_to_download = DEFAULT_ASSETS

    print(f"\nAssets directory: {assets_dir}")
    print(f"Assets to download: {', '.join(assets_to_download)}")

    # Download each asset
    success_count = 0
    fail_count = 0

    for asset_name in assets_to_download:
        if download_asset(asset_name, assets_dir):
            success_count += 1
        else:
            fail_count += 1

    # Summary
    print(f"\n{'=' * 60}")
    print("DOWNLOAD SUMMARY")
    print(f"{'=' * 60}")
    print(f"Successful: {success_count}")
    print(f"Failed: {fail_count}")

    if fail_count > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
