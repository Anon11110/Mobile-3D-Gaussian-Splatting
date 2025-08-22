#!/usr/bin/env python3
"""
Build process orchestration for the build system.
Manages the complete build process with single-responsibility methods.
"""

import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

from ..terminal import term
from .constants import BuildConstants, Messages
from .types import GeneratorInfo, Result, BuildError
from .output_strategies import OutputManager, OutputStrategyFactory


# ============================================================================
# BUILD CONFIGURATION
# ============================================================================


@dataclass
class BuildConfiguration:
    """Configuration for build operations."""

    source_dir: Path
    build_dir: Path
    build_type: str
    generator_info: GeneratorInfo
    parallel_jobs: Optional[int] = None
    clean_first: bool = False
    verbose: bool = True
    output_level: str = "warnings"

    @classmethod
    def create(
        cls,
        source_dir: Path,
        build_dir: Path,
        parallel_jobs: Optional[int] = None,
        clean_first: bool = False,
        verbose: bool = True,
        output_level: str = "warnings",
    ) -> "BuildConfiguration":
        """Create build configuration with auto-detected settings."""
        generator_info = GeneratorInfo.detect(build_dir)
        build_type = cls._detect_build_type(build_dir, generator_info)

        return cls(
            source_dir=source_dir,
            build_dir=build_dir,
            build_type=build_type,
            generator_info=generator_info,
            parallel_jobs=parallel_jobs,
            clean_first=clean_first,
            verbose=verbose,
            output_level=output_level,
        )

    @staticmethod
    def _detect_build_type(build_dir: Path, generator_info: GeneratorInfo) -> str:
        """Detect build type from CMake cache and directory structure."""
        build_type = BuildConfiguration._get_build_type_from_cache(build_dir)

        # For multi-config generators, check directory existence
        if generator_info.is_multi_config and build_type == "Release":
            if (build_dir / BuildConstants.BIN_SUBDIR_DEBUG).exists():
                build_type = "Debug"

        return build_type

    @staticmethod
    def _get_build_type_from_cache(build_dir: Path) -> str:
        """Extract build type from CMakeCache.txt."""
        try:
            cache_file = build_dir / BuildConstants.CMAKE_CACHE_FILE
            if cache_file.exists():
                content = cache_file.read_text()
                for line in content.splitlines():
                    if (
                        line.startswith(BuildConstants.CMAKE_BUILD_TYPE_CACHE_PREFIX)
                        and "=" in line
                    ):
                        build_type = line.split("=", 1)[1]
                        if build_type:
                            return build_type
        except Exception:
            pass

        return "Release"  # Default fallback


# ============================================================================
# BUILD RESULT
# ============================================================================


@dataclass
class BuildResult:
    """Result of a build operation."""

    target: str
    success: bool
    build_time: float
    error_message: Optional[str] = None
    stdout: Optional[str] = None
    stderr: Optional[str] = None

    @classmethod
    def success_result(cls, target: str, build_time: float) -> "BuildResult":
        """Create successful build result."""
        return cls(target=target, success=True, build_time=build_time)

    @classmethod
    def failure_result(
        cls,
        target: str,
        build_time: float,
        error_message: str,
        stdout: str = None,
        stderr: str = None,
    ) -> "BuildResult":
        """Create failed build result."""
        return cls(
            target=target,
            success=False,
            build_time=build_time,
            error_message=error_message,
            stdout=stdout,
            stderr=stderr,
        )


@dataclass
class BuildSummary:
    """Summary of multiple build operations."""

    results: List[BuildResult]
    total_time: float
    success_count: int
    failure_count: int

    @classmethod
    def from_results(
        cls, results: List[BuildResult], total_time: float
    ) -> "BuildSummary":
        """Create summary from build results."""
        success_count = sum(1 for r in results if r.success)
        failure_count = len(results) - success_count

        return cls(
            results=results,
            total_time=total_time,
            success_count=success_count,
            failure_count=failure_count,
        )

    @property
    def overall_success(self) -> bool:
        """Check if all builds were successful."""
        return self.failure_count == 0


# ============================================================================
# BUILD ORCHESTRATOR
# ============================================================================


class BuildOrchestrator:
    """Orchestrates the complete build process with single-responsibility methods."""

    def __init__(self, config: BuildConfiguration):
        """Initialize build orchestrator with configuration."""
        self.config = config
        self.output_manager = self._create_output_manager()

    def build_targets(self, targets: List[str]) -> Result[BuildSummary]:
        """Build multiple targets and return comprehensive results."""
        if not self._is_project_configured():
            return Result.fail(
                BuildError(
                    Messages.PROJECT_NOT_CONFIGURED.format(path=self.config.build_dir)
                )
            )

        # Clean if requested
        if self.config.clean_first:
            clean_result = self._clean_build_directory()
            if not clean_result.success:
                return Result.fail(clean_result.error)

        # Display build information
        self._display_build_info(targets)

        # Build each target
        results = []
        overall_start_time = time.time()

        for target in targets:
            result = self.build_single_target(target)
            results.append(result)

            # Stop on first failure for non-verbose mode
            if not result.success and not self.config.verbose:
                break

        total_time = time.time() - overall_start_time
        summary = BuildSummary.from_results(results, total_time)

        # Display summary
        self._display_build_summary(summary)

        if summary.overall_success:
            return Result.ok(summary)
        else:
            return Result.fail(
                BuildError(f"Build failed: {summary.failure_count} targets failed")
            )

    def build_single_target(self, target: str) -> BuildResult:
        """Build a single target with comprehensive error handling."""
        self._display_target_start(target)

        cmd = self._build_cmake_command(target)

        if self.config.verbose:
            self._display_command(cmd)

        start_time = time.time()

        try:
            if self.config.verbose:
                # Stream output in real-time
                result = subprocess.run(cmd, cwd=self.config.build_dir, check=True)
                build_time = time.time() - start_time

                self._display_target_success(target, build_time)
                return BuildResult.success_result(target, build_time)
            else:
                # Capture output for filtering
                result = subprocess.run(
                    cmd,
                    cwd=self.config.build_dir,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                build_time = time.time() - start_time

                if result.returncode == 0:
                    self._display_target_success(target, build_time)
                    return BuildResult.success_result(target, build_time)
                else:
                    error_msg = f"Failed to build target '{target}'"
                    self._display_target_failure(
                        target, result.stdout or "", result.stderr or ""
                    )
                    return BuildResult.failure_result(
                        target, build_time, error_msg, result.stdout, result.stderr
                    )

        except subprocess.CalledProcessError as e:
            build_time = time.time() - start_time
            error_msg = f"Failed to build target '{target}': {e}"
            term.error(error_msg)
            return BuildResult.failure_result(target, build_time, error_msg)

    def _clean_build_directory(self) -> Result[None]:
        """Handle clean operations if requested."""
        term.section("Cleaning build directory")

        try:
            result = subprocess.run(
                [
                    BuildConstants.CMAKE_EXECUTABLE,
                    "--build",
                    str(self.config.build_dir),
                    "--target",
                    "clean",
                ],
                cwd=self.config.build_dir,
                check=True,
            )
            return Result.ok(None)
        except subprocess.CalledProcessError as e:
            term.warn(f"Clean failed, continuing anyway: {e}")
            return Result.ok(None)  # Continue even if clean fails

    def _build_cmake_command(self, target: str) -> List[str]:
        """Build CMake command for target."""
        cmd = [BuildConstants.CMAKE_EXECUTABLE, "--build", str(self.config.build_dir)]

        if self.config.generator_info.is_multi_config:
            cmd.extend(["--config", self.config.build_type])

        cmd.extend(["--target", target])

        if self.config.parallel_jobs:
            cmd.extend(["--parallel", str(self.config.parallel_jobs)])
        else:
            cmd.append("--parallel")

        return cmd

    def _is_project_configured(self) -> bool:
        """Check if project is already configured."""
        return (self.config.build_dir / BuildConstants.CMAKE_CACHE_FILE).exists()

    def _create_output_manager(self) -> OutputManager:
        """Create output manager based on configuration."""
        if self.config.verbose:
            strategy = OutputStrategyFactory.create_strategy("verbose")
        else:
            strategy = OutputStrategyFactory.create_strategy(self.config.output_level)
        return OutputManager(strategy)

    def _display_build_info(self, targets: List[str]) -> None:
        """Display build information."""
        term.section(f"Building targets: {', '.join(targets)}")

        if self.config.verbose:
            term.kv("Build directory", str(self.config.build_dir))
            term.kv("Build type", self.config.build_type)
            term.kv("Generator", self.config.generator_info.name or "Unknown")

    def _display_target_start(self, target: str) -> None:
        """Display target build start message."""
        if self.config.verbose:
            term.info(f"Building target: {target}")
        else:
            term.info(Messages.BUILDING_TARGET.format(target=target))

    def _display_command(self, cmd: List[str]) -> None:
        """Display command being executed."""
        if self.config.verbose:
            term.kv("Command", " ".join(cmd))

    def _display_target_success(self, target: str, build_time: float) -> None:
        """Display successful target build message."""
        if self.config.verbose:
            term.success(f"Built {target} in {build_time:.1f}s")
        else:
            term.success(
                Messages.BUILT_TARGET_SUCCESS.format(target=target, time=build_time)
            )

    def _display_target_failure(self, target: str, stdout: str, stderr: str) -> None:
        """Display failed target build message with filtered output."""
        term.error(f"Failed to build target '{target}'!")

        # Filter output using output manager
        filtered_stdout, filtered_stderr = self.output_manager.filter_output(
            stdout, stderr
        )

        if filtered_stderr:
            print(filtered_stderr)
        if filtered_stdout:
            print(filtered_stdout)

    def _display_build_summary(self, summary: BuildSummary) -> None:
        """Display build summary."""
        if summary.overall_success:
            term.success(
                f"Successfully built {summary.success_count}/{len(summary.results)} targets"
            )
        else:
            term.error(
                f"Build failed: {summary.failure_count} targets failed, {summary.success_count} succeeded"
            )

        if self.config.verbose and summary.total_time > 0:
            term.kv("Total build time", f"{summary.total_time:.1f}s")
