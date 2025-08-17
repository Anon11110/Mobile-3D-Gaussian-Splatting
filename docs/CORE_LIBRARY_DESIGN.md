# Core Library Design

## Summary

The core library serves as the foundational static library for the Mobile 3D Gaussian Splatting engine, providing essential utilities across four focused modules: Math, Logging, Timer, and Virtual File System. This dependency-free library targets high-performance 3D graphics applications with support for 500K Gaussians at 60fps on desktop and 100K at 30fps on mobile platforms. The architecture emphasizes zero-overhead abstractions, clean separation of concerns, and production-ready thread safety while maintaining minimal external dependencies beyond GLM and the standard library.

## Math Module

The math module provides a comprehensive 3D graphics foundation through a sophisticated header-only wrapper around GLM. It exposes fundamental types like vec2, vec3, vec4, mat3, mat4, and quat through clean using declarations, while adding specialized geometry classes including AABB, Sphere, Frustum, Affine transforms, and Color utilities. The implementation leverages template metaprogramming to achieve zero runtime overhead, enabling aggressive compiler inlining and compile-time optimization. This design isolates GLM as an implementation detail while providing a complete mathematical foundation for 3D graphics, geometric operations, and color management. The header-only approach ensures optimal performance but may impact compilation times in large projects.

## Logging System

The logging system implements a production-grade solution built on spdlog with a thread-safe singleton pattern. It provides template functions with perfect forwarding for zero-overhead variadic logging, supporting severity levels from verbose to critical with automatic build-type detection. The system features environment variable configuration through LOG_LEVEL, runtime sink management for file and console output, automatic stack trace generation on errors, and mutex-protected thread safety. Template functions like log_info() and log_error() offer type-safe formatting with fmt library support, while traditional LOG_* macros maintain backward compatibility. The architecture enables runtime configuration of output destinations and formatting while ensuring thread-safe operation in multi-threaded rendering environments.

## Timer System

The timer system delivers high-precision timing capabilities through a clean interface built on std::chrono. The Timer class provides start, stop, and reset functionality with elapsed time measurements in multiple units from nanoseconds to seconds, while the FPSCounter class offers smooth frame rate calculation with configurable update intervals. Both components use composition patterns and state management to ensure accurate measurements with minimal overhead. The design directly maps to std::chrono operations to avoid abstraction penalties while providing convenient APIs for performance monitoring and frame rate analysis in real-time applications.

## Virtual File System

The virtual file system implements a pragmatic file abstraction through free functions designed for future extensibility. Current functionality includes file reading, existence checking, size queries, and path resolution using std::filesystem internally. The stateless interface simplifies usage and testing while preparing for future enhancements like archive backends and embedded resource support. Exception-based error handling provides descriptive messages for debugging, though the synchronous nature limits real-time streaming applications. The design prioritizes simplicity and extensibility over advanced features, making it suitable for asset loading during initialization phases.

## Architecture and Performance

The library employs sophisticated design patterns including facade pattern for math operations, template metaprogramming for logging, and composition over inheritance throughout. Zero-overhead abstractions are achieved through header-only math implementations, template-based operations, and direct standard library mapping. Thread safety is guaranteed through static local variable initialization for singletons and spdlog's multi-threaded sink support. The modular design prevents circular dependencies and enables parallel development, while minimal memory allocation patterns support mobile platform constraints. Performance characteristics include stack-allocated math operations, negligible logging overhead for disabled levels, and efficient file I/O suitable for startup scenarios.

## Future Direction

Immediate priorities focus on build optimization through precompiled headers to manage compilation times and comprehensive unit testing for production readiness. Medium-term enhancements include asynchronous VFS APIs for real-time streaming, archive backend support for packaged assets, and platform-specific SIMD optimizations for math operations. Long-term goals encompass mobile platform optimization, web platform compatibility through Emscripten, and GPU compute shader integration points. The logging system's spdlog foundation already provides file backends and structured logging capabilities, while the extensible architecture supports custom allocators and advanced memory management for performance-critical applications.