#pragma once

#include <streambuf>
#include <istream>
#include <string>
#include <sstream>
#include <vector>

namespace breeze::http {

// Streambuf that reads chunked-encoded data from an underlying std::istream
// and presents the decoded bytes to callers.
class chunked_streambuf : public std::streambuf {
public:
    explicit chunked_streambuf(std::istream& src, std::size_t buff_size = 8192)
        : src_(src), buffer_(buff_size), eof_(false) {
        setg(nullptr, nullptr, nullptr);
    }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (eof_) return traits_type::eof();

        // If no current chunk, read chunk size line
        if (remaining_in_chunk_ == 0) {
            std::string line;
            if (!std::getline(src_, line)) { eof_ = true; return traits_type::eof(); }
            // Trim CRLF
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Parse hex length (may include extensions after ';')
            std::istringstream ss;
            size_t semipos = line.find(';');
            std::string hexpart = (semipos == std::string::npos) ? line : line.substr(0, semipos);
            ss.str(hexpart);
            ss >> std::hex >> remaining_in_chunk_;
            if (remaining_in_chunk_ == 0) {
                // read and discard trailing headers until empty line
                while (std::getline(src_, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) break;
                }
                eof_ = true;
                return traits_type::eof();
            }
        }

        // Read up to buffer size or remaining_in_chunk_
        std::size_t toread = std::min(buffer_.size(), remaining_in_chunk_);
        src_.read(buffer_.data(), static_cast<std::streamsize>(toread));
        std::streamsize n = src_.gcount();
        if (n <= 0) { eof_ = true; return traits_type::eof(); }
        remaining_in_chunk_ -= static_cast<size_t>(n);
        // If we've consumed the chunk, consume the trailing CRLF
        if (remaining_in_chunk_ == 0) {
            // consume CRLF after chunk
            char cr = 0; char lf = 0;
            src_.get(cr);
            if (cr == '\r') src_.get(lf);
            else if (cr == '\n') {
                // already LF; fine
            } else {
                // unexpected; ignore
            }
        }

        setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
        return traits_type::to_int_type(*gptr());
    }

private:
    std::istream& src_;
    std::vector<char> buffer_;
    size_t remaining_in_chunk_ = 0;
    bool eof_ = false;
};

class chunked_istream : public std::istream {
public:
    explicit chunked_istream(std::istream& src) : std::istream(nullptr), buf_(src) { rdbuf(&buf_); }
private:
    chunked_streambuf buf_;
};

} // namespace breeze::http

