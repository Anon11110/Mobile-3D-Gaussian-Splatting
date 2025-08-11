# Mobile 3D Gaussian Splatting

## Cross-Platform Support

Use `scripts/configure.py` to configure CMake for your platform. It selects sensible defaults (generator, flags, Vulkan/MoltenVK setup) and prints the exact build command to run next.

### Quick start

```bash
# Default configuration (platform defaults, Release)
python3 scripts/configure.py

# Clean Debug with validation layers
python3 scripts/configure.py --clean --build-type Debug --validation

# Pick a generator explicitly
python3 scripts/configure.py --generator "Unix Makefiles"
```

After configuration:

```bash
cd build
# For multi-config generators (Visual Studio, Xcode):
cmake --build . --config Debug --parallel
# For single-config generators (Ninja, Unix Makefiles):
cmake --build . --parallel
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
