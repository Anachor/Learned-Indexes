#pragma once

#include <stdexcept>
#include <vector>

#include "orourke.hpp"

// Brute-force O'Rourke, for testing. O(m^2) per point for a segment of m
// points. Exact integer arithmetic.
template <typename T = int64_t>
class BruteORourke : public ORourke<T> {
    using I = __int128;

    struct ExactLine {
        I x1, y1, dx, dy;  // Line through (x1, y1) with slope dy / dx, dx > 0.
    };

    I delta_ = 0;
    std::vector<T> xs_, ys_;  // points of the current segment
    ExactLine line_{};        // a line fitting every point of the current segment

    // Is line l within +-delta of point (x, y)?
    bool fits(const ExactLine &l, T x, T y) const {
        I err = (l.y1 - I(y)) * l.dx + l.dy * (I(x) - l.x1);
        return -delta_ * l.dx <= err && err <= delta_ * l.dx;
    }

    void push(T x, T y) {
        xs_.push_back(x);
        ys_.push_back(y);
    }

public:
    using ORourke<T>::ORourke;

protected:
    void start(T delta) override {
        delta_ = delta;
        xs_.clear();
        ys_.clear();
    }

    bool try_add(T x, T y) override {
        if (!xs_.empty() && x <= xs_.back()) throw std::logic_error("x must be strictly increasing");

        size_t m = xs_.size();
        if (m == 0 || (m >= 2 && fits(line_, x, y))) {
            push(x, y);
            return true;
        }

        // Try every line through an endpoint of an earlier point i and an
        // endpoint of (x, y), where an endpoint is y +- delta.
        for (size_t i = 0; i < m; ++i) {
            for (I si : {-delta_, delta_}) {
                for (I sp : {-delta_, delta_}) {
                    I x1 = xs_[i], y1 = I(ys_[i]) + si;
                    ExactLine cand{x1, y1, I(x) - x1, I(y) + sp - y1};
                    bool ok = fits(cand, x, y);
                    for (size_t k = 0; k < m && ok; ++k) ok = fits(cand, xs_[k], ys_[k]);
                    if (ok) {
                        line_ = cand;
                        push(x, y);
                        return true;
                    }
                }
            }
        }

        // line_ still describes the segment that just ended.
        xs_.clear();
        ys_.clear();
        return false;
    }

    Line line() const override {
        return {(long double)line_.x1, (long double)line_.y1, (long double)line_.dy / (long double)line_.dx};
    }
};
