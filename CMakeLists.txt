cmake_minimum_required(VERSION 3.21)
project(workdesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ================= 引入第三方库 =================

# 1. zlib
add_subdirectory(third_party/zlib zlib_build)

# 2. libzip
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(LIBZIP_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBZIP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libzip libzip_build)

# 3. pugixml
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/pugixml pugixml_build)

# 4. Aspose.PDF
add_subdirectory(third_party/aspose_pdf aspose_pdf_build)

# ================= Qt 配置 =================
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Sql Network)
qt_standard_project_setup()

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)