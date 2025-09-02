#pragma once
#include "utils.hpp"
#include "persistence.hpp"

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <chrono>
#include <optional>
#include <string>

namespace chat {

// Bring Asio awaitable combinators (needed for: reader() || idle_watchdog())
using namespace asio::experimental::awaitable_operators;

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using tcp = asio::ip::tcp;
// namespace this_coro = asio::this_coro; // (unused)

struct Config {
    std::string host = "0.0.0.0";
    uint16_t     port = 7777;
    std::string  data_dir = "./data";
    std::size_t  max_send_queue = 256;
    std::size_t  history_on_join = 20;
};

struct Session; // fwd decl

struct Room : std::enable_shared_from_this<Room> {
    std::string name;
    std::unordered_set<Session*> members; // raw ptrs owned elsewhere
    Persistence& persist;

    explicit Room(std::string n, Persistence& p) : name(std::move(n)), persist(p) {}

    void join(Session* s)  { members.insert(s); }
    void leave(Session* s) { members.erase(s); }

    void broadcast(const nlohmann::json& j);
};

struct Session : std::enable_shared_from_this<Session> {
    tcp::socket socket;
    asio::steady_timer idle_timer;
    Config& cfg;
    Persistence& persist;
    std::unordered_map<std::string, std::shared_ptr<Room>>& rooms;

    std::string user;
    std::shared_ptr<Room> room;

    std::deque<std::string> sendq;
    bool writing = false;

    explicit Session(tcp::socket sock, Config& cfg, Persistence& p,
                     std::unordered_map<std::string, std::shared_ptr<Room>>& rooms)
        : socket(std::move(sock)), idle_timer(socket.get_executor()),
          cfg(cfg), persist(p), rooms(rooms) {}

    awaitable<void> start() {
        spdlog::info("session started from {}", socket.remote_endpoint().address().to_string());
        // run both reader and idle timers
        co_await (reader() || idle_watchdog());
        co_return;
    }

    awaitable<void> reader() {
        try {
            for (;;) {
                auto payload = co_await read_frame(socket);
                idle_timer.expires_after(std::chrono::seconds(60)); // reset idle on any activity
                nlohmann::json j = nlohmann::json::parse(payload, nullptr, true, true);

                auto tp = j.value("type", std::string{});
                if (tp == "join") {
                    std::string r = j.value("room", "general");
                    user = j.value("user", "anon");

                    if (!room || room->name != r) {
                        if (room) room->leave(this);
                        room = get_or_create_room(r);
                        room->join(this);

                        // system message
                        nlohmann::json sys = {
                            {"type","sys"},
                            {"text","welcome"},
                            {"room", r}
                        };
                        send_json(sys);

                        // history
                        auto hist = persist.load_last(r, cfg.history_on_join);
                        for (auto& msg : hist) send_json(msg);
                    }
                } else if (tp == "chat") {
                    if (!room) continue;
                    std::string text = j.value("text", "");
                    nlohmann::json out = {
                        {"type","chat"},
                        {"room", room->name},
                        {"user", user},
                        {"text", text},
                        {"ts", std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count()}
                    };
                    room->broadcast(out);
                    persist.append(room->name, out);
                } else if (tp == "history") {
                    std::string r = j.value("room", room ? room->name : "general");
                    std::size_t n = j.value("n", 20);
                    auto hist = persist.load_last(r, n);
                    for (auto& msg : hist) send_json(msg);
                } else if (tp == "ping") {
                    send_json({{"type","pong"}});
                } else {
                    // ignore unknown
                }
            }
        } catch (std::exception& e) {
            spdlog::info("session end: {}", e.what());
        }
        // cleanup
        if (room) room->leave(this);
        asio::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);
        co_return;
    }

    awaitable<void> idle_watchdog() {
        idle_timer.expires_after(std::chrono::seconds(60));
        asio::error_code ec;
        co_await idle_timer.async_wait(use_awaitable);
        // if timer fired without being reset -> close
        spdlog::info("idle timeout, closing session");
        socket.cancel();
        co_return;
    }

    void send_json(const nlohmann::json& j) {
        std::string s = j.dump();
        if (sendq.size() > cfg.max_send_queue) {
            spdlog::warn("send queue overflow, dropping connection");
            socket.cancel();
            return;
        }
        sendq.emplace_back(std::move(s));
        if (!writing) {
            writing = true;
            co_spawn(socket.get_executor(), writer(), detached);
        }
    }

    awaitable<void> writer() {
        try {
            while (!sendq.empty()) {
                std::string msg = std::move(sendq.front());
                sendq.pop_front();
                co_await write_frame(socket, msg);
            }
        } catch (std::exception& e) {
            spdlog::info("writer end: {}", e.what());
        }
        writing = false;
        co_return;
    }

    std::shared_ptr<Room> get_or_create_room(const std::string& r) {
        auto it = rooms.find(r);
        if (it != rooms.end()) return it->second;
        auto nr = std::make_shared<Room>(r, persist);
        rooms.emplace(r, nr);
        return nr;
    }
};

inline void Room::broadcast(const nlohmann::json& j) {
    std::string s = j.dump();
    for (auto* m : members) {
        if (m) {
            if (m->sendq.size() > m->cfg.max_send_queue) {
                spdlog::warn("send queue overflow for member, dropping (room={})", name);
                m->socket.cancel();
                continue;
            }
            m->sendq.emplace_back(s);
            if (!m->writing) {
                m->writing = true;
                co_spawn(m->socket.get_executor(), m->writer(), detached);
            }
        }
    }
}

struct Server {
    asio::io_context ioc;
    tcp::acceptor acc;
    Config cfg;
    Persistence persist;
    std::unordered_map<std::string, std::shared_ptr<Room>> rooms;

    explicit Server(const Config& c)
        : ioc(), acc(ioc), cfg(c), persist(cfg.data_dir) {}

    awaitable<void> accept_loop() {
        tcp::endpoint ep{asio::ip::make_address(cfg.host), cfg.port};
        acc.open(ep.protocol());
        acc.set_option(asio::socket_base::reuse_address(true));
        acc.bind(ep);
        acc.listen();

        spdlog::info("listening on {}:{}", cfg.host, cfg.port);
        for (;;) {
            tcp::socket s = co_await acc.async_accept(use_awaitable);
            auto sess = std::make_shared<Session>(std::move(s), cfg, persist, rooms);
            co_spawn(ioc, sess->start(), detached);
        }
    }

    void run() {
        co_spawn(ioc, accept_loop(), detached);
        ioc.run();
    }
};

} // namespace chat

