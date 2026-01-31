#include "file_processor.hpp"
#include "../common/common.hpp"
#include "compress_to_lz4.hpp"
#include <chrono>

// グローバル削除キューインスタンス
std::unique_ptr<FastDeleteQueue> deleteQueue;

bool processFileSet(const FileSet &fileSet, const std::string &outputDir, bool deleteAfter, int maxThreads, int lz4Acceleration)
{
    try
    {
        // 処理開始時間を記録
        auto startTime = std::chrono::high_resolution_clock::now();

        // 出力パスを取得
        std::string outputPath = fileSet.getOutputPath(outputDir);

        // 既に処理済みならスキップ
        if (fs::exists(outputPath))
        {
            LOG("Skipping already processed set: " << outputPath);
            return true;
        }

        // ---------- 並列ファイル読み込み + LZ4圧縮 + メモリ上展開テスト ----------
        // maxThreadsスレッドで1つのファイルセットを並列処理
        // 圧縮の整合性はメモリ上で検証される（書き込み前）
        if (!compressFilesToLZ4(fileSet.files, outputPath, maxThreads, lz4Acceleration))
        {
            LOG("Error: Failed to compress files to LZ4 (or decompression test failed)");
            return false;
        }
        // 先頭ファイルを出力ディレクトリにコピー
        if (!fileSet.firstFile.empty())
        {
            fs::path firstFilePath(fileSet.firstFile);
            fs::path destPath = fs::path(outputDir) / firstFilePath.filename();

            try
            {
                // ファイルが存在している場合は上書き
                if (fs::exists(destPath))
                {
                    fs::remove(destPath);
                }
                fs::copy_file(firstFilePath, destPath, fs::copy_options::overwrite_existing);
            }
            catch (const std::exception &e)
            {
                LOG("Error copying first file: " << e.what());
            }
        }

        // 元ファイルを削除 - 削除キューに追加（展開テスト成功後のみ）
        if (deleteAfter)
        {
            // 削除タスクを削除キューに追加（すべてのファイルを削除）
            deleteQueue->push(fileSet.files);
        }

        // 処理終了時間と経過時間を計算
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        LOG("Created: " << fs::path(outputPath).filename().string() << " - Processing time: " << duration << " ms");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG("Error processing file set: " << e.what());
        return false;
    }
}

