# Core Library Design

## Summary

The core library serves as the foundational static library for the Mobile 3D Gaussian Splatting engine, providing essential utilities across critical modules: Math, Logging, Timer, Virtual File System, Platform Abstraction, Memory Management, and Custom STL Containers.

This dependency-free library targets high-performance 3D graphics applications with support for 500K Gaussians at 60fps on desktop and 100K at 30fps on mobile platforms. The architecture emphasizes zero-overhead abstractions, clean separation of concerns, production-ready thread safety, and sophisticated memory management through polymorphic memory resources (PMR). The design provides conditional compilation support for seamless switching between custom high-performance implementations and standard library fallbacks.

## Custom STL Container System

The container system implements a sophisticated dual-mode architecture supporting both high-performance custom implementations and standard library fallbacks through conditional compilation. All custom containers leverage C++17 polymorphic memory resources for flexible, efficient memory management with zero-cost abstractions.

The implementation provides production-grade replacements for critical STL containers including `vector`, `array`, `unordered_map`, `queue`, and `string`. Each container is designed with renderer-specific optimizations, featuring PMR integration with mimalloc backend, trivial destructor optimizations for POD types, and move-first semantics for efficient data flow. The `unordered_map` utilizes ankerl::unordered_dense for superior performance with custom hash functions optimized for graphics primitives.

The conditional compilation system (`MSPLAT_USE_STD_CONTAINERS`) enables seamless switching between implementations, facilitating debugging with standard containers while deploying with optimized versions. Factory functions provide ergonomic construction with appropriate memory resources, supporting frame arena allocation for transient data and custom memory resource injection for specialized allocation strategies.

## Advanced Memory Management

The memory subsystem implements a multi-tiered allocation strategy centered around a high-performance frame arena system for transient allocations. The `FrameArenaResource` provides a multi-buffered bump allocator with configurable arena count matching GPU in-flight frames, ensuring zero allocation overhead for per-frame data.

Each arena operates as a monotonic buffer with constant-time allocation, automatic reset between frames, and thread-safe concurrent allocation within frames. The system integrates seamlessly with PMR infrastructure, providing standard `memory_resource` interface while utilizing mimalloc as the upstream allocator for persistent allocations. The architecture supports 64MB default arena sizes with triple-buffering for typical rendering pipelines.

The PMR integration enables sophisticated allocation strategies including custom allocators per subsystem, efficient memory pooling for frequent allocations, and NUMA-aware allocation on supported platforms. The upstream allocator abstraction allows runtime selection of memory backends, facilitating integration with platform-specific allocators while maintaining consistent interfaces across the codebase.

## Math Module

The math module provides a comprehensive 3D graphics foundation through a sophisticated header-only wrapper around GLM. The modular design exposes fundamental types through clean using declarations while maintaining GLM as an implementation detail, ensuring future flexibility in backend selection.

Core types include vectors (`vec2`, `vec3`, `vec4`), matrices (`mat2`, `mat3`, `mat4`), and quaternions (`quat`) with full operator overloading and SIMD optimizations. Specialized geometry classes provide `AABB` for spatial queries and culling, `Sphere` for bounding volume hierarchies, `Frustum` for view culling operations, and `Affine` for optimized transform chains. The `Color` utility handles linear/sRGB conversions with hardware acceleration where available.

The implementation leverages template metaprogramming for zero runtime overhead, aggressive inlining through forced inline attributes, compile-time constant evaluation for known values, and expression templates for compound operations. This design provides a complete mathematical foundation for 3D graphics while maintaining optimal performance through careful abstraction design.

## Logging System

The logging system implements a production-grade solution built on spdlog with sophisticated thread-safe singleton initialization using lambda-based RAII patterns. The architecture provides seamless integration with spdlog's high-performance backend while maintaining a clean, renderer-specific interface.

Template functions with perfect forwarding enable zero-overhead variadic logging with compile-time format string validation through fmt library integration. The system supports configurable severity levels from verbose to critical with automatic build-type detection, setting appropriate defaults for debug and release configurations. Environment variable configuration through `LOG_LEVEL` enables runtime adjustment without recompilation.

Advanced features include thread-safe sink management for runtime output redirection, automatic backtrace generation maintaining the last 32 messages for error analysis, mutex-protected operations ensuring thread safety in multi-threaded rendering, and automatic flushing on error conditions for crash resilience. The pattern-based formatting provides timestamp, severity, and message content with ANSI color support for enhanced terminal readability.

## Virtual File System

The virtual file system implements a sophisticated abstraction layer through carefully designed interfaces enabling future extensibility to multiple backends. The architecture centers on two core abstractions: immutable data blocks and sequential access streams, providing a clean separation between data ownership and access patterns.

The `IBlob` interface represents immutable memory blocks with concrete `Blob` implementation owning data through PMR-aware vectors. This design enables efficient memory mapping for large files, zero-copy data sharing between subsystems, and lazy loading strategies for on-demand resource access. The `IStream` interface provides sequential access patterns with implementations including `FileStream` for OS file access with buffered I/O, `MemoryStream` for in-memory data access, and `SubStream` for windowed access to larger streams.

The `IFileSystem` interface defines backend operations supporting multiple storage backends including native filesystem through standard library, archive systems for packaged resources, network backends for streaming assets, and embedded resources for shader inclusion. Factory functions and RAII patterns ensure exception safety while maintaining high performance through minimal abstraction overhead.

## Platform Abstraction

The platform module provides essential system-level operations with consistent interfaces across Windows, macOS, Linux, iOS, and Android platforms. The implementation emphasizes zero-overhead abstractions while handling platform-specific optimizations transparently.

Memory operations include `aligned_malloc` and `aligned_free` for SIMD-aligned allocations critical for mathematics operations and GPU buffer requirements. System query functions provide `get_page_size()` for virtual memory optimization, `get_cache_line_size()` for data structure padding to prevent false sharing, and `get_backtrace()` for debugging support in development builds. Each function includes platform-specific implementations with appropriate fallbacks for unsupported features.

The abstraction layer facilitates future platform additions while maintaining binary compatibility, enabling platform-specific optimizations without interface changes. Compile-time platform detection ensures zero runtime overhead for platform checks while maintaining clean separation from higher-level subsystems.

## Timer System

The timer system delivers high-precision timing capabilities through a clean interface built on `std::chrono` with careful attention to clock selection for maximum precision. The implementation provides both one-shot timing and continuous frame rate monitoring with minimal overhead.

The `Timer` class encapsulates high-resolution clock operations with start, stop, and reset functionality, elapsed time queries in multiple units from nanoseconds to seconds, and pause/resume support for complex profiling scenarios. State management ensures accurate measurements across multiple timing sessions while maintaining thread-local storage for multi-threaded profiling.

The `FPSCounter` class provides smooth frame rate calculation through exponential moving averages with configurable update intervals for display stability. The design maps directly to `std::chrono` operations avoiding abstraction penalties while providing convenient APIs for common graphics programming patterns including frame time analysis and performance bottleneck identification.

## Architecture and Performance

The core library architecture emphasizes production-grade quality through several key design principles that permeate every module. Zero-overhead abstractions ensure optimal performance while maintaining clean interfaces, achieved through aggressive inlining, template metaprogramming, and compile-time optimization.

The PMR-based memory strategy provides unprecedented flexibility in allocation patterns, supporting per-frame transient allocations with zero overhead, custom allocators for subsystem-specific optimization, and integration with platform-specific memory systems. The frame arena system eliminates allocation overhead for temporary data while maintaining simple, safe interfaces through RAII patterns.

Conditional compilation support enables seamless transitions between development and production configurations, facilitating debugging with standard containers while deploying optimized implementations. The architecture supports incremental adoption, allowing subsystems to migrate to custom containers gradually while maintaining compatibility.

Thread safety is carefully considered throughout, with immutable interfaces where possible, explicit synchronization for mutable shared state, and lock-free algorithms for critical paths. The singleton patterns employ sophisticated initialization techniques ensuring thread-safe construction without runtime overhead.

## Implementation Strategy

The implementation strategy balances performance requirements with development velocity through careful architectural choices. The conditional compilation system (`MSPLAT_USE_STD_CONTAINERS`) enables rapid prototyping with familiar standard library interfaces while maintaining the ability to deploy highly optimized custom implementations.

Integration of best-in-class third-party libraries including spdlog for logging, mimalloc for memory allocation, ankerl::unordered_dense for hash maps, and GLM for mathematics provides production-tested foundations while maintaining consistent interfaces through wrapper layers. This approach leverages community expertise while retaining architectural flexibility.

The header-only design for performance-critical components eliminates function call overhead while potentially increasing compilation times. This tradeoff is carefully managed through modular header organization, forward declarations where possible, and precompiled header support for frequently used components.

Future extensibility is preserved through interface-based design for major subsystems, allowing backend replacements without API changes. The architecture anticipates mobile platform requirements including limited memory bandwidth, thermal constraints, and battery optimization while maintaining desktop performance targets.