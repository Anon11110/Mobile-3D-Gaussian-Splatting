# Module Architecture Documentation

This document describes the module dependencies and architecture for the build configuration system.

## Module Overview

The build configuration system is organized into several specialized modules that work together to provide a robust cross-platform build solution.

## High-Level Architecture

### Simplified Main Flow

```mermaid
graph LR
    %% Main flow
    configure["🎯 configure.py<br/>(Entry Point)"]
    cmake_core["⚙️ cmake_core.py<br/>(CMake Operations)"]
    orchestrator["🎼 orchestrator.py<br/>(Build Control)"]

    %% Supporting modules
    validation["✓ validation.py"]
    platforms["🖥️ platforms/"]

    %% Flow
    configure ==>|"commands"| cmake_core
    cmake_core ==>|"builds"| orchestrator
    configure -.->|"validates"| validation
    configure -.->|"configures"| platforms

    style configure fill:#ff9999,stroke:#333,stroke-width:3px
    style cmake_core fill:#9999ff,stroke:#333,stroke-width:2px
    style orchestrator fill:#9999ff,stroke:#333,stroke-width:2px
```

### Layered Architecture

```mermaid
graph TB
    subgraph "Layer 1: Entry Point"
        configure["🎯 configure.py<br/>(Main Entry)"]
    end

    subgraph "Layer 2: Core Operations"
        cmake_core["cmake_core.py<br/>(CMake Ops)"]
        orchestrator["orchestrator.py<br/>(Build Control)"]
        validation["validation.py<br/>(Rules)"]
    end

    subgraph "Layer 3: Platform Abstraction"
        platforms["platforms/"]
        subgraph "Platform Implementations"
            windows["windows.py"]
            macos["macos.py"]
            linux["linux.py"]
        end
    end

    subgraph "Layer 4: Support Modules"
        types["types.py<br/>(Core Types)"]
        output_strategies["output_strategies.py<br/>(Output Filter)"]
    end

    subgraph "Layer 5: Foundation"
        constants["constants.py"]
        term["terminal/term.py"]
        progress["terminal/progress.py"]
    end

    %% Primary dependencies (thick arrows)
    configure ==>|primary| cmake_core
    cmake_core ==>|delegates| orchestrator

    %% Secondary dependencies (normal arrows)
    configure -->|uses| validation
    configure -->|uses| platforms
    orchestrator -->|filters| output_strategies

    %% Foundation dependencies (dotted lines)
    cmake_core -.->|formats| term
    orchestrator -.->|shows| progress
    validation -.->|uses| types
    types -.->|defines| constants

    %% Platform relationships
    platforms ==>|implements| windows
    platforms ==>|implements| macos
    platforms ==>|implements| linux

    style configure fill:#ff9999,stroke:#333,stroke-width:4px
    style cmake_core fill:#9999ff,stroke:#333,stroke-width:3px
    style orchestrator fill:#9999ff,stroke:#333,stroke-width:3px
    style validation fill:#99ccff,stroke:#333,stroke-width:2px
    style types fill:#99ff99,stroke:#333,stroke-width:2px
    style constants fill:#ccffcc,stroke:#333,stroke-width:2px
    style term fill:#ccffcc,stroke:#333,stroke-width:2px
    style progress fill:#ccffcc,stroke:#333,stroke-width:2px
```

## Detailed Module Dependencies

### Complete Dependency Graph

```mermaid
graph LR
    %% Use left-to-right for better readability
    subgraph "Foundation Layer"
        constants["constants.py"]
        term["term.py"]
        progress["progress.py"]
    end

    subgraph "Type System"
        types["types.py"]
    end

    subgraph "Support Layer"
        output["output_strategies.py"]
        validation["validation.py"]
    end

    subgraph "Platform Layer"
        platformBase["platformBase.py"]
        platforms["platforms/__init__.py"]
        windows["windows.py"]
        macos["macos.py"]
        linux["linux.py"]
    end

    subgraph "Core Operations"
        orchestrator["orchestrator.py"]
        cmake_core["cmake_core.py"]
    end

    subgraph "Entry"
        configure["configure.py"]
    end

    %% Foundation dependencies
    types --> term
    types --> constants

    %% Support layer
    output --> constants
    validation --> types
    validation --> constants

    %% Platform layer
    platformBase --> types
    windows --> platformBase
    windows --> term
    macos --> platformBase
    macos --> term
    linux --> platformBase
    linux --> term
    platforms --> platformBase
    platforms --> windows
    platforms --> macos
    platforms --> linux

    %% Core operations
    orchestrator --> term
    orchestrator --> progress
    orchestrator --> constants
    orchestrator --> types
    orchestrator --> output

    cmake_core --> term
    cmake_core --> progress
    cmake_core --> constants
    cmake_core --> types
    cmake_core --> output
    cmake_core --> orchestrator

    %% Entry point
    configure --> validation
    configure --> cmake_core
    configure --> types
    configure --> constants
    configure --> platforms
    configure --> term

    %% Styling
    style configure fill:#ff9999
    style cmake_core fill:#9999ff
    style orchestrator fill:#9999ff
    style types fill:#99ff99
    style constants fill:#ccffcc
```

## Dependency Legend

- **Thick arrows (==>)**: Primary control flow
- **Normal arrows (-->)**: Direct dependencies
- **Dotted arrows (-.->)**: Utility/formatting dependencies
- **Colors**:
  - 🔴 Red: Entry point
  - 🔵 Blue: Core operations
  - 🟢 Green: Foundation/support modules
  - ⬜ Light: Platform-specific code

## Module Purposes

### Core Modules

- **configure.py**: Main entry point and command-line interface. Orchestrates all modules and implements the command pattern for configure/build operations.

- **constants.py**: Centralized build constants, messages, and configuration values. No dependencies on other project modules.

- **types.py**: Core type definitions including error classes, Result<T> type, enums, and build-related data structures. Depends on terminal/term.py and constants.py.

### Terminal Modules

- **terminal/term.py**: Terminal formatting, colors, and output utilities. Base module with no dependencies.

- **terminal/progress.py**: Progress indicators with spinning animations and elapsed time display for long-running operations. No dependencies on other project modules.

### Build Operation Modules

- **cmake_core.py**: CMake-specific operations and utilities including configuration, target discovery, and executable running. This is the main CMake interface module.

- **orchestrator.py**: Build process orchestration with single-responsibility methods. Manages the complete build lifecycle with comprehensive error handling.

- **validation.py**: Validation rules framework for checking build configurations, paths, and command arguments.

- **output_strategies.py**: Output filtering strategies for managing build output verbosity levels (errors, warnings, info, verbose).

### Platform Modules

- **platforms/**: Platform-specific configurations for Windows, macOS, and Linux.
  - **platformBase.py**: Abstract base class defining the platform configuration interface
  - **windows.py**: Windows-specific build configuration and Visual Studio detection
  - **macos.py**: macOS-specific configuration with Vulkan/MoltenVK setup
  - **linux.py**: Linux-specific configuration with package detection

## Dependency Flow

1. **Base modules** (no dependencies):
   - terminal/term.py
   - terminal/progress.py
   - constants.py

2. **Core types** (minimal dependencies):
   - types.py → terminal/term.py, constants.py

3. **Platform layer**:
   - platformBase.py → types.py
   - Platform implementations → platformBase.py, terminal/term.py

4. **Validation layer**:
   - validation.py → types.py, constants.py

5. **Output management**:
   - output_strategies.py → constants.py

6. **Build orchestration**:
   - orchestrator.py → types.py, constants.py, output_strategies.py, terminal modules

7. **CMake operations**:
   - cmake_core.py → All above modules plus orchestrator.py

8. **Main interface**:
   - configure.py → Orchestrates all modules

## Design Principles

1. **Single Responsibility**: Each module has a clear, focused purpose
2. **Dependency Inversion**: Higher-level modules depend on abstractions (types.py)
3. **Separation of Concerns**: UI, business logic, and platform code are separated
4. **Progressive Enhancement**: Base functionality with optional progress indicators
5. **Platform Abstraction**: Platform differences hidden behind common interface

## Module Communication

- **Result<T> Pattern**: Used for error handling throughout the system
- **Strategy Pattern**: Output filtering uses strategy pattern for different verbosity levels
- **Command Pattern**: Main configure.py uses command pattern for different operations
- **Abstract Factory**: Platform configuration uses factory pattern for platform selection