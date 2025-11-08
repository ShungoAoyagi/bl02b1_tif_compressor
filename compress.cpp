#include "src/common/common.hpp"
#include "src/compress/directory_monitor.hpp"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    // Default settings
    std::string watchDir = "Z:";
    std::string outputDir = "Z:";
    std::string basePattern = "test";
    std::string baseSuffix = "_##_#####.tif";
    int setSize = 100;
    int pollInterval = 1;               // Poll interval in seconds
    const int maxThreads = 8;           // Maximum number of threads (fixed)
    const int maxProcesses = 1;         // Maximum number of processes (fixed)
    const int lz4Acceleration = 4;      // LZ4 acceleration parameter (1=default, higher=faster but lower compression)
    const bool deleteAfter = true;      // Always delete source files after processing
    const bool stopOnInterrupt = false; // Never stop on Enter key

    std::cout << "=== bl02b1_tif_compressor ===" << std::endl;
    std::cout << "Version 0.2.0" << std::endl;
    std::cout << "Author: Shungo AOYAGI" << std::endl;
    std::cout << "Date: 2025-11-08" << std::endl;
    std::cout << "If you have any questions, please contact me at aoyagi-shungo011@g.ecc.u-tokyo.ac.jp" << std::endl;

    // Get user input
    std::cout << "=== tif_compressor Settings ===" << std::endl;

    // Watch directory input
    std::cout << "Enter directory to monitor: ";
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty())
    {
        watchDir = input;
    }

    // Output directory input
    std::cout << "Enter directory for output files: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        outputDir = input;
    }

    // File pattern input
    std::cout << "Enter filename prefix: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        basePattern = input + baseSuffix;
    }

    // Set size input
    std::cout << "Enter number of files per set: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        try
        {
            setSize = std::stoi(input);
        }
        catch (const std::exception &e)
        {
            std::cout << "Invalid input. Using default value: " << setSize << std::endl;
        }
    }

    std::cout << "\n=== Monitor Configuration ===" << std::endl;
    std::cout << "Watch directory: " << watchDir << std::endl;
    std::cout << "Output directory: " << outputDir << std::endl;
    std::cout << "File pattern: " << basePattern << std::endl;
    std::cout << "Set size: " << setSize << std::endl;
    std::cout << "\nStarting monitor...\n"
              << std::endl;

    try
    {
        monitorDirectory(watchDir, outputDir, basePattern, setSize, pollInterval, maxThreads, maxProcesses, lz4Acceleration, deleteAfter, stopOnInterrupt);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

