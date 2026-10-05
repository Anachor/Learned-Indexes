#pragma once

// Exact geometry: points (key, 2*slot), lines within k of them (k = 2*delta),
// and O'Rourke's algorithm. All arithmetic is exact in __int128: keys can be any
// int64_t as long as slots stay below MAX_CAPACITY and k at most MAX_K.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace lpma {

using Key = int64_t;
using i128 = __int128;

inline constexpr int64_t MAX_K = int64_t(1) << 58;
inline constexpr size_t MAX_CAPACITY = size_t(1) << 57;

struct Pt {
    Key x;
    int64_t y;  // 2 * slot
    bool operator==(const Pt &) const = default;
};

// Positive when a, b, c turn left (c above the line a->b when a.x < b.x).
inline i128 cross(Pt a, Pt b, Pt c) {
    return (i128(b.x) - a.x) * (i128(c.y) - a.y) - (i128(b.y) - a.y) * (i128(c.x) - a.x);
}

// slope(a, b) < slope(c, d), for a.x < b.x and c.x < d.x.
inline bool slope_less(Pt a, Pt b, Pt c, Pt d) {
    return (i128(b.y) - a.y) * (i128(d.x) - c.x) < (i128(d.y) - c.y) * (i128(b.x) - a.x);
}

inline i128 floor_div(i128 a, i128 b) { return a / b - (a % b != 0 && a < 0); }  // b > 0
inline i128 ceil_div(i128 a, i128 b) { return a / b + (a % b != 0 && a > 0); }   // b > 0

// a/b < c/d for b, d > 0, without multiplying them together (it could
// overflow): compare integer parts, then the flipped fractional parts.
inline bool fraction_less(i128 a, i128 b, i128 c, i128 d) {
    while (true) {
        i128 qa = floor_div(a, b), qc = floor_div(c, d);
        if (qa != qc) return qa < qc;
        a -= qa * b;
        c -= qc * d;
        if (a == 0 || c == 0) return a == 0 && c != 0;
        std::tie(a, b, c, d) = std::make_tuple(d, c, b, a);  // a/b < c/d  <=>  d/c < b/a
    }
}

// The line through anchor with slope dy / dx, dx > 0.
struct ExactLine {
    Pt anchor{0, 0};
    i128 dy = 0, dx = 1;
    bool operator==(const ExactLine &) const = default;

    // dx * line(x)
    i128 scaled(Key x) const { return i128(anchor.y) * dx + dy * (i128(x) - anchor.x); }

    // |line(p.x) - p.y| <= k
    bool within(Pt p, int64_t k) const {
        i128 error = scaled(p.x) - i128(p.y) * dx;
        return -i128(k) * dx <= error && error <= i128(k) * dx;
    }

    // The slots s with |line(x) - 2s| <= k, as [first, second].
    std::pair<i128, i128> slot_window(Key x, int64_t k) const {
        i128 v = scaled(x), d = 2 * dx;
        return {ceil_div(v - i128(k) * dx, d), floor_div(v + i128(k) * dx, d)};
    }

    // The same line, maybe through another anchor.
    bool same(const ExactLine &o) const {
        return dy * o.dx == o.dy * dx && o.scaled(anchor.x) == i128(anchor.y) * o.dx;
    }
};

// O'Rourke's algorithm, as in PGM-index but exact: takes points in increasing x
// while one line is within k of all of them. Each point p gives the constraint
// points (p.x, p.y + k) above and (p.x, p.y - k) below; the fitting lines'
// extreme slopes run min_from_ -> min_to_ and max_from_ -> max_to_.
class Fitter {
public:
    explicit Fitter(int64_t k = 0) : k_(k) {}

    void reset() { n_ = 0; }
    void reset(int64_t k) {
        k_ = k;
        n_ = 0;
    }
    size_t size() const { return n_; }

    // Adds p if one line still fits every point; otherwise returns false and
    // changes nothing.
    bool add(Pt p) {
        if (n_ > 0 && p.x <= last_x_) throw std::logic_error("Fitter: x must increase");
        Pt hi{p.x, p.y + k_}, lo{p.x, p.y - k_};
        if (n_ == 0) {
            min_from_ = hi;
            max_from_ = lo;
            upper_.assign(1, hi);
            lower_.assign(1, lo);
            upper_start_ = lower_start_ = 0;
        } else if (n_ == 1) {
            min_to_ = lo;
            max_to_ = hi;
            upper_.push_back(hi);
            lower_.push_back(lo);
        } else {
            // hi below the min-slope line, or lo above the max-slope line
            if (slope_less(min_to_, hi, min_from_, min_to_) || slope_less(max_from_, max_to_, max_to_, lo)) return false;

            if (slope_less(max_from_, hi, max_from_, max_to_)) {  // hi lowers the max slope
                size_t best = lower_start_;
                while (best + 1 < lower_.size() && !slope_less(lower_[best], hi, lower_[best + 1], hi)) ++best;
                max_from_ = lower_[best];
                max_to_ = hi;
                lower_start_ = best;
                while (upper_.size() >= upper_start_ + 2 && cross(upper_[upper_.size() - 2], upper_.back(), hi) <= 0)
                    upper_.pop_back();
                upper_.push_back(hi);
            }
            if (slope_less(min_from_, min_to_, min_from_, lo)) {  // lo raises the min slope
                size_t best = upper_start_;
                while (best + 1 < upper_.size() && upper_[best + 1].x < lo.x &&
                       !slope_less(upper_[best + 1], lo, upper_[best], lo))
                    ++best;
                min_from_ = upper_[best];
                min_to_ = lo;
                upper_start_ = best;
                while (lower_.size() >= lower_start_ + 2 && cross(lower_[lower_.size() - 2], lower_.back(), lo) >= 0)
                    lower_.pop_back();
                lower_.push_back(lo);
            }
        }
        last_x_ = p.x;
        ++n_;
        return true;
    }

    // The min-slope line that fits; for one point, the horizontal line through it.
    ExactLine line() const {
        if (n_ == 0) throw std::logic_error("Fitter: no points");
        if (n_ == 1) return {{min_from_.x, min_from_.y - k_}, 0, 1};
        return {min_from_, i128(min_to_.y) - min_from_.y, i128(min_to_.x) - min_from_.x};
    }

private:
    int64_t k_;
    size_t n_ = 0;
    Key last_x_ = 0;
    Pt min_from_{}, min_to_{}, max_from_{}, max_to_{};
    std::vector<Pt> upper_, lower_;  // constraint points that can still set an extreme slope
    size_t upper_start_ = 0, lower_start_ = 0;
};

}  // namespace lpma
