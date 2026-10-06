#pragma once

// The GPLA (src/GPLA) as a GRE index (third_party/GRE), named gpla -
// the tree hull with 32-point leaves, delta = 16 - with any of these after it:
//
//   -delta8          delta = 8 (any multiple of 0.5)
//   -vector, -scan   another hull (-tree is the default)
//   -leaf16          the tree hull's leaves of 16 points (1, 4, 8, 16, 32, 64, 128)
//
// in any order: gpla-delta8, gpla-vector, gpla-delta8-vector, gpla-delta64-leaf128.
//
// GRE builds as C++17 (ALEX and the STX B+-tree break under C++20) and the
// GPLA needs C++20, so the index lives in gpla_index.cpp, compiled on its
// own; GRE sees only this factory. Single-threaded and keys only: get leaves
// the payload alone (GRE never reads it back), scan returns payload 0, and
// update and remove are not supported.

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "../../third_party/GRE/src/competitor/indexInterface.h"

// The index called name, or nullptr if no gpla is called that.
indexInterface<uint64_t, uint64_t> *make_gpla_index(const std::string &name);
