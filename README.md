# Mobile-3D-Gaussian-Splatting

## Cross-Platform Configuration Script

This project includes a Python-based configuration script (`configure.py`) that automatically detects your platform and sets up the appropriate CMake configuration for building the Mobile 3D Gaussian Splatting project.

### Quick Start

```bash
# Basic configuration (uses platform defaults)
python3 configure.py

# Clean build with specific generator
python3 configure.py --clean --generator "Unix Makefiles"

# Debug build with validation layers
python3 configure.py --build-type Debug --validation --clean
```

### Platform-Specific Behavior

#### 🪟 Windows
- **Default Generator**: Visual Studio 17 2022 (x64)
- **Vulkan Detection**: Uses `VULKAN_SDK` environment variable
- **Requirements**: Vulkan SDK installed from LunarG

#### 🍎 macOS
- **Default Generator**: Xcode
- **Vulkan Detection**: Checks common Homebrew and manual install paths
- **MoltenVK**: Automatically detects and configures MoltenVK ICD
- **Requirements**: Vulkan SDK or `brew install vulkan-loader`

#### 🐧 Linux
- **Default Generator**: Unix Makefiles
- **Vulkan Detection**: Uses pkg-config first, then `VULKAN_SDK`
- **Requirements**: `libvulkan-dev` package or Vulkan SDK

### Command Line Options

```
usage: configure.py [-h] [--build-type {Debug,Release,RelWithDebInfo,MinSizeRel}]
                    [--clean] [--validation] [--generator GENERATOR]
                    [--build-dir BUILD_DIR]

options:
  -h, --help            show this help message and exit
  --build-type BUILD_TYPE
                        CMake build type (default: Release)
  --clean               Clean build directory before configuration
  --validation          Enable Vulkan validation layers
  --generator GENERATOR Override CMake generator
  --build-dir BUILD_DIR Build directory path (default: build)
```

### Example Usage

#### Development Setup (macOS/Linux)
```bash
# Configure for development with Unix Makefiles
python3 configure.py --build-type Debug --generator "Unix Makefiles" --clean

# Build
cd build
make -j$(nproc)

# Run with proper environment
export VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json  # macOS only
./bin/triangle
```

#### Production Build (All Platforms)
```bash
# Configure for optimized release build
python3 configure.py --build-type Release --clean

# Build (platform-specific)
cd build

# Windows (Visual Studio)
cmake --build . --config Release

# macOS (Xcode)
cmake --build . --config Release
# or open Mobile-3D-Gaussian-Splatting.xcodeproj

# Linux (Make)
make -j$(nproc)
```

#### Cross-Platform CI/CD
```bash
# Generic build script that works on all platforms
python3 configure.py --build-type Release --clean
cd build
cmake --build . --config Release
```

### Platform Requirements

#### All Platforms
- CMake 3.20+
- Python 3.6+
- C++17 compatible compiler

#### Windows
- Visual Studio 2019+ (with C++ workload)
- Vulkan SDK from https://vulkan.lunarg.com/

#### macOS
- Xcode Command Line Tools: `xcode-select --install`
- Vulkan SDK: Download from LunarG or `brew install vulkan-loader`

#### Linux
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libvulkan-dev vulkan-tools

# Fedora
sudo dnf install gcc-c++ cmake vulkan-loader-devel vulkan-tools

# Arch
sudo pacman -S base-devel cmake vulkan-devel vulkan-tools
```

### Troubleshooting

#### Vulkan SDK Not Found
```bash
# Set environment variable manually
export VULKAN_SDK=/path/to/vulkan/sdk  # Linux/macOS
set VULKAN_SDK=C:\VulkanSDK\1.3.x.x    # Windows

python3 configure.py
```

#### MoltenVK Issues (macOS)
```bash
# Check if MoltenVK is installed
ls /usr/local/share/vulkan/icd.d/MoltenVK_icd.json

# Install via Homebrew if missing
brew install molten-vk
```

#### Generator Issues
```bash
# Force specific generator
python3 configure.py --generator "Unix Makefiles"

# Available generators (check with cmake --help)
python3 configure.py --generator "Ninja"
python3 configure.py --generator "Xcode"
```

#### Build Environment
The script automatically sets required environment variables. If you need to run the executable later:

```bash
# macOS - MoltenVK environment
export VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json
./bin/triangle

# Windows/Linux - usually no additional environment needed
./bin/triangle
```

### Integration with IDEs

#### Visual Studio (Windows)
```bash
python3 configure.py
cd build
# Open Mobile-3D-Gaussian-Splatting.sln
```

#### Xcode (macOS)
```bash
python3 configure.py
cd build
# Open Mobile-3D-Gaussian-Splatting.xcodeproj
```

#### VS Code (All Platforms)
```bash
python3 configure.py --generator "Unix Makefiles"
# VS Code will automatically detect the build directory
```
