#ifndef LZ4_DECOMPRESSOR_HPP
#define LZ4_DECOMPRESSOR_HPP

#include <string>
#include <vector>
#include <cstdint>

// マジックナンバー（"LZ4A"）
constexpr uint32_t LZ4_ARCHIVE_MAGIC = 0x41345A4C;  // "LZ4A" in little endian
constexpr uint32_t LZ4_ARCHIVE_VERSION = 1;

// メタデータ構造体
struct FileMetadata
{
    std::string filename;
    std::string extension;
    size_t originalSize;
    size_t dataOffset;  // 圧縮データ内でのオフセット
};

// メモリ上に展開されたファイルを表す構造体
struct FileEntry
{
    std::string name;       // 元のファイル名
    std::vector<char> data; // ファイルの中身（バイナリデータ）
};

/// LZ4アーカイブファイルを解凍してメモリ上に展開する関数
/// @param lz4FilePath: LZ4アーカイブファイルのパス
/// @return FileEntryのvector
std::vector<FileEntry> decompressLZ4Archive(const std::string& lz4FilePath);

#endif // LZ4_DECOMPRESSOR_HPP

