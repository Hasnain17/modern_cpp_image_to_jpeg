#pragma once

#include <ostream>
#include <sstream>

namespace utils {

/// Print into stream using C++17 fold expression
template<typename... Args>
void print(std::ostream &out, Args &&... args) {
    ((out << ' ' << std::forward<Args>(args)), ...);
}

template<typename... Args>
std::string from_parts(Args const &... args) {
    std::stringstream msg;
    print(msg, args...);
    return msg.str();
}

// C++20 will have string_view::starts_with. Until then use this replacement.
constexpr bool startsWith(const std::string_view &str, const std::string_view &prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}


// C++20 will have string_view::ends_with. Until then use this replacement.
constexpr bool endsWith(const std::string_view &str, const std::string_view &suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

void replace_all(std::string &data, std::string_view toSearch, std::string_view replaceStr) {
    // Get the first occurrence
    size_t pos = data.find(toSearch);

    // Repeat till end is reached
    while (pos != std::string::npos) {
        // Replace this occurrence of Sub String
        data.replace(pos, toSearch.size(), replaceStr);
        // Get the next occurrence from the current position
        pos = data.find(toSearch, pos + replaceStr.size());
    }
}

}
