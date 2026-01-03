#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <cstring>

namespace breeze::http {

struct UploadedFile {
    std::string filename;
    std::string content_type;
    std::string content; // binary-safe storage
};

// Parsers: helper static functions to parse HTTP bodies
class Parsers {
public:
    // Parse query string into map
    static std::unordered_map<std::string, std::string> parse_query(const std::string& qs) {
        std::unordered_map<std::string, std::string> out;
        std::istringstream stream(qs);
        std::string pair;
        while (std::getline(stream, pair, '&')) {
            size_t pos = pair.find('=');
            if (pos != std::string::npos) {
                out[url_decode(pair.substr(0, pos))] = url_decode(pair.substr(pos + 1));
            }
        }
        return out;
    }

    // URL decode (percent decode)
    static std::string url_decode(const std::string& s) {
        std::string out; out.reserve(s.size());
        auto hex = [](char ch)->int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '%' && i + 2 < s.size()) {
                int hi = hex(s[i+1]);
                int lo = hex(s[i+2]);
                if (hi >= 0 && lo >= 0) {
                    char decoded = static_cast<char>((hi << 4) | lo);
                    out.push_back(decoded);
                    i += 2;
                    continue;
                }
            }
            out.push_back(c);
        }
        return out;
    }

    // Simple form (application/x-www-form-urlencoded) parsing
    static std::unordered_map<std::string, std::string> parse_form_urlencoded(const std::string& body) {
        return parse_query(body);
    }

    // Parse multipart/form-data. This is a simple parser that supports file and field parts.
    // It is not a complete spec implementation but works for typical usages.
    static void parse_multipart(const std::string& body, const std::string& boundary,
                                std::unordered_map<std::string, std::string>& fields,
                                std::unordered_map<std::string, UploadedFile>& files) {
        std::string b = "--" + boundary;
        size_t pos = 0;
        while (true) {
            size_t start = body.find(b, pos);
            if (start == std::string::npos) break;
            start += b.size();
            if (body.size() > start && body[start] == '\r') start += 2; // skip CRLF
            size_t end = body.find(b, start);
            if (end == std::string::npos) break;
            std::string part = body.substr(start, end - start);

            // split headers and content
            size_t hdr_end = part.find("\r\n\r\n");
            if (hdr_end == std::string::npos) continue;
            std::string hdrs = part.substr(0, hdr_end);
            std::string content = part.substr(hdr_end + 4);
            // strip possible trailing CRLF
            if (!content.empty() && content.back() == '\n') {
                if (content.size() >= 2 && content[content.size()-2] == '\r') content.erase(content.end()-2, content.end());
                else content.pop_back();
            }

            std::istringstream hs(hdrs);
            std::string line;
            std::string name;
            std::string filename;
            std::string ctype;
            while (std::getline(hs, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string hname = line.substr(0, colon);
                std::string hval = line.substr(colon + 1);
                // trim
                auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); };
                auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); };
                ltrim(hval); rtrim(hval);
                if (hname == "Content-Disposition") {
                    // parse name and filename
                    size_t npos = hval.find("name=");
                    if (npos != std::string::npos) {
                        size_t q1 = hval.find('"', npos);
                        size_t q2 = hval.find('"', q1+1);
                        if (q1 != std::string::npos && q2 != std::string::npos) name = hval.substr(q1+1, q2-q1-1);
                    }
                    size_t fpos = hval.find("filename=");
                    if (fpos != std::string::npos) {
                        size_t q1 = hval.find('"', fpos);
                        size_t q2 = hval.find('"', q1+1);
                        if (q1 != std::string::npos && q2 != std::string::npos) filename = hval.substr(q1+1, q2-q1-1);
                    }
                } else if (hname == "Content-Type") {
                    ltrim(hval); rtrim(hval);
                    ctype = hval;
                }
            }

            if (!filename.empty()) {
                UploadedFile uf;
                uf.filename = filename;
                uf.content_type = ctype;
                uf.content = content;
                files[name] = std::move(uf);
            } else if (!name.empty()) {
                fields[name] = content;
            }

            pos = end;
        }
    }
};

} // namespace breeze::http

