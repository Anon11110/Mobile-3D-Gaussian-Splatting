#pragma once

#include <unordered_map>

#ifdef MSPLAT_USE_SYSTEM_STL
namespace msplat {
namespace core {
namespace containers {

template<typename K, typename V>
using unordered_map = std::unordered_map<K, V>;

}
}
}
#else
#include "memory.h"
#include "unordered_dense.h"

namespace msplat {
namespace core {
namespace containers {

template<typename K, typename V>
using unordered_map = ankerl::unordered_dense::map<K, V, 
                        std::hash<K>, std::equal_to<K>,
                        StlAllocatorAdapter<std::pair<const K, V>>>;

}
}
}
#endif