cmake_minimum_required(VERSION 3.10)
project(bl02b1_tif_compressor)

# C++17 を必須とする設定
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows環境での完全静的リンク設定
if(WIN32)
    if(MINGW)
        # MinGWの場合の完全静的リンク
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -static")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -Wl,-Bstatic")
    endif()
endif()

# snappyとlz4をスタティックライブラリとしてビルド
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

find_package(Threads REQUIRED)

# LZ4ライブラリ
set(LZ4_BUILD_CLI OFF CACHE BOOL "Build lz4 program" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "Build lz4c program" FORCE)
add_subdirectory(lz4/build/cmake)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lz4/lib)

# Snappyライブラリ
add_subdirectory(snappy)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/snappy)

# libtiffライブラリ
add_subdirectory(tiff)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tiff/libtiff)

# srcディレクトリのソースファイル
set(SRC_COMMON_FILES
    src/common/common.cpp
)

set(SRC_COMPRESS_FILES
    src/compress/compress_to_lz4.cpp
    src/compress/compress_to_snappy.cpp
    src/compress/file_set.cpp
    src/compress/fast_delete_queue.cpp
    src/compress/file_processor.cpp
    src/compress/file_index.cpp
    src/compress/directory_monitor.cpp
)

set(SRC_DECOMPRESS_FILES
    src/decompress/lz4_decompressor.cpp
    src/decompress/tiff_processor.cpp
    src/decompress/rename_finf.cpp
)

# 実行ファイルの作成（圧縮プログラム）
add_executable(bl02b1_tif_compressor main.cpp ${SRC_COMMON_FILES} ${SRC_COMPRESS_FILES})

# ライブラリのリンク
target_link_libraries(bl02b1_tif_compressor lz4_static snappy Threads::Threads)
target_include_directories(bl02b1_tif_compressor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 解凍プログラムの作成
add_executable(bl02b1_tif_decompressor decompress.cpp ${SRC_COMMON_FILES} ${SRC_DECOMPRESS_FILES})

# 解凍プログラムのライブラリリンク
target_link_libraries(bl02b1_tif_decompressor lz4_static tiff Threads::Threads)
target_include_directories(bl02b1_tif_decompressor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# インストール先の設定（オプション）
install(TARGETS bl02b1_tif_compressor bl02b1_tif_decompressor DESTINATION bin)

