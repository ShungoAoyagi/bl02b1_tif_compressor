#ifndef COMPRESS_TO_LZ4_HPP
#define COMPRESS_TO_LZ4_HPP

#include <string>
#include <set>

// ファイルのセットを並列で読み込み、LZ4で圧縮する
// ファイル名、ファイルサイズ、ファイル形式などのメタデータを含めて圧縮する
// files: 圧縮するファイルパスのセット
// outputPath: 出力ファイルパス
// maxThreads: 並列読み込みの最大スレッド数（デフォルト: 4）
// lz4Acceleration: LZ4圧縮の高速化パラメータ（1=default、高いほど高速だが圧縮率低下、デフォルト: 1）
// 戻り値: 成功した場合true、失敗した場合false
bool compressFilesToLZ4(const std::set<std::string>& files, 
                        const std::string& outputPath,
                        int maxThreads = 4,
                        int lz4Acceleration = 1);

#endif // COMPRESS_TO_LZ4_HPP

