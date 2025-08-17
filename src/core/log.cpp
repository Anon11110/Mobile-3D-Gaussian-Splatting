#include "core/log.h"

// This file is intentionally minimal.
// The spdlog-based logger implementation is header-only
// to leverage template functions and singleton pattern.
// All logging functionality is implemented in log.h.

namespace msplat::log
{

// This file exists to maintain CMake build structure
// and provide a place for future non-template implementations
// if needed.

}        // namespace msplat::log