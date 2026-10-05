# GPLA optimizations

Every optimization of the GPLA (`src/GPLA/`, `src/Hull/`, and the parts of `src/PMA/` it
relies on), written to go into the paper: what it does, what it saves, why the guarantee
still holds, how it was checked, and where it is in the code. New optimizations are added
here as they land, with their before/after numbers.

Notation: n keys; a segment has m points (key, 2 * slot); k = 2 * delta (so half-integer
deltas stay integral); lambda segments; B the leaf size of T2's blocks. The guarantee is
**at most 2 * optimal - 1 segments**, which holds because no two neighbouring segments can
be joined (one line within k of both): taking disjoint neighbouring pairs, each holds a
break of the optimal segmentation, so floor(lambda / 2) <= optimal - 1.

**Evidence.** Correctness is checked by `tests/gpla` (after every insert, all hulls and
three PMA configurations: `problem()` checks the PMA, every segment's line and hull, that no
two neighbours can be joined, and at most 2 * optimal - 1 segments) and `tests/hulls`
(joins against brute force, splits against building from scratch). Most optimizations
below predate any timing, so each says how to measure it; numbers go here once run.

---

## 1. Repair only what an insert rewrote

*Since `63b6622`. `GPLA::repair`, `src/GPLA/gpla.hpp`.*

The PMA reports which slots an insert rewrote (a leaf, or a rebalanced window) and the
smallest and largest key in them. Only the segments with keys in that range change:

- the rewritten slots are segmented again by greedy O'Rourke;
- a segment the window cuts keeps its parts outside the window, with its old line
  (`Segment::split`) - they are unchanged points, so the line still fits them;
- these pieces, plus one untouched neighbour on each side, are joined where one line fits.

Cost per insert: O'Rourke over the rewritten window (Theta(log n) slots for a leaf,
amortized O(log^2 n) for rebalances), one split, and at most 4 join attempts (2.),
instead of re-segmenting everything. A capacity doubling re-segments all keys in one pass,
O(n), amortized O(1) per insert.

Guarantee: pairs outside the repaired range are unchanged. Consecutive rebuilt pieces
cannot be joined (greedy ends a piece exactly when the next point does not fit). Every
other new pair is tried (2.).

## 2. At most four join attempts per insert

*Since `2c72814`. `GPLA::join_neighbours`.*

The pieces are, left to right: neighbour A, the cut segment's left part L, the rebuilt
pieces, the right part R, neighbour B. Each piece is tried against the (possibly already
joined) piece before it, except two rebuilt pieces, which greedy made maximal. A pair that
fails is never tried again: later joins only add points to its right side, and a superset
of a set no line fits is not fitted either. The same argument covers the outer pairs (A's
left neighbour with A joined to L, and so on). So at most 4 attempts - A-L, L-first
rebuilt, last rebuilt-R, R-B - and no two neighbours can be joined afterwards.

## 3. Joins on hull vertices (T1)

*Since `63b6622`. `VectorHull`, `src/Hull/vector_hull.hpp`.*

A line is within k of every point exactly when it is within k of the vertices of their
upper and lower convex hulls: y - line(x) is linear, so its maximum over the points is at
an upper-hull vertex and its minimum at a lower-hull vertex. T1 keeps both hulls as vectors
and decides a join by running O'Rourke over the vertices of both segments only:
O(h_l + h_r) instead of T0's O(m_l + m_r), h the number of hull vertices. The joined hull is
the two hulls merged by the monotone chain, O(h_l + h_r). The price: a split rebuilds both
parts' hulls from the PMA, O(m).

T0 (`ScanHull`) is the baseline: it keeps nothing, so a split is free and a join streams
every point of both segments through O'Rourke.

To measure: `tests/gpla_speed --hulls scan,vector,tree` (inserts and lookups, per hull).

## 4. A tree of hulls (T2): polylogarithmic joins and splits

*Since `2c72814`. `BasicTreeHull`, `src/Hull/tree_hull.hpp`; searches in `chains.hpp`.*

T2 keeps a treap over the segment's points in key order (leaves) whose inner nodes are the
gaps between neighbouring leaves, and every node knows its subtree's upper and lower hull,
in the style of Overmars and van Leeuwen. Three choices keep it small and fast:

- **A node stores three numbers per hull**, not the hull: its vertex count, how many
  vertices come from the left child's front, and how many from the right child's back (the
  bridge). A vertex is found by walking down, O(depth). So a node is O(1) words.
- **Bridges by halving** (`bridge`): each step looks at the middle vertex of both chains
  against the line through them and drops at least half of one chain - O(log m) steps.
- **A join is decided before anything changes.** `try_join` builds a virtual view of the
  two roots' joined hull (one bridge per hull) and finds the lowest-slope fitting line on
  it (`lowest_line`: the vertical width at slope s is convex in s with breakpoints at the
  hull edges' slopes, so nested binary searches find the last breakpoint where the width
  is still above 2k, and the line through the extreme points there). Only if a line fits
  are the trees fused. A failed join costs only searches and leaves both segments as they
  were - and failures are common: of about 3.5 attempts per insert, about 1.6 fail (the
  checks against the neighbours A and B; preliminary, n = 998, genome and covid).

Cost: a split or a join touches the O(log m) nodes on one path (expected, treap); each node
update is two bridge searches of O(log m) steps whose vertex accesses each walk O(log m)
levels, so O(log^3 m) expected, plus O(B) per leaf refreshed (5.). The join decision
(`lowest_line`) is O(log^2 m) vertex accesses, so also O(log^3 m).

## 5. Leaves of B points in T2 (B = 32)

*Since `801efa8`. `BasicTreeHull<B>`; `TreeHull = BasicTreeHull<32>`.*

Instead of one point per leaf, a leaf holds up to B consecutive points in a fixed array
(`Block`), with its own hulls as vertex indices (`uint16_t`), computed by the monotone chain
in O(B) (`refresh_leaf`). Invariant: any two neighbouring leaves hold more than B points
together - a join (`fuse`) or a split (`tidy_back`, `tidy_front`) merges two that do not -
so m points take fewer than 2m / B + 1 leaves.

Saves: the tree has ~2m / B leaves instead of m, so it is log B levels shallower (shorter
vertex walks, fewer node updates per split or join), with far fewer allocations; a vertex
access ends in an array. Costs: O(B) to refresh a touched leaf, and memory - a block is the
same size however many points it holds (8 + 20B bytes, 648 bytes at B = 32), so a segment
of a few points still pays a whole block.

B is chosen by measurement: `tree1`, `tree4`, ..., `tree128` name `BasicTreeHull<B>`
(`src/Hull/by_name.hpp`) on every command line. To measure:
`tests/gpla_speed --hulls tree1,tree4,tree8,tree16,tree32,tree64,tree128`, and in GRE
`bench/gre/compare.sh DATASETS gpla-leaf8 gpla-leaf16 gpla gpla-leaf64`.

## 6. Pools for T2's nodes and blocks

*Nodes since `2c72814`, blocks since `801efa8`. `pool`, `take`, `release` in `tree_hull.hpp`.*

Released nodes and blocks go to per-thread free lists and are reused, so the splits and
joins of every insert do not call the allocator. Memory held by the free lists is not
returned (and not counted by `bytes()`).

## 7. Exact arithmetic in 128 bits

*Since `63b6622`; `fraction_less` since `1e7087c`. `src/Hull/geometry.hpp`, `chains.hpp`.*

Points are (key, 2 * slot) and k = 2 * delta, all integers, so every predicate - turns,
slope comparisons, a line's value and its slot window - is computed exactly in `__int128`,
with no floating-point error and no big integers. Keys can be any `int64_t` as long as
slots stay below 2^57 and k at most 2^58. Where a comparison of two fractions would need a
256-bit product (`above_at` in the bridge search), `fraction_less` compares a/b < c/d by
integer parts and then the flipped remainders, as in Euclid's algorithm. The lines are
therefore exact: PGM rounds each intercept to an integer, so its lines can be off by one
(see `notes/workflow.md`); ours are within delta of every point.

O'Rourke itself (`Fitter`) is PGM's algorithm made exact: it keeps only the constraint
points that can still set an extreme slope, with start indices that only move forward, so
it is amortized O(1) per point (`cf6c2b6`).

## 8. Lookups: a window of 2 * delta + 1 slots inside the segment

*Since `63b6622`. `GPLA::lower_bound_slot`.*

A lookup finds the segment (the segment map, O(log lambda)); if x is above the segment's
last key, the answer is the next segment's first slot, with no line evaluated. Otherwise
the line gives the window [ceil((line(x) - k) / 2), floor((line(x) + k) / 2)], at most
k + 1 = 2 * delta + 1 slots, clamped to the segment's slots, and a binary search over it
that skips empty slots. So a lookup is ~log2 lambda + log2 delta + 1 comparisons, the qc of
the experiments; `tests/gpla_speed` counts them.

## 9. Reused scratch state

*Since `63b6622`.* O'Rourke's state is reused instead of allocated per call
(`scratch_fitter`, per thread, and `GPLA::fitter_`), as is the PMA's rebalance buffer
(`buffer_`). Rebuilt segments go back into the segment map in order with `emplace_hint`,
amortized O(1) per insertion instead of a search.

## 10. PMA choices GPLA relies on

*`src/PMA/pma.hpp`, since `773ea14` / `cf2b93b`.*

- Leaves of Theta(log n) slots (the smallest power of two >= log2 capacity), densities
  from 1.0 at a leaf to 0.75 at the root, interpolated by height.
- An insert into a leaf shifts toward the nearer empty slot, so fewer keys move - and
  fewer points change for 1.
- The update report (`describe_update`): old and new slot range and the affected keys,
  which is what lets 1. repair only the rewritten window. For a shift inside a leaf it
  reports the whole leaf (simpler, and independent of the shifting policy), so GPLA
  re-segments the leaf's Theta(log n) points even when only a few moved.

## Build

All our binaries build at `-O3` since `21bf58a` (previously `-O2`), without
`-march=native`. GRE builds every index, GPLA included, at `-O3 -march=native -DNDEBUG`.

---

## Baseline: GRE, 1M keys

The state before the optimizations below. Commit `8d1112c`: GPLA = T2, B = 32, delta = 16.
`bench/gre/compare.sh` on 1M keys, evenly spaced by rank, from the first 2M keys of GRE's
covid and genome (`covid-short`, `genome-short`); single thread, GRE at `-O3 -march=native`.
Rows in `results/gre/{covid,genome}-short_1000000.csv`, 2026-10-05.

| dataset | index | read-only Mops/s | balanced Mops/s | write-only Mops/s | MB after writes |
|---|---|---|---|---|---|
| genome-short | alex | 14.598 | 5.855 | 3.853 | 28.89 |
| genome-short | gpla | 3.879 | 0.078 | 0.040 | 52.55 |
| covid-short | alex | 10.812 | 3.990 | 3.985 | 29.04 |
| covid-short | gpla | 4.388 | 0.062 | 0.033 | 52.10 |

Lookups are about 3x slower than ALEX, inserts about 100x (about 25 us each).

### Profile of the inserts, 1M keys

Commit `801efa8` (before the rename to GPLA and the delta = 16 default): T2, B = 32,
**delta = 32**. GRE's write-only workload on the same 1M covid keys
(`covid-short_1000000`): a bulk load of 500K keys, which GPLA inserts one by one in
sorted order, then 500K inserts. `perf record -e cycles:u -F 499 --call-graph dwarf`, 18K
samples, 2026-10-05.

About 149G cycles for the 1M inserts: about 150K cycles each. The bulk load takes 42% of
them and the timed inserts 56%, with the same breakdown.

| function, with what it calls | share |
|---|---|
| `GPLA::insert` | 97.7% |
| - the PMA's own insert (`PMA::insert`, including `spread` 2.7%) | 4.5% |
| - `GPLA::repair` | 92.7% |
| -- `join_neighbours` (`lowest_line` 20.4%, the trees' `join` 13.2%) | 45.9% |
| -- `segment`: O'Rourke over the rewritten slots and new hulls (`Fitter::add` 11.7%, `Segment::build` 6.9%) | 27.4% |
| -- `Segment::split` (`split_nodes` 16.9%) | 18.1% |
| T2's node updates (`update`: two bridges), within the above | 31.9% |

| function, its own time | share |
|---|---|
| `BasicTreeHull::vertex` (the walk to a vertex, 4.) | 38.5% |
| `bridge` | 12.9% |
| `Fitter::add` (O'Rourke) | 11.2% |
| `refresh_leaf` (a leaf's hulls, 5.) | 10.9% |
| `lowest_line` | 8.6% |
| `PMA::spread` | 2.7% |
| `GPLA::segment` | 1.6% |
| `PMA::insert` | 1.4% |
| `split_nodes` | 1.0% |
| `__divmodti4` (128-bit division, 7.) | 0.9% |
| `Joined::at` | 0.8% |
| `malloc` | 0.6% |

Reading hull vertices - `vertex`, `bridge`, `lowest_line`, `Joined::at` - is about 61% of
insert time: the price of storing a node's hull as three counts (4.), as every vertex read
walks O(depth) levels, and bridges and `lowest_line` make several reads per step. The
PMA's own work is under 5%. To repeat (needs `kernel.perf_event_paranoid` <= 2):
`perf record -e cycles:u -F 499 --call-graph dwarf -o /tmp/gpla.perf
third_party/GRE/build/microbench --keys_file=third_party/GRE/resources/covid-short_1000000
--keys_file_type=binary --read=0 --insert=1 --init_table_ratio=0.5 --operations_num=1000000
--table_size=-1 --thread_num=1 --index=gpla-delta32 --output_path=/dev/null`, then
`perf report -i /tmp/gpla.perf --no-children --no-inline -g none` (own time) or
`--children` (with callees).

## Next (not done yet)

Where the time goes (T2: the 1M profile above; T1: preliminary, n = 998): every insert
splits the segment it lands in and joins it back. With T1, `split` is 45% of insert time
and O'Rourke in the joins 24%; with T2, the vertex walks, bridges and `lowest_line`, about
61%. Yet in about 86% of inserts (genome, covid) one segment covers the rewritten
leaf and its line still fits it.

1. **Fast path with non-joinability witnesses.** Keep that segment as it is. To keep the
   guarantee, its pairs with A and B must stay unjoinable, and the shifted points could make
   them joinable. Helly's theorem in the plane (each point's constraint is a convex strip of
   (slope, intercept)) gives a witness: if no line fits a set, no line fits some 3 of its
   points. Store 3 such points per neighbouring pair (O'Rourke yields them when it fails:
   the failing point and the 2 constraints at the feasible region's extreme vertex); if none
   of them moved, the pair is still unjoinable in O(1); otherwise check it again. Guarantee
   kept exactly. To measure first: how often a witness point lies in the rewritten leaf.
2. **A cache-friendly segment map** (B+-tree, or a sorted array of first keys with the
   segments out of line) instead of `std::map`: lookups now chase ~log2 lambda pointers
   through nodes spread among T2's blocks.
3. **Model-guided inserts.** The PMA finds an insert's slot by its own binary search over
   all slots; the GPLA's lookup could give it.
4. **No allocations per insert** (`repair`'s vectors as members).
5. **A real bulk load** (GRE loads keys one by one; load time is not measured, but it sets
   the layout).
6. **The exact shifted range in the PMA's report** instead of the whole leaf (10.).
