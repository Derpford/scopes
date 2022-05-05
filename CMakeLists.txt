cmake_minimum_required(VERSION 3.15)
project(scopes-lib VERSION 0.1.10)
option(USE_ASAN_UBSAN "Use address sanitizer" OFF)

if(MSVC)
  # This doesn't seem to line up to what vcpkg is expecting for some reason
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CPP_WARNINGS "-Wno-error=date-time -Wno-unused-but-set-variable -Wno-sign-compare -Wno-vla -Wno-enum-compare -Wno-comment -Wno-misleading-indentation -Wno-pragmas -Wno-variadic-macros -Wno-int-in-bool-context -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter")
else()
  set(CPP_WARNINGS "-Wno-error=date-time -Wno-nonnull-compare -Wno-unused-but-set-variable -Wno-sign-compare -Wno-vla -Wno-enum-compare -Wno-comment -Wno-misleading-indentation -Wno-pragmas -Wno-variadic-macros -Wno-int-in-bool-context -Wno-unused-variable -Wno-unused-function")
endif()
set(BIN_DIR "bin")

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(ASAN_USAN_OPTS "-fsanitize=address" "-fsanitize-address-use-after-scope" "-fno-omit-frame-pointer" "-fsanitize=undefined" "-fno-common")

add_subdirectory(src)
