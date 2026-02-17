#pragma once

#include <cstdint>

namespace msplat::engine
{

// Number of frames in flight
// With N > 1, the CPU can record frame N while the GPU renders frame N-1.
// This value is shared between the app (for per-frame resource arrays) and
// the sort backend (for async compute pipeline depth).
static constexpr uint32_t k_maxFramesInFlight = 2;

}        // namespace msplat::engine
