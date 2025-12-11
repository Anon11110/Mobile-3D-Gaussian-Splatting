# Hybrid Splat Renderer (CPU + GPU Sorting)

Design for a single example target under `examples/hybrid-splat-renderer` that merges `naive-splat-cpu` and `gpu-sorting-renderer` into one switchable app while keeping legacy examples intact for reference.

## 1. Objectives and Scope
- One binary that can swap between CPU and GPU sorting without restarting.
- Preserve both baselines: CPU correctness path and GPU radix sorter (prescan and integrated scan) with verification and benchmarking toggles.
- Reuse the existing rendering path (splat raster shaders, descriptor layout, instanced quad draw) for comparable visuals.
- Provide controls and metrics to compare quality, correctness, and performance between backends.
- Location: `examples/hybrid-splat-renderer/`; add `CMakeLists.txt`, `main.cpp`, `hybrid_splat_renderer_app.h/.cpp`, and this doc.

### Non-goals
- No engine-level refactors beyond hosting both backends (keep `engine::Scene`, `GpuSplatSorter`, shader I/O, RHI APIs).
- Do not modify or delete `naive-splat-cpu` or `gpu-sorting-renderer`; they remain as references.
- No new shader work; reuse `splat_raster.vert/frag` and the current descriptor layout.

## 2. Architecture Overview
- Load splat data via `engine::SplatLoader` (default `assets/flowers_1.ply`; allow overrides like `train_7000.ply` or custom path).
- Apply the GPU renderer coordinate conversion (negate Z, adjust quaternion components) for consistency.
- Camera defaults: perspective ~45°, near 0.1, far 1000, position z=5 looking at origin; WASD + mouse look; ESC exits.
- Guard rendering when minimized (width/height == 0).
- Keep scene GPU buffers resident; swap only the sorted index buffer binding when changing backends.

## 3. Sorting Backends
- Interface: `ISplatSortBackend` with init, per-frame update (view changes), optional verification hooks, and `GetSortedIndices()` handle.
- CPU backend:
  - Wrap `engine::Scene` CPU sort path (`UpdateView` + `ConsumeAndUploadSortedIndices`).
  - Resort only on view changes; keep uploads asynchronous where possible.
  - Expose last sort and upload durations for UI/telemetry.
- GPU backend:
  - Wrap `engine::GpuSplatSorter`; runtime switching between prescan and integrated scan.
  - Toggle sorting on/off; verification (simple order check and comprehensive variant); benchmark mode (skip present/vsync).
  - Rebind descriptor when the sorter emits a new sorted index buffer.
- Backend switching:
  - Runtime UI/keyboard switch; rebind descriptor set binding 5 to the active backend buffer.
  - Preserve scene/camera state; avoid reloads.
  - If a backend fails to init (missing features), fall back and surface a UI/log warning.

## 4. Rendering Pipeline and Resources
- Reuse descriptor layout: UBO + positions + covariances3D + colors + SH rest + sorted indices.
- Share quad index buffer creation (triangle strip 0,1,2,3).
- UBO mirrors existing `FrameUBO`: view/projection with Vulkan Y flip, camera position, viewport, focal, splat scale, alpha cull threshold, max splat radius, filter toggle, basis viewport, inverse focal adjustment.
- Command flow matches GPU renderer: barrier to render target, render, optional ImGui overlay, barrier to present; keep fence/semaphore pattern per frame/image.
- Reuse GPU renderer ImGui setup (descriptor pool, dynamic rendering pipeline info).

## 5. UI and Controls
- ImGui overlay on by default with toggle.
- Panels:
  - Backend selection (CPU vs GPU) and sorting enabled state.
  - GPU method combo (prescan vs integrated scan) and verification buttons (simple/comprehensive).
  - Sorting enable/disable (renders source order when off).
  - Benchmark toggle (skip present/vsync) and FPS history graph (reuse history array).
  - Scene stats: total splats, frame count, last sort/upload durations.
  - Assets: quick buttons for common assets, text input for custom path, button to generate random test data for verification.
  - Help: keyboard shortcuts.
- Shortcuts: SPACE toggle sorting; M switch GPU method; V verify; B benchmark; H toggle UI; ESC exit.

## 6. Performance and Telemetry
- Keep `timer::FPSCounter` and history graph; log FPS periodically (match GPU cadence).
- Track sort time, upload time, and buffer churn per backend.
- Benchmark mode disables present (and optionally UI) to reduce overhead; stats still update.

## 7. Verification Strategy
- Support GPU sorter verification (simple order check, comprehensive position-based check).
- Optional cross-backend comparison: run CPU sort once and compare ordering to GPU output for regressions.
- Include random test scene generator (10M splats, edge cases near camera) from GPU renderer to stress verification.
- Report verification in logs and UI with clear pass/fail.

## 8. Error Handling and Cleanup
- Handle swapchain out-of-date/minimized gracefully (skip frame).
- Unmap/destroy descriptor sets and mapped UBOs in order; wait for device idle on shutdown.
- When switching backends, release backend-specific resources but keep shared scene buffers alive.

## 9. Build and Integration Notes
- Add `add_subdirectory(hybrid-splat-renderer)` to `examples/CMakeLists.txt` when implementing.
- Link the same dependencies as the GPU renderer (RHI, msplat engine, ImGui/Vulkan backend).
- Keep asset paths relative to repo root for existing run scripts.

## 10. Execution Plan
- Phase 1: Skeleton
  - Add CMake target, main entry, app shell with shared descriptor/pipeline creation and quad IB.
  - Load default asset, camera setup, minimize guard, UBO write, render path without backend switching yet.
- Phase 2: Backend abstraction
  - Implement `ISplatSortBackend`; wire CPU backend (Scene update/upload) and GPU backend (GpuSplatSorter with method toggle).
  - Hook descriptor binding 5 to backend output; ensure seamless switching without scene reloads.
- Phase 3: UI and controls
  - Add ImGui overlay, backend selector, sorting toggle, GPU method combo, verification controls, asset selectors, stats, and shortcuts.
  - Add benchmark toggle (skip present) and FPS history graph.
- Phase 4: Verification and polish
  - Integrate GPU verification modes, optional cross-backend compare, and random test scene generator.
  - Harden error handling, cleanup paths, and logging of sort/upload timings.

## 11. Testing Checklist
- Run CPU and GPU backends on the same asset; verify visual parity and stable switching at runtime.
- Toggle sorting off/on and confirm rendering stability and descriptor rebind.
- Exercise GPU method switching, verification modes, and benchmark toggle.
- Generate random test data and confirm verification reporting.
- Resize/minimize window; confirm render loop skips appropriately without deadlocks.
