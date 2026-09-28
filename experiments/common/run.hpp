#pragma once

// Run folders and meta.json, shared by the experiments.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "permutation.hpp"
#include "segmentation.hpp"

// Creates DIR/<run> for the next unused run number and returns its path.
inline std::string next_run_dir(const std::string &base) {
    namespace fs = std::filesystem;
    int run = 0;
    if (fs::exists(base)) {
        for (const auto &entry : fs::directory_iterator(base)) {
            std::string name = entry.path().filename().string();
            if (entry.is_directory() && !name.empty() &&
                name.find_first_not_of("0123456789") == std::string::npos) {
                run = std::max(run, std::stoi(name));
            }
        }
    }
    std::string dir = base + "/" + std::to_string(run + 1);
    fs::create_directories(dir);
    return dir;
}

// The commit this binary was built from, baked in by experiments/<exp>/build.sh.
// A plain g++ build leaves it unknown.
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#ifndef GIT_DIRTY
#define GIT_DIRTY "unknown"  // "true" / "false" from build.sh
#endif

inline std::string json_string(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
            out += buffer;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

inline std::string utc_now() {
    std::time_t now = std::time(nullptr);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buffer;
}

// DIR/<run>/meta.json: what the run is, so its numbers can be interpreted and
// reproduced. Rewritten after every n, so an interrupted run says how far it
// got: status stays "running" unless the run finished.
struct RunMeta {
    std::string experiment, path, command, started, finished;
    std::string cost = "log2(delta) + log2(lambda)";
    int run = 0;
    uint64_t seed = 0;
    std::vector<size_t> sizes;
    // Experiment-specific fields, written after cost: (key, JSON value).
    std::vector<std::pair<std::string, std::string>> extra;
    struct Timing { size_t n; double seconds, mean_evaluations; size_t max_evaluations; };
    std::vector<Timing> timing;

    void write() const {
        std::ostringstream o;
        o << "{\n"
          << "  \"experiment\": " << json_string(experiment) << ",\n"
          << "  \"run\": " << run << ",\n"
          << "  \"status\": \"" << (finished.empty() ? "running" : "complete") << "\",\n"
          << "  \"seed\": " << seed << ",\n"
          << "  \"seed_rule\": \"each n uses seed + n\",\n"
          << "  \"permutation\": " << json_string(ORDER.spec()) << ",\n"
          << "  \"ns\": [";
        for (size_t i = 0; i < sizes.size(); ++i) o << (i ? ", " : "") << sizes[i];
        o << "],\n  \"fixed_deltas\": [";
        for (size_t i = 0; i < FIXED_K.size(); ++i) o << (i ? ", " : "") << double(FIXED_K[i]) / 2;
        o << "],\n"
          << "  \"cost\": " << json_string(cost) << ",\n"
          << "  \"tiebreaker\": " << json_string(tiebreaker_name()) << ",\n";
        for (const auto &[key, value] : extra) o << "  " << json_string(key) << ": " << value << ",\n";
        o << "  \"commit\": " << json_string(GIT_COMMIT) << ",\n"
          << "  \"dirty\": " << (std::string(GIT_DIRTY) == "unknown" ? "null" : GIT_DIRTY) << ",\n"
          << "  \"command\": " << json_string(command) << ",\n"
          << "  \"started\": " << json_string(started) << ",\n"
          << "  \"finished\": " << (finished.empty() ? "null" : json_string(finished)) << ",\n"
          << "  \"timing\": {";
        for (size_t i = 0; i < timing.size(); ++i) {
            const Timing &t = timing[i];
            o << (i ? "," : "") << "\n    \"" << t.n << "\": {\"seconds\": " << t.seconds
              << ", \"mean_evaluations\": " << t.mean_evaluations
              << ", \"max_evaluations\": " << t.max_evaluations << "}";
        }
        o << (timing.empty() ? "}" : "\n  }") << "\n}\n";

        // Write and rename, so a reader never sees half a file.
        std::string temporary = path + ".tmp";
        std::ofstream(temporary) << o.str();
        std::filesystem::rename(temporary, path);
    }
};
