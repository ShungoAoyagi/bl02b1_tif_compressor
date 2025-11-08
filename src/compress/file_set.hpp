#ifndef FILE_SET_HPP
#define FILE_SET_HPP

#include <set>
#include <string>
#include <vector>

// ファイルセットをグループ化するための構造体
struct FileSet
{
    int run;
    int setNumber;               // setNumberはファイルセットの先頭番号
    std::set<std::string> files; // セット内のファイルパス
    std::string firstFile;       // 最初のファイル（パターン基準）

    // 出力ファイル名の生成
    std::string getOutputPath(const std::string &outputDir) const;
};

// ディレクトリをスキャンし、パターンに合致するファイルをセットとしてグループ化
std::vector<FileSet> scanAndGroupFiles(const std::string &dir, const std::string &basePattern, int setSize);

// セットが完全であるか確認（ファイル数がsetSize個あるか）
bool isSetComplete(const FileSet &fileSet, int setSize);

// 既に処理済みのセットか確認（出力ファイルが存在するか）
bool isSetProcessed(const FileSet &fileSet, const std::string &outputDir);

#endif // FILE_SET_HPP

