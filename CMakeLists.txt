cmake_minimum_required(VERSION 3.24)
project(mrbones_port LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(mbcore STATIC
    src/core/sha256.c
    src/core/cdimage.c
    src/core/identity.c
    src/platform/config.c)
target_include_directories(mbcore PUBLIC src)

add_executable(mbverify src/tools/mbverify.c)
target_link_libraries(mbverify mbcore)

enable_testing()
foreach(t test_sha256 test_cdimage test_config)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} mbcore)
    add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()
