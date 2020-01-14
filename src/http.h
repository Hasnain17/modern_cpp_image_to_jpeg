/*!
 * Standalone HTTP Downloader with a simple URL parser, HTTP response parser, ByteView type and internal 16kb buffer.
 * Requires C++17: Uses fold expressions, std::array, std::byte, std::string_view.
 * No C-isms like C-casts have been used.
 *
 * Features:
 * * No memory allocations, uses a user callback for responses.
 * * Conditionally supports SSL via openSSL
 * Limitations:
 * * Uses exceptions for error reporting. C++ exceptions may allocate and will perform a costly stack unwind.
 * * Requires a well-formed HTTP-like response (eg requires a \n\r sequence to detect the http status line).
 *   The 16kb buffer size is the maximum for this impl to try to parse an HTTP header though.
 */
#include <stdexcept>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstdio>

#include <string>
#include <utility>
#include <vector>
#include <sstream>
#include <utility>
#include <cstddef>
#include <functional>
#include <string_view>
#include <algorithm>

// Socket/IP headers (linux/mac)
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <iomanip>
#include <netdb.h>

// strerror
#include <cstring>
#include <exception>

// C++17: Conditional include
#if __has_include(<openssl/ssl.h>)

#include <openssl/ssl.h>
#include <openssl/err.h>

#else
using SSL = void*;
#endif

#ifdef WITH_HTTPS
#undef WITH_HTTPS
#define WITH_HTTPS true
#else
#define WITH_HTTPS false
#endif

#include "http_header_parser.h"
#include "url.h"
#include "stream_utils.h"
#include "byte_view.h"

namespace Socket {
using namespace utils;

/**
 * HttpSocket allows to query a given URL via HTTP (and HTTPS via openSSL) and receive the response.
 *
 * Encapsulates the C-Socket POSIX API in a RAII fashion.
 * HttpSocket is not movable (buffer array is part of the type) and not copyable (socket fd).
 * For demonstrational purposes, boost_asio ip::tcp::socket is the more sophisticated choice.
 */
template<bool with_https>
class HttpSocket {
    int socketId_;
    SSL *cSSL_;
    Url url_;
    static constexpr int invalidSocketId = -1;
    static constexpr std::size_t bufferSize = 16096;
    std::array<std::byte, bufferSize> buffer_;
    std::string used_ip_;
public:
    HttpSocket(HttpSocket const &) = delete;

    HttpSocket &operator=(HttpSocket const &) = delete;

    HttpSocket &operator=(HttpSocket &&move) = delete;

    [[nodiscard]] int getSocketId() const { return socketId_; }

    /**
     * Create a new http socket. Parses the url and resolves the host to an IP addr.
     * @param url A valid URL.
     *
     * Exceptions: Throws if the url does not contain a host part, if the IP resolution failed
     * or connecting to the destination failed.
     */
    explicit HttpSocket(const Url &url)
            : socketId_(::socket(PF_INET, SOCK_STREAM, 0)), cSSL_(nullptr), url_(url), buffer_{} {

        // Receive timeout of 2 seconds
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(socketId_, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv);

        if (url_.host_.empty())
            throw std::runtime_error(from_parts("HttpSocket::", __func__, ": URL invalid"));
        auto host_str = std::string(url_.host_);
        const char *host = host_str.c_str();
        struct hostent *he = gethostbyname(host);

        if (he == nullptr) {
            switch (h_errno) {
                case NO_ADDRESS:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__, "The name is valid but it has no address",
                                       strerror(h_errno)));
                case NO_RECOVERY:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__,
                                       "A non-recoverable name server error occurred",
                                       strerror(h_errno)));
                case TRY_AGAIN:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__,
                                       "The name server is temporarily unavailable",
                                       strerror(h_errno)));
                case HOST_NOT_FOUND:
                default:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__, "The host was not found",
                                       strerror(h_errno)));
            }
        }

        auto ip_addr = (struct in_addr *) he->h_addr_list[0];
        auto ip_addr_str = inet_ntoa(*(ip_addr));
        used_ip_ = ip_addr_str;

        struct sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(url_.protocol_ == "https" ? 443 : 80);
        serverAddr.sin_addr.s_addr = ip_addr->s_addr;

        if (::connect(getSocketId(), (struct sockaddr *) &serverAddr, sizeof(serverAddr)) != 0) {
            close();
            throw std::runtime_error(from_parts("HttpSocket::", __func__, ": connect: ", strerror(errno)));
        }

        if (url_.protocol_ == "https") {
            if constexpr (with_https) {
                init_ssl();
                init_ssl_session();
            } else throw std::runtime_error("OpenSSL not compiled in, but https url requested");
        }
    }

    [[nodiscard]] HttpParsedResponse requestURL(HttpHeaderParser::Callback write_back) {
        std::stringstream msg;
        msg << "GET " << url_.path_and_query_ << " HTTP/1.1\r\n" << "host: " << url_.host_ << "\r\n"
            << "user-agent: Mozilla/5.0 (X11; Fedora; Linux x86_64)\r\n" << "accept: */*\r\n" << "\r\n";
        auto str = msg.str();
        putMessageData(str.data(), str.size());
        return receive(std::move(write_back));
    }

    ~HttpSocket() {
        // This object has been closed or moved.
        if (socketId_ == invalidSocketId) { return; }
        try { close(); }
        catch (...) {
        }
    }

    /**
     * Return the peer IP addr. Returns an empty string if IP resolving failed.
     */
    std::string_view remote_ip() {
        return used_ip_;
    }

private:
    /**
     * Reads via openSSL or the socket directly, depending on if this is a https or http connection.
     * This method is only available if https is enabled.
     */
    template<bool with_https_ = with_https, typename = std::enable_if_t<with_https_, std::size_t>>
    [[nodiscard]] std::size_t read_from_socket(std::enable_if_t<with_https_, std::size_t> dataRead) noexcept {
        if (cSSL_) {
            std::size_t r = 0;
            while (true) {
                // See https://www.openssl.org/docs/man1.0.2/man3/SSL_get_error.html. The read operation may have to be repeated.
                r = SSL_read(cSSL_, this->buffer_.data() + dataRead, this->buffer_.size() - dataRead);
                if (r < 0) {
                    int e = SSL_get_error(cSSL_, r);
                    if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) break;
                }
                break;
            }
            return r;
        }
        return read(getSocketId(), this->buffer_.data() + dataRead, this->buffer_.size() - dataRead);
    }

    /**
     * Reads via the stream socket directly.
     * This method is only available if http is not enabled.
     */
    template<bool with_https_ = with_https, typename = std::enable_if_t<!with_https_, std::size_t>>
    [[nodiscard]] std::size_t read_from_socket(std::size_t dataRead) noexcept {
        return read(getSocketId(), this->buffer_.data() + dataRead, this->buffer_.size() - dataRead);
    }

    template<bool with_https_ = with_https, typename = std::enable_if_t<with_https_, std::size_t>>
    void init_ssl_session() {
        SSL_CTX *ctx = nullptr;

        const SSL_METHOD *method = TLS_client_method();
        if (method == nullptr) throw std::runtime_error(from_parts("HttpSocket::SSL", strerror(errno)));

        ctx = SSL_CTX_new(method);
        if (ctx == nullptr) throw std::runtime_error(from_parts("HttpSocket::SSL", strerror(errno)));

        cSSL_ = SSL_new(ctx);
        SSL_set_fd(cSSL_, this->socketId_);
        int err_connect = SSL_connect(cSSL_);
        if (err_connect != 1) {
            int err = SSL_get_error(cSSL_, err_connect);
            if (err == SSL_ERROR_SYSCALL) {
                if (errno != 0)
                    throw std::runtime_error(
                            from_parts("HttpSocket::SSL::connect: ", strerror(errno)));
            }
            std::stringstream stringstream;
            char buffer[1024] = {};
            while (unsigned long err = ERR_get_error()) {
                ERR_error_string_n(err, reinterpret_cast<char *>(&buffer), sizeof(buffer));
                if (buffer[0]) stringstream << buffer << "\n";
            }
            throw std::runtime_error(from_parts("HttpSocket::SSL", stringstream.str()));
        }

        // std::cout << "SSL connection using " << SSL_get_cipher (cSSL_) << std::endl;
    }

    template<bool with_https_ = with_https, typename = std::enable_if_t<with_https_, std::size_t>>
    static void init_ssl() {
        SSL_library_init();
        OPENSSL_add_all_algorithms_noconf();
        SSL_load_error_strings();
    }

    /**
     * Receives data and calls the given callback method for each new, received chunk.
     * The http status line and headers are not handed to the callback, but parsed first.
     * @param write_back A method that handles received data.
     */
    inline HttpParsedResponse receive(HttpHeaderParser::Callback write_back) {
        if (socketId_ == 0) {
            throw std::logic_error(from_parts("HttpSocket::", __func__,
                                              ": accept called on a bad socket object"));
        }

        // Closes the write half of the TCP stream socket (if not using SSL)
        if (!cSSL_) {
            if (::shutdown(getSocketId(), SHUT_WR) != 0) {
                throw std::domain_error(
                        from_parts("HTTPProtocol::", __func__, ": shutdown: critical error: ", strerror(errno)));
            }
        }

        HttpHeaderParser parser;
        std::size_t dataRead = 0;
        while (dataRead < this->buffer_.size()) {
            // Append to the internal buffer until EOF or the buffer is full
            std::size_t get = read_from_socket(dataRead);
            if (get == static_cast<std::size_t>(-1)) {
                switch (errno) {
                    case 0:
                        break;
                    case EBADF:
                    case EFAULT:
                    case EINVAL:
                    case ENXIO: {
                        // Fatal error. Programming bug
                        throw std::domain_error(
                                from_parts("HttpSocket::", __func__, ": read: critical error: ",
                                           strerror(errno)));
                    }
                    case EIO:
                    case ENOBUFS:
                    case ENOMEM: {
                        // Resource acquisition failure or device error
                        throw std::runtime_error(
                                from_parts("HttpSocket::", __func__, ": read: resource failure: ",
                                           strerror(errno)));
                    }
                    case EINTR:
                        // TODO: Check for user interrupt flags.
                        //       Beyond the scope of this project
                        //       so continue normal operations.
                    case ETIMEDOUT:
                    case EAGAIN: {
                        // Temporary error.
                        // Simply retry the read.
                        continue;
                    }
                    case ECONNRESET:
                    case ENOTCONN: {
                        // Connection broken.
                        // Return the data we have available and exit
                        // as if the connection was closed correctly.
                        get = 0;
                        break;
                    }
                    default: {
                        throw std::runtime_error(
                                from_parts("HttpSocket::", __func__, ": read: returned -1:",
                                           strerror(errno)));
                    }
                }
            }
            if (get == 0) {
                break;
            }
            dataRead += get;
            if (parser.parse(this->buffer_.data(), dataRead, write_back)) {
                dataRead = 0;
            }
            if (parser.receive_done()) break;
            if (!parser.has_parsed() && this->buffer_.size() <= dataRead) {
                throw std::runtime_error(
                        from_parts("HttpSocket::", __func__,
                                   ": Did not receive HTTP status line and headers",
                                   strerror(errno)));
            }
        }
        return parser.parsed_header();
    }

    /**
     * Close socket connections. Write and read attempts will result in an exception.
     */
    void close() {
        if (socketId_ == invalidSocketId) { return; }
        /// Cpp17: If constexpr to selectively execute the openSSL shutdown sequence
        if constexpr (with_https) {
            if (cSSL_) {
                SSL_shutdown(cSSL_);
                cSSL_ = nullptr;
                socketId_ = invalidSocketId;
                return;
            }
        }
        while (true) {
            int state = ::close(socketId_);
            if (state == invalidSocketId) {
                break;
            }
            switch (errno) {
                case EBADF:
                    throw std::domain_error(
                            from_parts("HttpSocket::", __func__, ": close: EBADF:", socketId_, " ",
                                       strerror(errno)));
                case EIO:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__, ": close: EIO:", socketId_, " ",
                                       strerror(errno)));
                case EINTR: {
                    // Usually: Check for user interrupt flags.
                    break;
                }
                default:
                    throw std::runtime_error(
                            from_parts("HttpSocket::", __func__, ": close: ???:", socketId_, " ",
                                       strerror(errno)));
            }
        }
        socketId_ = invalidSocketId;
    }

    template<bool with_https_ = with_https, typename = std::enable_if_t<with_https_, std::size_t>>
    std::size_t
    write_to_socket(std::enable_if_t<with_https_, std::size_t> dataWritten, char const *buffer, std::size_t size) {
        return cSSL_ ?//
               SSL_write(cSSL_, buffer + dataWritten, size - dataWritten) :
               write(getSocketId(), buffer + dataWritten, size - dataWritten);
    }

    template<bool with_https_ = with_https, typename = std::enable_if_t<!with_https_, std::size_t>>
    std::size_t write_to_socket(std::size_t dataWritten, char const *buffer, std::size_t size) {
        return write(getSocketId(), buffer + dataWritten, size - dataWritten);
    }

    void putMessageData(char const *buffer, std::size_t size) {
        std::size_t dataWritten = 0;

        while (dataWritten < size) {
            std::size_t put = write_to_socket(dataWritten, buffer, size);
            if (put == static_cast<std::size_t>(-1)) {
                switch (errno) {
                    case EINVAL:
                    case EBADF:
                    case ECONNRESET:
                    case ENXIO:
                    case EPIPE: {
                        // Fatal error. Programming bug
                        throw std::domain_error(
                                from_parts("HttpSocket::", __func__, ": write: critical error: ",
                                           strerror(errno)));
                    }
                    case EDQUOT:
                    case EFBIG:
                    case EIO:
                    case ENETDOWN:
                    case ENETUNREACH:
                    case ENOSPC: {
                        // Resource acquisition failure or device error
                        throw std::runtime_error(
                                from_parts("HttpSocket::", __func__, ": write: resource failure: ",
                                           strerror(errno)));
                    }
                    case EINTR:
                        // TODO: Check for user interrupt flags.
                        //       Beyond the scope of this project
                        //       so continue normal operations.
                    case EAGAIN: {
                        // Temporary error.
                        // Simply retry the read.
                        continue;
                    }
                    default: {
                        throw std::runtime_error(
                                from_parts("HttpSocket::", __func__, ": write: returned -1: ",
                                           strerror(errno)));
                    }
                }
            }
            dataWritten += put;
        }
    }
};

/**
 * Downloads data from a http URL and writes it to the given output stream.
 *
 * @param stream Output stream for the received data
 * @param url A valid URL.
 * @return Returns an option resolving to the received size if successful and resolving to false otherwise.
 */
std::optional<std::size_t> writeHttpResponseTo(std::ostream &stream, const Url &url) {
    Socket::HttpSocket<WITH_HTTPS> connect(url);

    std::cout << "Resolved IP " << connect.remote_ip() << std::endl;

    std::string error_string;
    auto write_back = [&stream, &error_string](HttpParsedResponse header, ByteView data) {
        if (data.size_ == 0) return;
        if (header.status_code == 200) {
            // Print a little bit feedback (percentage of download progress)
            if (header.received_bytes < header.length)
                std::cout << std::round(header.received_bytes * 100 / header.length) << "% " << std::flush;
            stream.write(reinterpret_cast<const char *>(data.ptr_), data.size_);
        } else {
            error_string = std::string(reinterpret_cast<const char *>(data.ptr_), data.size_);
        }
    };

    auto parsed = connect.requestURL(write_back);

    if (parsed.status_code == 200) {
        std::cout << std::endl;
    } else {
        std::cerr << "Failed to GET http response " << parsed.status_code << " " << error_string.size() << " " << error_string << std::endl;
    }

    return parsed.received_bytes ? std::make_optional(parsed.received_bytes) : false;
}

}