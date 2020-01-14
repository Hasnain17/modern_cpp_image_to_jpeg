cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)

add_executable( test_url test_url.cpp )
target_include_directories(test_url PUBLIC ../src ../src/original)
target_compile_options(test_url PRIVATE -Wall -Wextra)

add_test( basic_test test_url 0 )
add_test( invalid_url test_url 1 )
add_test( url_move test_url 2 )
add_test( is_url test_url 3 )
add_test( test_crawler_regex test_url 4 )

add_executable( test_jpeg test_jpeg_out.cpp ../src/toojpeg_17.cpp )
target_include_directories(test_jpeg PUBLIC ../src ../src/original)
target_compile_options(test_jpeg PRIVATE -Wall -Wextra)

add_test( jpeg_out_color test_jpeg 0 )
add_test( jpeg_out_gray test_jpeg 1 )
