// Windows環境でmin/maxマクロを無効化（インクルードの前に定義）
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "directory_monitor.hpp"
#include "../common/common.hpp"
#include "file_processor.hpp"
#include <chrono>
#include <set>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <future>

IndexedDirectoryMonitor::IndexedDirectoryMonitor(const std::string &watchDir, const std::string &basePattern, int setSize)
    : running(true), newDataAvailable(false), producerFinishedScan(false)
{
    task.watchDir = watchDir;
    task.basePattern = basePattern;
    task.setSize = setSize;

    // メモリマップドインデックスを初期化（setSize付き）
    fileIndex = std::make_unique<MemoryMappedFileIndex>(watchDir, setSize);

    // 正規表現パターン作成
    filePattern = std::regex(basePattern.substr(0, basePattern.find("_##_")) +
                             "_([0-9]{2})_([0-9]{5})\\.tif");

    scanner_thread = std::thread(&IndexedDirectoryMonitor::scannerWorker, this);
}

IndexedDirectoryMonitor::~IndexedDirectoryMonitor()
{
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        running = false;
    }
    if (scanner_thread.joinable())
    {
        scanner_thread.join();
    }
}

void IndexedDirectoryMonitor::scannerWorker()
{
    bool firstScan = true;

    try
    {
        while (running)
        {
            try
            {
                if (firstScan)
                {
                    performFullScan();
                    firstScan = false;
                    LOG("Initial full scan completed. Switching to incremental scanning only.");
                }
                else
                {
                    performIncrementalScan();
                }

                // 1セットずつ効率的に取得（メモリ節約）
                updateFileSets();

                // スキャン間隔を調整（ディスクI/Oを減らす）
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            catch (const std::exception &e)
            {
                LOG("Error in scanner worker loop: " << e.what());
                // エラーが発生しても処理を継続するため、少し待機してから次のイテレーションへ
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG("Fatal error in scanner worker thread: " << e.what());
        // スレッドが終了する前にrunningフラグをfalseにして、他のスレッドに通知
        running = false;
    }
    catch (...)
    {
        LOG("Unknown fatal error in scanner worker thread");
        running = false;
    }
}

void IndexedDirectoryMonitor::performFullScan()
{
    auto startTime = std::chrono::high_resolution_clock::now();
    LOG("Performing full scan with memory-mapped index (parallel)");

    try
    {
        // ステップ1: ファイルエントリーを全て収集（シングルスレッド）
        std::vector<fs::directory_entry> entries;
        entries.reserve(100000); // 数十万ファイルに備えて事前確保
        
        for (const auto &entry : fs::directory_iterator(task.watchDir))
        {
            if (entry.is_regular_file())
            {
                entries.push_back(entry);
            }
        }
        
        LOG("Found " << entries.size() << " files, processing in parallel...");

        // ステップ2: 並列処理
        const size_t numThreads = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;
        std::atomic<size_t> processedCount(0);
        std::atomic<size_t> matchedCount(0);
        
        // ファイルを均等に分割
        size_t chunkSize = (entries.size() + numThreads - 1) / numThreads;
        
        for (size_t threadId = 0; threadId < numThreads; ++threadId)
        {
            size_t start = threadId * chunkSize;
            size_t end = std::min(start + chunkSize, entries.size());
            
            if (start >= entries.size())
                break;
            
            threads.emplace_back([this, &entries, start, end, &processedCount, &matchedCount]()
            {
                try
                {
                    for (size_t i = start; i < end; ++i)
                    {
                        try
                        {
                            const auto &entry = entries[i];
                            
                            // ファイル名とパスを取得
                            std::string filename = entry.path().filename().string();
                            std::string filepath = entry.path().string();
                            
                            // ファイルの更新時刻を取得（並列実行可能）
                            auto lastWriteTime = entry.last_write_time();
                            
                            // パターンに一致するか確認（並列実行可能）
                            std::smatch matches;
                            if (std::regex_match(filename, matches, filePattern) && matches.size() >= 3)
                            {
                                int run = std::stoi(matches[1].str());
                                int fileNumber = std::stoi(matches[2].str());
                                
                                // インデックスへのアクセスは排他制御が必要
                                {
                                    std::lock_guard<std::mutex> lock(index_mutex);
                                    
                                    // ファイルが変更されているか確認
                                    if (fileIndex->hasFileChanged(filepath, lastWriteTime))
                                    {
                                        // インデックスにファイルを追加または更新
                                        bool processed = !fileIndex->hasFileChanged(filepath, lastWriteTime);
                                        fileIndex->addFile(filepath, run, fileNumber, lastWriteTime, processed);
                                    }
                                }
                                
                                matchedCount++;
                            }
                            
                            processedCount++;
                            
                            // 進捗表示（10%刻み）
                            if (processedCount % (entries.size() / 10 + 1) == 0 && entries.size() > 10000)
                            {
                                size_t progress = (processedCount * 100) / entries.size();
                                LOG("Scan progress: " << progress << "% (" << processedCount << "/" << entries.size() << " files)");
                            }
                        }
                        catch (const std::exception &e)
                        {
                            // 個別ファイルのエラーは記録して継続
                            LOG("Error processing file in parallel scan: " << e.what());
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    LOG("Fatal error in parallel scan thread: " << e.what());
                }
                catch (...)
                {
                    LOG("Unknown fatal error in parallel scan thread");
                }
            });
        }
        
        // 全スレッドの完了を待機
        for (auto &thread : threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        
        LOG("Full scan completed: " << processedCount << " files processed, " 
            << matchedCount << " files matched pattern");

        // ステップ3: クリーンアップ（シングルスレッド）
        fileIndex->cleanup();
        
        // ステップ4: 未処理の完全なセットをタスクキューに積む
        LOG("Enqueuing complete file sets to task queue...");
        
        // getAllFileSetsで未処理のセットを取得してキューに積む
        std::vector<FileSet> allSets = fileIndex->getAllFileSets(false);
        
        size_t enqueuedCount = 0;
        for (const auto &fileSet : allSets)
        {
            // 完全なセット（setSize個のファイルがある）をキューに積む
            if (fileSet.files.size() >= static_cast<size_t>(task.setSize))
            {
                enqueueTask(fileSet.run, fileSet.setNumber);
                enqueuedCount++;
            }
        }
        
        LOG("Enqueued " << enqueuedCount << " complete file sets to task queue");
        
        // 初回スキャン完了フラグを立てる
        producerFinishedScan = true;
        queueCV.notify_all();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        LOG("Full scan completed in " << duration.count() << " ms using " 
            << threads.size() << " threads");
    }
    catch (const std::exception &e)
    {
        LOG("Error scanning directory: " << e.what());
    }
}

void IndexedDirectoryMonitor::performIncrementalScan()
{
    try
    {
        // 新規/変更されたファイルのみを効率的にチェック
        size_t newFilesFound = 0;
        size_t updatedFiles = 0;
        std::set<TaskKey> updatedSets; // 更新されたセットを記録

        for (const auto &entry : fs::directory_iterator(task.watchDir))
        {
            try
            {
                // ファイルの存在と種類を確認
                if (!entry.exists() || !entry.is_regular_file())
                    continue;

                std::string filepath = entry.path().string();
                std::string filename = entry.path().filename().string();

                // パターンに一致するか確認（早期リターンで効率化）
                std::smatch matches;
                if (!std::regex_match(filename, matches, filePattern) || matches.size() < 3)
                    continue;

                int run = std::stoi(matches[1].str());
                int fileNumber = std::stoi(matches[2].str());

                // ファイルの更新時刻を取得（例外が発生する可能性あり）
                auto lastWriteTime = entry.last_write_time();

                // ファイルが変更されているか確認（読み取り）をロックで保護
                bool changed;
                {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    changed = fileIndex->hasFileChanged(filepath, lastWriteTime);
                }

                if (changed)
                {
                    // インデックスを更新（書き込み）をロックで保護
                    {
                        std::lock_guard<std::mutex> lock(index_mutex);
                        fileIndex->addFile(filepath, run, fileNumber, lastWriteTime, false);
                    }
                    
                    // このファイルが属するTaskKeyを記録
                    TaskKey taskKey;
                    taskKey.run = run;
                    taskKey.setNumber = ((fileNumber - 1) / task.setSize) * task.setSize + 1;
                    updatedSets.insert(taskKey);
                    
                    newFilesFound++;
                }
                // else: ファイルは既にインデックスに存在し、変更もない → 何もしない
            }
            catch (const fs::filesystem_error &e)
            {
                // ファイルが削除された、またはアクセスできない場合
                // （イテレーション中にファイルが削除されるケース）
                // 警告を出さずにスキップ（次のcleanup()で削除される）
                continue;
            }
            catch (const std::exception &e)
            {
                // その他のエラー（パース失敗など）
                LOG("Warning processing file in incremental scan: " << e.what());
                continue;
            }
        }

        // 更新されたセットが完全になったかチェックしてキューに積む
        if (!updatedSets.empty())
        {
            for (const auto &taskKey : updatedSets)
            {
                FileSet testSet;
                bool found;
                {
                    std::lock_guard<std::mutex> lock(index_mutex);
                    found = fileIndex->getFileSet(taskKey, testSet);
                }
                
                if (found)
                {
                    // 完全なセット（task.setSizeファイル）かつ未処理の場合のみキューに追加
                    if (testSet.files.size() >= static_cast<size_t>(task.setSize) && !testSet.processed)
                    {
                        // enqueueTask内で重複チェックが行われるので安全
                        enqueueTask(taskKey.run, taskKey.setNumber);
                    }
                }
            }
        }

    }
    catch (const fs::filesystem_error &e)
    {
        // ディレクトリ自体にアクセスできない場合
        LOG("Warning: Cannot access directory in incremental scan: " << e.what());
    }
    catch (const std::exception &e)
    {
        LOG("Warning in incremental scan: " << e.what());
    }
}

void IndexedDirectoryMonitor::updateFileSets()
{
    // メインループが直接getNextCompleteFileSetを呼ぶため、
    // ここでは新しいファイルが追加されたことを通知するだけ
    // （処理はメインループ側で行う）
}

std::vector<FileSet> IndexedDirectoryMonitor::getLatestFileSets(bool waitForNew)
{
    std::unique_lock<std::mutex> lock(data_mutex);
    if (waitForNew)
    {
        cv.wait(lock, [this]()
                { return newDataAvailable || !running; });
    }
    
    // データを取得したら、必ず消費済みにする
    std::vector<FileSet> result = latestFileSets;
    if (!result.empty())
    {
        latestFileSets.clear();
        newDataAvailable = false;
    }
    
    return result;
}

bool IndexedDirectoryMonitor::isDataAvailable()
{
    std::lock_guard<std::mutex> lock(data_mutex);
    return newDataAvailable;
}

void IndexedDirectoryMonitor::markDataProcessed()
{
    std::lock_guard<std::mutex> lock(data_mutex);
    newDataAvailable = false;
}

void IndexedDirectoryMonitor::markFileSetProcessed(const FileSet &processedSet, bool processed)
{
    // TaskKeyを作成
    TaskKey taskKey;
    taskKey.run = processedSet.run;
    taskKey.setNumber = processedSet.setNumber;
    
    // FileSet全体を処理済みとしてマーク（ロックで保護）
    {
        std::lock_guard<std::mutex> lock(index_mutex);
        fileIndex->markFileSetProcessed(taskKey, processed);
    }
}

size_t IndexedDirectoryMonitor::getIndexSize() const
{
    return fileIndex->size();
}

void IndexedDirectoryMonitor::enqueueTask(int run, int setNumber)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    TaskKey taskKey;
    taskKey.run = run;
    taskKey.setNumber = setNumber;
    taskQueue.push(taskKey);
    queueCV.notify_one();
}

bool IndexedDirectoryMonitor::getNextTaskKey(TaskKey &outKey)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    
    // キューが空の場合、producerFinishedScanがfalseなら待機
    while (taskQueue.empty() && !producerFinishedScan)
    {
        queueCV.wait(lock);
    }
    
    // キューにタスクがあればpop
    if (!taskQueue.empty())
    {
        outKey = taskQueue.front();
        taskQueue.pop();
        return true;
    }
    
    // キューが空で、かつproducerFinishedScanがtrueならタスクなし
    return false;
}

bool IndexedDirectoryMonitor::getFileSet(const TaskKey &taskKey, FileSet &outFileSet)
{
    std::lock_guard<std::mutex> lock(index_mutex);
    return fileIndex->getFileSet(taskKey, outFileSet);
}

void monitorDirectory(const std::string &watchDir, const std::string &outputDir,
                      const std::string &basePattern, int setSize, int pollInterval,
                      int maxThreads, int maxProcesses, int lz4Acceleration, bool deleteAfter, bool stopOnInterrupt)
{
    bool running = true;

    LOG("Starting indexed directory monitor on: " << watchDir);
    LOG("Output directory: " << outputDir);
    LOG("Set size: " << setSize << " files");
    LOG("Max threads per set: " << maxThreads);
    LOG("Max concurrent processes: " << maxProcesses);

    // 削除キューを初期化
    deleteQueue = std::make_unique<FastDeleteQueue>();

    // メモリマップドインデックスを使用するモニターを初期化
    IndexedDirectoryMonitor dirMonitor(watchDir, basePattern, setSize);

    // 出力ディレクトリがなければ作成
    try
    {
        fs::create_directories(outputDir);
    }
    catch (const std::exception &e)
    {
        LOG("Error creating output directory: " << e.what());
        return;
    }

    // futureプール（非ブロッキングで完了検出可能）
    std::vector<std::future<std::pair<FileSet, bool>>> futures;

    // Ctrl+C 処理
    if (stopOnInterrupt)
    {
        std::thread interruptThread([&running]()
                                    {
            try
            {
                LOG("Press Enter to stop the monitor...");
                std::cin.get();
                running = false;
                LOG("Stopping monitor...");
            }
            catch (const std::exception &e)
            {
                LOG("Error in interrupt thread: " << e.what());
            }
            catch (...)
            {
                LOG("Unknown error in interrupt thread");
            } });
        interruptThread.detach();
    }

    // メインループ
    while (running)
    {
        try
        {
            // 完了したタスクをクリーンアップ（非ブロッキング、並列枠を空ける）
            for (auto it = futures.begin(); it != futures.end();)
            {
                // wait_for(0)で非ブロッキングに完了をチェック
                if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    // 結果を取得してエラー処理
                    try
                    {
                        auto result = it->get();
                        const FileSet &completedSet = result.first;
                        bool ok = result.second;
                        if (!ok)
                        {
                            LOG("Warning: Task completed with error, reverting processed flag: run " 
                                << completedSet.run << ", set " << completedSet.setNumber);
                            // 失敗時は未処理に戻す
                            dirMonitor.markFileSetProcessed(completedSet, false);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        LOG("Exception in task: " << e.what());
                    }
                    it = futures.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // 並列処理枠が空いている限り、新しいセットを取得して処理
            bool processedAny = false;
            while (futures.size() < static_cast<size_t>(maxProcesses))
            {
                // タスクキューから軽量なキーを取得 (O(1))
                TaskKey taskKey;
                if (!dirMonitor.getNextTaskKey(taskKey))
                {
                    // キューが空になり、初回スキャンも完了した場合
                    break; // ループを抜ける
                }

                // FileSetをO(1)で取得（スレッドセーフ）
                FileSet fileSet;
                if (!dirMonitor.getFileSet(taskKey, fileSet))
                {
                    LOG("Failed to get FileSet for: run " << taskKey.run << ", set " << taskKey.setNumber);
                    continue; // 次のキーへ
                }

                // セットが完全であるか確認（念のため二重チェック）
                if (!isSetComplete(fileSet, setSize))
                {
                    LOG("Warning: Incomplete set received: run " << fileSet.run 
                        << ", set " << fileSet.setNumber << " (" << fileSet.files.size() 
                        << "/" << setSize << " files)");
                    continue;
                }

                // 既に出力ファイルが存在するかチェック
                if (isSetProcessed(fileSet, outputDir))
                {
                    LOG("Set already processed: run " << fileSet.run << ", set " << fileSet.setNumber);
                    dirMonitor.markFileSetProcessed(fileSet);
                    processedAny = true;
                    continue; // 次のセットをチェック
                }

                LOG("Processing set: run " << fileSet.run << ", set " << fileSet.setNumber 
                    << " (" << fileSet.files.size() << " files)");

                // 処理開始前に即座に処理済みとしてマーク（重複検出を防ぐ）
                dirMonitor.markFileSetProcessed(fileSet);

                // 新しいタスクを非同期で起動（std::asyncで真の並列処理）
                futures.emplace_back(std::async(std::launch::async, [=]() {
                    bool ok = processFileSet(fileSet, outputDir, deleteAfter, maxThreads, lz4Acceleration);
                    return std::make_pair(fileSet, ok);
                }));
                processedAny = true;
            }

            // セットを処理しなかった場合は少し待機
            if (!processedAny)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        catch (const std::exception &e)
        {
            LOG("Error in monitor loop: " << e.what());
            // エラー時は短い待機を入れる
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // 残りのタスクが完了するのを待つ
    LOG("Waiting for remaining tasks to complete...");
    for (auto &future : futures)
    {
        try
        {
            // 各タスクの完了を待ち、結果を取得
            auto result = future.get();
            const FileSet &completedSet = result.first;
            bool ok = result.second;
            if (!ok)
            {
                LOG("Warning: Final task completed with error, reverting processed flag: run " 
                    << completedSet.run << ", set " << completedSet.setNumber);
                // 失敗時は未処理に戻す
                dirMonitor.markFileSetProcessed(completedSet, false);
            }
        }
        catch (const std::exception &e)
        {
            LOG("Exception in final task: " << e.what());
        }
    }

    // 削除キューを解放
    LOG("Waiting for delete queue to finish...");
    deleteQueue.reset();

    LOG("Monitor stopped.");
}

