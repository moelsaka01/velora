cmake_minimum_required(VERSION 3.25)
project(Velora LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

enable_testing()

add_executable(smoke_test tests/smoke_test.cpp)
target_include_directories(smoke_test PRIVATE src)
add_test(NAME smoke_test COMMAND smoke_test)
