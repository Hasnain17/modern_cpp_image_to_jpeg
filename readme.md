# Modern C++17 (and some C++14) features in practise

This project shows off a few of the new core language additions as well as std enhancements
and is a tool to auto-convert all recognised image files in a given directory or on a web-page to jpg files.

Implemented are a C++17 version of the toojpeg library (~600 LOC), an http 'socket' (~600 LOC), a webpage-crawler (~100 LOC).
Find benchmark results at the end of this document.

## Build

The buildsystem is based on [CMake](https://cmake.org/download/) (>3.1), no further dependencies are required.
openSSL is optional and if found, allows for https URLs to be crawled for images.

Build with: `mkdir build && cd build && cmake ../ && make` on Unix systems.

## Run

To load and convert png, gif, jpg, bmp files in a directory, add the input directory as first argument
and the output directory optionally as second argument:

```shell script
./image_to_jpeg ./dir_with_images ./out
```

If input and output directory are the same, all jpeg encoded files will be stored with a `.new.jpg` extension.

The first argument can also be a URL. The page will be downloaded and all images referenced within an `<img src="..">` tag
will be downloaded and jpeg encoded. The default quality is 90.

```shell script
./image_to_jpeg https://en.wikipedia.org/wiki/Wikipedia:Manual_of_Style/Images ./out
```
## Tests

Tests are stored in the `tests/` directory. Build them with `cmake` and `make tests`.
A small C++17 featured test harness has been written in tests/tests.h to avoid external dependencies.

## Documentation

The code is documented Doxygen-compatible. Build the documentation via `cmake` and `make doc`.
A prebuild documentation is checked-in at `doc/html`.

## C-legacy

Throughout this project C-legacy has been avoided as much as possible.
This includes old-style casts, C-Posix API (except the net Socket API) and the C preprocessor (macros).

For example:
* `std::array<uint8_t,8*8>` instead of `uint8_t data[8*8]`
* `std::experimental::source_location` instead of `__LINE__`, `__FILE` etc
* `constexpr` instead of macros and `if constexpr ()` for conditional compilations

C-Libraries like the stb_image library have been properly C++ type wrapped (see `src/image_loader.h`) to take advantage of RAII etc.

## C++17 version of the toojpeg library

The original toojpeg only uses C++11 `auto` keyword and is apart from that pretty much C++03.
Its code size, many precomputed lookup tables and numeric values and the IO related domain
makes it a good candidate to experiment with C++17 features.

#### Modern Application Programming Interfaces

Having an API that cannot (un)intentionally being misused while offering an efficient interface is essential.
Ideally the API expresses the developers intention via the C++ type system in regard to input arguments lifetime
and ownership. 

C++17 added the `nodiscard` attribute so that the compilers warn if return values are not used. `noexcept`
allows the compiler to avoid generating stack-unwinding code for functions that will never throw,
which results in better runtime performance if exceptions are enabled:

```c++
[[nodiscard]] const char * data() const noexcept {/*...*/}
```

Smart pointers are used to express ownership:

```c++
explicit BitWriter(std::unique_ptr<std::ostream> &&output_) : output(std::move(output_)) {}
```

While in a non-owning situation, raw pointers and custom (wrapper) types like `ByteView` (a pointer + size) are the correct choice:

```c++
BitWriter &operator<<(ByteView data) { output->write(data.data(), data.size()); return *this; }

bool writeJpeg(..., const uint8_t *pixels, ...) {/*...*/}
```

The new `std::byte` type (C++17) has been used, instead of (unsigned) `char`s.
To quote cppreference:
> "std::byte is a distinct type that implements the concept of byte as specified in the C++ language definition."

To accommodate the fact that `std::byte` cannot simply (implicitly) being created via a integral number,
a (constexpr) user-defined literal (C++14) helps out (usage: `0xff_bn`):

```c++
constexpr std::byte operator "" _bn(unsigned long long v) { return std::byte(v); }
```

And finally new composed data types like `std::optional`, `std::any`, `std::variant` (and the existing `std::tuple`)
and automatic destructuring ("Structured binding declaration") of those (C++17) allow for richer return types:

```c++
constexpr auto scaled_luminance_chrominance(...) -> std::tuple<std::array<...>, std::array<...>> {
    /* ... */
    return std::make_tuple(scaledLuminance, scaledChrominance);
}

/// Structured binding declaration
auto [scaledLuminance, scaledChrominance] = scaled_luminance_chrominance(...);
``` 

#### Compile-time precompute with `constexpr`

It is not unusual to be in the situation of deciding within space vs time tradeoff bounds.
A C++ developer up to C++17 may have favoured runtime computations or "magical constants",
just because it is more convenient than writing an external generator tool and include that
and the results in a buildsystem.

With C++17 `constexpr` got much more expressive (temporary state like inline variables are supported) 
and it will be extended even more with C++20 (const-boundary aware heap usage).

In this project in multiple occasions `constexpr` was used, eg to eliminate "magical numbers":

```c++
// Before
const auto SqrtHalfSqrt = 1.306562965f;
// After
constexpr double SqrtHalfSqrt = sqrt((2 + sqrt(2)) / 2);
```

C++ does not yet offer `constexpr` math (an RFC exists),
but implementing the approximation for the square root for example is done in just a few lines:
```c++
double constexpr sqrt(double x) { return sqrt_helper(x, x, 0); }
double constexpr sqrt_helper(double x, double curr, double prev) {
    return is_close(curr, prev) ? curr : sqrt_helper(x, 0.5 * (curr + x / curr), curr);
}
constexpr auto is_close(T a, T b) -> bool {
    return std::abs(a - b) <= std::numeric_limits<T>::epsilon() * std::abs(a + b)
           || std::abs(a - b) < std::numeric_limits<T>::min();
}
```

Especially useful is `constexpr` to pre-compute lookup tables for example the huffman table
(see `constexpr std::array<BitCode, 256> generateHuffmanTable(const uint8_t numCodes[16], const uint8_t *values)` in `toojpeg_17.hpp`),
and quantisation tables (in ` constexpr auto quant_table(const std::array<uint8_t, 8 * 8> defaults) -> std::array<std::byte, 8 * 8>`).

This allows to use the exact math writen in the original papers (often modulo, division) as we do not need to care about
runtime penalties.

```c++
// Before (faster but not obvious substitutions)
auto row = ZigZagInv[i] >> 3;
auto column = ZigZagInv[i] & 7;
// After
auto row = ZigZagInv[i] / 8;
auto column = ZigZagInv[i] % 8;
```

## Http / TCP Socket

A tiny, blocking http tcp socket type has been implemented for this tool,
based on the Posix socket and network C-API. As with all C-wrapping types,
RAII is build on for resource management (socket closing, resource freeing),
which also works with the error handling strategy (exceptions).

> IMO: As soon as `std::expected` is part of C++ (http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0323r3.pdf),
> I will instantly swap out runtime costly exception usage. See also examples in https://github.com/TartanLlama/expected.
 
Although the type can be conditionally compiled
with and without SSL support (via openSSL), the implementation avoids massive
usage of `#ifdef`s.

Instead C++17's `__has_include` for conditional header including
and type traits in combination with `std::enable_if` (and a bit of SFINAE) have been used.
```c++
/// with ssl
template<bool with_https_ = with_https, typename = std::enable_if_t<with_https_, std::size_t>>
[[nodiscard]] std::size_t read_from_socket(std::enable_if_t<with_https_, std::size_t> dataRead) {
    return cSSL_ ? // Runtime decision if this is an SSL socket
           SSL_read(cSSL_, this->buffer_.data() + dataRead, this->buffer_.size() - dataRead) :
           read(getSocketId(), this->buffer_.data() + dataRead, this->buffer_.size() - dataRead);
}

// without ssl
template<bool with_https_ = with_https, typename = std::enable_if_t<!with_https_, std::size_t>>
[[nodiscard]] std::size_t read_from_socket(std::size_t dataRead) {
    return read(getSocketId(), this->buffer_.data() + dataRead, this->buffer_.size() - dataRead);
}

// Conditionally decide what to do, no #ifdef required anymore
if constexpr (with_https) {
    ...
}
```

## Extended C++ std: Regex, Filesystem, Parallel Algorithms

The filesystem submodule is a massive addition to C++17 and The Standard Library.

This project uses `std::filesystem` for simple operations like retrieving the current path, removing a file, checking
if a file exists, and for more elaborated tasks like enumerating all files, not directories, of a certain directory.
Even that is just a few lines of code:
```c++
using namespace std::filesystem;
const path input(argv[1]);
auto files = directory_iterator(input);
std::for_each(std::execution::par, std::filesystem::begin(files), std::filesystem::end(files), process_file);
```

The c++11 `std::regex` (and implicitly the C++17 `std::basic_regex` deduction guide) has been used for
the webpage image url crawler, found in `main.cpp::webpage_crawler`. `std::regex` is part of C++11 already,
but gained a few more convenience methods. The basic usage pattern in this project is:

```c++
std::string page = "...";
static std::regex url_regex(R"(.*src=["']([^"']*?(?:jpg|png|bmp|gif|pnm|JPG|PNG|BMP|GIF|PNM))["'].*)");
std::match_results<std::string::const_iterator> match;

while (std::regex_search(page, match, url_regex)) {
    auto image_url = Socket::Url::from_relative(url, match[1].str());
    page = match.suffix();
}
```

The parallel algorithm support of C++17's std has been used to read, compute and output multiple files in parallel:
```c++
#include <algorithm>
#include <execution>
std::for_each(std::execution::par, std::filesystem::begin(files), std::filesystem::end(files), process_file);
```

As this is not a memory bound operation, but mostly an IO one, this speeds up file processing.

`std::execution::par` is one of four (three in C++17) specified strategies and does not give guarantees in respect to the sequence
("invocations executing in the same thread are indeterminately sequenced") or the execution threads
("...are permitted to execute in either the invoking thread or in a thread implicitly created by the library").

## Benchmark

A benchmark binary downloads and loads a 2d-projected picture of the earth
(https://upload.wikimedia.org/wikipedia/commons/3/3d/Eckert4.jpg, 1.6MB, Creative Commons License)
and writes it with the original and re-implemented toojpeg library. 

Build the benchmark (found in `src/benchmark/main.cpp) via `cmake` and `make benchmark`.
Run it with `./benchmark` in the build directory. Each routine is run 20 times and the time is summed up.
No warm up happens, but invoking the benchmark multiple times shows similar numbers on my Core i7 8th Gen.

```
Original TooJpeg : 201 ms. Bytes: 275324
TooJpeg17 : 247 ms. Bytes: 275365
```

The output byte counts are different because the jpeg comment differs.
(The test suite makes sure that breaking changes to the code result in failing integration tests.)
No data is actually written to disk.

I have expected a performance gain, by pre-computing the lookup tables and got caught surprised by the number.

Using `std::array` comes with its own implications like an implicit full copy-by-value,
when passed by value in contrast to a C-Array. This results in much more copy operations
and function arguments cannot be simply passed in CPU registers but require the stack.
This can be observed in the given runtime penalty. To mitigate this, some argument-by-value's have
to be references/pointers instead.

A small benchmark suite, integrated into CI, helps to find negative impacting changes.
I have failed to set that up more early in the process.

## Acknowledgements

Used external libraries and header-only libraries:

* openSSL for https (optional)
* [`stb_image.h`](https://github.com/nothings/stb) for loading images (in src/vendor/sb_image.h).
* [PicoSHA2](https://github.com/okdshin/PicoSHA2) for the integration tests (in src/vendor/sha2.h).
* [TooJpeg](https://create.stephan-brumme.com/toojpeg/) the original jpeg encoding library (code can also be found in src/benchmark/toojpeg.*).

---

David Gräff, 2020 