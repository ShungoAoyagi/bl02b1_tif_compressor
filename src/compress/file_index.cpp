#include "file_index.hpp"
#include "../common/common.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <map>

MemoryMappedFileIndex::MemoryMappedFileIndex(const std::string &basePath)
    : indexFilePath(basePath + "/.file_index.bin"), modified(false)
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

void MemoryMappedFileIndex::addFile(const std::string &path, int run, int fileNumber,
                                    const fs::file_time_type &modTime, bool isProcessed)
{
    // パスマップでエントリを検索
    auto it = pathToIndexMap.find(path);

    if (it != pathToIndexMap.end())
    {
        // 既存のエントリを更新
        auto &entry = entries[it->second];
        entry.run = run;
        entry.fileNumber = fileNumber;
        entry.lastModified = modTime;
        entry.processed = isProcessed;
    }
    else
    {
        // 新しいエントリを追加
        IndexEntry newEntry;
        newEntry.run = run;
        newEntry.fileNumber = fileNumber;
        strncpy(newEntry.filePath, path.c_str(), sizeof(newEntry.filePath) - 1);
        newEntry.filePath[sizeof(newEntry.filePath) - 1] = '\0';
        newEntry.lastModified = modTime;
        newEntry.processed = isProcessed;

        // エントリを追加
        size_t index = entries.size();
        entries.push_back(newEntry);
        pathToIndexMap[path] = index;
    }

    modified = true;
}

bool MemoryMappedFileIndex::hasFileChanged(const std::string &path, const fs::file_time_type &currentModTime)
{
    auto it = pathToIndexMap.find(path);
    if (it != pathToIndexMap.end())
    {
        // インデックス内に存在すれば、更新時刻を比較
        return entries[it->second].lastModified != currentModTime;
    }
    // インデックスに存在しなければ、変更あり（新規ファイル）
    return true;
}

void MemoryMappedFileIndex::markProcessed(const std::string &path, bool processed)
{
    auto it = pathToIndexMap.find(path);
    if (it != pathToIndexMap.end())
    {
        entries[it->second].processed = processed;
        modified = true;
    }
}

void MemoryMappedFileIndex::markFileSetProcessed(int run, int setNumber, int setSize, bool processed)
{
    for (auto &entry : entries)
    {
        if (entry.run == run &&
            entry.fileNumber >= setNumber &&
            entry.fileNumber < setNumber + setSize)
        {
            entry.processed = processed;
            modified = true;
        }
    }
}

std::string MemoryMappedFileIndex::findFilePath(int run, int fileNumber)
{
    for (const auto &entry : entries)
    {
        if (entry.run == run && entry.fileNumber == fileNumber)
        {
            return entry.filePath;
        }
    }
    return "";
}

std::vector<FileSet> MemoryMappedFileIndex::getAllFileSets(int setSize, bool includeProcessed)
{
    std::map<std::pair<int, int>, FileSet> fileSets;

    for (const auto &entry : entries)
    {
        // 処理済みのファイルをスキップするオプション
        if (!includeProcessed && entry.processed)
            continue;

        int setNumber = ((entry.fileNumber - 1) / setSize) * setSize + 1;
        auto key = std::make_pair(entry.run, setNumber);

        if (fileSets.find(key) == fileSets.end())
        {
            FileSet newSet;
            newSet.run = entry.run;
            newSet.setNumber = setNumber;
            fileSets[key] = newSet;
        }

        fileSets[key].files.insert(entry.filePath);

        if (entry.fileNumber == setNumber)
        {
            fileSets[key].firstFile = entry.filePath;
        }
    }

    std::vector<FileSet> result;
    for (auto &pair : fileSets)
    {
        result.push_back(pair.second);
    }

    return result;
}

bool MemoryMappedFileIndex::getNextCompleteFileSet(int setSize, FileSet &outFileSet)
{
    std::map<std::pair<int, int>, FileSet> fileSets;

    // 未処理のファイルのみを対象にファイルセットを構築
    for (const auto &entry : entries)
    {
        // 処理済みのファイルはスキップ
        if (entry.processed)
            continue;

        int setNumber = ((entry.fileNumber - 1) / setSize) * setSize + 1;
        auto key = std::make_pair(entry.run, setNumber);

        if (fileSets.find(key) == fileSets.end())
        {
            FileSet newSet;
            newSet.run = entry.run;
            newSet.setNumber = setNumber;
            fileSets[key] = newSet;
        }

        fileSets[key].files.insert(entry.filePath);

        if (entry.fileNumber == setNumber)
        {
            fileSets[key].firstFile = entry.filePath;
        }
    }

    // 完全なセット（ファイル数がsetSize個）を優先度順に探す
    // 優先順位: run番号の小さい順 → setNumber の小さい順
    for (const auto &pair : fileSets)
    {
        const FileSet &fileSet = pair.second;
        if (fileSet.files.size() >= static_cast<size_t>(setSize))
        {
            // 完全なセットを見つけた
            outFileSet = fileSet;
            return true;
        }
    }

    // 完全なセットが見つからなかった
    return false;
}

void MemoryMappedFileIndex::clear()
{
    entries.clear();
    pathToIndexMap.clear();
    modified = true;
}

void MemoryMappedFileIndex::cleanup()
{
    size_t initialSize = entries.size();

    std::vector<size_t> indicesToRemove;

    for (size_t i = 0; i < entries.size(); i++)
    {
        if (!fs::exists(entries[i].filePath))
        {
            indicesToRemove.push_back(i);
        }
    }

    // 降順でインデックスを削除（昇順だとインデックスがずれる）
    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<size_t>());

    for (size_t idx : indicesToRemove)
    {
        // パスマップから削除
        pathToIndexMap.erase(entries[idx].filePath);

        // 最後の要素と交換して削除（高速）
        if (idx < entries.size() - 1)
        {
            std::swap(entries[idx], entries.back());
            // パスマップも更新
            pathToIndexMap[entries[idx].filePath] = idx;
        }
        entries.pop_back();
    }

    if (initialSize != entries.size())
    {
        modified = true;
    }
}

size_t MemoryMappedFileIndex::size() const
{
    return entries.size();
}

void MemoryMappedFileIndex::loadIndex()
{
    std::ifstream file(indexFilePath, std::ios::binary);
    if (!file)
        return;

    // インデックスのサイズを取得
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // ファイルサイズが妥当かチェック
    if (fileSize % sizeof(IndexEntry) != 0)
    {
        LOG("Invalid index file size");
        return;
    }

    // エントリ数を計算
    size_t entryCount = fileSize / sizeof(IndexEntry);
    entries.resize(entryCount);

    // 一度にすべてのエントリを読み込み
    file.read(reinterpret_cast<char *>(entries.data()), fileSize);

    // アクセス用のマップを構築
    for (size_t i = 0; i < entries.size(); i++)
    {
        pathToIndexMap[entries[i].filePath] = i;
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

    // 一度にすべてのエントリを書き込み
    file.write(reinterpret_cast<const char *>(entries.data()),
               entries.size() * sizeof(IndexEntry));
}

