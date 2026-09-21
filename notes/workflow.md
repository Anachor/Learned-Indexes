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





