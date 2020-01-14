//! A simple URL parser type
#pragma once

#include <string_view>
#include <algorithm>
#include <regex>
#include "stream_utils.h"

namespace Socket {
    /**
     * Represents a URL with its protocol, host, path and query components
     */
    struct Url {
        /**
         * Parse a url string into its components, accessible via the structs fields.
         *
         * An invalid url will throw.
         * @param url_s A URL like https://abc.org/path?query=abc
         */
        explicit Url(std::string_view url_s) { parse(url_s); }

        /**
         * Converts from relative to absolute url
         * @param absolute_url An absolute url that at least includes the protocol and host
         * @param url_s A relative part of an url like //host.domain/path or /path.
         * @return Returns an absolute URL or throws.
         */
        static Url from_relative(const Url &absolute_url, std::string_view url_s) {
            if (utils::startsWith(url_s, "http")) {
                return Url{url_s};
            } else if (utils::startsWith(url_s, "//")) {
                std::string url_str(absolute_url.protocol_);
                url_str += ":";
                url_str += url_s;
                return Url{url_str};
            } else if (utils::startsWith(url_s, "/")) {
                std::string url_str(absolute_url.protocol_);
                url_str += "://";
                url_str += absolute_url.host_;
                url_str += url_s;
                return Url{url_str};
            }
            throw std::runtime_error("Invalid URL!");
        }

        Url(Url &&move) = default;

        Url(const Url &cp) noexcept { parse(cp.url_); }

        Url &operator=(Url const &cp) noexcept {
            if (&cp == this) { return *this; }
            parse(cp.url_);
            return *this;
        }

        /// Returns true if the given string argument looks like a URL
        [[nodiscard]] static bool is_url(std::string_view url) {
            static std::regex url_regex(
                    R"(^(http|https):(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
                    std::regex::extended
            );
            std::match_results<std::string_view::const_iterator> match;
            return std::regex_match(url.cbegin(), url.cend(), match, url_regex);
        }

        [[nodiscard]] std::string_view full() const {
            return url_;
        }

    public:
        std::string_view protocol_, host_, path_, query_, path_and_query_;
    private:
        std::string url_;

        void parse(std::string_view url_s) {
            using namespace std;

            url_ = std::string(url_s);

            constexpr string_view prot_end("://");
            string::iterator prot_i = search(url_.begin(), url_.end(), prot_end.begin(), prot_end.end());
            if (prot_i == url_.end()) throw std::runtime_error("Invalid URL: protocol://host pattern required!");

            // A url protocol/host is lowercase
            transform(url_.begin(), prot_i, url_.begin(), std::ptr_fun<int, int>(tolower));
            protocol_ = string_view(url_).substr(0, distance(url_.begin(), prot_i));

            advance(prot_i, prot_end.length());
            string::iterator path_i = find(prot_i, url_.end(), '/');
            transform(prot_i, path_i, prot_i, std::ptr_fun<int, int>(tolower));
            host_ = string_view(url_).substr(distance(url_.begin(), prot_i), path_i - prot_i);

            string::iterator query_i = std::find(path_i, url_.end(), '?');
            path_ = string_view(url_).substr(distance(url_.begin(), path_i), query_i - path_i);

            path_and_query_ = string_view(url_).substr(distance(url_.begin(), path_i));

            if (query_i != url_.end()) ++query_i;
            query_ = string_view(url_).substr(distance(url_.begin(), query_i));
        }
    };
}