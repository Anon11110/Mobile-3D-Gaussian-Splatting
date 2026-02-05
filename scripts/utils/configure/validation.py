#!/usr/bin/env python3
"""
Validation framework for the build system.
Provides composable validation rules and orchestration capabilities.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional
from argparse import Namespace

from .types import Result, ValidationError
from .constants import BuildConstants, Messages


# ============================================================================
# VALIDATION CONTEXT
# ============================================================================


@dataclass
class ValidationContext:
    """Context for validation operations containing all necessary data."""

    source_dir: Path
    build_dir: Optional[Path] = None
    args: Optional[Namespace] = None


# ============================================================================
# VALIDATION RULE INTERFACE
# ============================================================================


class ValidationRule(ABC):
    """Abstract base class for validation rules."""

    @abstractmethod
    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate the context and return result with detailed error information."""
        pass


# ============================================================================
# CONCRETE VALIDATION RULES
# ============================================================================


class SourceDirectoryRule(ValidationRule):
    """Validate that source directory exists and is accessible."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate source directory exists."""
        if not context.source_dir.exists():
            return Result.fail(
                ValidationError(
                    Messages.SOURCE_DIR_NOT_FOUND.format(path=context.source_dir)
                )
            )

        if not context.source_dir.is_dir():
            return Result.fail(
                ValidationError(f"Source path is not a directory: {context.source_dir}")
            )

        return Result.ok(None)


class CMakeFileRule(ValidationRule):
    """Validate that CMakeLists.txt exists in source directory."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate CMakeLists.txt exists."""
        cmake_file = context.source_dir / "CMakeLists.txt"

        if not cmake_file.exists():
            return Result.fail(
                ValidationError(
                    Messages.CMAKE_FILE_NOT_FOUND.format(path=context.source_dir)
                )
            )

        if not cmake_file.is_file():
            return Result.fail(
                ValidationError(f"CMakeLists.txt is not a file: {cmake_file}")
            )

        return Result.ok(None)


class BuildTypeRule(ValidationRule):
    """Validate build type parameter."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate build type is valid."""
        if not context.args or not hasattr(context.args, "build_type"):
            return Result.ok(None)  # No build type to validate

        from .types import BuildType

        valid_types = [BuildType.DEBUG.value, BuildType.RELEASE.value, BuildType.RELWITHDEBINFO.value]
        if context.args.build_type not in valid_types:
            return Result.fail(
                ValidationError(
                    Messages.INVALID_BUILD_TYPE.format(
                        type=context.args.build_type, valid_types=", ".join(valid_types)
                    )
                )
            )

        return Result.ok(None)


class TargetSelectionRule(ValidationRule):
    """Validate target selection options are not conflicting."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate target selection options."""
        if not context.args:
            return Result.ok(None)  # No args to validate

        # Check for conflicting target selection options
        target_options = [
            bool(getattr(context.args, "targets", None)),
            getattr(context.args, "tests", False),
            getattr(context.args, "list_targets", False),
        ]

        if sum(target_options) > 1:
            return Result.fail(ValidationError(Messages.CONFLICTING_TARGET_OPTIONS))

        # Skip further validation if listing targets
        if getattr(context.args, "list_targets", False):
            return Result.ok(None)

        if not any(target_options):
            return Result.fail(ValidationError(Messages.NO_TARGETS_SPECIFIED))

        return Result.ok(None)


class RunOptionRule(ValidationRule):
    """Validate run option is used with exactly one target."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate run option usage."""
        if not context.args or not getattr(context.args, "run", False):
            return Result.ok(None)  # No run option to validate

        targets = getattr(context.args, "targets", None)
        if not targets or len(targets) != 1:
            return Result.fail(ValidationError(Messages.RUN_SINGLE_TARGET_ONLY))

        return Result.ok(None)


class BuildDirectoryRule(ValidationRule):
    """Validate build directory requirements."""

    def validate(self, context: ValidationContext) -> Result[None]:
        """Validate build directory."""
        if not context.build_dir:
            return Result.ok(None)  # No build directory to validate

        # Check that some ancestor directory exists (build dir may be nested
        # e.g. build/macos-arm64-xcode-Debug/ where build/ doesn't exist yet).
        # mkdir(parents=True) will create intermediates, so we only need to
        # verify the path is rooted under a real directory.
        check = context.build_dir
        while not check.exists():
            parent = check.parent
            if parent == check:
                return Result.fail(
                    ValidationError(
                        f"No existing ancestor directory for build path: {context.build_dir}"
                    )
                )
            check = parent
        if not check.is_dir():
            return Result.fail(
                ValidationError(
                    f"Ancestor path is not a directory: {check}"
                )
            )

        # If build directory exists, check it's actually a directory
        if context.build_dir.exists() and not context.build_dir.is_dir():
            return Result.fail(
                ValidationError(
                    f"Build path exists but is not a directory: {context.build_dir}"
                )
            )

        return Result.ok(None)


# ============================================================================
# VALIDATION ORCHESTRATOR
# ============================================================================


class Validator:
    """Manages validation rules and execution."""

    def __init__(self, rules: List[ValidationRule]):
        """Initialize validator with a list of rules."""
        self.rules = rules

    def validate_all(self, context: ValidationContext) -> Result[None]:
        """Run all validation rules and return first failure or success."""
        for rule in self.rules:
            result = rule.validate(context)
            if not result.success:
                return result

        return Result.ok(None)

    def add_rule(self, rule: ValidationRule) -> None:
        """Add a validation rule to the validator."""
        self.rules.append(rule)

    def remove_rule(self, rule_type: type) -> bool:
        """Remove a validation rule by type. Returns True if removed."""
        for i, rule in enumerate(self.rules):
            if isinstance(rule, rule_type):
                del self.rules[i]
                return True
        return False


# ============================================================================
# PREDEFINED VALIDATOR CONFIGURATIONS
# ============================================================================


def create_configure_validator() -> Validator:
    """Create validator for configure command."""
    return Validator(
        [
            SourceDirectoryRule(),
            CMakeFileRule(),
            BuildTypeRule(),
            BuildDirectoryRule(),
        ]
    )


def create_build_validator() -> Validator:
    """Create validator for build command."""
    return Validator(
        [
            SourceDirectoryRule(),
            TargetSelectionRule(),
            RunOptionRule(),
            BuildDirectoryRule(),
        ]
    )


def create_minimal_validator() -> Validator:
    """Create minimal validator with only essential rules."""
    return Validator(
        [
            SourceDirectoryRule(),
        ]
    )


def create_comprehensive_validator() -> Validator:
    """Create comprehensive validator with all available rules."""
    return Validator(
        [
            SourceDirectoryRule(),
            CMakeFileRule(),
            BuildTypeRule(),
            TargetSelectionRule(),
            RunOptionRule(),
            BuildDirectoryRule(),
        ]
    )


# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================


def validate_configure_context(
    source_dir: Path, build_dir: Path, args: Namespace
) -> Result[None]:
    """Convenience function to validate configure command context."""
    context = ValidationContext(source_dir=source_dir, build_dir=build_dir, args=args)
    validator = create_configure_validator()
    return validator.validate_all(context)


def validate_build_context(
    source_dir: Path, build_dir: Path, args: Namespace
) -> Result[None]:
    """Convenience function to validate build command context."""
    context = ValidationContext(source_dir=source_dir, build_dir=build_dir, args=args)
    validator = create_build_validator()
    return validator.validate_all(context)
