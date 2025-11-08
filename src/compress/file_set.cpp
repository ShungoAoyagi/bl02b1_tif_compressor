#include "file_set.hpp"
#include "../common/common.hpp"
#include <filesystem>
#include <regex>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

std::string FileSet::getOutputPath(const std::string &outputDir) const
{
    // 最初のファイルのパスからファイル名を取得
    fs::path firstFilePath(firstFile);
    std::string filename = firstFilePath.stem().string(); // 拡張子を除いたファイル名
    return outputDir + "/" + filename + ".lz4";
}

std::vector<FileSet> scanAndGroupFiles(const std::string &dir, const std::string &basePattern, int setSize)
{
    std::map<std::pair<int, int>, FileSet> fileSets; // (run, setNumber) -> FileSet

    // 正規表現パターン作成（例："test_(\d\d)_(\d\d\d\d\d)\.tif"）
    std::regex filePattern(basePattern.substr(0, basePattern.find("_##_")) +
                           "_([0-9]{2})_([0-9]{5})\\.tif");

    LOG("Scanning directory: " << dir);

    try
    {
        // ディレクトリを走査
        for (const auto &entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;

            std::string filename = entry.path().filename().string();
            std::smatch matches;

            if (std::regex_match(filename, matches, filePattern) && matches.size() >= 3)
            {
                int run = std::stoi(matches[1].str());
                int fileNumber = std::stoi(matches[2].str());
                int setNumber = ((fileNumber - 1) / setSize) * setSize + 1; // セットの先頭番号を計算

                auto key = std::make_pair(run, setNumber);
                if (fileSets.find(key) == fileSets.end())
                {
                    // 新しいセットを作成
                    FileSet newSet;
                    newSet.run = run;
                    newSet.setNumber = setNumber;
                    fileSets[key] = newSet;
                }

                // ファイルをセットに追加
                fileSets[key].files.insert(entry.path().string());

                // セット内の最初のファイルを記録
                if (fileNumber == setNumber)
                {
                    fileSets[key].firstFile = entry.path().string();
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG("Error scanning directory: " << e.what());
    }

    // マップからベクターに変換
    std::vector<FileSet> result;
    for (const auto &pair : fileSets)
    {
        result.push_back(pair.second);
    }

    // セット番号でソート
    std::sort(result.begin(), result.end(), [](const FileSet &a, const FileSet &b)
              {
        if (a.run != b.run) return a.run < b.run;
        return a.setNumber < b.setNumber; });

    return result;
}

bool isSetComplete(const FileSet &fileSet, int setSize)
{
    return fileSet.files.size() >= static_cast<size_t>(setSize);
}

bool isSetProcessed(const FileSet &fileSet, const std::string &outputDir)
{
    std::string outputPath = fileSet.getOutputPath(outputDir);
    return fs::exists(outputPath);
}

