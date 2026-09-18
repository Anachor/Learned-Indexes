- We start with an O'Rourke implementation.
    - PGM-index provides an implementation. See notes/orourke.md for details.
      - Correctness: Verified by stress testing against a brute-force implementation. 
        - To test it, we wrote our own brute-force version that counts the minimum
          number of segments given a set of points and delta.
        - It agrees with PGM on every case we tested (10,000 random cases) with y = rank, and n up to 100. We can't really test larger n because the brute-force implementation is O(n³).
        - Note: The segment count the PGM implementation returns is exact, but the line it returns for each segment is slightly off. PGM rounds each segment's intercept to an integer. The PGM paper says so too (footnote 6: storing intercepts as integers
              "increases epsilon by 1"), and its lookups search a slightly wider
              window to make up for it. So PGM solves the problem correctly (the segments and their count are exact), but the line it returns for each segment is slightly off.
    




