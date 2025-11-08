#include "common.hpp"

std::mutex cout_mutex;
std::ofstream log_file;

void initLogFile(const std::string &logDir)
{
    try
    {
        // ログディレクトリが存在しない場合は作成
        if (!fs::exists(logDir))
        {
            fs::create_directories(logDir);
        }

        // ログファイル名を生成（例: compressor_20251108_123456.log）
        std::string logFileName = logDir + "/compressor_" + getTimestampForFilename() + ".log";

        // ログファイルを開く
        log_file.open(logFileName, std::ios::out | std::ios::app);

        if (log_file.is_open())
        {
            std::cout << "Log file created: " << logFileName << std::endl;
            log_file << "=== bl02b1_tif_compressor Log ===" << std::endl;
            log_file << "Started at: " << getTimestamp() << std::endl;
            log_file << "======================================" << std::endl;
            log_file.flush();
        }
        else
        {
            std::cerr << "Warning: Could not open log file: " << logFileName << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing log file: " << e.what() << std::endl;
    }
}

void closeLogFile()
{
    if (log_file.is_open())
    {
        log_file << "======================================" << std::endl;
        log_file << "Ended at: " << getTimestamp() << std::endl;
        log_file << "=== End of Log ===" << std::endl;
        log_file.close();
    }
}

std::string zeroPad(int number, int width)
{
    std::ostringstream oss;
    oss << std::setw(width) << std::setfill('0') << number;
    return oss.str();
}

