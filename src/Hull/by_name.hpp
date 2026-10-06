#pragma once

// The hulls by name, for command lines: scan, vector, tree (TreeHull), and
// tree<B> for BasicTreeHull<B>, B = 1, 4, 8, 16, 32, 64 or 128, to measure
// which leaf size TreeHull should have.

#include <string>
#include <type_traits>

#include "scan_hull.hpp"
#include "tree_hull.hpp"
#include "vector_hull.hpp"

namespace gpla {

inline constexpr const char *HULL_NAMES = "scan, vector, tree, tree1, tree4, tree8, tree16, tree32, tree64 or tree128";

// Calls f(std::type_identity<H>{}) with the hull called name; false if none is.
template <class F>
bool with_hull(const std::string &name, F &&f) {
    if (name == "scan") f(std::type_identity<ScanHull>{});
    else if (name == "vector") f(std::type_identity<VectorHull>{});
    else if (name == "tree") f(std::type_identity<TreeHull>{});
    else if (name == "tree1") f(std::type_identity<BasicTreeHull<1>>{});
    else if (name == "tree4") f(std::type_identity<BasicTreeHull<4>>{});
    else if (name == "tree8") f(std::type_identity<BasicTreeHull<8>>{});
    else if (name == "tree16") f(std::type_identity<BasicTreeHull<16>>{});
    else if (name == "tree32") f(std::type_identity<BasicTreeHull<32>>{});
    else if (name == "tree64") f(std::type_identity<BasicTreeHull<64>>{});
    else if (name == "tree128") f(std::type_identity<BasicTreeHull<128>>{});
    else return false;
    return true;
}

}  // namespace gpla
