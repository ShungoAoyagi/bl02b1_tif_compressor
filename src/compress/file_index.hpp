#ifndef FILE_INDEX_HPP
#define FILE_INDEX_HPP

#include "file_set.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

// メモリマップドファイルを使用した高速インデックス
class MemoryMappedFileIndex
{
private:
    // シリアライズ用の内部構造体
    struct FileEntry
    {
        char filePath[512];      // 固定サイズパス
        int64_t lastModifiedTime; // 最終更新時刻（エポック秒、ポータブル）
    };
    
    // file_time_type <-> int64_t 変換ヘルパー
    static int64_t fileTimeToInt64(const fs::file_time_type &ftime);
    static fs::file_time_type int64ToFileTime(int64_t timestamp);

    std::string indexFilePath;
    bool modified;
    int setSize; // setSize を保持（TaskKey計算に必要）

    // セット中心のデータ構造
    std::map<TaskKey, FileSet> fileSetMap;
    
    // ファイルパスから所属するTaskKeyを引くマップ
    std::unordered_map<std::string, TaskKey> pathKeyMap;
    
    // ファイルパスと更新時刻を記録（変更検出用）
    std::unordered_map<std::string, int64_t> fileModTimeMap;

    void loadIndex();
    void saveIndex();
    
    // TaskKeyを計算するヘルパー
    TaskKey calculateTaskKey(int run, int fileNumber) const;

public:
    MemoryMappedFileIndex(const std::string &basePath, int setSize);
    ~MemoryMappedFileIndex();

    // ファイルをインデックスに追加または更新
    void addFile(const std::string &path, int run, int fileNumber,
                 const fs::file_time_type &modTime, bool isProcessed = false);

    // ファイルが変更されたかチェック
    bool hasFileChanged(const std::string &path, const fs::file_time_type &currentModTime);

    // 特定のファイルを処理済みとしてマーク
    void markProcessed(const std::string &path, bool processed = true);

    // ファイルセット全体を処理済みとしてマーク（TaskKeyを使用）
    void markFileSetProcessed(const TaskKey &taskKey, bool processed = true);

    // すべてのファイルセットを取得 (処理済みのセットはオプションでフィルタリング)
    std::vector<FileSet> getAllFileSets(bool includeProcessed = true);

    // 指定されたTaskKeyのFileSetをO(1)で取得（Producer-Consumer用）
    // 戻り値: セットの取得に成功した場合true、失敗した場合false
    bool getFileSet(const TaskKey &taskKey, FileSet &outFileSet);

    // インデックスの内容をクリア
    void clear();

    // 存在しないファイルをインデックスから除去
    void cleanup();

    // エントリ数を取得
    size_t size() const;
};

#endif // FILE_INDEX_HPP

