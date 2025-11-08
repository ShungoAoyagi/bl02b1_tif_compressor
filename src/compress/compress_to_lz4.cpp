#include "compress_to_lz4.hpp"
#include "../common/common.hpp"
#include <lz4.h>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cstring>

// マジックナンバー（"LZ4A"）
constexpr uint32_t LZ4_ARCHIVE_MAGIC = 0x41345A4C;  // "LZ4A" in little endian
constexpr uint32_t LZ4_ARCHIVE_VERSION = 1;

// ファイル読み込み結果を格納する構造体
struct FileReadResult
{
    std::string filepath;
    std::string data;
    size_t index;     // 元の順序を保持
    bool success;
};

// メタデータ構造体
struct FileMetadata
{
    std::string filename;
    std::string extension;
    size_t originalSize;
    size_t dataOffset;  // 圧縮データ内でのオフセット
};

// メタデータをバイナリデータにシリアライズする関数
static void serializeMetadata(const std::vector<FileMetadata>& metadata, std::string& output)
{
    // ヘッダー: マジックナンバー(4) + バージョン(4) + ファイル数(8)
    output.clear();
    
    // マジックナンバー
    output.append(reinterpret_cast<const char*>(&LZ4_ARCHIVE_MAGIC), sizeof(uint32_t));
    
    // バージョン
    output.append(reinterpret_cast<const char*>(&LZ4_ARCHIVE_VERSION), sizeof(uint32_t));
    
    // ファイル数
    uint64_t fileCount = metadata.size();
    output.append(reinterpret_cast<const char*>(&fileCount), sizeof(uint64_t));
    
    // 各ファイルのメタデータ
    for (const auto& meta : metadata)
    {
        // ファイル名の長さとファイル名
        uint32_t filenameLen = static_cast<uint32_t>(meta.filename.size());
        output.append(reinterpret_cast<const char*>(&filenameLen), sizeof(uint32_t));
        output.append(meta.filename);
        
        // 拡張子の長さと拡張子
        uint32_t extensionLen = static_cast<uint32_t>(meta.extension.size());
        output.append(reinterpret_cast<const char*>(&extensionLen), sizeof(uint32_t));
        output.append(meta.extension);
        
        // 元のファイルサイズ
        output.append(reinterpret_cast<const char*>(&meta.originalSize), sizeof(size_t));
        
        // データオフセット
        output.append(reinterpret_cast<const char*>(&meta.dataOffset), sizeof(size_t));
    }
}

// 並列でファイルを読み込む内部関数
static void readFileWorker(const std::vector<std::string>& filesToRead,
                          size_t startIdx,
                          size_t endIdx,
                          std::vector<FileReadResult>& results,
                          std::mutex& resultsMutex)
{
    for (size_t i = startIdx; i < endIdx; ++i)
    {
        FileReadResult result;
        result.filepath = filesToRead[i];
        result.index = i;
        result.success = false;

        try
        {
            std::ifstream file(filesToRead[i], std::ios::binary);
            if (file)
            {
                // ファイルサイズを取得
                file.seekg(0, std::ios::end);
                std::streamsize fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                // データを読み込む
                result.data.resize(fileSize);
                file.read(result.data.data(), fileSize);
                
                // 実際に読み込んだバイト数を確認（短い読み取り検出）
                std::streamsize bytesRead = file.gcount();
                if (bytesRead == fileSize)
                {
                    result.success = true;
                }
                else
                {
                    LOG("Warning: Short read on " << filesToRead[i] 
                        << " - expected " << fileSize << " bytes, got " << bytesRead << " bytes");
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG("Error reading file " << filesToRead[i] << ": " << e.what());
        }

        // 結果を追加（排他制御）
        {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results.push_back(result);
        }
    }
}

bool compressFilesToLZ4(const std::set<std::string>& files,
                        const std::string& outputPath,
                        int maxThreads,
                        int lz4Acceleration)
{
    if (files.empty())
    {
        LOG("Error: No files to compress");
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // ファイルリストをベクターに変換（並列処理のため）
    std::vector<std::string> fileList(files.begin(), files.end());
    
    // ---------- 並列でファイルを読み込む ----------
    std::vector<FileReadResult> readResults;
    std::mutex resultsMutex;
    std::vector<std::thread> threads;

    // ファイルを各スレッドに分配
    size_t filesPerThread = (fileList.size() + maxThreads - 1) / maxThreads;
    
    auto readStart = std::chrono::high_resolution_clock::now();

    for (int threadId = 0; threadId < maxThreads; ++threadId)
    {
        size_t startIdx = threadId * filesPerThread;
        size_t endIdx = std::min(startIdx + filesPerThread, fileList.size());

        if (startIdx >= fileList.size())
            break;

        threads.emplace_back(readFileWorker, std::ref(fileList), startIdx, endIdx, 
                           std::ref(readResults), std::ref(resultsMutex));
    }

    // 全スレッドの完了を待機
    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    auto readEnd = std::chrono::high_resolution_clock::now();
    auto readTime = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - readStart).count();

    // 読み込み結果を確認
    if (readResults.size() != fileList.size())
    {
        LOG("Error: Not all files were read successfully");
        return false;
    }

    for (const auto& result : readResults)
    {
        if (!result.success)
        {
            LOG("Error: Failed to read file: " << result.filepath);
            return false;
        }
    }

    // ---------- 読み込んだデータを元の順序で連結 ----------
    // インデックスでソート
    std::sort(readResults.begin(), readResults.end(),
              [](const FileReadResult& a, const FileReadResult& b) {
                  return a.index < b.index;
              });

    // メタデータを作成
    std::vector<FileMetadata> metadataList;
    size_t currentOffset = 0;
    
    for (const auto& result : readResults)
    {
        FileMetadata meta;
        
        // ファイルパスからファイル名と拡張子を取得
        fs::path filePath(result.filepath);
        meta.filename = filePath.filename().string();
        meta.extension = filePath.extension().string();
        meta.originalSize = result.data.size();
        meta.dataOffset = currentOffset;
        
        metadataList.push_back(meta);
        currentOffset += result.data.size();
    }

    // 総データサイズを計算
    size_t totalSize = 0;
    for (const auto& result : readResults)
    {
        totalSize += result.data.size();
    }

    // データを連結
    std::string combinedData;
    combinedData.reserve(totalSize);
    
    for (const auto& result : readResults)
    {
        combinedData.append(result.data);
    }

    // メモリ解放
    readResults.clear();
    readResults.shrink_to_fit();

    // ---------- LZ4圧縮 ----------
    auto compressStart = std::chrono::high_resolution_clock::now();

    // LZ4の最大圧縮サイズを取得
    int maxCompressedSize = LZ4_compressBound(static_cast<int>(combinedData.size()));
    if (maxCompressedSize <= 0)
    {
        LOG("Error: Invalid data size for LZ4 compression");
        return false;
    }

    std::vector<char> compressed(maxCompressedSize);
    
    // LZ4圧縮を実行（高速化パラメータを使用）
    int compressedSize = LZ4_compress_fast(
        combinedData.data(),
        compressed.data(),
        static_cast<int>(combinedData.size()),
        maxCompressedSize,
        lz4Acceleration
    );

    if (compressedSize <= 0)
    {
        LOG("Error: LZ4 compression failed");
        return false;
    }

    compressed.resize(compressedSize);

    auto compressEnd = std::chrono::high_resolution_clock::now();
    auto compressTime = std::chrono::duration_cast<std::chrono::milliseconds>(compressEnd - compressStart).count();
    
    double compressionRatio = (double)compressedSize / totalSize * 100.0;

    // 元データのメモリ解放
    combinedData.clear();
    combinedData.shrink_to_fit();

    // ---------- メタデータをシリアライズ ----------
    std::string serializedMetadata;
    serializeMetadata(metadataList, serializedMetadata);
    
    // メタデータのサイズ
    uint64_t metadataSize = serializedMetadata.size();

    // ---------- 出力ファイルに書き込む ----------
    try
    {
        // 出力ディレクトリが存在しない場合は作成
        fs::create_directories(fs::path(outputPath).parent_path());

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            LOG("Error: Cannot open output file: " << outputPath);
            return false;
        }

        // 1. メタデータのサイズを書き込む（8バイト）
        outFile.write(reinterpret_cast<const char*>(&metadataSize), sizeof(uint64_t));
        
        // 2. メタデータを書き込む
        outFile.write(serializedMetadata.data(), serializedMetadata.size());
        
        // 3. 圧縮されたデータサイズを書き込む（8バイト）
        uint64_t compressedDataSize = compressedSize;
        outFile.write(reinterpret_cast<const char*>(&compressedDataSize), sizeof(uint64_t));
        
        // 4. 圧縮データを書き込む
        outFile.write(compressed.data(), compressedSize);
        
        outFile.close();

        // ファイルが正しく書き込まれたか確認
        if (!fs::exists(outputPath))
        {
            LOG("Error: Output file was not created: " << outputPath);
            return false;
        }

        auto expectedSize = sizeof(uint64_t) + metadataSize + sizeof(uint64_t) + compressedSize;
        auto actualSize = fs::file_size(outputPath);
        if (actualSize != expectedSize)
        {
            LOG("Error: Output file size mismatch. Expected: " << expectedSize 
                << ", Actual: " << actualSize);
            return false;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        return true;
    }
    catch (const std::exception& e)
    {
        LOG("Error writing output file: " << e.what());
        return false;
    }
}

