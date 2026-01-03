#pragma once

#include "breeze/http/parsers.hpp"
#include <istream>
#include <string>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace breeze::http {

// Streaming multipart parser that reads from a std::istream and writes file parts
// to disk using a provided upload directory. It calls a callback for each field
// (name, value) and for each file part with the saved UploadedFile metadata.
class MultipartStreamParser {
 public:
     using FieldCallback = std::function<void(const std::string&, const std::string&)>;
     using FileCallback  = std::function<void(const std::string&, const UploadedFile&)>;

    enum class Result {
        Ok,
        RequestTooLarge,
        FileTooLarge,
        InvalidExtension,
        InvalidContentType,
        ScanRejected,
        WriteError,
        ParseError
    };

    // Add limits and validation callbacks
    MultipartStreamParser(std::istream& in, std::string boundary, std::string upload_dir,
                         size_t max_request_size = 0, size_t max_file_size = 0,
                         std::vector<std::string> allowed_exts = {},
                         std::vector<std::string> allowed_types = {},
                         std::function<bool(const std::string&, const UploadedFile&)> scan_cb = nullptr)
        : in_(in), boundary_("--" + std::move(boundary)), upload_dir_(std::move(upload_dir)),
          max_request_size_(max_request_size), max_file_size_(max_file_size),
          allowed_exts_(std::move(allowed_exts)), allowed_types_(std::move(allowed_types)), scan_cb_(std::move(scan_cb)) {}

    // Parse stream: this will sequentially read parts and call callbacks.
    // Returns detailed Result code.
    Result parse(const FieldCallback& on_field, const FileCallback& on_file) {
         std::string line;
         size_t total_bytes = 0;
         // Read until first boundary
         while (std::getline(in_, line)) {
            trim_crlf(line);
            if (line == boundary_) break;
         }

        while (true) {
            // Read headers of part
            std::unordered_map<std::string, std::string> headers;
            while (std::getline(in_, line)) {
                trim_crlf(line);
                if (line.empty()) break; // end headers
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string name = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                trim_whitespace(val);
                headers[name] = val;
            }

            if (headers.empty()) break; // no more parts

            // parse content-disposition
            std::string disposition = headers.count("Content-Disposition") ? headers["Content-Disposition"] : headers["content-disposition"];
            std::string name = extract_attribute(disposition, "name");
            std::string filename = extract_attribute(disposition, "filename");
            std::string ctype = headers.count("Content-Type") ? headers["Content-Type"] : headers["content-type"];

            if (!filename.empty()) {
                // file part: stream content until boundary into a temp file
                std::string safe_name = sanitize_filename(filename);
                std::string tmp_path = upload_dir_ + "/" + std::to_string(std::hash<std::string>{}(safe_name)) + "_" + safe_name;
                std::ofstream ofs(tmp_path, std::ios::binary);
                if (!ofs) {
                    // skip content if can't write
                    skip_part();
                } else {
                    size_t file_bytes = 0;
                    // read content lines until boundary
                    while (std::getline(in_, line)) {
                        // check for boundary line which starts with '--' + boundary suffix
                        if (!line.empty() && (line.rfind(boundary_, 0) == 0)) {
                            trim_crlf(line);
                            break; // boundary found
                        }
                        // write line with newline preserved
                        ofs << line << "\n";
                        file_bytes += line.size() + 1;
                        total_bytes += line.size() + 1;
                        // Per-file size check
                        if (max_file_size_ > 0 && file_bytes > max_file_size_) {
                            ofs.close();
                            // delete partial file
                            ::remove(tmp_path.c_str());
                            return Result::FileTooLarge;
                        }
                        // Per-request size check
                        if (max_request_size_ > 0 && total_bytes > max_request_size_) {
                            ofs.close();
                            ::remove(tmp_path.c_str());
                            return Result::RequestTooLarge;
                        }
                    }
                    ofs.close();
                    UploadedFile uf;
                    uf.filename = filename;
                    uf.content_type = ctype;
                    uf.path = tmp_path;
                    uf.content.clear();

                    // Validate extension if configured
                    if (!allowed_exts_.empty()) {
                        auto ext = get_extension(filename);
                        if (!ext.empty()) {
                            bool ok = false;
                            for (auto &ae : allowed_exts_) if (ext == ae) { ok = true; break; }
                            if (!ok) { ::remove(tmp_path.c_str()); return Result::InvalidExtension; }
                        } else { ::remove(tmp_path.c_str()); return Result::InvalidExtension; }
                    }
                    // Validate content-type if configured
                    if (!allowed_types_.empty() && !ctype.empty()) {
                        bool ok = false;
                        for (auto &at : allowed_types_) if (ctype.find(at) != std::string::npos) { ok = true; break; }
                        if (!ok) { ::remove(tmp_path.c_str()); return Result::InvalidContentType; }
                    }

                    // Optional scan callback (e.g., virus scanner)
                    if (scan_cb_) {
                        if (!scan_cb_(tmp_path, uf)) { ::remove(tmp_path.c_str()); return Result::ScanRejected; }
                    }

                    on_file(name, uf);
                    // ensure boundary is aligned
                    if (in_ && line.rfind(boundary_, 0) != 0) {
                        while (std::getline(in_, line)) { if (line.rfind(boundary_,0) == 0) break; }
                    }
                }
            } else {
                // form field: read lines until boundary and collect value
                std::ostringstream val;
                while (std::getline(in_, line)) {
                    if (!line.empty() && (line.rfind(boundary_, 0) == 0)) {
                        break;
                    }
                    trim_crlf(line);
                    val << line;
                    // preserve single newlines in field values
                    if (!in_.eof()) val << "\n";
                }
                std::string value = val.str();
                // trim trailing newline added
                if (!value.empty() && value.back() == '\n') value.pop_back();
                total_bytes += value.size();
                if (max_request_size_ > 0 && total_bytes > max_request_size_) return Result::RequestTooLarge;
                on_field(name, value);
                // if the last read line isn't boundary, attempt to continue
                if (in_ && line.rfind(boundary_, 0) != 0) {
                    while (std::getline(in_, line)) { if (line.rfind(boundary_,0) == 0) break; }
                }
            }

            // check for closing boundary
            if (!in_) break;
            std::streampos pos = in_.tellg();
            if (!std::getline(in_, line)) break;
            trim_crlf(line);
            if (line == (boundary_ + "--")) break; // end of multipart
            else if (line == boundary_) continue; // next part
            else {
                // rewind one line
                in_.seekg(pos);
            }
        }
        return Result::Ok;
    }

private:
    static std::string get_extension(const std::string& fn) {
        auto pos = fn.find_last_of('.');
        if (pos == std::string::npos) return "";
        std::string ext = fn.substr(pos + 1);
        // lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
    std::istream& in_;
    std::string boundary_;
    std::string upload_dir_;
    size_t max_request_size_ = 0;
    size_t max_file_size_ = 0;
    std::vector<std::string> allowed_exts_;
    std::vector<std::string> allowed_types_;
    std::function<bool(const std::string&, const UploadedFile&)> scan_cb_;

    static void trim_crlf(std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    }
    static void trim_whitespace(std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    }
    static std::string extract_attribute(const std::string& header_val, const std::string& attr) {
        size_t pos = header_val.find(attr + "=");
        if (pos == std::string::npos) return "";
        size_t q1 = header_val.find('"', pos);
        if (q1 == std::string::npos) return "";
        size_t q2 = header_val.find('"', q1+1);
        if (q2 == std::string::npos) return "";
        return header_val.substr(q1+1, q2-q1-1);
    }
    static std::string sanitize_filename(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (std::isalnum((unsigned char)c) || c=='_' || c=='-' || c=='.') out.push_back(c);
            else out.push_back('_');
        }
        return out;
    }

    void skip_part() {
        std::string line;
        while (std::getline(in_, line)) {
            if (!line.empty() && line.rfind(boundary_, 0) == 0) break;
        }
    }
};

} // namespace breeze::http

