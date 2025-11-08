#ifndef TIFF_PROCESSOR_HPP
#define TIFF_PROCESSOR_HPP

#include "lz4_decompressor.hpp"
#include <vector>
#include <string>
#include <cstdint>

// TIFFヘッダー情報を保持するための構造体
struct TiffHeaderInfo
{
    // 基本的なタグ
    uint16_t compression;
    uint16_t photometric;
    uint16_t orientation;
    uint16_t planarConfig;

    // 解像度関連
    float xResolution;
    float yResolution;
    uint16_t resolutionUnit;

    // 日時・ソフトウェア情報
    std::string dateTime;
    std::string software;
    std::string description;
    std::string artist;
    std::string copyright;
    
    TiffHeaderInfo();
};

/// メモリ内のTIFFファイルを読み込み、グレースケール画像をfloat配列に変換する
bool readTiffFloat(const FileEntry &entry, std::vector<float> &image, 
                   uint32_t &width, uint32_t &height, TiffHeaderInfo &headerInfo);

/// 後方互換性のためのオーバーロード
bool readTiffFloat(const FileEntry &entry, std::vector<float> &image, 
                   uint32_t &width, uint32_t &height);

/// float配列の画像データを32ビット符号付き整数TIFF形式で出力
bool writeTiffInt32Aligned(const std::string &file,
                          const std::vector<float> &img,
                          uint32_t w, uint32_t h,
                          const TiffHeaderInfo &hdr);

/// 後方互換性のためのオーバーロード
bool writeTiffInt32(const std::string &filename, const std::vector<float> &image, 
                   uint32_t width, uint32_t height);

/// 元のTIFFヘッダーをコピーして画像データだけを置き換える関数
bool writeTiffInt32WithOriginalHeader(const std::string &filename,
                                     const std::vector<float> &image,
                                     uint32_t width, uint32_t height,
                                     const FileEntry &originalTiffEntry);

/// libtiffを使ったTIFFマージ処理
void mergeTiffFilesWithLibTiff(const std::vector<FileEntry> &entries, 
                              const std::string &prefix_with_run, 
                              const std::string &outputFolder, 
                              const int s_img, const int e_img, 
                              const int integ_frame_num);

/// メモリ上のTIFFファイルを直接出力する関数
void extractTiffFilesFromMemory(const std::vector<FileEntry> &entries, 
                               const std::string &outputFolder);

#endif // TIFF_PROCESSOR_HPP

