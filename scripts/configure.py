#!/usr/bin/env python3
"""
Cross-platform CMake configuration script for the project.
"""

import os
import sys
import platform
import subprocess
import shutil
import argparse
import re
import time
from pathlib import Path
from utils import term


class PlatformConfig:
    """Base class for platform-specific configurations."""

    def __init__(self):
        self.platform_name = platform.system().lower()
        self.build_dir = Path("build")
        self.cmake_args = []
        self.env_vars = {}

    def detect_vulkan_sdk(self):
        """Detect Vulkan SDK installation."""
        vulkan_sdk = os.environ.get("VULKAN_SDK")
        if vulkan_sdk:
            return Path(vulkan_sdk)
        return None

    def setup_cmake_args(self, build_type="Release", enable_validation=False):
        """Setup basic CMake arguments."""
        self.cmake_args = [
            f"-DCMAKE_BUILD_TYPE={build_type}",
        ]

        if enable_validation:
            self.cmake_args.append("-DENABLE_VULKAN_VALIDATION=ON")

    def configure(self):
        """Platform-specific configuration. Override in subclasses."""
        pass


class WindowsConfig(PlatformConfig):
    """Windows-specific configuration."""

    def configure(self):
        term.section("Configuring for Windows")

        # Detect Vulkan SDK
        vulkan_sdk = self.detect_vulkan_sdk()
        if vulkan_sdk:
            term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
            self.cmake_args.extend(
                [
                    f"-DVULKAN_SDK={vulkan_sdk}",
                    f"-DVulkan_INCLUDE_DIRS={vulkan_sdk / 'Include'}",
                    f"-DVulkan_LIBRARIES={vulkan_sdk / 'Lib' / 'vulkan-1.lib'}",
                ]
            )
        else:
            term.warn(
                "Vulkan SDK not found. Please install Vulkan SDK and set VULKAN_SDK environment variable."
            )
            return False

        # Windows-specific CMake settings
        self.cmake_args.extend(
            [
                "-G",
                "Visual Studio 17 2022",  # Default to VS 2022, can be overridden
                "-A",
                "x64",
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
            ]
        )

        return True


class MacOSConfig(PlatformConfig):
    """macOS-specific configuration with MoltenVK support."""

    def detect_moltenvk(self):
        """Detect MoltenVK installation paths."""
        possible_paths = [
            "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json",
            "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json",
            Path.home()
            / "VulkanSDK"
            / "macOS"
            / "share"
            / "vulkan"
            / "icd.d"
            / "MoltenVK_icd.json",
        ]

        for path in possible_paths:
            if Path(path).exists():
                return Path(path)
        return None

    def configure(self):
        term.section("Configuring for macOS")

        # Detect Vulkan SDK
        vulkan_sdk = self.detect_vulkan_sdk()
        if vulkan_sdk:
            term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
        else:
            term.warn("VULKAN_SDK not set. Checking common installation paths...")
            # Check common macOS Vulkan SDK paths
            common_paths = [
                Path.home() / "VulkanSDK" / "macOS",
                Path("/usr/local"),
                Path("/opt/homebrew"),
            ]

            for path in common_paths:
                if (path / "lib" / "libvulkan.dylib").exists():
                    vulkan_sdk = path
                    term.success(f"Found Vulkan at: {vulkan_sdk}")
                    break

            if not vulkan_sdk:
                term.error("Vulkan SDK not found. Please install via:")
                print("   - Download from https://vulkan.lunarg.com/")
                print("   - Or install via Homebrew: brew install vulkan-loader")
                return False

        # Detect MoltenVK
        moltenvk_icd = self.detect_moltenvk()
        if moltenvk_icd:
            term.success(f"Found MoltenVK ICD at: {moltenvk_icd}")
            self.env_vars["VK_ICD_FILENAMES"] = str(moltenvk_icd)
        else:
            term.warn("MoltenVK ICD not found. Vulkan may not work properly.")

        # macOS-specific CMake settings
        self.cmake_args.extend(
            [
                "-G",
                "Xcode",  # Use Xcode generator by default
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15",  # Minimum macOS version
            ]
        )

        # MoltenVK specific settings
        self.cmake_args.extend(["-DVK_USE_PLATFORM_MACOS_MVK=ON", "-DMOLTENVK=ON"])

        return True


class LinuxConfig(PlatformConfig):
    """Linux-specific configuration."""

    def detect_vulkan_packages(self):
        """Detect Vulkan packages on Linux."""
        try:
            # Check for pkg-config
            result = subprocess.run(
                ["pkg-config", "--exists", "vulkan"], capture_output=True, text=True
            )
            return result.returncode == 0
        except FileNotFoundError:
            return False

    def configure(self):
        term.section("Configuring for Linux")

        # Check for Vulkan via pkg-config
        if self.detect_vulkan_packages():
            term.success("Found Vulkan via pkg-config")
        else:
            vulkan_sdk = self.detect_vulkan_sdk()
            if vulkan_sdk:
                term.success(f"Found Vulkan SDK at: {vulkan_sdk}")
                self.cmake_args.extend(
                    [
                        f"-DVULKAN_SDK={vulkan_sdk}",
                    ]
                )
            else:
                term.error("Vulkan not found. Please install via:")
                print("   - Ubuntu/Debian: sudo apt install libvulkan-dev vulkan-tools")
                print("   - Fedora: sudo dnf install vulkan-loader-devel vulkan-tools")
                print("   - Arch: sudo pacman -S vulkan-devel vulkan-tools")
                return False

        # Linux-specific CMake settings
        self.cmake_args.extend(
            [
                "-G",
                "Unix Makefiles",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",  # For IDE support
            ]
        )

        return True


def get_project_name(source_dir):
    """Extract project name from CMakeLists.txt, fallback to directory name."""
    cmake_file = source_dir / "CMakeLists.txt"

    if cmake_file.exists():
        try:
            content = cmake_file.read_text()
            # Look for project() declaration
            match = re.search(r"project\s*\(\s*([^\s)]+)", content, re.IGNORECASE)
            if match:
                return match.group(1)
        except Exception as e:
            term.warn(f"Could not parse CMakeLists.txt: {e}")

    # Fallback to directory name
    return source_dir.name


def get_platform_config():
    """Factory function to get platform-specific configuration."""
    system = platform.system().lower()

    if system == "windows":
        return WindowsConfig()
    elif system == "darwin":
        return MacOSConfig()
    elif system == "linux":
        return LinuxConfig()
    else:
        print(f"❌ Unsupported platform: {system}")
        return None


def clean_build_dir(build_dir):
    """Clean the build directory."""
    if build_dir.exists():
        print(f"🧹 Cleaning build directory: {build_dir}")
        shutil.rmtree(build_dir)
    build_dir.mkdir(exist_ok=True)


def run_cmake(config, source_dir, build_dir):
    """Run CMake configuration."""
    cmake_cmd = ["cmake"] + config.cmake_args + [str(source_dir)]

    term.section("Running CMake configuration")
    term.kv("Command", " ".join(cmake_cmd))
    term.kv("Working directory", str(build_dir))

    # Set environment variables
    env = os.environ.copy()
    env.update(config.env_vars)

    if config.env_vars:
        term.info("Environment variables:")
        for key, value in config.env_vars.items():
            print(f"  {key}={value}")

    try:
        result = subprocess.run(
            cmake_cmd, cwd=build_dir, env=env, check=True, text=True
        )
        return True
    except subprocess.CalledProcessError as e:
        term.error(f"CMake configuration failed: {e}")
        return False


def is_project_configured(build_dir):
    """Check if the project is properly configured."""
    if not build_dir.exists():
        return False
    
    # Check for CMakeCache.txt which indicates configuration has been run
    cmake_cache = build_dir / "CMakeCache.txt"
    return cmake_cache.exists()


def auto_configure(source_dir, build_dir, build_type="Release"):
    """Automatically configure the project with sensible defaults."""
    term.section("Auto-configuring project")
    term.info(f"Project not configured, running automatic configuration...")
    term.kv("Build type", build_type)
    
    # Get platform configuration
    config = get_platform_config()
    if not config:
        return False

    # Setup configuration with defaults
    config.setup_cmake_args(build_type, enable_validation=False)

    # Platform-specific configuration
    if not config.configure():
        return False

    # Create build directory
    build_dir.mkdir(exist_ok=True)

    # Run CMake configuration
    if not run_cmake(config, source_dir, build_dir):
        return False

    term.success("Auto-configuration completed successfully")
    return True


def discover_build_targets(build_dir):
    """Discover available CMake targets in the build directory."""
    targets = []
    
    # Common targets we know exist
    known_targets = [
        "triangle",
        "unit-tests", 
        "perf-tests",
        "msplat_core",
        "vulkan_rhi"
    ]
    
    # Try to get actual targets from CMake if available
    try:
        result = subprocess.run(
            ["cmake", "--build", str(build_dir), "--target", "help"],
            capture_output=True,
            text=True,
            cwd=build_dir
        )
        if result.returncode == 0:
            # Parse the output for actual targets
            for line in result.stdout.splitlines():
                line = line.strip()
                if line and not line.startswith("..."):
                    targets.append(line)
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Fallback to known targets
        targets = known_targets
    
    return targets if targets else known_targets


def build_targets(config, source_dir, build_dir, targets, parallel_jobs=None, clean_first=False):
    """Build specified targets using cmake --build."""
    
    if not is_project_configured(build_dir):
        term.error(f"Project not configured in: {build_dir}")
        term.info("Run configuration first with: python scripts/configure.py")
        return False
    
    if clean_first:
        term.section("Cleaning build directory")
        try:
            result = subprocess.run(
                ["cmake", "--build", str(build_dir), "--target", "clean"],
                cwd=build_dir,
                check=True
            )
        except subprocess.CalledProcessError as e:
            term.warn(f"Clean failed, continuing anyway: {e}")
    
    # Determine if this is a multi-config generator
    generator = None
    try:
        # Try to detect generator from cache
        cache_file = build_dir / "CMakeCache.txt"
        if cache_file.exists():
            content = cache_file.read_text()
            for line in content.splitlines():
                if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                    generator = line.split("=", 1)[1]
                    break
    except Exception:
        pass
    
    generator_lower = (generator or "").lower()
    is_multi_config = (
        ("visual studio" in generator_lower) or
        ("xcode" in generator_lower) or 
        ("multi-config" in generator_lower)
    )
    
    # Get build type from cache if not multi-config
    build_type = "Release"  # Default
    if not is_multi_config:
        try:
            cache_file = build_dir / "CMakeCache.txt"
            if cache_file.exists():
                content = cache_file.read_text()
                for line in content.splitlines():
                    if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                        build_type = line.split("=", 1)[1]
                        break
        except Exception:
            pass
    else:
        # For multi-config, assume Debug if build/bin/Debug exists, otherwise Release
        if (build_dir / "bin" / "Debug").exists():
            build_type = "Debug"
        elif (build_dir / "bin" / "Release").exists():
            build_type = "Release"
    
    term.section(f"Building targets: {', '.join(targets)}")
    term.kv("Build directory", str(build_dir))
    term.kv("Build type", build_type)
    term.kv("Generator", generator or "Unknown")
    
    # Build each target
    success_count = 0
    for target in targets:
        term.info(f"Building target: {target}")
        
        cmd = ["cmake", "--build", str(build_dir)]
        if is_multi_config:
            cmd.extend(["--config", build_type])
        cmd.extend(["--target", target])
        if parallel_jobs:
            cmd.extend(["--parallel", str(parallel_jobs)])
        else:
            cmd.append("--parallel")
        
        term.kv("Command", " ".join(cmd))
        
        try:
            start_time = time.time()
            result = subprocess.run(cmd, cwd=build_dir, check=True)
            end_time = time.time()
            
            build_time = end_time - start_time
            term.success(f"Built {target} in {build_time:.1f}s")
            success_count += 1
            
        except subprocess.CalledProcessError as e:
            term.error(f"Failed to build target '{target}': {e}")
            return False
    
    term.success(f"Successfully built {success_count}/{len(targets)} targets")
    return True


def run_executable(build_dir, target, build_type="Release"):
    """Run an executable target from the proper working directory."""
    # Determine executable path
    if build_type == "Debug":
        exe_dir = build_dir / "bin" / "Debug"
    else:
        exe_dir = build_dir / "bin" / "Release"
    
    exe_path = exe_dir / target
    if platform.system().lower() == "windows":
        exe_path = exe_path.with_suffix(".exe")
    
    if not exe_path.exists():
        term.error(f"Executable not found: {exe_path}")
        return False
    
    term.section(f"Running {target}")
    term.kv("Executable", str(exe_path))
    term.kv("Working directory", str(exe_dir))
    
    try:
        # Run from the executable's directory (important for asset loading)
        result = subprocess.run([f"./{target}"], cwd=exe_dir, check=False)
        
        if result.returncode == 0:
            term.success(f"{target} completed successfully")
        else:
            term.warn(f"{target} exited with code {result.returncode}")
        
        return result.returncode == 0
        
    except subprocess.CalledProcessError as e:
        term.error(f"Failed to run {target}: {e}")
        return False
    except FileNotFoundError:
        term.error(f"Executable not found or not executable: {exe_path}")
        return False


def main():
    # Create main parser
    parser = argparse.ArgumentParser(
        description="Cross-platform CMake configuration and build tool for Mobile 3D Gaussian Splatting",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/configure.py                    # Configure with defaults
  python scripts/configure.py --clean --debug   # Clean configure for Debug
  python scripts/configure.py build --target triangle  # Build triangle target
  python scripts/configure.py build --tests --run      # Build and run tests
  python scripts/configure.py build --list-targets     # Show available targets
        """
    )
    
    # Create subparsers for different commands
    subparsers = parser.add_subparsers(dest="command", help="Available commands")
    
    # Configure command (default behavior, so these are also top-level args)
    parser.add_argument(
        "--build-type",
        choices=["Debug", "Release"],
        default="Release",
        help="CMake build type",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build directory before configuration",
    )
    parser.add_argument(
        "--validation",
        action="store_true",
        help="Enable Vulkan validation layers (Debug builds)",
    )
    parser.add_argument("--generator", help="Override CMake generator")
    parser.add_argument("--build-dir", default="build", help="Build directory path")
    
    # Build subcommand
    build_parser = subparsers.add_parser("build", help="Build targets")
    build_parser.add_argument(
        "--target",
        action="append",
        dest="targets",
        help="Build specific target (can be used multiple times)"
    )
    build_parser.add_argument(
        "--list-targets",
        action="store_true",
        help="List available build targets"
    )
    build_parser.add_argument(
        "--tests",
        action="store_true", 
        help="Build all test targets (unit-tests, perf-tests)"
    )
    build_parser.add_argument(
        "--all",
        action="store_true",
        help="Build all targets"
    )
    build_parser.add_argument(
        "--run",
        action="store_true",
        help="Run executable after building (for single target)"
    )
    build_parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean before building"
    )
    build_parser.add_argument(
        "--parallel",
        type=int,
        help="Number of parallel build jobs"
    )
    build_parser.add_argument("--build-dir", default="build", help="Build directory path")

    args = parser.parse_args()

    # Resolve project root: scripts/ is one level below project root
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent.resolve()
    source_dir = root_dir
    build_dir = (root_dir / args.build_dir).resolve()

    # Handle build command
    if args.command == "build":
        # Auto-configure if project is not configured
        if not is_project_configured(build_dir):
            # Determine build type for auto-configuration
            # For build command, default to Debug if no specific preference is shown
            auto_build_type = "Debug"  # Default for development workflow
            
            if not auto_configure(source_dir, build_dir, auto_build_type):
                term.error("Auto-configuration failed")
                return 1
        
        # List targets mode
        if args.list_targets:
            term.section("Available Build Targets")
            targets = discover_build_targets(build_dir)
            for target in targets:
                print(f"  • {target}")
            return 0
        
        # Determine which targets to build
        targets_to_build = []
        
        if args.all:
            targets_to_build = discover_build_targets(build_dir)
        elif args.tests:
            targets_to_build = ["unit-tests", "perf-tests"]
        elif args.targets:
            targets_to_build = args.targets
        else:
            term.error("No targets specified. Use --target, --tests, --all, or --list-targets")
            return 1
        
        # Build the targets
        if not build_targets(None, source_dir, build_dir, targets_to_build, args.parallel, args.clean):
            return 1
        
        # Run executable if requested
        if args.run:
            if len(targets_to_build) != 1:
                term.error("--run can only be used with a single target")
                return 1
            
            target = targets_to_build[0]
            # Detect build type from build directory
            build_type = "Release"
            if (build_dir / "bin" / "Debug").exists():
                build_type = "Debug"
            
            if not run_executable(build_dir, target, build_type):
                return 1
        
        return 0
    
    # Default behavior: configure
    term.section("CMake Configuration Begins")
    term.kv("Platform", f"{platform.system()} {platform.machine()}")
    term.kv("Python", sys.version)
    term.kv("Source directory", str(source_dir))
    term.kv("Build directory", str(build_dir))
    term.kv("Build type", args.build_type)

    # Get platform configuration
    config = get_platform_config()
    if not config:
        return 1

    # Setup configuration
    config.setup_cmake_args(args.build_type, args.validation)

    # Platform-specific configuration
    if not config.configure():
        return 1

    # Override generator if specified (after platform configuration)
    if args.generator:
        # Remove existing generator arguments
        new_args = []
        skip_next = False
        for arg in config.cmake_args:
            if skip_next:
                skip_next = False
                continue
            if arg == "-G":
                skip_next = True
                continue
            new_args.append(arg)
        config.cmake_args = new_args
        config.cmake_args.extend(["-G", args.generator])

    # Clean build directory if requested
    if args.clean:
        clean_build_dir(build_dir)
    else:
        build_dir.mkdir(exist_ok=True)

    # Run CMake
    if not run_cmake(config, source_dir, build_dir):
        return 1

    # Get project name
    project_name = get_project_name(source_dir)

    term.success("Configuration completed successfully")
    term.sep()
    term.info("Next steps:")

    # Print build command suggestions
    term.info("Build with the new build command:")
    print(f"  python scripts/configure.py build --target triangle")
    print(f"  python scripts/configure.py build --tests --run")
    print(f"  python scripts/configure.py build --all")
    print(f"  python scripts/configure.py build --list-targets")
    
    term.info("Or use traditional cmake commands:")
    
    # Print a single copy-pasteable command for building
    generator = None
    try:
        g_index = config.cmake_args.index("-G")
        generator = config.cmake_args[g_index + 1]
    except ValueError:
        generator = None
    except IndexError:
        generator = None

    generator_lower = (generator or "").lower()
    is_multi_config = (
        ("visual studio" in generator_lower) or
        ("xcode" in generator_lower) or 
        ("multi-config" in generator_lower)
    )

    abs_build_dir = str(build_dir.resolve())

    if is_multi_config:
        print(
            f"  cmake --build {abs_build_dir} --config {args.build_type} --parallel"
        )
        if "visual studio" in generator_lower or platform.system().lower() == "windows":
            sln_path = build_dir / f"{project_name}.sln"
            term.info(f"Or open the generated .sln in Visual Studio:")
            print(f'  start "{sln_path}"')
        if "xcode" in generator_lower or platform.system().lower() == "darwin":
            xcodeproj_path = build_dir / f"{project_name}.xcodeproj"
            term.info(f"Or open the generated .xcodeproj in Xcode:")
            print(f'  open "{xcodeproj_path}"')
    else:
        # Single-config generators (e.g., Ninja, Unix Makefiles) use CMAKE_BUILD_TYPE set at configure time
        print(f"  cmake --build {abs_build_dir} --parallel")

    return 0


if __name__ == "__main__":
    sys.exit(main())
