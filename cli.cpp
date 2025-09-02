
#include "../src/utils.hpp"
#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using tcp = asio::ip::tcp;

asio::awaitable<void> reader(tcp::socket& sock) {
    try {
        for (;;) {
            auto payload = co_await chat::read_frame(sock);
            try {
                auto j = nlohmann::json::parse(payload, nullptr, true, true);
                std::cout << j.dump() << std::endl;
            } catch (...) {
                std::cout << payload << std::endl;
            }
        }
    } catch (...) {
        co_return;
    }
}

asio::awaitable<void> writer(tcp::socket& sock, std::string user, std::string room) {
    // send join
    {
        nlohmann::json j = {{"type","join"}, {"room", room}, {"user", user}};
        std::string payload = j.dump();
        co_await chat::write_frame(sock, payload);
    }
    // read stdin lines and send as chat
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "/quit") break;
        nlohmann::json j = {{"type","chat"}, {"room", room}, {"user", user}, {"text", line}};
        std::string payload = j.dump();
        co_await chat::write_frame(sock, payload);
    }
    sock.close();
    co_return;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <user> <room>\n";
        return 1;
    }
    std::string host = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    std::string user = argv[3];
    std::string room = argv[4];

    try {
        asio::io_context ioc;
        tcp::resolver res(ioc);
        auto eps = res.resolve(host, std::to_string(port));
        tcp::socket sock(ioc);
        asio::connect(sock, eps);

        co_spawn(ioc, reader(sock), detached);
        co_spawn(ioc, writer(sock, user, room), detached);
        ioc.run();
    } catch (std::exception& e) {
        spdlog::error("client error: {}", e.what());
        return 1;
    }
    return 0;
}
