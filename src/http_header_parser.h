#pragma once

#include "byte_view.h"
#include "stream_utils.h"

namespace Socket {
    using namespace utils;

    /**
     * Contains some useful information about the parsed http response.
     */
    struct HttpParsedResponse {
        int status_code = 0;
        std::size_t length = 0;
        std::size_t received_bytes = 0;
    };

    /**
     * Parses a block of incoming data for the HTTP response line and HTTP headers
     * and forwards everything after to a callback function.
     */
    class HttpHeaderParser {
        HttpParsedResponse parsed = {};

    public:
        using Callback = std::function<void(HttpParsedResponse, ByteView)>;

        /**
         * Parses an incoming buffer for the HTTP status line and headers if not done yet.
         * @param data
         * @param received_len
         * @param buffer_len
         * @return Returns true if the data has been consumed. Returns false if more data is necessary for parsing to succeed.
         */
        bool parse(std::byte *data, std::size_t received_len, Callback write_back) {
            if (parsed.status_code > 0) {
                parsed.received_bytes += received_len;
                write_back(parsed, ByteView(data, received_len));
                return true;
            } else {
                using namespace std;
                string_view all_received = string_view(reinterpret_cast<char *>(data), received_len);

                string_view::iterator line_end = find(all_received.begin(), all_received.end(), '\n');
                if (line_end == all_received.end()) return false;
                string_view first_line = all_received.substr(0, distance(all_received.begin(), line_end));


                char space1 = '\0';
                char space2 = '\0';
                char backslashR = '\0';
                int status_code = 0;
                char status_desc[1024];
                int count = std::sscanf(first_line.begin(), "HTTP/1.1%c%d%c%1023[^\r\n]%c",
                                        &space1,
                                        &status_code,
                                        &space2,
                                        status_desc,
                                        &backslashR);
                if (count != 5 || space1 != ' ' || space2 != ' ' || backslashR != '\r' || status_code < 100 ||
                    status_code >= 600) {
                    throw std::runtime_error(
                            from_parts("HttpParsedResponse::", __func__, ": Invalid HTTP Status Line:",
                                       " count(6)=", count,
                                       " space1(32)=", static_cast<int>(space1),
                                       " space2(32)=", static_cast<int>(space2),
                                       " backslashR(10)=", static_cast<int>(backslashR),
                                       " status_code=", status_code,
                                       "Line: >", std::string(first_line), "<"));
                }

                std::size_t content_length = 0;

                while (true) {
                    advance(line_end, 1);
                    string_view::iterator line_start = line_end;
                    std::size_t pos = distance(all_received.begin(), line_end);
                    line_end = find(line_end, all_received.end(), '\n');
                    if (line_end == all_received.end()) return false;
                    string_view next_line = all_received.substr(pos, distance(line_start, line_end));

                    // A very simple header-done condition: An empty line (two \r\n) indicates the end of the http header block.
                    if (next_line.size() <= 1) {
                        break;
                    }

                    if (find(next_line.begin(), next_line.end(), ':') == next_line.end()) {
                        throw std::runtime_error(
                                from_parts("HttpParsedResponse::", __func__, ": Header line missing colon(:)"));
                    }
                    if (std::sscanf(next_line.begin(), "Transfer-Encoding : identity%c", &backslashR) == 1 &&
                        backslashR == '\r') {
                        throw std::domain_error(
                                from_parts("HttpParsedResponse::", __func__, ": Identity encoding not supported"));
                    }
                    if (std::sscanf(next_line.begin(), "Content-Length : %lu%c", &content_length, &backslashR) == 2 &&
                        backslashR == '\r') {
                    }
                    if (std::sscanf(next_line.begin(), "Content-Type : multipart/byteranges%c", &backslashR) == 1 &&
                        backslashR == '\r') {
                        throw std::domain_error(
                                from_parts("HttpParsedResponse::", __func__, ": Mult-Part encoding not supported"));
                    }
                }

                // We require a content length header
                if (content_length == 0) {
                    throw std::domain_error(
                            from_parts("HttpParsedResponse::", __func__, ": Mult-Part encoding not supported"));
                }

                if (line_end != all_received.end()) advance(line_end, 1);
                auto consumed_len = distance(all_received.begin(), line_end);
                auto data_bytes_len = received_len - consumed_len;

                parsed = HttpParsedResponse{status_code, content_length, data_bytes_len};

                // The rest of the received data block is handed over to the callback
                write_back(parsed, ByteView(data + consumed_len, data_bytes_len));
                return true;
            }
        }

        /**
         * Returns true if the http status line and headers have been received.
         * All further data will be send straight to the callback.
         */
        bool has_parsed() {
            return parsed.status_code != 0;
        }

        /// Returns true if all bytes (according to the http header content-length) have been received
        bool receive_done() {
            return parsed.length == parsed.received_bytes;
        }

        HttpParsedResponse parsed_header() {
            return parsed;
        }
    };
}