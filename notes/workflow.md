- We start with an O'Rourke implementation.
    - PGM-index provides an implementation. See notes/orourke.md for details.
      - Correctness: Verified by stress testing against a brute-force implementation. 
        - To test it, we wrote our own brute-force version that counts the minimum
          number of segments given a set of points and delta.
        - It agrees with PGM on every case we tested (10,000 random cases) with y = rank, and n up to 100. We can't really test larger n because the brute-force implementation is O(n³).
        - Note: The segment count the PGM implementation returns is exact, but the line it returns for each segment is slightly off. PGM rounds each segment's intercept to an integer. The PGM paper says so too (footnote 6: storing intercepts as integers
              "increases epsilon by 1"), and its lookups search a slightly wider
              window to make up for it. So PGM solves the problem correctly (the segments and their count are exact), but the line it returns for each segment is slightly off.
    - ZLW (Zhonglue's) provides a second implementation (third_party/ZLW). Only change to it: a getter for its segment's support points.
    - All three (PGM, ZLW, brute force) now share one interface (src/ORourke/orourke.hpp), so they are interchangeable.
      - Each segment's line is now exactly within delta for all three (for PGM, we build it from its exact slope range instead of its rounded line).
    - The stress test now also uses y values other than ranks (gaps like a PMA, flat runs, random), and checks every line, not just the counts.
        - Results: PGM and ZLW agree with brute force on every case, n up to 1000.
    - Speed comparison (tests/orourke_speed.cpp), 1,000,000 random keys, delta 16:
        - Results: PGM about 16 ms, ZLW about 52 ms. PGM is about 3x faster. Both give the same segments.
        - The first run used to look much slower. That was the memory allocator cleaning up after the key generator, not O'Rourke; fixed by generating keys differently.
    - PGM also has a parallel version, make_segmentation_par. It is not optimal.
        - It splits the keys into one chunk per thread and segments each chunk separately, so every chunk boundary forces a segment to end. It can use up to (threads - 1) more segments than optimal.
        - Neither the PGM paper nor the code mentions this; the paper only says its construction was single-threaded.

- Experiment 1 (experiments/exp1): static O'Rourke on every prefix of a permutation of 1..n.
    - Points are (key, rank). delta = k/2 for integer k, so delta covers the half-integers;
      O'Rourke runs on (key, 2*rank) with integer bound k.
    - Model complexity: qc = log2(lambda) + log2(delta), lambda = number of segments.
      Early code used log2(2 delta), 1 more; run 1's CSVs were recomputed to the current definition.
    - 1a (best delta per prefix): minimises k * lambda exactly. lambda never grows with k,
      so the search only visits the steps of lambda and prunes ranges that cannot win.
      - Correctness: matches trying every k on every prefix (n up to 512, 6 seeds);
        PGM's segment counts match brute force up to t = 500; an independent sweep over
        delta = 0.5..16 agrees on all 1024 prefixes of n = 1024.
    - 1b: fixed delta = 0.5, 1, 2, 4, 8, 16, 32.
    - Ties (equal delta * lambda, so equal qc): --tiebreaker minlambda (default; the fewest
      segments, so the largest delta) or mindelta (the smallest delta - what every run so far
      used, from before the option existed). qc
      is the same; the pick differs on about 10% of prefixes at n = 512 (uniform 49, zipf:16,1 63,
      blocks:64 51 of 512), typically lambda = 2 at delta = 1/2 against lambda = 1 at delta = 1.
      Both pass --validate (exp1 and exp2).
    - Insertion orders (--permutation) and how each behaves:
      - uniform (random): lambda is almost always 1.
        - Why: a random subset deviates from a straight line like sqrt(t). Splitting into
          lambda pieces lowers delta only by sqrt(lambda) but adds log2(lambda), so one
          segment wins; qc grows about 1/2 per doubling of n.
      - blocks:64: acts like almost sorted, with delta always 1/2 - each segment is
        basically a line through its points with no error.
      - zipf:16,1 (key regions picked with weight 1/rank^s): the most interesting one -
        both delta and lambda vary, and qc is a bit higher than random.
      - zipf:4,1,3 (recursive zipf): delta basically 1/2, like blocks.
      - probing (linear probing): mostly like random, but with large spikes near the end,
        when clustering happens.
      - bitrev:p (almost sorted, then bits reversed; p*n random swaps first): only checked
        at n = 512. With no swaps every prefix is nearly an even grid: lambda = 1 with
        delta about 1, the best case; swaps move it toward random.
      - At n around 512 one permutation is noisy: the share of prefixes with lambda = 1
        ranged 63-95% over six uniform seeds. Compare orders at large n.
    - Runs: 1 uniform, 2 zipf:16,1, 3 blocks:64, 4 probing, 6 zipf:4,1,3.
      Each run folder has meta.json (seed, permutation, commit, timing, status).

- Experiment 2 (planned): Bentley-Saxe + O'Rourke, the way the dynamic PGM does it.
    - The PGM paper (notes/p1162-ferragina.pdf, Section 3) describes the classic logarithmic method:
      sets S0, ..., Sb, each empty or of size exactly 2^0, 2^1, ..., 2^b, with a PGM-index on
      every non-empty set. An insert finds the first empty Si and builds a new PGM over
      S0 ∪ ... ∪ Si-1 ∪ {x}. Deletes are tombstones. For external memory it uses sizes B^0, ..., B^b'.
      The experiments (Section 7.3) only say epsilon = 64.
    - The code (DynamicPGMIndex, third_party/PGM-index/include/pgm/pgm_index_dynamic.hpp)
      differs from the paper. None of this is in the paper:
      - Base 8 by default: level i holds up to 8^i keys (constructor argument base).
      - A buffer: levels up to min_level = ceil_log_base(128) - (base == 2) are one unindexed
        buffer, 1 + 8 + 64 + 512 = 585 keys at base 8 (argument buffer_level).
      - No PGM-index on levels below min_index_level = max(min_level + 1, ceil_log_base(2^24)),
        so levels under about 16.7M keys are searched without one (argument index_level).
      - Levels can be partly full: a merge goes into the first level with room.
      - The buffer is a plain sorted array of the newest keys: an insert goes into its place by
        binary search while there is room; when full, its 585 keys + the new one are merged into
        the first bigger level with room. Lookups binary-search it (about 10 comparisons), no segments.
      - With these defaults (base 8: min_index_level = level 8, up to 8^8 keys) and our n <= 65536
        (at most level 6), the code would build no PGM at all: every level is binary-searched.
    - We follow the paper, not the code: the paper is what we cite, and the code's choices trade
      model quality for speed (cache, fewer rebuilds). Its DynamicPGMIndex is the baseline only if
      we ever measure wall-clock times.
    - Overflow with base B > 2 (to investigate later): the paper gives no rule for base B (it cites
      [2, 35]). At base 2 sizes add up exactly (1 + 2 + ... + 2^(i-1) + 1 = 2^i), so every level is
      empty or full. At base 8 they don't (1 + 8 + 64 + 1 = 74, not 512), so either:
      - the code's rule: level i holds up to B^i keys; on overflow, carry the load up, adding each
        level passed, to the first level with room, and merge all of it there. A level takes about B
        chunks from below before it pours into the next, and is re-segmented on each: about log_B n
        levels, but each key is rebuilt about B times per level. Example (base 8, buffer 585): level 4
        (4096) gets 586, 1172, ..., 3516, then 586 + 3516 = 4102 goes to level 5 and level 4 empties.
      - or the textbook rule: level i holds up to B - 1 full sets of size B^i, each with its own model
        (a base-B counter); B sets of size B^i merge into one of size B^(i+1).
      At base 2 the two are the same. For a --base flag, the code's rule: one set, one (delta, lambda)
      per level.
    - Decision: exp2 is simple base-2 Bentley-Saxe with no buffer: every level is empty or exactly
      full, and O'Rourke runs on every non-empty level.
    - Implementation (experiments/exp2): each insert builds one level, segmented on (key, 2*rank
      within the level) - 2a at FIXED_K, 2b at the level's own best k (exp1's search). The CSV has
      one row group per build; the plot sums the levels present at each t (t's 1-bits).
      - Shared with exp1 (experiments/common/): permutations, the search, run folders and meta.json.
        Moving them out left exp1's output byte for byte the same (6 orders, n = 128..512).
      - Correctness: --validate checks the levels and each build's search and segment sizes (all
        orders, n up to 512). At t = 2^j there is one level holding the whole prefix, so 2b equals
        exp1's static best there - it does, exactly.
      - Cost per level can be -1 (a level exact at delta = 1/2 with one segment), so at small n
        2b can come out below the static best. Open question: keep log2(delta), or use log2(2 delta)
        per level so an exact level costs 0.
