//! Benchmarks the Cpp17 variant of toojpeg with the original one

#include <iostream>
#include "toojpeg_17.h"
#include "http.h"

#include "toojpeg.h"

#include <fstream>
#include <filesystem>
#include <chrono>
#include <image_loader.h>

int main() {
    std::ios_base::sync_with_stdio(false);
    using std::cout;
    using std::endl;
    using namespace std::filesystem;
    using namespace std::chrono;
    using namespace std::chrono_literals;

    // Benchmark: Download benchmark file
    auto benchmark_file = current_path().append("world.jpg");
    if (!exists(benchmark_file)) {
        cout << "Download benchmark file" << endl;
        auto benchmark_file_tmp = current_path().append("world.jpg.tmp");
        std::ofstream benchmark_file_stream("world.jpg.tmp",
                                            std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);

        auto res = Socket::writeHttpResponseTo(benchmark_file_stream,
                                               Socket::Url(
                                                       "https://upload.wikimedia.org/wikipedia/commons/3/3d/Eckert4.jpg"));
        if (res) {
            rename(benchmark_file_tmp, benchmark_file);
        } else {
            remove(benchmark_file_tmp);
            std::cerr << "failed to download benchmark file" << endl;
        }
    }

    // Load benchmark file
    ImageLoader loader{benchmark_file.native().c_str()};
    if (!loader.is_valid()) {
        throw std::runtime_error("Failed to load benchmark image file");
    }

    // Convert with original (20x)
    {
        // Reserve output buffer capacity
        std::vector<char> output{};
        output.reserve(300'000);
        auto sum = 0ms;
        for (auto i = 0; i < 20; ++i) {
            output.clear();
            auto start = steady_clock::now();
            if (!TooJpeg::writeJpeg(
                    [&output](ByteView v) { output.insert(output.end(), &v.ptr_[0], &v.ptr_[v.size_]); }, *loader, 800,
                    600, true, 90, false,
                    "Benchmark image")) {
                throw std::runtime_error("Failed to benchmark original TooJpeg");
            }
            auto end = steady_clock::now();
            sum += duration_cast<milliseconds>(end - start);
        }
        cout << "Original TooJpeg : " << sum.count() << " ms. Bytes: " << output.size() << endl;
    }

    // Convert with toojpeg17 (20x)
    {
        std::vector<std::uint8_t> output{};
        output.reserve(300'000);
        auto sum = 0ms;
        for (auto i = 0; i < 20; ++i) {
            output.clear();
            auto start = steady_clock::now();
            if (!TooJpeg17::writeJpeg<90>(
                    [&output](ByteView v) { output.insert(output.end(), &v.ptr_[0], &v.ptr_[v.size_]); }, *loader,
                    800, 600, false, true, "Benchmark image")) {
                throw std::runtime_error("Failed to benchmark original TooJpeg");
            }
            auto end = steady_clock::now();
            sum += duration_cast<milliseconds>(end - start);
        }
        cout << "TooJpeg17 (Fixed Quality) : " << sum.count() << " ms. Bytes: " << output.size() << endl;
    }

    // Convert with toojpeg17 (20x)
    {
        std::vector<std::uint8_t> output{};
        output.reserve(300'000);
        auto sum = 0ms;
        for (auto i = 0; i < 20; ++i) {
            output.clear();
            auto start = steady_clock::now();
            if (!TooJpeg17::writeJpegQuality(
                    [&output](ByteView v) { output.insert(output.end(), &v.ptr_[0], &v.ptr_[v.size_]); }, *loader,
                    800, 600, false, true, 90, "Benchmark image")) {
                throw std::runtime_error("Failed to benchmark original TooJpeg");
            }
            auto end = steady_clock::now();
            sum += duration_cast<milliseconds>(end - start);
        }
        cout << "TooJpeg17 (Dynamic Quality): " << sum.count() << " ms. Bytes: " << output.size() << endl;
    }

    return 0;
}
