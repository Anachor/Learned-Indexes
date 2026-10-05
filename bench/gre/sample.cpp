// Writes m keys of a GRE key file, evenly spaced by rank: of its n distinct
// keys, sorted, those at ranks floor(i * n / m) for i = 0, ..., m - 1. The
// shape of the distribution stays; its fine detail does not, so local hardness
// (PLA at a small epsilon) is not that of the full file.
//
// A key file is binary: the key count, then the keys, all uint64. A file
// shorter than its count says (a download cut short) is read as far as it goes.
//
//   g++-11 -std=c++20 -O2 bench/gre/sample.cpp -o bench/gre/sample
//   ./bench/gre/sample IN OUT M

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: " << argv[0] << " IN OUT M\n";
        return 2;
    }
    const char *in_path = argv[1], *out_path = argv[2];
    uint64_t m = std::strtoull(argv[3], nullptr, 10);

    std::ifstream in(in_path, std::ios::binary);
    uint64_t count = 0;
    if (!in || !in.read(reinterpret_cast<char *>(&count), sizeof count)) {
        std::cerr << "cannot read " << in_path << "\n";
        return 1;
    }
    uint64_t present = (std::filesystem::file_size(in_path) - sizeof count) / sizeof(uint64_t);
    if (present < count) {
        std::cerr << in_path << " says " << count << " keys but holds " << present << " - cut short?\n";
        count = present;
    }
    std::vector<uint64_t> keys(count);
    in.read(reinterpret_cast<char *>(keys.data()), std::streamsize(count * sizeof(uint64_t)));
    if (!std::is_sorted(keys.begin(), keys.end())) std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    uint64_t n = keys.size();
    if (m == 0 || m > n) {
        std::cerr << "M must be in 1.." << n << " (the distinct keys)\n";
        return 2;
    }
    std::vector<uint64_t> sample(m);
    for (uint64_t i = 0; i < m; ++i) sample[i] = keys[uint64_t((unsigned __int128)i * n / m)];

    std::ofstream out(out_path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&m), sizeof m);
    out.write(reinterpret_cast<const char *>(sample.data()), std::streamsize(m * sizeof(uint64_t)));
    if (!out) {
        std::cerr << "cannot write " << out_path << "\n";
        return 1;
    }
    std::cout << "wrote " << m << " of " << n << " distinct keys to " << out_path << "\n";
    return 0;
}
