#ifndef DIRECTORY_MONITOR_HPP
#define DIRECTORY_MONITOR_HPP

#include "file_set.hpp"
#include "file_index.hpp"
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <regex>
#include <memory>
#include <queue>
#include <atomic>

// メモリマップドインデックスを使用したディレクトリモニター
class IndexedDirectoryMonitor
{
private:
    struct MonitorTask
    {
        std::string watchDir;
        std::string basePattern;
        int setSize;
    };

    std::thread scanner_thread;
    std::mutex data_mutex;
    std::condition_variable cv;
    bool running;
    MonitorTask task;
    std::vector<FileSet> latestFileSets;
    bool newDataAvailable;

    // メモリマップドインデックス
    std::unique_ptr<MemoryMappedFileIndex> fileIndex;
    std::mutex index_mutex; // fileIndex保護用のミューテックス

    // パターンマッチング用の正規表現
    std::regex filePattern;

    // Producer-Consumerモデル用のタスクキュー
    std::queue<TaskKey> taskQueue;
    std::set<TaskKey> enqueuedTasks; // 既にキューに積まれているTaskKeyを追跡
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::atomic<bool> producerFinishedScan; // 初回スキャン完了フラグ

    void scannerWorker();
    void performFullScan();
    void performIncrementalScan();
    void updateFileSets();

public:
    IndexedDirectoryMonitor(const std::string &watchDir, const std::string &outputDir, const std::string &basePattern, int setSize);
    ~IndexedDirectoryMonitor();

    std::vector<FileSet> getLatestFileSets(bool waitForNew = false);
    bool isDataAvailable();
    void markDataProcessed();
    void markFileSetProcessed(const FileSet &processedSet, bool processed = true);
    size_t getIndexSize() const;
    
    // メインスレッドが呼び出す新メソッド（キューからタスクキーを取得）
    bool getNextTaskKey(TaskKey &outKey);

    // スレッドセーフなFileSet取得メソッド
    bool getFileSet(const TaskKey &taskKey, FileSet &outFileSet);
    
    // インデックスを手動で保存（強制終了対策）
    void saveIndexNow();
    
    // タスクをキューに追加（スキャナーや再キューイング時に使用）
    void enqueueTask(int run, int setNumber);
    
    // FileSetを圧縮待ちqueueの最後に戻す（展開テスト失敗時など）
    void requeueFileSet(const FileSet &fileSet);
};

// メインの監視関数
void monitorDirectory(const std::string &watchDir, const std::string &outputDir,
                      const std::string &basePattern, int setSize, int pollInterval,
                      int maxThreads, int maxProcesses, int lz4Acceleration, bool deleteAfter, bool stopOnInterrupt);

#endif // DIRECTORY_MONITOR_HPP

