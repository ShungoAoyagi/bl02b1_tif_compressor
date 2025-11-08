#ifndef COMPRESS_TO_SNAPPY_HPP
#define COMPRESS_TO_SNAPPY_HPP

#include <string>
#include <set>

// ファイルのセットを並列で読み込み、snappyで圧縮する
// ファイル名、ファイルサイズ、ファイル形式などのメタデータを含めて圧縮する
// files: 圧縮するファイルパスのセット
// outputPath: 出力ファイルパス
// maxThreads: 並列読み込みの最大スレッド数（デフォルト: 4）
// 戻り値: 成功した場合true、失敗した場合false
bool compressFilesToSnappy(const std::set<std::string>& files, 
                          const std::string& outputPath,
                          int maxThreads = 4);

#endif // COMPRESS_TO_SNAPPY_HPP
