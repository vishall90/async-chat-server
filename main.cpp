
#include "chat_server.hpp"
#include <fstream>

using namespace chat;

static Config load_config(const std::string& path) {
    Config c;
    try {
        std::ifstream ifs(path);
        nlohmann::json j; ifs >> j;
        c.host = j.value("host", c.host);
        c.port = j.value("port", c.port);
        c.data_dir = j.value("data_dir", c.data_dir);
        c.max_send_queue = j.value("max_send_queue", c.max_send_queue);
        c.history_on_join = j.value("history_on_join", c.history_on_join);
    } catch (...) {
        spdlog::warn("Failed to read config at {}, using defaults", path);
    }
    return c;
}

int main(int argc, char** argv) {
    std::string cfg_path = "./config/config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            cfg_path = argv[++i];
        }
    }

    auto cfg = load_config(cfg_path);
    try {
        chat::Server s(cfg);
        s.run();
    } catch (std::exception& e) {
        spdlog::error("fatal: {}", e.what());
        return 1;
    }
    return 0;
}
