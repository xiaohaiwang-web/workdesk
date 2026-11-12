cmake_minimum_required(VERSION 3.21)
project(workdesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 在编译第三方库前，先关闭 Qt 的自动工具，避免报一堆烦人的警告
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

# ================= 引入第三方库 =================

# 1. llama.cpp
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/llama.cpp llama_build)

# 2. zlib
add_subdirectory(third_party/zlib zlib_build)

# 3. libzip (依赖刚引入的 zlib)
# 核心修复：强制关闭 libzip 的 install 配置，防止依赖冲突
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

# 4. pugixml
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/pugixml pugixml_build)

# 5. Aspose.PDF
add_subdirectory(third_party/aspose_pdf aspose_pdf_build)

# ================= Qt 配置 =================
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Sql Network)
qt_standard_project_setup()

# 恢复我们主程序需要的 Qt 自动工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)