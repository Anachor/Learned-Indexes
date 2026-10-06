#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <limits>
#include <string>
#include <vector>

#include "../src/ORourke/brute_orourke.hpp"
#include "../src/ORourke/pgm_orourke.hpp"
#include "../src/ORourke/zlw_orourke.hpp"

const int64_t DEFAULT_DELTA = 16;

void usage(const char *prog) {
    std::cerr << "usage: " << prog << " -m METHOD -t T -n N [-d DELTA] [seed]\n"
              << "  -m METHOD   pgm, zlw or brute\n"
              << "  -t T        number of runs\n"
              << "  -n N        points per run\n"
              << "  -d DELTA    error bound (default " << DEFAULT_DELTA << ")\n"
              << "  seed        random if omitted\n";
    std::exit(1);
}

// Parses a whole string as a non-negative integer, or prints usage.
uint64_t parse_number(const char *s, const char *prog) {
    try {
        size_t used;
        std::string str = s;
        if (str.empty() || str[0] == '-') usage(prog);
        uint64_t v = std::stoull(str, &used);
        if (used != str.size()) usage(prog);
        return v;
    } catch (const std::exception &) {
        usage(prog);
    }
    return 0;
}

// n distinct uniform random int64 keys >= 0, sorted.
std::vector<int64_t> random_keys(size_t n, std::mt19937_64 &rng) {
    std::uniform_int_distribution<int64_t> dist(0, std::numeric_limits<int64_t>::max());
    std::vector<int64_t> keys;
    keys.reserve(n);
    while (keys.size() < n) {
        while (keys.size() < n) keys.push_back(dist(rng));
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }
    return keys;
}

// Times O'Rourke's segmentation of N random sorted keys (y = rank), T times.
// Prints the number of segments and the time of each run, then the mean time.
int main(int argc, char **argv) {
    std::string method;
    uint64_t T = 0, N = 0, seed = 0;
    int64_t delta = DEFAULT_DELTA;
    bool has_seed = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool takes_value = arg == "-m" || arg == "-t" || arg == "-n" || arg == "-d";
        if (takes_value && i + 1 >= argc) usage(argv[0]);

        if (arg == "-m") {
            method = argv[++i];
        } else if (arg == "-t") {
            T = parse_number(argv[++i], argv[0]);
        } else if (arg == "-n") {
            N = parse_number(argv[++i], argv[0]);
        } else if (arg == "-d") {
            delta = int64_t(parse_number(argv[++i], argv[0]));
        } else if (!has_seed) {
            seed = parse_number(argv[i], argv[0]);
            has_seed = true;
        } else {
            usage(argv[0]);
        }
    }
    if (T < 1 || N < 1) usage(argv[0]);

    std::unique_ptr<ORourke<int64_t>> orourke;
    if (method == "pgm") {
        orourke = std::make_unique<PgmORourke<int64_t>>(delta);
    } else if (method == "zlw") {
        orourke = std::make_unique<ZlwORourke<int64_t>>(delta);
    } else if (method == "brute") {
        orourke = std::make_unique<BruteORourke<int64_t>>(delta);
    } else {
        usage(argv[0]);
    }

    if (!has_seed) seed = std::random_device{}();
    std::cout << "method: " << method << "  T: " << T << "  N: " << N << "  delta: " << delta
              << "  seed: " << seed << std::endl;

    std::mt19937_64 rng(seed);
    double total_ms = 0;
    for (uint64_t t = 0; t < T; ++t) {
        std::vector<int64_t> keys = random_keys(N, rng);

        auto start = std::chrono::steady_clock::now();
        size_t segments = orourke->segmentation(keys).size();
        auto end = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        total_ms += ms;
        std::cout << "run " << t << ": segments=" << segments << " time=" << ms << " ms" << std::endl;
    }
    std::cout << "mean time: " << total_ms / T << " ms" << std::endl;
}
