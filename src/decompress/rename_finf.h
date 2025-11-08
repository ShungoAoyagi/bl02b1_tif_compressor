#ifndef RENAME_FINF_H
#define RENAME_FINF_H

#include <string>
#include <vector>

/// 指定されたディレクトリ内の.finfファイルを検索
std::vector<std::string> search_finf_files(const std::string& directory);

/// 単一の.finfファイルを処理
void process_finf_file(const std::string& input_path, const std::string& output_path);

/// 全ての.finfファイルを処理
int process_all_finf_files(const std::string& input_dir, const std::string& output_dir);

#endif // RENAME_FINF_H

