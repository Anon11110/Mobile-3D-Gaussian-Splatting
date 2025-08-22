#!/usr/bin/env python3
"""
Output filtering strategies for the build system.
"""

from abc import ABC, abstractmethod
from typing import List, Tuple
from .constants import BuildConstants


# ============================================================================
# OUTPUT STRATEGY INTERFACE
# ============================================================================


class OutputStrategy(ABC):
    """Abstract base class for output filtering strategies."""

    @abstractmethod
    def should_show_line(self, line: str) -> bool:
        """Determine if a line should be displayed based on the strategy."""
        pass

    @abstractmethod
    def get_max_lines(self) -> int:
        """Get maximum lines to display for this strategy."""
        pass

    @abstractmethod
    def show_stderr(self) -> bool:
        """Determine if stderr should be shown for this strategy."""
        pass

    def get_name(self) -> str:
        """Get the name of this strategy."""
        return self.__class__.__name__


# ============================================================================
# CONCRETE OUTPUT STRATEGIES
# ============================================================================


class ErrorOnlyStrategy(OutputStrategy):
    """Show only error-level messages."""

    ERROR_PATTERNS = ["error", "failed", "fatal", "failure", "cannot", "abort"]

    def should_show_line(self, line: str) -> bool:
        """Show line if it contains error patterns."""
        line_lower = line.lower()
        return any(pattern in line_lower for pattern in self.ERROR_PATTERNS)

    def get_max_lines(self) -> int:
        """Return maximum lines for error-only output."""
        return BuildConstants.MAX_OUTPUT_LINES

    def show_stderr(self) -> bool:
        """Always show stderr for errors."""
        return True


class WarningLevelStrategy(OutputStrategy):
    """Show warnings and errors."""

    ERROR_PATTERNS = ["error", "failed", "fatal", "failure", "cannot", "abort"]
    WARNING_PATTERNS = ["warning", "warn", "deprecated", "notice", "caution"]

    def should_show_line(self, line: str) -> bool:
        """Show line if it contains error or warning patterns."""
        line_lower = line.lower()
        all_patterns = self.ERROR_PATTERNS + self.WARNING_PATTERNS
        return any(pattern in line_lower for pattern in all_patterns)

    def get_max_lines(self) -> int:
        """Return maximum lines for warning-level output."""
        return BuildConstants.MAX_OUTPUT_LINES

    def show_stderr(self) -> bool:
        """Always show stderr for warnings and errors."""
        return True


class InfoLevelStrategy(OutputStrategy):
    """Show informational messages, warnings, and errors."""

    ERROR_PATTERNS = ["error", "failed", "fatal", "failure", "cannot", "abort"]
    WARNING_PATTERNS = ["warning", "warn", "deprecated", "notice", "caution"]
    INFO_PATTERNS = ["note", "info", "hint", "suggestion"]

    def should_show_line(self, line: str) -> bool:
        """Show line if it contains error, warning, or info patterns."""
        line_lower = line.lower()
        all_patterns = self.ERROR_PATTERNS + self.WARNING_PATTERNS + self.INFO_PATTERNS
        return any(pattern in line_lower for pattern in all_patterns)

    def get_max_lines(self) -> int:
        """Return maximum lines for info-level output."""
        return BuildConstants.MAX_OUTPUT_LINES

    def show_stderr(self) -> bool:
        """Always show stderr for all levels."""
        return True


class VerboseStrategy(OutputStrategy):
    """Show all output without filtering."""

    def should_show_line(self, line: str) -> bool:
        """Show all lines in verbose mode."""
        return True

    def get_max_lines(self) -> int:
        """Return unlimited lines for verbose output."""
        return float("inf")  # No limit in verbose mode

    def show_stderr(self) -> bool:
        """Always show stderr in verbose mode."""
        return True


class CustomStrategy(OutputStrategy):
    """Custom strategy with user-defined patterns."""

    def __init__(
        self, patterns: List[str], max_lines: int = None, show_stderr: bool = True
    ):
        """Initialize custom strategy with user-defined patterns."""
        self.patterns = [pattern.lower() for pattern in patterns]
        self.max_lines = max_lines or BuildConstants.MAX_OUTPUT_LINES
        self._show_stderr = show_stderr

    def should_show_line(self, line: str) -> bool:
        """Show line if it matches custom patterns."""
        line_lower = line.lower()
        return any(pattern in line_lower for pattern in self.patterns)

    def get_max_lines(self) -> int:
        """Return custom maximum lines."""
        return self.max_lines

    def show_stderr(self) -> bool:
        """Return custom stderr setting."""
        return self._show_stderr


# ============================================================================
# OUTPUT MANAGER
# ============================================================================


class OutputManager:
    """Manages output filtering using strategies."""

    def __init__(self, strategy: OutputStrategy):
        """Initialize output manager with a strategy."""
        self.strategy = strategy

    def set_strategy(self, strategy: OutputStrategy) -> None:
        """Change the output filtering strategy."""
        self.strategy = strategy

    def filter_output(self, stdout: str, stderr: str) -> Tuple[str, str]:
        """Filter output based on current strategy.

        Args:
            stdout: Standard output from build process
            stderr: Standard error from build process

        Returns:
            Tuple of (filtered_stdout, filtered_stderr)
        """
        filtered_stderr = stderr if self.strategy.show_stderr() else ""

        if not stdout:
            return "", filtered_stderr

        # Handle verbose strategy (no filtering)
        if isinstance(self.strategy, VerboseStrategy):
            return stdout, filtered_stderr

        # Apply strategy filtering
        stdout_lines = stdout.splitlines()
        matching_lines = []

        for line in stdout_lines:
            if self.strategy.should_show_line(line):
                matching_lines.append(line)

        # Apply line limit
        max_lines = self.strategy.get_max_lines()
        if max_lines != float("inf") and len(matching_lines) > max_lines:
            matching_lines = matching_lines[: int(max_lines)]
            matching_lines.append(
                f"... (output truncated after {int(max_lines)} lines)"
            )

        filtered_stdout = "\n".join(matching_lines) if matching_lines else ""
        return filtered_stdout, filtered_stderr

    def get_strategy_name(self) -> str:
        """Get the name of the current strategy."""
        return self.strategy.get_name()


# ============================================================================
# STRATEGY FACTORY
# ============================================================================


class OutputStrategyFactory:
    """Factory for creating output strategies."""

    @staticmethod
    def create_strategy(level: str) -> OutputStrategy:
        """Create strategy based on level string.

        Args:
            level: Output level ("errors", "warnings", "info", "all", "verbose")

        Returns:
            OutputStrategy instance

        Raises:
            ValueError: If level is not recognized
        """
        level_lower = level.lower()

        if level_lower == "errors":
            return ErrorOnlyStrategy()
        elif level_lower == "warnings":
            return WarningLevelStrategy()
        elif level_lower == "info":
            return InfoLevelStrategy()
        elif level_lower in ["all", "verbose"]:
            return VerboseStrategy()
        else:
            raise ValueError(f"Unknown output level: {level}")

    @staticmethod
    def create_custom_strategy(
        patterns: List[str], max_lines: int = None, show_stderr: bool = True
    ) -> CustomStrategy:
        """Create custom strategy with specified patterns."""
        return CustomStrategy(patterns, max_lines, show_stderr)


# ============================================================================
# MIGRATION COMPATIBILITY
# ============================================================================


def create_output_manager_from_config(output_level: str = "warnings") -> OutputManager:
    """Create OutputManager with strategy based on configuration level.

    This function provides backward compatibility with the old OutputConfig approach.
    """
    strategy = OutputStrategyFactory.create_strategy(output_level)
    return OutputManager(strategy)
