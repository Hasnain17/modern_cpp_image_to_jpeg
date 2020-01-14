//! Encodes images in a directory to jpeg.
//! If the first argument is a web-page, that web-page is crawled for image URLs, those are downloaded and then converted to jpeg.
//! Example usage:
//! modern_cpp_features https://create.stephan-brumme.com/toojpeg/ output
//! modern_cpp_features image_input_dir output

#include <iostream>
#include "toojpeg_17.h"
#include "http.h"
#include "image_loader.h"

#include <fstream>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <execution>
#include <string>

using std::cout;
using std::cerr;
using std::endl;
using namespace std::filesystem;

/**
 * If the given directory entry is a file, load it with {@link ImageLoader}, compute the jpeg and write it to disk with a ".new.jpg" suffix.
 * @param file
 */
void process_file(const directory_entry &file) {
    if (!file.is_regular_file()) return;

    // Load image and put it into a unique_ptr with a deleter (stbi_image_free) for RAII
    ImageLoader image{file.path().c_str()};

    auto file_path = file.path();
    auto filename_ = std::prev(file_path.end());
    std::string_view filename(filename_->native());

    if (utils::endsWith(filename, ".new.jpg")) { return; }

    file_path.concat(".new.jpg");

    if (exists(file_path)) {
        cout << "Already converted file skipped: " << file_path << endl;
        return;
    }

    if (image.is_valid()) {
        std::ofstream outfile(file_path,
                              std::ios_base::trunc | std::ios_base::out |
                              std::ios_base::binary);
        bool ok = TooJpeg17::writeJpeg<90>([&outfile](ByteView v) { outfile.write(v.data(), v.size_); }, *image,
                                           image.width, image.height, false, image.channels != 2,
                                           "TooJpeg17 converted image");
        if (ok) cout << "File converted: " << file_path << endl;
    }
}

/**
 * Given a url, the page will be downloaded and all supported images found in <img src=".."> tags
 * will be downloaded to the given output directory.
 *
 * @param url A URL
 * @param output An existing output directory
 * @return Returns true on success and false otherwise. May throw on unexpected IO errors.
 */
bool webpage_crawler(const Socket::Url &url, const path &output) {
    // Download given URL page
    std::stringstream page_stream{};
    auto res = Socket::writeHttpResponseTo(page_stream, url);
    if (!res) {
        cerr << "Failed to download page at given url " << url.full() << endl;
        return false;
    }

    // Perform regex search
    auto page = page_stream.str();
    static std::regex url_regex(R"(.*src=["']([^"']*?(?:jpg|png|bmp|gif|pnm|JPG|PNG|BMP|GIF|PNM))["'].*)");
    std::match_results<std::string::const_iterator> match;

    while (std::regex_search(page, match, url_regex)) {
        auto image_url = Socket::Url::from_relative(url, match[1].str());

        std::string file_name(image_url.path_);
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        utils::replace_all(file_name, "%2", "&");
        std::cout << "Downloading " << file_name << std::endl;

        auto result_file = output;
        result_file.append(file_name);
        auto tmp_file = result_file;
        tmp_file += ".tmp";
        std::ofstream outfile(tmp_file, std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
        res = Socket::writeHttpResponseTo(outfile, image_url);
        if (res) {
            rename(tmp_file, result_file);
        } else {
            remove(tmp_file);
            std::cerr << "\tFailed to download: " << url.full() << endl;
        }

        page = match.suffix();
    }
    return true;
}

int main(int argc, char *argv[]) {
    // Argument parsing
    if (argc < 2) {
        cerr << "No input directory provided" << endl;
        return 1;
    }
    bool is_url = Socket::Url::is_url(argv[1]);
    if (is_url && argc < 3) {
        cerr << "If first argument is a weg-page URL, the second argument must be the output directory!" << endl;
        return 1;
    }
    // Input is either the first argument (after argv[0] which refers to the binary name most of the time),
    // or if the first argument is a url, it is the second argument.
    const path input(is_url ? argv[2] : argv[1]);
    const path output(argv[argc > 2 ? 2 : 1]);

    if (!exists(input)) throw std::runtime_error("Input directory does not exist!");
    if (!exists(output) && !create_directories(output)) throw std::runtime_error("Output directory not writeable!");

    if (is_url) {
        Socket::Url url(argv[1]);
        if (!webpage_crawler(url, output)) return -1;
    }

    auto files = directory_iterator(input);
    stbi_set_flip_vertically_on_load(true);
    std::for_each(std::execution::par, std::filesystem::begin(files), std::filesystem::end(files), process_file);

    return 0;
}
