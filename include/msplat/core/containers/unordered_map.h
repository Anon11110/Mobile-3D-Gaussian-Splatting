#pragma once

#include <unordered_map>

#ifdef MSPLAT_USE_SYSTEM_STL
namespace msplat::container
{

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V>;

}
#else
#	include "hash.h"
#	include "memory.h"
#	include "third-party/unordered_dense/unordered_dense.h"

namespace msplat::container
{

template <typename K, typename V, typename Hash = msplat::container::hash<K>>
using unordered_map = ankerl::unordered_dense::map<K, V, Hash>;

}
#endif