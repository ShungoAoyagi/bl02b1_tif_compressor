#ifndef COMMON_HPP
#define COMMON_HPP

#include <iostream>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <string>
#include <cstdint>

// ファイルシステム名前空間のエイリアス
namespace fs = std::filesystem;

// スレッドセーフなログ出力用
extern std::mutex cout_mutex;

// タイムスタンプを取得する関数
inline std::string getTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
    
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_tm = std::localtime(&now_time_t);
    auto ms = value.count() % 1000;
    
    std::ostringstream timestamp;
    timestamp << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S") 
              << "." << std::setfill('0') << std::setw(3) << ms;
    
    return timestamp.str();
}

#define LOG(msg)                                      \
    {                                                 \
        std::lock_guard<std::mutex> lock(cout_mutex); \
        std::cout << "[" << getTimestamp() << "] " << msg << std::endl; \
    }

// ユーティリティ関数：数値を指定桁数のゼロ埋め文字列に変換
std::string zeroPad(int number, int width);

#endif // COMMON_HPP

