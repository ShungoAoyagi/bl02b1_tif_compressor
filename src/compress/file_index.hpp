#ifndef FILE_INDEX_HPP
#define FILE_INDEX_HPP

#include "file_set.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

// メモリマップドファイルを使用した高速インデックス
class MemoryMappedFileIndex
{
private:
    struct IndexEntry
    {
        int run;
        int fileNumber;
        char filePath[512];              // 固定サイズパス
        fs::file_time_type lastModified; // 最終更新時刻
        bool processed;                  // 処理済みフラグ
    };

    std::string indexFilePath;
    std::vector<IndexEntry> entries;
    bool modified;

    // インデックスエントリへの高速アクセス用マップ
    std::unordered_map<std::string, size_t> pathToIndexMap;

    void loadIndex();
    void saveIndex();

public:
    MemoryMappedFileIndex(const std::string &basePath);
    ~MemoryMappedFileIndex();

    // ファイルをインデックスに追加または更新
    void addFile(const std::string &path, int run, int fileNumber,
                 const fs::file_time_type &modTime, bool isProcessed = false);

    // ファイルが変更されたかチェック
    bool hasFileChanged(const std::string &path, const fs::file_time_type &currentModTime);

    // 特定のファイルを処理済みとしてマーク
    void markProcessed(const std::string &path, bool processed = true);

    // ファイルセット全体を処理済みとしてマーク
    void markFileSetProcessed(int run, int setNumber, int setSize, bool processed = true);

    // 特定のrun/fileNumberのファイルパスを検索
    std::string findFilePath(int run, int fileNumber);

    // すべてのファイルセットを取得 (処理済みのセットはオプションでフィルタリング)
    std::vector<FileSet> getAllFileSets(int setSize, bool includeProcessed = true);

    // 次の完全なファイルセットを1つだけ取得（未処理のみ、効率的に1セットずつ処理）
    // 戻り値: 完全なセットが見つかった場合true、見つからなかった場合false
    bool getNextCompleteFileSet(int setSize, FileSet &outFileSet);

    // インデックスの内容をクリア
    void clear();

    // 存在しないファイルをインデックスから除去
    void cleanup();

    // エントリ数を取得
    size_t size() const;
};

#endif // FILE_INDEX_HPP

