#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <ctime>
#include <cmath>
#include <cstdarg>
#include <tiffio.h>

#include "src/common/common.hpp"
#include "src/decompress/lz4_decompressor.hpp"
#include "src/decompress/tiff_processor.hpp"
#include "src/decompress/rename_finf.h"

namespace fs = std::filesystem;

// 警告を抑制するためのカスタムハンドラ（何も出力しない）
void quietTiffWarningHandler(const char *module, const char *fmt, va_list ap)
{
    // 何もしない（警告を表示しない）
}

/// LZ4ファイルを処理する関数
/// run_type = 0: 解凍したtifファイルをそのまま出力
/// run_type = 1: 解凍したtifファイルをマージして出力
int processLZ4File(const std::string &filename, const int mergeImageNumber,
                   const std::string &outputFolder, const std::string &prefix_with_run,
                   const int runNumber, const int s_img, const int e_img, const int run_type)
{
    try
    {
        std::cout << "Processing: " << filename << std::endl;

        // 1. LZ4アーカイブを解凍してメモリ上に展開
        auto entries = decompressLZ4Archive(filename);
        
        if (entries.empty())
        {
            std::cerr << "No files extracted from: " << filename << std::endl;
            return 1;
        }

        // 2. 処理方法の分岐
        if (run_type == 0)
        {
            // run_typeが0の場合：tifファイルをそのまま出力
            extractTiffFilesFromMemory(entries, outputFolder);
        }
        else
        {
            // run_typeが1の場合：libtiffによるTIFFファイルのマージ処理を呼び出し
            mergeTiffFilesWithLibTiff(entries, prefix_with_run, outputFolder, s_img, e_img, mergeImageNumber);
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "File " << filename << " exception error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}

/// バッチ処理による最適化版 processLZ4Files
int processLZ4Files(const std::string &input_dir, const std::string &output_dir, const std::string &prefix,
                    const int s_run, const int e_run, const int s_img, const int e_img,
                    const int merge_frame_num, const int run_type)
{
    int incre_num = e_img - s_img + 1;
    const int file_num_per_lz4 = 100;
    int inc_set = static_cast<int>(std::round(double(incre_num) / file_num_per_lz4));
    std::cout << "incre_num: " << incre_num << std::endl;
    std::cout << "inc_set: " << inc_set << std::endl;

    // 利用可能なコア数を取得（または手動設定）
    const int max_concurrent_tasks = 3;
    std::cout << "Using " << max_concurrent_tasks << " concurrent tasks" << std::endl;

    // バッチサイズの決定（各スレッドが処理するファイル数）
    const int batch_size = std::max(1, inc_set / max_concurrent_tasks);
    std::cout << "Batch size: " << batch_size << " files per task" << std::endl;

    std::mutex cout_mutex;

    for (int j = s_run; j <= e_run; j++)
    {
        std::string run = "_" + zeroPad(j, 2) + "_";

        // バッチ単位で処理するためのスレッド配列
        std::vector<std::thread> threads;

        // バッチ単位でタスクを作成
        for (int batch_start = 0; batch_start < inc_set; batch_start += batch_size)
        {
            int batch_end = std::min(batch_start + batch_size - 1, inc_set - 1);

            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Queueing batch from " << zeroPad(j, 2) << "_"
                          << zeroPad(batch_start * file_num_per_lz4 + 1, 5)
                          << " to " << zeroPad(j, 2) << "_"
                          << zeroPad((batch_end + 1) * file_num_per_lz4, 5) << std::endl;
            }

            // バッチ処理用のスレッドを作成
            threads.emplace_back([=, &cout_mutex]()
                                 {
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "Processing batch from " << zeroPad(j, 2) << "_" 
                              << zeroPad(batch_start * file_num_per_lz4 + 1, 5) << std::endl;
                }
                
                // バッチ内の各ファイルを処理
                for (int i = batch_start; i <= batch_end; i++) {
                    std::string lz4_file = input_dir + "/" + prefix + run + 
                                          zeroPad(i * file_num_per_lz4 + 1, 5) + ".lz4";
                    
                    processLZ4File(
                        lz4_file, merge_frame_num, output_dir,
                        prefix + run, j,
                        i * file_num_per_lz4 + 1, (i + 1) * file_num_per_lz4, run_type
                    );
                }
                
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "Completed batch from " << zeroPad(j, 2) << "_" 
                              << zeroPad(batch_start * file_num_per_lz4 + 1, 5) << std::endl;
                } });

            // スレッド数が上限に達したら、全スレッドの完了を待つ
            if (threads.size() >= max_concurrent_tasks)
            {
                for (auto &thread : threads)
                {
                    thread.join();
                }
                threads.clear();
            }
        }

        // 残りのスレッドの完了を待つ
        for (auto &thread : threads)
        {
            thread.join();
        }
    }

    return 0;
}

int main()
{
    // TIFFライブラリの警告ハンドラを上書きして、出力を抑制
    TIFFSetWarningHandler(quietTiffWarningHandler);

    std::string input_dir;
    std::string output_dir;
    std::string prefix;
    int s_run;
    int e_run;
    int s_img;
    int e_img;
    int merge_frame_num = 1; // デフォルト値を設定
    int run_type;            // 0: 解凍したtifをそのまま出力, 1: 解凍したtifをマージして出力

    std::cout << "Input directory: ";
    std::cin >> input_dir;
    std::cout << "Output directory: ";
    std::cin >> output_dir;
    std::cout << "Prefix: ";
    std::cin >> prefix;
    std::cout << "Start run: ";
    std::cin >> s_run;
    std::cout << "End run: ";
    std::cin >> e_run;
    std::cout << "Start image: ";
    std::cin >> s_img;
    std::cout << "End image: ";
    std::cin >> e_img;

    std::cout << "Run type (0: output tif files without merging, 1: output tif files with merging): ";
    std::cin >> run_type;

    // run_typeが1の場合のみmerge_frame_numを入力
    if (run_type == 1)
    {
        std::cout << "Merge frame number: ";
        std::cin >> merge_frame_num;
    }

    clock_t start_time = clock();

    processLZ4Files(input_dir, output_dir, prefix, s_run, e_run, s_img, e_img, merge_frame_num, run_type);
    
    clock_t end_time = clock();
    double elapsed_time = static_cast<double>(end_time - start_time) / CLOCKS_PER_SEC;
    std::cout << "Elapsed time: " << elapsed_time << " seconds" << std::endl;
    
    // Ask if user wants to convert .finf files
    std::cout << "\nConvert .finf and save to output directory? (y/n): ";
    std::string response;
    std::cin >> response;
    
    if (response == "y" || response == "Y") {
        std::cout << "\nStarting .finf file conversion..." << std::endl;
        
        // First search for .finf files in input directory
        std::vector<std::string> finf_files = search_finf_files(input_dir);
        
        std::string finf_input_dir = input_dir;
        
        // If no .finf files found, ask user to specify input directory
        if (finf_files.empty()) {
            std::cout << "\nNo .finf files found in input directory" << std::endl;
            std::cout << "Please specify input directory for .finf files: ";
            std::cin.ignore(); // Clear input buffer
            std::getline(std::cin, finf_input_dir);
            
            // Search again in the specified directory
            finf_files = search_finf_files(finf_input_dir);
            
            if (finf_files.empty()) {
                std::cout << "\nNo .finf files found in specified directory either" << std::endl;
                std::cout << "Skipping .finf file conversion" << std::endl;
            }
        }
        
        // Process .finf files if found
        if (!finf_files.empty()) {
            int processed_count = process_all_finf_files(finf_input_dir, output_dir);
            if (processed_count > 0) {
                std::cout << "\n.finf file conversion completed (" << processed_count << " file(s))" << std::endl;
            }
        }
    } else {
        std::cout << "\nSkipped .finf file conversion" << std::endl;
    }
    
    std::cout << "\nPress Enter to finish..." << std::endl;
    std::cin.get();
    std::cin.get();

    return 0;
}

