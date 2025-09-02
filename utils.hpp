
#pragma once
#include <asio.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chat {

// Big-endian 32-bit helpers
inline void write_u32_be(char* out, std::uint32_t v) {
    out[0] = static_cast<char>((v >> 24) & 0xFF);
    out[1] = static_cast<char>((v >> 16) & 0xFF);
    out[2] = static_cast<char>((v >> 8) & 0xFF);
    out[3] = static_cast<char>((v) & 0xFF);
}
inline std::uint32_t read_u32_be(const char* in) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(in[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[2])) << 8)  |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(in[3])));
}

// Awaitable read exactly n bytes
inline asio::awaitable<void> read_exact(asio::ip::tcp::socket& sock, void* data, std::size_t n) {
    std::size_t read = 0;
    while (read < n) {
        std::size_t r = co_await sock.async_read_some(asio::buffer(static_cast<char*>(data) + read, n - read), asio::use_awaitable);
        if (r == 0) {
            throw std::runtime_error("connection closed while reading");
        }
        read += r;
    }
    co_return;
}

// Awaitable write exactly n bytes
inline asio::awaitable<void> write_all(asio::ip::tcp::socket& sock, const void* data, std::size_t n) {
    std::size_t written = 0;
    while (written < n) {
        std::size_t w = co_await sock.async_write_some(asio::buffer(static_cast<const char*>(data) + written, n - written), asio::use_awaitable);
        written += w;
    }
    co_return;
}

// Read a length-prefixed frame (4-byte big-endian length, then payload)
inline asio::awaitable<std::string> read_frame(asio::ip::tcp::socket& sock) {
    char lenbuf[4];
    co_await read_exact(sock, lenbuf, 4);
    std::uint32_t len = read_u32_be(lenbuf);
    std::string payload;
    payload.resize(len);
    if (len > 0) {
        co_await read_exact(sock, payload.data(), len);
    }
    co_return payload;
}

// Write a length-prefixed frame
inline asio::awaitable<void> write_frame(asio::ip::tcp::socket& sock, std::string_view payload) {
    char lenbuf[4];
    write_u32_be(lenbuf, static_cast<std::uint32_t>(payload.size()));
    co_await write_all(sock, lenbuf, 4);
    if (!payload.empty()) {
        co_await write_all(sock, payload.data(), payload.size());
    }
    co_return;
}

} // namespace chat
