# Mobile 3D Gaussian Splatting

## Cross-Platform Support

Use `scripts/configure.py` to configure and build your project. It handles platform detection, CMake configuration, and provides convenient build commands with target management.

### Quick start

```bash
# Configure with platform defaults (Release build)
python3 scripts/configure.py

# Configure Debug build with validation layers
python3 scripts/configure.py --clean --build-type Debug --validation

# Build and run the triangle example
python3 scripts/configure.py build --target triangle --run

# Build and run all tests
python3 scripts/configure.py build --tests --run

# List all available build targets
python3 scripts/configure.py build --list-targets
```

### Build Commands

After configuration, use the integrated build system:

```bash
# Build specific targets
python3 scripts/configure.py build --target triangle
python3 scripts/configure.py build --target unit-tests --target perf-tests

# Build shortcuts
python3 scripts/configure.py build --all         # Build all targets
python3 scripts/configure.py build --tests       # Build test targets only

# Build and run (single target only)
python3 scripts/configure.py build --target triangle --run

# Advanced options
python3 scripts/configure.py build --clean --parallel 8 --target triangle
```

Traditional CMake commands still work:

```bash
# For multi-config generators (Visual Studio, Xcode):
cmake --build build --config Debug --parallel
# For single-config generators (Ninja, Unix Makefiles):
cmake --build build --parallel
```

### Platform notes

- **Windows**: Defaults to Visual Studio 2022 (x64). Install the Vulkan SDK and ensure `VULKAN_SDK` is set.
- **macOS**: Defaults to Xcode. MoltenVK ICD is auto-detected when available. Vulkan loader via Homebrew is supported.
- **Linux**: Defaults to Unix Makefiles. Vulkan is detected via pkg-config or `VULKAN_SDK`.

### Options

```bash
python3 scripts/configure.py \
  [--build-type Debug|Release] \
  [--clean] [--validation] \
  [--generator "Xcode"|"Visual Studio 17 2022"|"Ninja"|"Unix Makefiles"] \
  [--build-dir build]
```

### Tips

- Need a fresh configure? Add `--clean`.
- Prefer Ninja? `--generator "Ninja"` (single-config; uses `--build-type`).
- IDE users can open the generated project in Visual Studio/Xcode after configuration.
