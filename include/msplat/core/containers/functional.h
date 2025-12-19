#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace msplat::container
{

// Using std:: versions for both MSPLAT_USE_STD_CONTAINERS and non-MSPLAT_USE_STD_CONTAINERS cases
#ifdef MSPLAT_USE_STD_CONTAINERS

// Function objects
using std::equal_to;
using std::greater;
using std::greater_equal;
using std::less;
using std::less_equal;
using std::not_equal_to;

// Function wrapper
using std::function;

// Invoke utility
using std::invoke;

#else

// Still using std:: versions for simplicity
using std::equal_to;
using std::function;
using std::greater;
using std::greater_equal;
using std::invoke;
using std::less;
using std::less_equal;
using std::not_equal_to;

#endif        // MSPLAT_USE_STD_CONTAINERS

}        // namespace msplat::container