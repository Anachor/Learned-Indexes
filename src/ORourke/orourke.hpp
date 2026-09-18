#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The line y = y0 + slope * (x - x0).
struct Line {
    long double x0, y0, slope;
    long double operator()(long double x) const { return y0 + slope * (x - x0); }
};

// Points begin..end-1 and a line within +-delta of all of them.
struct Segment {
    size_t begin, end;
    Line line;
};

// O'Rourke: splits points (x, y), added in order of strictly increasing x, into
// segments, each with one line within +-delta of all its points.
template <typename T = int64_t>
class ORourke {
public:
    explicit ORourke(T delta) : delta_(delta) {}
    virtual ~ORourke() = default;

    T delta() const { return delta_; }

    // Starts over with no points and error bound delta.
    void reset(T delta) {
        delta_ = delta;
        n_ = 0;
    }

    // Adds (x, y). x must be greater than the previous x.
    // If (x, y) fits the current segment, adds it and returns nothing.
    // Otherwise closes the current segment, returns its line, and starts a new
    // segment with (x, y).
    std::optional<Line> add_point(T x, T y) {
        if (n_ == 0) start(delta_);
        if (try_add(x, y)) {
            if (n_++ == 0) first_ = {(long double)x, (long double)y, 0};
            return std::nullopt;
        }
        Line closed = line();
        try_add(x, y);
        n_ = 1;
        first_ = {(long double)x, (long double)y, 0};
        return closed;
    }

    // A line within +-delta of every point of the current segment, including
    // the last point added. Horizontal through the point for a one-point
    // segment. Undefined before the first add_point.
    Line current() const { return n_ == 1 ? first_ : line(); }

    // Minimum number of segments covering the points (x[i], i), and the
    // segments themselves, in order. x must be strictly increasing. Starts over.
    std::vector<Segment> segmentation(const std::vector<T> &x) {
        reset(delta_);
        std::vector<Segment> segments;
        size_t begin = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            if (auto closed = add_point(x[i], T(i))) {
                segments.push_back({begin, i, *closed});
                begin = i;
            }
        }
        if (!x.empty()) segments.push_back({begin, x.size(), current()});
        return segments;
    }

protected:
    // These methods are implementation defined
    
    // Starts an empty segment with error bound delta.
    virtual void start(T delta) = 0;

    // Adds (x, y) to the current segment if one line is still within +-delta
    // of every point in it, and returns true. Otherwise returns false; the
    // point is not added, and the next try_add starts a new segment.
    virtual bool try_add(T x, T y) = 0;

    // A line within +-delta of every point of the current segment (at least
    // two points), or right after try_add returns false, of that segment.
    virtual Line line() const = 0;

private:
    T delta_;
    size_t n_ = 0;  // points in the current segment
    Line first_{};  // horizontal line through the current segment's first point
};
