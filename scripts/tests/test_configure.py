#!/usr/bin/env python3
"""
Unit tests for configure.py refactored functions.
"""

import unittest
import tempfile
import argparse
import sys
from pathlib import Path
from unittest.mock import patch, MagicMock, mock_open

# Add parent directory to path to import from scripts/
sys.path.append(str(Path(__file__).parent.parent))

from utils import term
from utils.configure_utils import (
    # Error handling and Result types
    ConfigureError,
    PlatformError,
    CMakeError,
    BuildError,
    ValidationError,
    Result,
    handle_error,
    log_environment_info,
    # Types
    BuildType,
    Generator,
    GeneratorInfo,
)
from configure import (
    create_argument_parser,
    # Command classes
    Command,
    ConfigureCommand,
    BuildCommand,
    CommandFactory,
)


class TestBuildTypeEnum(unittest.TestCase):
    """Test BuildType enum functionality."""

    def test_debug_value(self):
        self.assertEqual(BuildType.DEBUG.value, "Debug")

    def test_release_value(self):
        self.assertEqual(BuildType.RELEASE.value, "Release")

    def test_enum_members(self):
        self.assertEqual(len(BuildType), 2)
        self.assertIn(BuildType.DEBUG, BuildType)
        self.assertIn(BuildType.RELEASE, BuildType)


class TestGeneratorEnum(unittest.TestCase):
    """Test Generator enum functionality."""

    def test_visual_studio_value(self):
        self.assertEqual(Generator.VISUAL_STUDIO_2022.value, "Visual Studio 17 2022")

    def test_xcode_value(self):
        self.assertEqual(Generator.XCODE.value, "Xcode")

    def test_unix_makefiles_value(self):
        self.assertEqual(Generator.UNIX_MAKEFILES.value, "Unix Makefiles")

    def test_ninja_value(self):
        self.assertEqual(Generator.NINJA.value, "Ninja")

    def test_enum_members(self):
        self.assertEqual(len(Generator), 4)


class TestGeneratorInfo(unittest.TestCase):
    """Test GeneratorInfo dataclass and detection functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.build_dir = Path(self.temp_dir)

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_detect_visual_studio(self):
        """Test detection of Visual Studio generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_BUILD_TYPE:STRING=Release
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Visual Studio 17 2022")
        self.assertTrue(info.is_multi_config)

    def test_detect_xcode(self):
        """Test detection of Xcode generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Xcode
CMAKE_BUILD_TYPE:STRING=Debug
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Xcode")
        self.assertTrue(info.is_multi_config)

    def test_detect_unix_makefiles(self):
        """Test detection of Unix Makefiles generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Unix Makefiles
CMAKE_BUILD_TYPE:STRING=Release
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Unix Makefiles")
        self.assertFalse(info.is_multi_config)

    def test_detect_ninja(self):
        """Test detection of Ninja generator."""
        cache_content = """
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_BUILD_TYPE:STRING=Debug
"""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text(cache_content)

        info = GeneratorInfo.detect(self.build_dir)
        self.assertEqual(info.name, "Ninja")
        self.assertFalse(info.is_multi_config)

    def test_detect_no_cache_file(self):
        """Test detection when CMakeCache.txt doesn't exist."""
        info = GeneratorInfo.detect(self.build_dir)
        self.assertIsNone(info.name)
        self.assertFalse(info.is_multi_config)

    def test_detect_malformed_cache(self):
        """Test detection with malformed CMakeCache.txt."""
        cache_file = self.build_dir / "CMakeCache.txt"
        cache_file.write_text("malformed content")

        info = GeneratorInfo.detect(self.build_dir)
        self.assertIsNone(info.name)
        self.assertFalse(info.is_multi_config)

    def test_detect_multi_config_detection(self):
        """Test multi-config detection logic."""
        test_cases = [
            ("Visual Studio 16 2019", True),
            ("Visual Studio 17 2022", True),
            ("Xcode", True),
            ("Some Multi-Config Generator", True),
            ("Unix Makefiles", False),
            ("Ninja", False),
            ("", False),
        ]

        for generator_name, expected_multi_config in test_cases:
            with self.subTest(generator=generator_name):
                cache_content = f"CMAKE_GENERATOR:INTERNAL={generator_name}\n"
                cache_file = self.build_dir / "CMakeCache.txt"
                cache_file.write_text(cache_content)

                info = GeneratorInfo.detect(self.build_dir)
                self.assertEqual(info.is_multi_config, expected_multi_config)


class TestCreateArgumentParser(unittest.TestCase):
    """Test argument parser creation and parsing."""

    def setUp(self):
        self.parser = create_argument_parser()

    def test_parser_creation(self):
        """Test that parser is created successfully."""
        self.assertIsInstance(self.parser, argparse.ArgumentParser)

    def test_default_arguments(self):
        """Test default argument parsing."""
        args = self.parser.parse_args([])
        self.assertEqual(args.build_type, "Release")
        self.assertFalse(args.clean)
        self.assertFalse(args.validation)
        self.assertIsNone(args.generator)
        self.assertEqual(args.build_dir, "build")
        self.assertIsNone(args.command)

    def test_configure_arguments(self):
        """Test configure command arguments."""
        args = self.parser.parse_args(
            [
                "--build-type",
                "Debug",
                "--clean",
                "--validation",
                "--generator",
                "Ninja",
                "--build-dir",
                "custom_build",
            ]
        )

        self.assertEqual(args.build_type, "Debug")
        self.assertTrue(args.clean)
        self.assertTrue(args.validation)
        self.assertEqual(args.generator, "Ninja")
        self.assertEqual(args.build_dir, "custom_build")

    def test_build_command_basic(self):
        """Test basic build command parsing."""
        args = self.parser.parse_args(["build", "--target", "triangle"])
        self.assertEqual(args.command, "build")
        self.assertEqual(args.targets, ["triangle"])
        self.assertFalse(args.tests)
        self.assertFalse(args.all)
        self.assertFalse(args.run)

    def test_build_command_multiple_targets(self):
        """Test build command with multiple targets."""
        args = self.parser.parse_args(
            ["build", "--target", "triangle", "--target", "unit-tests"]
        )
        self.assertEqual(args.targets, ["triangle", "unit-tests"])

    def test_build_command_tests(self):
        """Test build command with tests flag."""
        args = self.parser.parse_args(["build", "--tests", "--run"])
        self.assertTrue(args.tests)
        self.assertTrue(args.run)

    def test_build_command_all(self):
        """Test build command with all flag."""
        args = self.parser.parse_args(["build", "--all", "--parallel", "8"])
        self.assertTrue(args.all)
        self.assertEqual(args.parallel, 8)

    def test_build_command_list_targets(self):
        """Test build command with list-targets flag."""
        args = self.parser.parse_args(["build", "--list-targets"])
        self.assertTrue(args.list_targets)

    def test_invalid_build_type(self):
        """Test invalid build type raises error."""
        with self.assertRaises(SystemExit):
            self.parser.parse_args(["--build-type", "Invalid"])


# Phase 3 Tests - Command Pattern and Error Handling
class TestErrorHandling(unittest.TestCase):
    """Test error handling and result types."""

    def test_configure_error_creation(self):
        """Test ConfigureError creation and properties."""
        error = ConfigureError("Test message", exit_code=2, details="Test details")
        self.assertEqual(error.message, "Test message")
        self.assertEqual(error.exit_code, 2)
        self.assertEqual(error.details, "Test details")

    def test_specialized_errors(self):
        """Test specialized error types."""
        platform_error = PlatformError("Platform issue")
        cmake_error = CMakeError("CMake issue")
        build_error = BuildError("Build issue")
        validation_error = ValidationError("Validation issue")

        self.assertIsInstance(platform_error, ConfigureError)
        self.assertIsInstance(cmake_error, ConfigureError)
        self.assertIsInstance(build_error, ConfigureError)
        self.assertIsInstance(validation_error, ConfigureError)

    def test_result_ok(self):
        """Test successful Result creation."""
        result = Result.ok("success_value")
        self.assertTrue(result.success)
        self.assertEqual(result.value, "success_value")
        self.assertIsNone(result.error)

    def test_result_fail(self):
        """Test failed Result creation."""
        error = ConfigureError("Test error")
        result = Result.fail(error)
        self.assertFalse(result.success)
        self.assertIsNone(result.value)
        self.assertEqual(result.error, error)

    def test_result_unwrap_success(self):
        """Test unwrapping successful result."""
        result = Result.ok(42)
        value = result.unwrap()
        self.assertEqual(value, 42)

    def test_result_unwrap_failure(self):
        """Test unwrapping failed result raises error."""
        error = ConfigureError("Test error")
        result = Result.fail(error)
        with self.assertRaises(ConfigureError):
            result.unwrap()

    @patch("utils.configure_utils.term")
    def test_handle_error_basic(self, mock_term):
        """Test basic error handling."""
        error = ConfigureError("Test error", exit_code=5)
        exit_code = handle_error(error)

        mock_term.error.assert_called_once_with("Test error")
        self.assertEqual(exit_code, 5)

    @patch("utils.configure_utils.term")
    def test_handle_error_with_details(self, mock_term):
        """Test error handling with details."""
        error = ConfigureError("Test error", details="Additional info")
        handle_error(error)

        mock_term.error.assert_called_once_with("Test error")
        mock_term.info.assert_called_with("Details: Additional info")


class TestCommandFactory(unittest.TestCase):
    """Test CommandFactory functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_create_configure_command(self):
        """Test creating configure command."""
        args = MagicMock()
        args.command = None

        result = CommandFactory.create_command(args, self.source_dir, self.build_dir)

        self.assertTrue(result.success)
        self.assertIsInstance(result.value, ConfigureCommand)

    def test_create_build_command(self):
        """Test creating build command."""
        args = MagicMock()
        args.command = "build"

        result = CommandFactory.create_command(args, self.source_dir, self.build_dir)

        self.assertTrue(result.success)
        self.assertIsInstance(result.value, BuildCommand)


class TestConfigureCommand(unittest.TestCase):
    """Test ConfigureCommand functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

        # Create CMakeLists.txt
        (self.source_dir / "CMakeLists.txt").write_text("project(TestProject)")

        # Create mock args
        self.mock_args = MagicMock()
        self.mock_args.build_type = "Release"
        self.mock_args.validation = False
        self.mock_args.generator = None
        self.mock_args.clean = False

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_validation_success(self):
        """Test successful validation."""
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertTrue(result.success)

    def test_validation_no_source_dir(self):
        """Test validation with missing source directory."""
        non_existent_dir = Path(self.temp_dir) / "nonexistent"
        command = ConfigureCommand(self.mock_args, non_existent_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Source directory not found", result.error.message)

    def test_validation_no_cmake_file(self):
        """Test validation with missing CMakeLists.txt."""
        (self.source_dir / "CMakeLists.txt").unlink()
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("CMakeLists.txt not found", result.error.message)

    def test_validation_invalid_build_type(self):
        """Test validation with invalid build type."""
        self.mock_args.build_type = "Invalid"
        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Invalid build type", result.error.message)

    @patch("configure.get_platform_config")
    @patch("utils.configure_utils.run_cmake")
    @patch("utils.configure_utils.print_success_instructions")
    def test_execute_success(
        self, mock_print_success, mock_run_cmake, mock_get_platform_config
    ):
        """Test successful command execution."""
        mock_config = MagicMock()
        mock_config.configure.return_value = True
        mock_config.cmake_args = []
        mock_config.env_vars = {}
        mock_get_platform_config.return_value = mock_config
        mock_run_cmake.return_value = True

        command = ConfigureCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.execute()

        self.assertTrue(result.success)
        self.assertEqual(result.value, 0)


class TestBuildCommand(unittest.TestCase):
    """Test BuildCommand functionality."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.source_dir = Path(self.temp_dir) / "source"
        self.build_dir = Path(self.temp_dir) / "build"
        self.source_dir.mkdir()
        self.build_dir.mkdir()

        # Create mock args
        self.mock_args = MagicMock()
        self.mock_args.targets = None
        self.mock_args.tests = False
        self.mock_args.all = False
        self.mock_args.list_targets = False
        self.mock_args.run = False
        self.mock_args.parallel = None
        self.mock_args.clean = False

    def tearDown(self):
        import shutil

        shutil.rmtree(self.temp_dir)

    def test_validation_conflicting_options(self):
        """Test validation with conflicting target options."""
        self.mock_args.targets = ["triangle"]
        self.mock_args.tests = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("Only one target selection option", result.error.message)

    def test_validation_no_targets(self):
        """Test validation with no targets specified."""
        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn("No targets specified", result.error.message)

    def test_validation_run_multiple_targets(self):
        """Test validation with run flag and multiple targets."""
        self.mock_args.targets = ["triangle", "unit-tests"]
        self.mock_args.run = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command.validate()

        self.assertFalse(result.success)
        self.assertIsInstance(result.error, ValidationError)
        self.assertIn(
            "--run can only be used with a single target", result.error.message
        )

    def test_determine_targets_all(self):
        """Test determining targets with --all flag."""
        self.mock_args.all = True

        with patch("configure.discover_build_targets") as mock_discover_targets:
            mock_discover_targets.return_value = [
                "triangle",
                "unit-tests",
                "perf-tests",
            ]

            command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
            result = command._determine_targets()

            self.assertTrue(result.success)
            # The result should match what discover_build_targets returns
            self.assertEqual(result.value, ["triangle", "unit-tests", "perf-tests"])
            mock_discover_targets.assert_called_once_with(self.build_dir)

    def test_determine_targets_tests(self):
        """Test determining targets with --tests flag."""
        self.mock_args.tests = True

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command._determine_targets()

        self.assertTrue(result.success)
        self.assertEqual(result.value, ["unit-tests", "perf-tests"])

    def test_determine_targets_specific(self):
        """Test determining specific targets."""
        self.mock_args.targets = ["triangle", "vulkan_rhi"]

        command = BuildCommand(self.mock_args, self.source_dir, self.build_dir)
        result = command._determine_targets()

        self.assertTrue(result.success)
        self.assertEqual(result.value, ["triangle", "vulkan_rhi"])


# Phase 3 successfully replaced old handle functions with command pattern architecture


if __name__ == "__main__":
    unittest.main()
