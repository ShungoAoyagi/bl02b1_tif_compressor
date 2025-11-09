#include "lz4_decompressor.hpp"
#include "../common/common.hpp"
#include <lz4.h>
#include <fstream>
#include <iostream>
#include <cstring>

// メタデータをデシリアライズする関数
static bool deserializeMetadata(const char* data, size_t dataSize, std::vector<FileMetadata>& metadata)
{
    size_t offset = 0;
    
    // マジックナンバーを確認
    if (dataSize < sizeof(uint32_t))
    {
        std::cerr << "Error: Invalid metadata size" << std::endl;
        return false;
    }
    
    uint32_t magic;
    std::memcpy(&magic, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    if (magic != LZ4_ARCHIVE_MAGIC)
    {
        std::cerr << "Error: Invalid magic number" << std::endl;
        return false;
    }
    
    // バージョンを確認
    if (dataSize < offset + sizeof(uint32_t))
    {
        std::cerr << "Error: Invalid metadata size" << std::endl;
        return false;
    }
    
    uint32_t version;
    std::memcpy(&version, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    if (version != LZ4_ARCHIVE_VERSION)
    {
        std::cerr << "Error: Unsupported version" << std::endl;
        return false;
    }
    
    // ファイル数を取得
    if (dataSize < offset + sizeof(uint64_t))
    {
        std::cerr << "Error: Invalid metadata size" << std::endl;
        return false;
    }
    
    uint64_t fileCount;
    std::memcpy(&fileCount, data + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // 各ファイルのメタデータを読み込む
    for (uint64_t i = 0; i < fileCount; ++i)
    {
        FileMetadata meta;
        
        // ファイル名の長さとファイル名
        if (dataSize < offset + sizeof(uint32_t))
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        uint32_t filenameLen;
        std::memcpy(&filenameLen, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        if (dataSize < offset + filenameLen)
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        meta.filename = std::string(data + offset, filenameLen);
        offset += filenameLen;
        
        // 拡張子の長さと拡張子
        if (dataSize < offset + sizeof(uint32_t))
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        uint32_t extensionLen;
        std::memcpy(&extensionLen, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        if (dataSize < offset + extensionLen)
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        meta.extension = std::string(data + offset, extensionLen);
        offset += extensionLen;
        
        // 元のファイルサイズ
        if (dataSize < offset + sizeof(size_t))
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        std::memcpy(&meta.originalSize, data + offset, sizeof(size_t));
        offset += sizeof(size_t);
        
        // データオフセット
        if (dataSize < offset + sizeof(size_t))
        {
            std::cerr << "Error: Invalid metadata size" << std::endl;
            return false;
        }
        
        std::memcpy(&meta.dataOffset, data + offset, sizeof(size_t));
        offset += sizeof(size_t);
        
        metadata.push_back(meta);
    }
    
    return true;
}

std::vector<FileEntry> decompressLZ4Archive(const std::string& lz4FilePath)
{
    std::vector<FileEntry> entries;
    
    try
    {
        // 1. ファイルを開く
        std::ifstream inFile(lz4FilePath, std::ios::binary);
        if (!inFile)
        {
            std::cerr << "Error: Cannot open file: " << lz4FilePath << std::endl;
            return entries;
        }
        
        // 2. メタデータのサイズを読み込む
        uint64_t metadataSize;
        inFile.read(reinterpret_cast<char*>(&metadataSize), sizeof(uint64_t));
        if (!inFile)
        {
            std::cerr << "Error: Failed to read metadata size" << std::endl;
            return entries;
        }
        
        // 3. メタデータを読み込む
        std::vector<char> metadataBuffer(metadataSize);
        inFile.read(metadataBuffer.data(), metadataSize);
        if (!inFile)
        {
            std::cerr << "Error: Failed to read metadata" << std::endl;
            return entries;
        }
        
        // 4. メタデータをデシリアライズ
        std::vector<FileMetadata> metadata;
        if (!deserializeMetadata(metadataBuffer.data(), metadataSize, metadata))
        {
            std::cerr << "Error: Failed to deserialize metadata" << std::endl;
            return entries;
        }
        
        // 5. 圧縮データのサイズを読み込む
        uint64_t compressedSize;
        inFile.read(reinterpret_cast<char*>(&compressedSize), sizeof(uint64_t));
        if (!inFile)
        {
            std::cerr << "Error: Failed to read compressed data size" << std::endl;
            return entries;
        }
        
        // 6. 圧縮データを読み込む
        std::vector<char> compressedData(compressedSize);
        inFile.read(compressedData.data(), compressedSize);
        if (!inFile)
        {
            std::cerr << "Error: Failed to read compressed data" << std::endl;
            return entries;
        }
        
        inFile.close();
        
        // 7. 解凍後のデータサイズを計算
        size_t totalUncompressedSize = 0;
        for (const auto& meta : metadata)
        {
            totalUncompressedSize += meta.originalSize;
        }
        
        // 8. LZ4解凍
        std::vector<char> uncompressedData(totalUncompressedSize);
        int decompressedSize = LZ4_decompress_safe(
            compressedData.data(),
            uncompressedData.data(),
            static_cast<int>(compressedSize),
            static_cast<int>(totalUncompressedSize)
        );
        
        if (decompressedSize < 0)
        {
            std::cerr << "Error: LZ4 decompression failed" << std::endl;
            return entries;
        }
        
        if (static_cast<size_t>(decompressedSize) != totalUncompressedSize)
        {
            std::cerr << "Error: Decompressed size mismatch" << std::endl;
            return entries;
        }
        
        // 9. 各ファイルのデータを分割してFileEntryに格納
        for (const auto& meta : metadata)
        {
            FileEntry entry;
            entry.name = meta.filename;
            entry.data.resize(meta.originalSize);
            
            std::memcpy(entry.data.data(), 
                       uncompressedData.data() + meta.dataOffset, 
                       meta.originalSize);
            
            entries.push_back(std::move(entry));
        }
        
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        entries.clear();
    }
    
    return entries;
}

