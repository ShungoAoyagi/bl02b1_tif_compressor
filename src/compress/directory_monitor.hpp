#ifndef DIRECTORY_MONITOR_HPP
#define DIRECTORY_MONITOR_HPP

#include "file_set.hpp"
#include "file_index.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <regex>
#include <memory>

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

    // パターンマッチング用の正規表現
    std::regex filePattern;

    void scannerWorker();
    void performFullScan();
    void performIncrementalScan();
    void updateFileSets();

public:
    IndexedDirectoryMonitor(const std::string &watchDir, const std::string &basePattern, int setSize);
    ~IndexedDirectoryMonitor();

    std::vector<FileSet> getLatestFileSets(bool waitForNew = false);
    bool isDataAvailable();
    void markDataProcessed();
    void markFileSetProcessed(const FileSet &processedSet, bool processed = true);
    size_t getIndexSize() const;
    
    // インデックスから直接次の完全なセットを取得（並列処理用）
    bool getNextCompleteFileSet(FileSet &outFileSet);
};

// メインの監視関数
void monitorDirectory(const std::string &watchDir, const std::string &outputDir,
                      const std::string &basePattern, int setSize, int pollInterval,
                      int maxThreads, int maxProcesses, int lz4Acceleration, bool deleteAfter, bool stopOnInterrupt);

#endif // DIRECTORY_MONITOR_HPP

