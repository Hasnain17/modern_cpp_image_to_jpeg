//! A real test suite would use GTest etc
#include "tests.h"
#include "url.h"

#include <regex>
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace Socket;

void testBasic() {
    double foo = 2.0;
    double bar = 1.0;

    ASSERT_THROW(foo != bar);
    ASSERT_EQUAL(foo, 2.0);
    ASSERT_EQUAL(bar, 1.0);
}

void testInvalid() {
    try {
        Url url("");
        BAIL("Url parsing should have thrown");
    } catch (...) {
    }
}

void testMove() {
    Url url("https://domain.org");
    Url def = std::move(url);
    ASSERT_EQUAL(def.protocol_, "https");
    ASSERT_EQUAL(def.host_, "domain.org");
}

void testIsUrl() {
    ASSERT_EQUAL(Url::is_url("https://domain.org"), true);
    ASSERT_EQUAL(Url::is_url("some_string"), false);
}

void testRegex() {
    const std::string_view page = R"(\n<img alt="Flag of Japan.svg" src="//upload.wikimedia.org/wikipedia/en/thumb/9/9e/Flag_of_Japan.svg/30px-Flag_of_Japan.svg.png" decoding="async"\n)";
    static const std::regex url_regex(R"(src=["']([^"']*?(?:jpg|png|bmp|gif|pnm|JPG|PNG|BMP|GIF|PNM))["'])");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(page.cbegin(), page.cend(), match, url_regex)) {
        BAIL("Regex does not match");
    }

    Url absolute_url{"https://upload.wikipedia.org"};
    ASSERT_THROW(match.length() > 2);
    auto image_url = Socket::Url::from_relative(absolute_url, match[1].str());
    ASSERT_EQUAL("https://upload.wikimedia.org/wikipedia/en/thumb/9/9e/Flag_of_Japan.svg/30px-Flag_of_Japan.svg.png",
                 image_url.full());

    auto filename = std::filesystem::current_path().parent_path().parent_path().append(
            "tests/webcrawling_testpage.txt");
    std::fstream file(filename, std::ios::binary | std::ios::in);
    std::istream_iterator<char> start(file), end;
    std::string crawled_page_test_data(start, end);

    std::match_results<std::string::const_iterator> str_match;
    while (std::regex_search(crawled_page_test_data, str_match, url_regex)) {
        ASSERT_THROW(str_match.length() > 2);
        auto image_url = Socket::Url::from_relative(absolute_url, str_match[1].str());

        std::string file_name;
        file_name.reserve(image_url.path_.size());
        std::transform(image_url.path_.cbegin() + 1, image_url.path_.cend(), std::back_inserter(file_name),
                       [](const char c) { return (c == '/') ? '_' : c; });
        std::cout << file_name << std::endl;

        crawled_page_test_data = str_match.suffix();
    }
}

int main(int argc, char *argv[]) {
    ASSERT_EQUAL(argc, 2);
    switch (argv[1][0]) {
        case '0':
            testBasic();
            break;
        case '1':
            testInvalid();
            break;
        case '2':
            testMove();
            break;
        case '3':
            testIsUrl();
            break;
        case '4':
            testRegex();
            break;
    }
}