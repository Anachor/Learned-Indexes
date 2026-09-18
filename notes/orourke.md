# O'Rourke

Given points (x, y) with strictly increasing x and an error bound delta, decides whether one line is within ±delta of every point.

## PGM implementation

`third_party/PGM-index/include/pgm/piecewise_linear_model.hpp`

### Way 1: a model object 

We create the model and feed points one by one. 

```cpp
OptimalPiecewiseLinearModel<int64_t, int64_t> model(delta);
model.add_point(0, 0);   // true / false
model.get_segment();
```

- `OptimalPiecewiseLinearModel<X, Y>(epsilon)`: X is the key type, Y the position type, epsilon the error bound.
- `add_point(x, y)`: Adds a new point (x, y). Returns true if the current segment still fits every point in the segment; false if not, and the model is ready for a new segment. Throws if x doesn't increase.
- `get_segment()`: the current (or just-ended) segment.
  `get_floating_point_segment(origin)` gives the line as (slope, intercept).


### Way 2: `make_segmentation`

A plain function called with the whole array. It uses OptimalPiecewiseLinearModel internally. The y values are always the indices.

```cpp
std::vector<int64_t> keys = {3, 7, 8, 14};

size_t L = make_segmentation(
    keys.size(),                              // n
    delta,                                    // error bound
    [&](size_t i) { return keys[i]; },        // in:  gives key i
    [&](auto segment) { /* store it */ });    // out: called once per segment

// L = number of segments
```

## Brute-force check

`tests/orourke.cpp`

- `brute_force(tc)`: minimum number of segments, independent of PGM. O(n³).
- `stress_test`: compares it with `pgm_orourke` on random cases. 0 mismatches so far (y = ranks only).
- Run: `./orourke [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]`
