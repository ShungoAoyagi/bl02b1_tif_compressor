#ifndef FILE_PROCESSOR_HPP
#define FILE_PROCESSOR_HPP

#include "file_set.hpp"
#include "fast_delete_queue.hpp"
#include <memory>

// グローバル削除キューインスタンスの外部宣言
extern std::unique_ptr<FastDeleteQueue> deleteQueue;

// ファイルセットを処理する関数
bool processFileSet(const FileSet &fileSet, const std::string &outputDir, bool deleteAfter = true, int maxThreads = 4, int lz4Acceleration = 1);

#endif // FILE_PROCESSOR_HPP

