#pragma once

#include <streambuf>
#include <istream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <cstring>

namespace breeze::http {

// A streambuf that reads from a socket file descriptor using recv. It provides a
// readable std::istream interface so we can stream request bodies without
// buffering the entire request in memory.
class socket_streambuf : public std::streambuf {
public:
    explicit socket_streambuf(int fd, std::size_t buff_size = 8192) : fd_(fd), buffer_(buff_size) {
        setg(nullptr, nullptr, nullptr);
    }

    ~socket_streambuf() override {
        // Do not close the fd here; caller manages it
    }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        ssize_t n = recv(fd_, buffer_.data(), buffer_.size(), 0);
        if (n <= 0) return traits_type::eof();
        setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
        return traits_type::to_int_type(*gptr());
    }

private:
    int fd_;
    std::vector<char> buffer_;
};

// A simple istream wrapper around socket_streambuf.
class socket_istream : public std::istream {
public:
    explicit socket_istream(int fd) : std::istream(nullptr), buf_(fd) { rdbuf(&buf_); }
    ~socket_istream() override = default;
private:
    socket_streambuf buf_;
};

} // namespace breeze::http

