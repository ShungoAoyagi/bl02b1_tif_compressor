#ifndef FAST_DELETE_QUEUE_HPP
#define FAST_DELETE_QUEUE_HPP

#include <string>
#include <vector>
#include <set>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

// Windows専用高速削除クラス
class WindowsFastDeleteQueue
{
private:
    struct DeleteTask
    {
        std::vector<std::string> files;
        std::string firstFile; // 削除しないファイル
    };

    std::queue<DeleteTask> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::thread worker_thread;
    bool running;

    // Windows Shell APIを使用したバッチ削除
    bool batchDeleteFiles(const std::vector<std::string> &filePaths);

    // 単一ファイル削除（フォールバック用）
    bool deleteSingleFile(const std::string &filePath);

    // 安全性チェック関数
    bool isSafeToDelete(const std::string &filePath);

    // ワーカースレッド関数
    void worker();

public:
    WindowsFastDeleteQueue();
    ~WindowsFastDeleteQueue();

    void push(const std::set<std::string> &files, const std::string &firstFile = "");
    void push(const std::vector<std::string> &files, const std::string &firstFile = "");
    size_t size();
};

// Windows用の高速削除キューのエイリアス
using FastDeleteQueue = WindowsFastDeleteQueue;

#else
// 非Windows環境では基本的な削除クラスを使用（必要に応じて実装）
class BasicDeleteQueue
{
private:
    std::queue<std::vector<std::string>> tasks;
    std::mutex queue_mutex;

public:
    void push(const std::set<std::string> &files, const std::string &firstFile = "");
    void push(const std::vector<std::string> &files, const std::string &firstFile = "");
    size_t size();
};

using FastDeleteQueue = BasicDeleteQueue;
#endif

#endif // FAST_DELETE_QUEUE_HPP

