#include "fast_delete_queue.hpp"
#include "../common/common.hpp"
#include <algorithm>
#include <chrono>
#include <regex>
#include <iomanip>

#ifdef _WIN32

WindowsFastDeleteQueue::WindowsFastDeleteQueue() : running(true)
{
    worker_thread = std::thread(&WindowsFastDeleteQueue::worker, this);
}

WindowsFastDeleteQueue::~WindowsFastDeleteQueue()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        running = false;
    }
    cv.notify_all();
    if (worker_thread.joinable())
    {
        worker_thread.join();
    }
}

bool WindowsFastDeleteQueue::batchDeleteFiles(const std::vector<std::string> &filePaths)
{
    if (filePaths.empty())
        return true;

    auto totalStart = std::chrono::high_resolution_clock::now();

    // まず高速DeleteFile APIを試行
    auto fastStart = std::chrono::high_resolution_clock::now();
    bool useFastMethod = filePaths.size() >= 10; // 10ファイル以上なら高速方式

    if (useFastMethod)
    {
        int successCount = 0;
        for (const auto &path : filePaths)
        {
            std::string normalizedPath = path;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

            if (DeleteFileA(normalizedPath.c_str()))
            {
                successCount++;
            }
            else
            {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND)
                {
                    successCount++; // 既に削除済み
                }
            }
        }

        auto fastEnd = std::chrono::high_resolution_clock::now();
        auto fastTime = std::chrono::duration_cast<std::chrono::milliseconds>(fastEnd - fastStart).count();

        LOG("Delete files completed: " << successCount << "/" << filePaths.size()
                                      << " files in " << fastTime << " ms");

        return successCount == filePaths.size();
    }

    // フォールバック：元のSHFileOperation（小さなファイル数の場合のみ）
    std::string fileList;
    for (const auto &path : filePaths)
    {
        std::string normalizedPath = path;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');
        fileList += normalizedPath + '\0';
    }
    fileList += '\0'; // 二重null終端

    SHFILEOPSTRUCTA fileOp = {};
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = fileList.c_str();
    fileOp.pTo = nullptr;
    fileOp.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    fileOp.hwnd = nullptr;

    auto deleteStart = std::chrono::high_resolution_clock::now();
    int result = SHFileOperationA(&fileOp);
    auto deleteEnd = std::chrono::high_resolution_clock::now();

    auto deleteTime = std::chrono::duration_cast<std::chrono::milliseconds>(deleteEnd - deleteStart).count();
    LOG("SHFileOperation delete time: " << deleteTime << " ms for " << filePaths.size() << " files");

    if (result != 0 || fileOp.fAnyOperationsAborted)
    {
        LOG("Shell API batch delete failed");
        return false;
    }

    return true;
}

bool WindowsFastDeleteQueue::deleteSingleFile(const std::string &filePath)
{
    std::string normalizedPath = filePath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

    bool success = DeleteFileA(normalizedPath.c_str());

    if (success)
    {
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND)
    {
        return true; // 既に削除済みと見なす
    }

    LOG("DeleteFile failed for " << filePath << " with error: " << error);
    return false;
}

bool WindowsFastDeleteQueue::isSafeToDelete(const std::string &filePath)
{
    try
    {
        if (!fs::exists(filePath))
        {
            return false;
        }

        if (!fs::is_regular_file(filePath))
        {
            LOG("Warning: Not a regular file, skipping: " << filePath);
            return false;
        }

        fs::path pathObj(filePath);
        if (pathObj.extension().string() != ".tif")
        {
            LOG("Warning: File extension is not .tif, skipping: " << filePath);
            return false;
        }

        std::string filename = pathObj.filename().string();
        std::regex safetyPattern(".*_[0-9]{2}_[0-9]{5}\\.tif$");
        if (!std::regex_match(filename, safetyPattern))
        {
            LOG("Warning: Filename pattern mismatch, skipping: " << filename);
            return false;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        LOG("Error during safety check for " << filePath << ": " << e.what());
        return false;
    }
}

void WindowsFastDeleteQueue::worker()
{
    try
    {
        while (running)
        {
            try
            {
                DeleteTask task;

                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    if (tasks.empty())
                    {
                        cv.wait_for(lock, std::chrono::seconds(1), [this]
                                    { return !tasks.empty() || !running; });
                        if (!running && tasks.empty())
                            break;
                        if (tasks.empty())
                            continue;
                    }

                    task = tasks.front();
                    tasks.pop();
                }

                // 削除対象ファイルのフィルタリング
                std::vector<std::string> safeFilesToDelete;

                for (const auto &filePath : task.files)
                {
                    // 最初のファイルは削除しない
                    if (filePath == task.firstFile)
                    {
                        continue;
                    }

                    // 安全性チェック
                    if (isSafeToDelete(filePath))
                    {
                        safeFilesToDelete.push_back(filePath);
                    }
                }

                // バッチ削除を実行
                if (!safeFilesToDelete.empty())
                {
                    bool success = false;
                    if (safeFilesToDelete.size() > 1)
                    {
                        success = batchDeleteFiles(safeFilesToDelete);
                        if (!success)
                        {
                            LOG("Batch delete failed, falling back to individual deletion");
                            int successCount = 0;

                            for (const auto &filePath : safeFilesToDelete)
                            {
                                if (deleteSingleFile(filePath))
                                {
                                    successCount++;
                                }
                            }
                        }
                    }
                    else
                    {
                        deleteSingleFile(safeFilesToDelete[0]);
                    }
                }
                else
                {
                    LOG("No files to delete after filtering");
                }
            }
            catch (const std::exception &e)
            {
                LOG("Error in delete worker loop: " << e.what());
                // エラーが発生しても処理を継続
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG("Fatal error in delete worker thread: " << e.what());
        running = false;
    }
    catch (...)
    {
        LOG("Unknown fatal error in delete worker thread");
        running = false;
    }
}

void WindowsFastDeleteQueue::push(const std::set<std::string> &files, const std::string &firstFile)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        DeleteTask task;
        task.files.assign(files.begin(), files.end());
        task.firstFile = firstFile;
        tasks.push(task);
    }
    cv.notify_one();
}

void WindowsFastDeleteQueue::push(const std::vector<std::string> &files, const std::string &firstFile)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        DeleteTask task;
        task.files = files;
        task.firstFile = firstFile;
        tasks.push(task);
    }
    cv.notify_one();
}

size_t WindowsFastDeleteQueue::size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}

#else

// 非Windows環境用の基本的な実装
void BasicDeleteQueue::push(const std::set<std::string> &files, const std::string &firstFile)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::vector<std::string> fileVec(files.begin(), files.end());
    tasks.push(fileVec);
}

void BasicDeleteQueue::push(const std::vector<std::string> &files, const std::string &firstFile)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    tasks.push(files);
}

size_t BasicDeleteQueue::size()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}

#endif

