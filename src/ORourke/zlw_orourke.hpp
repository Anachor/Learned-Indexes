#pragma once

#include "orourke.hpp"

// zlw's FastPLAWorkspace, without its experiment driver.
#define main zlw_main
#include "../../third_party/ZLW/zlw_orourke.cpp"
#undef main

// O'Rourke using zlw's FastPLAWorkspace.
template <typename T = int64_t>
class ZlwORourke : public ORourke<T> {
    FastPLAWorkspace w_;

public:
    using ORourke<T>::ORourke;

protected:
    void start(T delta) override { w_.reset((Real)delta); }

    bool try_add(T x, T y) override {
        if (w_.add({(Real)x, (Real)y})) return true;
        w_.reset((Real)this->delta());
        return false;
    }

    // The max-slope feasible line, through rect()[1] and rect()[3].
    Line line() const override {
        const Point *r = w_.rect();
        return {r[1].x, r[1].y, (r[3].y - r[1].y) / (r[3].x - r[1].x)};
    }
};
