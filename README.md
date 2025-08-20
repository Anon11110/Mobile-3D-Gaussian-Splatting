# Mobile 3D Gaussian Splatting

Cross-platform 3D Gaussian Splatting implementation with Vulkan/MoltenVK backend, targeting desktop and mobile platforms.

## Quick Start

```bash
# Configure and build triangle example (most common workflow)
python3 scripts/configure.py
python3 scripts/configure.py build --target triangle --run

# Debug build with validation
python3 scripts/configure.py --clean --build-type Debug --validation
python3 scripts/configure.py build --target triangle --run

# Build and run tests
python3 scripts/configure.py build --tests --run

# Show detailed build output (useful for debugging)
python3 scripts/configure.py --verbose --build-type Debug
python3 scripts/configure.py build --target triangle --verbose
```

## Cross-Platform Usage

The `scripts/configure.py` handles platform detection, CMake configuration, and provides unified build commands across Windows, macOS, and Linux.

### Configuration

```bash
# Default Release build
python3 scripts/configure.py

# Debug build with fresh configuration
python3 scripts/configure.py --clean --build-type Debug --validation

# Override generator (useful for Ninja builds)
python3 scripts/configure.py --generator "Ninja" --build-type Debug
```

### Building

```bash
# Build specific targets
python3 scripts/configure.py build --target triangle
python3 scripts/configure.py build --target unit-tests --target perf-tests

# Build shortcuts
python3 scripts/configure.py build --all         # All targets
python3 scripts/configure.py build --tests       # Test targets only

# Build and run (single target)
python3 scripts/configure.py build --target triangle --run

# List available targets
python3 scripts/configure.py build --list-targets

# Advanced build options
python3 scripts/configure.py build --clean --parallel 8 --target triangle
```

### Build Output Control

By default, build commands use quiet mode for cleaner output:

```bash
# Quiet mode (default) - shows minimal output and build progress
python3 scripts/configure.py build --target triangle
# Output: 🔨 Building target: triangle
#         ✅ Built triangle in 2.3s

# Verbose mode - shows all build tool output (CMake, Xcode, MSBuild, etc.)
python3 scripts/configure.py build --target triangle --verbose
# Output: [Full xcodebuild/cmake output with all compilation details]
```

**Important**: Error messages are **always displayed** regardless of verbose setting, ensuring you never miss build failures.

### Platform Requirements

- **Windows**: Visual Studio 2022, Vulkan SDK with `VULKAN_SDK` environment variable
- **macOS**: Xcode, Vulkan SDK (MoltenVK auto-detected), or Vulkan loader via Homebrew
- **Linux**: GCC/Clang, Vulkan development packages (detected via pkg-config or `VULKAN_SDK`)

### Configuration Options

```bash
python3 scripts/configure.py \
  [--build-type Debug|Release] \
  [--clean] \
  [--validation] \
  [--generator "Generator Name"] \
  [--build-dir build] \
  [--verbose]

python3 scripts/configure.py build \
  [--target TARGET] \
  [--tests|--all|--list-targets] \
  [--run] \
  [--clean] \
  [--parallel N] \
  [--verbose]
```

**Available generators by platform:**
- Windows: "Visual Studio 17 2022", "Ninja"
- macOS: "Xcode", "Ninja", "Unix Makefiles"
- Linux: "Unix Makefiles", "Ninja"

### CMake Fallback

Traditional CMake commands work for advanced scenarios:

```bash
# Multi-config (Visual Studio, Xcode)
cmake --build build --config Debug --parallel

# Single-config (Ninja, Unix Makefiles)
cmake --build build --parallel
```
