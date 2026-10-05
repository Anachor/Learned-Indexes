#pragma once

// The learned PMA (src/LPMA) as a GRE index (third_party/GRE), named lpma -
// the tree hull with 32-point leaves, delta = 32 - with any of these after it:
//
//   -delta8          delta = 8 (any multiple of 0.5)
//   -vector, -scan   another hull (-tree is the default)
//   -leaf16          the tree hull's leaves of 16 points (1, 4, 8, 16, 32, 64, 128)
//
// in any order: lpma-delta8, lpma-vector, lpma-delta8-vector, lpma-delta64-leaf128.
//
// GRE builds as C++17 (ALEX and the STX B+-tree break under C++20) and the
// learned PMA needs C++20, so the index lives in lpma_index.cpp, compiled on its
// own; GRE sees only this factory. Single-threaded and keys only: get leaves
// the payload alone (GRE never reads it back), scan returns payload 0, and
// update and remove are not supported.

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "../../third_party/GRE/src/competitor/indexInterface.h"

// The index called name, or nullptr if no lpma is called that.
indexInterface<uint64_t, uint64_t> *make_lpma_index(const std::string &name);
