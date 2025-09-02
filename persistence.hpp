
#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>

namespace chat {

struct Persistence {
    std::filesystem::path data_dir;

    explicit Persistence(std::filesystem::path dir) : data_dir(std::move(dir)) {
        std::filesystem::create_directories(data_dir);
    }

    std::filesystem::path room_file(const std::string& room) const {
        return data_dir / (room + ".log");
    }

    void append(const std::string& room, const nlohmann::json& j) {
        std::ofstream ofs(room_file(room), std::ios::app);
        ofs << j.dump() << "\n";
    }

    // naive implementation: read whole file and return last N json entries
    std::vector<nlohmann::json> load_last(const std::string& room, std::size_t n) {
        std::vector<nlohmann::json> out;
        std::ifstream ifs(room_file(room));
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            try {
                out.emplace_back(nlohmann::json::parse(line));
            } catch (...) {
                // skip malformed
            }
        }
        if (out.size() > n) {
            out.erase(out.begin(), out.end() - n);
        }
        return out;
    }
};

} // namespace chat
