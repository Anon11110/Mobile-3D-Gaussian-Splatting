# Mobile 3D Gaussian Splatting (3DGS) — Implementation Plan

## **Goals**

* Real-time 3DGS renderer with **hybrid transparency (HT)** and **ray-based evaluation** compatible with HTGS assets/exports. Implement four blending modes: HYBRID\_BLEND, ALPHA\_BLEND\_FIRST\_K, ALPHA\_BLEND\_GLOBAL\_ORDERING, OIT\_BLEND.

* Desktop-first bring-up (Windows/Linux Vulkan; macOS Metal), then port to Android/iOS once transfer gates are met.

* Minimal, modular codebase split into **core / scene / rhi / render / app** to keep build units small, enable clear pass boundaries, and make backend parity straightforward.

---

## Module Boundaries (project-scoped functionality)

### core/

* Math types (float2/3/4, mat3/4), transforms, frustum utilities.

* Logging (scoped timers, per-pass GPU/CPU timings), small VFS (read-only), JSON config, CLI args.

* Ring allocators (CPU), telemetry counters.

### scene/

* Loaders: HTGS **PLY** (fields: μ, orientation/Σ, scales, opacity, SH), COLMAP camera intrinsics/extrinsics, example config presets (Mip-NeRF360, Tanks & Temples).

* Checkpoint→GPU packers (interleaved or AoS-of-SoA): μ FP32; quat 16-bit; log-scales FP16; opacity FP16; SH ≤deg3 FP16/10-bit.

* Camera path management (keyframes, time-based interpolation), dataset utilities (image scale, near/far defaults).

* Note: downstream renderer **must** perform **ray-based evaluation**; EWA will artifact.

### rhi/

* Backends: Vulkan (desktop/mobile) and Metal (macOS/iOS).

* Device/swapchain (or CAMetalLayer), pipeline cache, descriptor/argument buffer managers, transient upload heaps, barrier helpers, timestamp queries.

### render/

* Pass graph and reusable passes:

  * BoundsAndBinningPass (compute): tight perspective bounds \+ 8×8 tile binning.

  * SplatRasterPass (VS generates NDC quads; FS/compute does ray-based max-contribution evaluation).

  * HTCompositePass: K-core \+ tail; toggles for the three alternative modes.

  * PostFXPass: tonemap (and optional TAA later).

  * DebugVizPass: overdraw/tile histograms, *K* heatmap, tail energy, near-plane diagnostics.

### app/

* Desktop windowing (GLFW), camera controller (orbit/FPS), ImGui HUD (mode/K/threshold toggles, perf counters), CLI scenario runner (headless capture).

* Config loader (JSON), per-scene presets (resolution, near/far, thresholds).

---

## Phases, Steps, Objectives, and Acceptance Criteria

*(Person-hours; two engineers can parallelize.)*

### Phase 0 — Repo plumbing & toolchain (24–40 h)

**Steps**

1. CMake presets (Win/Linux Vulkan; macOS Metal); shader pipeline GLSL→SPIR-V and SPIR-V→MSL (SPIRV-Cross).

    **Objective:** one-command build on all desktops.

    **Acceptance:** CI builds pass; shaders compile to SPIR-V/MSL.

2. Minimal RHI bring-up (device, swapchain/CAMetalLayer, pipeline cache, descriptor/arg-buffers, transient upload).

    **Objective:** basic graphics triangle.

    **Acceptance:** **colored triangle** renders on desktop Vulkan and Metal.

3. ImGui HUD skeleton \+ frame timings.

    **Objective:** visible FPS and GPU/CPU ms.

    **Acceptance:** on-screen counters update each frame.

---

### Phase 1 — Data I/O & packing (20–36 h)

**Steps**

1. HTGS PLY importer and packing to GPU buffers.

    **Objective:** load μ/Σ(or orientation+scales)/opacity/SH into SSBOs/arg-buffers with FP16 where safe.

    **Acceptance:** buffer upload verified; counts and memory match logs.

2. Camera & dataset presets (Mip-NeRF360; Tanks & Temples), near/far defaults.

    **Objective:** ready-to-load scene configs.

    **Acceptance:** scene config parses; near≈0.2, far≈1000 default available.

3. Ray-based requirement noted in scene docs.

    **Objective:** prevent misuse with EWA.

    **Acceptance:** loader warns if non-ray path selected; docs cite requirement.

---

### Phase 2 — Perspective-tight bounds & tile binning (compute) (48–72 h)

**Steps**

1. Tight perspective screen-space bounds (closed-form, inversion-free).

    **Objective:** numerically stable bounds for 3D Gaussians under perspective.

    **Acceptance:** bounds overlay matches authors’ spec on test scenes.

2. 8×8 tile binning with prefix-sum compaction; per-tile cap & stats.

    **Objective:** reduce overdraw and improve locality.

    **Acceptance:** tile histogram shows significant reduction vs naive Nσ bounds.

3. Near-plane diagnostics.

    **Objective:** identify culled/edge cases near z=near; prepare extension.

    **Acceptance:** debug view highlights near-plane culls noted by authors.

---

### Phase 3 — Splat evaluation kernel (56–84 h)

**Steps**

1. VS quad synthesis in NDC from μ \+ orientation \+ scales; pass affine info.

    **Objective:** conservative four-vertex coverage per splat.

    **Acceptance:** quads align with bounds; no gaps on stress rotations.

2. Fragment/compute **ray-based evaluation at point of maximum contribution** (inversion-free).

    **Objective:** stable per-pixel evaluation (plane/Plücker style).

    **Acceptance:** numerical checks pass; visual match against CUDA reference captures.

3. Early-out with α threshold.

    **Objective:** prune tiny contributions.

    **Acceptance:** perf improves with identical images above ε.

---

### Phase 4 — Hybrid transparency \+ alternative modes (68–100 h)

**Steps**

1. **HYBRID\_BLEND**: per-pixel **K-buffer** (depth/α/color) \+ **tail** (∑α·c, ∏(1−α)).

    **Objective:** sort & blend first K, accumulate tail; composite.

    **Acceptance:** matches reference within ε on diagnostic sequences; no popping.

2. **ALPHA\_BLEND\_FIRST\_K**, **ALPHA\_BLEND\_GLOBAL\_ORDERING**, **OIT\_BLEND** toggles.

    **Objective:** ablation/validation suite.

    **Acceptance:** mode toggles behave per definitions on test scenes.

3. Two-threshold policy (core 0.05, tail 0.0039) as defaults, runtime-tunable.

    **Objective:** core capacity protection and quality tail.

    **Acceptance:** visual quality improves vs single global threshold.

---

### Phase 5 — Desktop performance pass (44–72 h)

**Steps**

1. Tile size & wave/subgroup tuning; FP16 math; LDS/threadgroup K-buffer layout.

    **Objective:** stable frametimes at target res.

    **Acceptance:** 1080p desktop achieves budget with K∈[8,16].

2. Debug visualizations: overdraw, tail energy, *K* heatmap; frame pacing.

    **Objective:** perf regressions easy to spot.

    **Acceptance:** overlays toggle at runtime; capture scripts record metrics.

---

### **Phase 6 — Quality & robustness (56–96 h)**

**Steps**

1. Anti-aliasing for ray-based eval (3D min-size filtering / multi-sample strategy).

    **Objective:** mitigate single-ray flicker at zoom-out (EWA is not applicable).

    **Acceptance:** reduced temporal shimmer on author-noted cases.

2. Near-plane extension.

    **Objective:** handle partially visible ellipsoids crossing near plane.

    **Acceptance:** fewer false culls; document new max-contribution rules.

3. Background handling.

    **Objective:** support black/color/skybox; preserve black fast-path equivalence.

    **Acceptance:** black path equals “no-op” blend; other backgrounds validated.

---

### Phase 7 — Metal backend parity (macOS) (40–64 h)

**Steps**

1. Heaps & argument buffers; barrier mapping; timestamp queries.

    **Objective:** feature parity with Vulkan path.

    **Acceptance:** identical images and timings within tolerance.

2. Threadgroup K-buffer path and SPIR-V→MSL reflection glue.

    **Objective:** efficient per-tile storage.

    **Acceptance:** GPU ms comparable; no validation errors.

---

### Phase 8 — Innovation tracks (choose 2–3 first) (80–140 h total)

**Candidates & Objectives**

* **Dynamic per-pixel K** via tail transmittance: raise K where (1−T\_tail) high, lower elsewhere. **Acceptance:** quality within ε at lower cost.

* **Quantization & streaming** (chunked LOD; dequant in-shader). **Acceptance:** memory ↓ with minimal ΔSSIM/LPIPS.

* **Moment-aided tail** (2–3 moments when ∑α\_tail high). **Acceptance:** reduces tail over/under-darkening on opaque clusters.

* **Foveated K** (desktop proof): ↑K in fovea/motion tiles. **Acceptance:** energy ↓ with stable quality.

---

### Phase 9 — Testing & repro (32–56 h)

**Steps**

1. Scene packs and camera paths (garden / playground, etc.).

    **Objective:** deterministic captures.

    **Acceptance:** scripts output FPS/GPU-ms/overdraw/*K*/tail energy \+ SSIM/LPIPS.

2. Configs seeded from authors’ examples (image scaling, COLMAP layout).

    **Objective:** quick “known-good” runs.

    **Acceptance:** runs replicate baseline structure.

---

### Phase 10 — Transfer to mobile (Android Vulkan / iOS Metal) (60–100 h)

**Gates (must be met on desktop before port)**

* HYBRID correctness and no popping across motion; mode parity verified.

* Tight bounds & binning efficiency demonstrated; near-plane diagnostics in place.

* Threshold defaults and K∈\[8,16\] stable in multiple scenes.

**Steps**

1. Android Vulkan (FIFO swapchain, dynamic rendering, timeline semaphores, AHardwareBuffer staging; AGI/Perfetto markers).

    **Acceptance:** 1080p target fps with governor (resolution/K/thresholds).

2. iOS Metal (CAMetalLayer, heaps/arg-buffers, threadgroup K-buffer; Xcode GPU counters).

    **Acceptance:** stable thermals; same visual output.

**Total estimate:** **480–786 h** (feature depth and optimization choices drive variance).

---

## Renderer Pipeline (passes)

1. **BoundsAndBinningPass** (compute): perspective-tight bounds (inversion-free) and compact 8×8 tile lists; overlays for bounds/overdraw.

2. **SplatRasterPass** (VS/FS or compute-raster hybrid): VS emits four-vertex conservative quads; FS/compute performs ray-based max-contribution evaluation; early α rejection.

3. **HTCompositePass**: **HYBRID** default (K-core \+ tail) and toggles for **FIRST\_K**, **GLOBAL\_ORDER**, **OIT**.

4. **PostFXPass**: tonemap; optional TAA later.

5. **DebugVizPass**: overdraw/tile histograms, tail energy, K heatmap, near-plane diagnostics.

---

## Configuration Defaults

* blend\_mode: HYBRID\_BLEND (default), ALPHA\_BLEND\_FIRST\_K, ALPHA\_BLEND\_GLOBAL\_ORDERING, OIT\_BLEND.

* K: 8–16 (runtime-tunable).

* alpha\_threshold\_core: **0.05**, alpha\_threshold\_tail: **0.0039** (runtime-tunable).

* near: **0.2**, far: **1000.0** as baselines; per-scene overrides.

* background: black/color/skybox; black fast-path equivalence preserved.

---

## Folder Organization (proposed)

```
/ (repo root)
  CMakeLists.txt
  cmake/                 \# toolchain and shader rules
    compileshaders.cmake
  configs/               \# scene presets (mipnerf360, tanks\_temples)
  docs/
    renderer\_design.md   \# pass specs, data layout, thresholds, FAQs
  core/
    include/core/        \# math.h, frustum.h, log.h, json.h, vfs.h, timers.h
    src/                 \# tiny VFS, logging, JSON, timers, allocators
  scene/
    include/scene/       \# ply\_loader.h, colmap\_io.h, camera.h, packer.h
    src/                 \# ply\_loader.cpp, pack\_checkpoint.cpp, camera.cpp
    tools/               \# convert\_ply\_inria.py, pack\_checkpoint.py
  rhi/
    include/rhi/         \# rhi\_device.h, rhi\_memory.h, rhi\_pipeline.h, rhi\_cmd.h
    src/common/          \# pipeline\_cache.cpp, barriers.cpp, descriptors.cpp
    src/vk/              \# device\_vk.cpp, swapchain\_vk.cpp, cmd\_vk.cpp, ...
    src/metal/           \# device\_mtl.mm, layer\_mtl.mm, cmd\_mtl.mm, ...
  render/
    BoundsAndBinningPass/
      bounds\_cs.glsl     \# SPIR-V; MSL generated at build
      bounds\_pass.h/.cpp
    SplatRasterPass/
      splat\_vs.glsl
      splat\_fs.glsl      \# alt: compute kernel path
      splat\_pass.h/.cpp
    HTCompositePass/
      ht\_core\_tail\_cs.glsl
      ht\_pass.h/.cpp
    PostFXPass/
      tonemap\_ps.glsl
      postfx\_pass.h/.cpp
    DebugVizPass/
      debug\_ps.glsl
      debug\_pass.h/.cpp
  app/
    main\_desktop.cpp     \# GLFW/Cocoa \+ ImGui; scenario runner
    cli\_runner.cpp       \# headless capture
  shaders/               \# shared headers, packing, SH utils
    gs\_common.glsl
  thirdparty/            \# (minimal; header-only or fetched via CMake)
  tests/
    test\_scenes.cpp      \# loads configs; asserts basic invariants
    test\_math.cpp        \# bounds math sanity tests
```
---

## References

* **HTGS project (GitHub)** — official code, README, blending modes, export guidance: https://github.com/nerficg-project/HTGS

* **HTGS project page** — overview, method context: https://fhahlbohm.github.io/htgs/

* **Paper (Computer Graphics Forum 2025 / arXiv)** — hybrid transparency in 3DGS, OIT context: https://arxiv.org/abs/2410.08129

* **Modular renderer exemplars** — structure ideas only:

  * Donut: https://github.com/NVIDIA-RTX/Donut/tree/main

  * NVRHI: https://github.com/NVIDIA-RTX/NVRHI
