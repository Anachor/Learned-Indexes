#pragma once

// Exact geometry for the learned PMA (learned_pma.hpp).
//
// A key x stored at PMA slot s is the point (x, 2s). A line fits a set of points
// when it is within k of every one, k = 2*delta, as in the experiments: delta
// covers the half-integers and everything stays integral.
//
// Every decision is made in __int128. Keys may be any int64_t; slots must stay
// below MAX_CAPACITY and k at most MAX_K, which keeps every product below 2^127.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lpma {

using Key = int64_t;
using i128 = __int128;

// y = 2 * slot < 2^58 and k <= 2^58: y differences, +- 2k, stay below 2^60, and
// key differences below 2^64, so every product is below 2^124.
inline constexpr int64_t MAX_K = int64_t(1) << 58;
inline constexpr size_t MAX_CAPACITY = size_t(1) << 57;

struct Pt {
    Key x;
    int64_t y;  // 2 * slot
    bool operator==(const Pt &) const = default;
};

// (b - a) x (c - a): positive when a, b, c turn left.
inline i128 cross(Pt a, Pt b, Pt c) {
    return (i128(b.x) - a.x) * (i128(c.y) - a.y) - (i128(b.y) - a.y) * (i128(c.x) - a.x);
}

// slope(a, b) < slope(c, d), for a.x < b.x and c.x < d.x.
inline bool slope_less(Pt a, Pt b, Pt c, Pt d) {
    return (i128(b.y) - a.y) * (i128(d.x) - c.x) < (i128(d.y) - c.y) * (i128(b.x) - a.x);
}

// Rounding divisions, for b > 0.
inline i128 floor_div(i128 a, i128 b) { return a / b - (a % b != 0 && a < 0); }
inline i128 ceil_div(i128 a, i128 b) { return a / b + (a % b != 0 && a > 0); }

// The line through anchor with slope dy / dx, dx > 0.
struct ExactLine {
    Pt anchor{0, 0};
    i128 dy = 0, dx = 1;
    bool operator==(const ExactLine &) const = default;

    // dx * line(x), exactly.
    i128 scaled(Key x) const { return i128(anchor.y) * dx + dy * (i128(x) - anchor.x); }

    // |line(p.x) - p.y| <= k.
    bool within(Pt p, int64_t k) const {
        i128 error = scaled(p.x) - i128(p.y) * dx;
        return -i128(k) * dx <= error && error <= i128(k) * dx;
    }

    // The slots s with |line(x) - 2s| <= k: [first, second], empty when first > second.
    std::pair<i128, i128> slot_window(Key x, int64_t k) const {
        i128 v = scaled(x), d = 2 * dx;
        return {ceil_div(v - i128(k) * dx, d), floor_div(v + i128(k) * dx, d)};
    }

    // The same line as o, maybe through another anchor.
    bool same(const ExactLine &o) const {
        return dy * o.dx == o.dy * dx && o.scaled(anchor.x) == i128(anchor.y) * o.dx;
    }
};

// O'Rourke's algorithm: takes points in increasing x while one line is within
// k of all of them. The steps of PGM-index's OptimalPiecewiseLinearModel (see
// src/ORourke/pgm_orourke.hpp), but exact, and a failed add changes nothing.
//
// Each point gives an upper constraint (x, y + k) and a lower one (x, y - k).
// The feasible lines' extreme slopes are the min-slope line rect[0] -> rect[2]
// (an upper point to a lower one) and the max-slope line rect[1] -> rect[3];
// upper_ and lower_ hold the constraint points that can still define them.
class Fitter {
public:
    explicit Fitter(int64_t k = 0) : k_(k) {}

    // Starts over with no points.
    void reset() { n_ = 0; }
    void reset(int64_t k) {
        k_ = k;
        n_ = 0;
    }

    int64_t k() const { return k_; }
    size_t size() const { return n_; }

    // Adds p if one line is within k of it and of every point added since the
    // last reset; otherwise returns false and changes nothing. p.x must be
    // greater than every x added so far.
    bool add(Pt p) {
        if (n_ > 0 && p.x <= last_x_) throw std::logic_error("Fitter: x must increase");
        Pt hi{p.x, p.y + k_}, lo{p.x, p.y - k_};
        if (n_ == 0) {
            rect_[0] = hi;
            rect_[1] = lo;
            upper_.assign(1, hi);
            lower_.assign(1, lo);
            upper_start_ = lower_start_ = 0;
        } else if (n_ == 1) {
            rect_[2] = lo;
            rect_[3] = hi;
            upper_.push_back(hi);
            lower_.push_back(lo);
        } else {
            // hi below the min-slope line, or lo above the max-slope line
            if (slope_less(rect_[2], hi, rect_[0], rect_[2]) || slope_less(rect_[1], rect_[3], rect_[3], lo))
                return false;

            if (slope_less(rect_[1], hi, rect_[1], rect_[3])) {
                // hi below the max-slope line: the new one goes from the lower
                // point of least slope to hi
                size_t best = lower_start_;
                for (size_t i = lower_start_ + 1; i < lower_.size(); ++i) {
                    if (slope_less(lower_[best], hi, lower_[i], hi)) break;
                    best = i;
                }
                rect_[1] = lower_[best];
                rect_[3] = hi;
                lower_start_ = best;

                size_t end = upper_.size();
                while (end >= upper_start_ + 2 && cross(upper_[end - 2], upper_[end - 1], hi) <= 0) --end;
                upper_.resize(end);
                upper_.push_back(hi);
            }

            if (slope_less(rect_[0], rect_[2], rect_[0], lo)) {
                // lo above the min-slope line: the new one goes from the upper
                // point of greatest slope to lo (hi, just added, is not a candidate)
                size_t best = upper_start_;
                for (size_t i = upper_start_ + 1; i < upper_.size() && upper_[i].x < lo.x; ++i) {
                    if (slope_less(upper_[i], lo, upper_[best], lo)) break;
                    best = i;
                }
                rect_[0] = upper_[best];
                rect_[2] = lo;
                upper_start_ = best;

                size_t end = lower_.size();
                while (end >= lower_start_ + 2 && cross(lower_[end - 2], lower_[end - 1], lo) >= 0) --end;
                lower_.resize(end);
                lower_.push_back(lo);
            }
        }
        last_x_ = p.x;
        ++n_;
        return true;
    }

    // A line within k of every point added: the min-slope one, or for a single
    // point the horizontal line through it.
    ExactLine line() const {
        if (n_ == 0) throw std::logic_error("Fitter: no points");
        if (n_ == 1) return {{rect_[0].x, rect_[0].y - k_}, 0, 1};
        return {rect_[0], i128(rect_[2].y) - rect_[0].y, i128(rect_[2].x) - rect_[0].x};
    }

private:
    int64_t k_;
    size_t n_ = 0;
    Key last_x_ = 0;
    Pt rect_[4]{};
    std::vector<Pt> upper_, lower_;
    size_t upper_start_ = 0, lower_start_ = 0;
};

}  // namespace lpma
