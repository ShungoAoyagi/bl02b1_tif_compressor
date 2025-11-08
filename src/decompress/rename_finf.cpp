#include "rename_finf.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fs = std::filesystem;

std::vector<std::string> search_finf_files(const std::string& directory) {
    std::string suffix = ".finf";
    std::vector<std::string> finf_files;
    std::cout << "directory: " << directory << std::endl;
    
    if (!fs::exists(directory)) {
        std::cerr << "Error: Directory '" << directory << "' not found" << std::endl;
        return finf_files;
    }
    
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().string();
                if (filename.size() >= suffix.size() &&
                    filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    finf_files.push_back(filename);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return finf_files;
}

void process_finf_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream file(input_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file '" << input_path << "'" << std::endl;
        return;
    }
    
    std::vector<std::string> lines;
    std::string line;
    
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    
    for (size_t i = 0; i < lines.size(); ++i) {
        std::istringstream iss(lines[i]);
        std::string prefix;
        iss >> prefix;
        
        if (prefix == "do") {
            double do_value;
            if (iss >> do_value) {
                do_value = do_value * 10;
                std::ostringstream oss;
                oss << std::left << "do\t" << do_value;
                lines[i] = oss.str();
            }
        } else if (prefix == "Nim") {
            int nim_value;
            if (iss >> nim_value) {
                nim_value = nim_value / 10;
                std::ostringstream oss;
                oss << std::left << "Nim\t" << nim_value;
                lines[i] = oss.str();
            }
        } else if (prefix == "Eti") {
            double eti_value;
            if (iss >> eti_value) {
                eti_value = eti_value * 10;
                std::ostringstream oss;
                oss << std::left << "Eti\t" << eti_value;
                lines[i] = oss.str();
            }
        }
    }
    
    fs::path output_file_path(output_path);
    fs::path output_dir = output_file_path.parent_path();
    if (!output_dir.empty() && !fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }
    
    std::ofstream out_file(output_path);
    if (!out_file.is_open()) {
        std::cerr << "Error: Cannot open file for writing '" << output_path << "'" << std::endl;
        return;
    }
    
    for (const auto& l : lines) {
        out_file << l << std::endl;
    }
    out_file.close();
}

int process_all_finf_files(const std::string& input_dir, const std::string& output_dir) {
    std::vector<std::string> finf_list = search_finf_files(input_dir);
    
    if (finf_list.empty()) {
        std::cout << "No .finf files found in input directory" << std::endl;
        return 0;
    }
    
    std::cout << "Found " << finf_list.size() << " .finf file(s)" << std::endl;
    
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }
    
    for (const auto& input_path : finf_list) {
        fs::path input_file_path(input_path);
        std::string filename = input_file_path.filename().string();
        
        std::string output_path = output_dir;
        if (output_path.back() != '/' && output_path.back() != '\\') {
            output_path += "/";
        }
        output_path += filename;
        
        process_finf_file(input_path, output_path);
        std::cout << "Processed: " << filename << std::endl;
    }
    
    return finf_list.size();
}

