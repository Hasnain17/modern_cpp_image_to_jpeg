///! Small test harness. No C-ism used, but requires C++20 (C++17 experimental) std::source_location.
#include <string>
#include <stdexcept>
#include <experimental/source_location>

constexpr void ASSERT_THROW(bool condition, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if (!condition) {
        throw std::runtime_error(std::string(location.file_name())
                                 + std::string(":")
                                 + std::to_string(location.line())
                                 + std::string(" in ")
                                 + std::string(location.function_name())
        );
    }
}

// Would be simpler with C++20 concepts
template<typename X, typename Y>
constexpr void ASSERT_EQUAL(X x, Y y, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if constexpr(std::is_convertible_v<Y, std::string>) {
        if ((x) != (y)) {
            throw std::runtime_error(std::string(location.file_name())
                                     + std::string(":")
                                     + std::to_string(location.line())
                                     + std::string(" in ")
                                     + std::string(location.function_name())
                                     + std::string(": ")
                                     + std::string(x)
                                     + std::string(" != ")
                                     + std::string(y)
            );
        }
    }
    if constexpr(std::is_arithmetic_v<std::decay_t<Y>>) {
        if ((x) != (y)) {
            throw std::runtime_error(std::string(location.file_name())
                                     + std::string(":")
                                     + std::to_string(location.line())
                                     + std::string(" in ")
                                     + std::string(location.function_name())
                                     + std::string(": ")
                                     + std::to_string((x))
                                     + std::string(" != ")
                                     + std::to_string((y))
            );
        }
    }
}

template<typename T>
constexpr void BAIL(T msg, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    throw std::runtime_error(std::string(location.file_name())
                             + std::string(":")
                             + std::to_string(location.line())
                             + std::string(" in ")
                             + std::string(location.function_name())
                             + std::string(": ")
                             + std::string(msg)
    );
}