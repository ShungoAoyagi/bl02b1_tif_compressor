#include "file_index.hpp"
#include "../common/common.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <map>
#include <chrono>

// file_time_type を int64_t（ミリ秒単位のエポック時刻）に変換
int64_t MemoryMappedFileIndex::fileTimeToInt64(const fs::file_time_type &ftime)
{
    // file_time_typeをsystem_clockに変換してエポック秒を取得
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
}

// int64_t（ミリ秒単位のエポック時刻）を file_time_type に変換
fs::file_time_type MemoryMappedFileIndex::int64ToFileTime(int64_t timestamp)
{
    auto sysTime = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp));
    auto fileTime = fs::file_time_type::clock::now() + 
                   (sysTime - std::chrono::system_clock::now());
    return fileTime;
}

MemoryMappedFileIndex::MemoryMappedFileIndex(const std::string &basePath, int setSize)
    : indexFilePath(basePath + "/.file_index.bin"), modified(false), setSize(setSize)
{
    loadIndex();
}

MemoryMappedFileIndex::~MemoryMappedFileIndex()
{
    if (modified)
    {
        saveIndex();
    }
}

TaskKey MemoryMappedFileIndex::calculateTaskKey(int run, int fileNumber) const
{
    TaskKey key;
    key.run = run;
    key.setNumber = ((fileNumber - 1) / setSize) * setSize + 1;
    return key;
}

void MemoryMappedFileIndex::addFile(const std::string &path, int run, int fileNumber,
                                    const fs::file_time_type &modTime, bool isProcessed)
{
    // TaskKeyを計算
    TaskKey taskKey = calculateTaskKey(run, fileNumber);
    
    // pathKeyMapを更新
    pathKeyMap[path] = taskKey;
    
    // fileModTimeMapを更新
    fileModTimeMap[path] = fileTimeToInt64(modTime);
    
    // FileSetを取得または作成
    FileSet &fileSet = fileSetMap[taskKey];
    fileSet.run = taskKey.run;
    fileSet.setNumber = taskKey.setNumber;
    fileSet.processed = isProcessed;
    
    // ファイルを追加
    fileSet.files.insert(path);
    
    // firstFileを設定（setNumberと同じファイル番号）
    if (fileNumber == taskKey.setNumber)
    {
        fileSet.firstFile = path;
    }

    modified = true;
}

bool MemoryMappedFileIndex::hasFileChanged(const std::string &path, const fs::file_time_type &currentModTime)
{
    auto it = fileModTimeMap.find(path);
    if (it != fileModTimeMap.end())
    {
        // インデックス内に存在すれば、更新時刻を比較
        int64_t currentTime = fileTimeToInt64(currentModTime);
        return it->second != currentTime;
    }
    // インデックスに存在しなければ、変更あり（新規ファイル）
    return true;
}

void MemoryMappedFileIndex::markProcessed(const std::string &path, bool processed)
{
    // pathKeyMapからTaskKeyを取得
    auto keyIt = pathKeyMap.find(path);
    if (keyIt != pathKeyMap.end())
    {
        // FileSetのprocessedフラグを更新
        auto setIt = fileSetMap.find(keyIt->second);
        if (setIt != fileSetMap.end())
        {
            setIt->second.processed = processed;
            modified = true;
        }
    }
}

void MemoryMappedFileIndex::markFileSetProcessed(const TaskKey &taskKey, bool processed)
{
    auto it = fileSetMap.find(taskKey);
    if (it != fileSetMap.end())
    {
        it->second.processed = processed;
        modified = true;
    }
}

std::vector<FileSet> MemoryMappedFileIndex::getAllFileSets(bool includeProcessed)
{
    std::vector<FileSet> result;
    
    for (const auto &pair : fileSetMap)
    {
        const FileSet &fileSet = pair.second;
        
        // 処理済みのセットをスキップするオプション
        if (!includeProcessed && fileSet.processed)
            continue;
        
        result.push_back(fileSet);
    }
    
    return result;
}

bool MemoryMappedFileIndex::getFileSet(const TaskKey &taskKey, FileSet &outFileSet)
{
    // O(log N) または O(1) でFileSetを取得
    auto it = fileSetMap.find(taskKey);
    if (it != fileSetMap.end())
    {
        outFileSet = it->second;
        return true;
    }
    return false;
}

void MemoryMappedFileIndex::clear()
{
    fileSetMap.clear();
    pathKeyMap.clear();
    fileModTimeMap.clear();
    modified = true;
}

void MemoryMappedFileIndex::cleanup()
{
    size_t initialSize = fileModTimeMap.size();
    
    std::vector<std::string> pathsToRemove;
    
    // 存在しないファイルを検出
    for (const auto &pair : fileModTimeMap)
    {
        if (!fs::exists(pair.first))
        {
            pathsToRemove.push_back(pair.first);
        }
    }
    
    // 削除処理
    for (const std::string &path : pathsToRemove)
    {
        // pathKeyMapから削除
        auto keyIt = pathKeyMap.find(path);
        if (keyIt != pathKeyMap.end())
        {
            TaskKey taskKey = keyIt->second;
            pathKeyMap.erase(keyIt);
            
            // FileSetからファイルを削除
            auto setIt = fileSetMap.find(taskKey);
            if (setIt != fileSetMap.end())
            {
                setIt->second.files.erase(path);
                
                // FileSetが空になったら削除
                if (setIt->second.files.empty())
                {
                    fileSetMap.erase(setIt);
                }
            }
        }
        
        // fileModTimeMapから削除
        fileModTimeMap.erase(path);
    }
    
    if (initialSize != fileModTimeMap.size())
    {
        modified = true;
    }
}

size_t MemoryMappedFileIndex::size() const
{
    return fileModTimeMap.size();
}

void MemoryMappedFileIndex::loadIndex()
{
    std::ifstream file(indexFilePath, std::ios::binary);
    if (!file)
        return;

    try
    {
        // セット数を読み込み
        uint32_t numSets = 0;
        file.read(reinterpret_cast<char *>(&numSets), sizeof(numSets));
        if (!file)
            return;
        
        // 各セットを読み込み
        for (uint32_t i = 0; i < numSets; ++i)
        {
            // TaskKeyを読み込み
            TaskKey taskKey;
            file.read(reinterpret_cast<char *>(&taskKey.run), sizeof(taskKey.run));
            file.read(reinterpret_cast<char *>(&taskKey.setNumber), sizeof(taskKey.setNumber));
            
            // processedフラグを読み込み
            bool processed;
            file.read(reinterpret_cast<char *>(&processed), sizeof(processed));
            
            // ファイル数を読み込み
            uint32_t numFiles = 0;
            file.read(reinterpret_cast<char *>(&numFiles), sizeof(numFiles));
            
            // FileSetを作成
            FileSet fileSet;
            fileSet.run = taskKey.run;
            fileSet.setNumber = taskKey.setNumber;
            fileSet.processed = processed;
            
            // 各ファイルを読み込み
            for (uint32_t j = 0; j < numFiles; ++j)
            {
                FileEntry entry;
                file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
                
                std::string path(entry.filePath);
                fileSet.files.insert(path);
                
                // pathKeyMapとfileModTimeMapを更新
                pathKeyMap[path] = taskKey;
                fileModTimeMap[path] = entry.lastModifiedTime;
                
                // firstFileを設定（セットの最初のファイル）
                if (fileSet.firstFile.empty())
                {
                    fileSet.firstFile = path;
                }
            }
            
            // fileSetMapに追加
            fileSetMap[taskKey] = fileSet;
        }
    }
    catch (const std::exception &e)
    {
        LOG("Error loading index: " << e.what());
        // エラー時はクリア
        clear();
    }
}

void MemoryMappedFileIndex::saveIndex()
{
    std::ofstream file(indexFilePath, std::ios::binary);
    if (!file)
    {
        LOG("Failed to save index file");
        return;
    }

    try
    {
        // セット数を書き込み
        uint32_t numSets = static_cast<uint32_t>(fileSetMap.size());
        file.write(reinterpret_cast<const char *>(&numSets), sizeof(numSets));
        
        // 各セットを書き込み
        for (const auto &setPair : fileSetMap)
        {
            const TaskKey &taskKey = setPair.first;
            const FileSet &fileSet = setPair.second;
            
            // TaskKeyを書き込み
            file.write(reinterpret_cast<const char *>(&taskKey.run), sizeof(taskKey.run));
            file.write(reinterpret_cast<const char *>(&taskKey.setNumber), sizeof(taskKey.setNumber));
            
            // processedフラグを書き込み
            file.write(reinterpret_cast<const char *>(&fileSet.processed), sizeof(fileSet.processed));
            
            // ファイル数を書き込み
            uint32_t numFiles = static_cast<uint32_t>(fileSet.files.size());
            file.write(reinterpret_cast<const char *>(&numFiles), sizeof(numFiles));
            
            // 各ファイルを書き込み
            for (const std::string &path : fileSet.files)
            {
                FileEntry entry;
                strncpy(entry.filePath, path.c_str(), sizeof(entry.filePath) - 1);
                entry.filePath[sizeof(entry.filePath) - 1] = '\0';
                
                // fileModTimeMapから更新時刻を取得
                auto timeIt = fileModTimeMap.find(path);
                if (timeIt != fileModTimeMap.end())
                {
                    entry.lastModifiedTime = timeIt->second;
                }
                else
                {
                    entry.lastModifiedTime = 0;
                }
                
                file.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG("Error saving index: " << e.what());
    }
}

