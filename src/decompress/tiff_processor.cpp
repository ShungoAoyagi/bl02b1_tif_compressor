#include "tiff_processor.hpp"
#include "../common/common.hpp"
#include <tiffio.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdarg>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

TiffHeaderInfo::TiffHeaderInfo()
    : compression(1),      // COMPRESSION_NONE
      photometric(1),      // PHOTOMETRIC_MINISBLACK
      orientation(1),      // ORIENTATION_TOPLEFT
      planarConfig(1),     // PLANARCONFIG_CONTIG
      xResolution(1.0f),
      yResolution(1.0f),
      resolutionUnit(2)    // RESUNIT_INCH
{
}

// --- メモリからTIFF読み出し用のコールバックと構造体 ---
struct MemBuffer
{
    const unsigned char *data;
    tsize_t size;
    tsize_t pos;
};

static tsize_t memRead(thandle_t handle, void *buf, tsize_t size)
{
    MemBuffer *mem = reinterpret_cast<MemBuffer *>(handle);
    tsize_t remaining = mem->size - mem->pos;
    tsize_t toCopy = (size > remaining) ? remaining : size;
    std::memcpy(buf, mem->data + mem->pos, toCopy);
    mem->pos += toCopy;
    return toCopy;
}

static toff_t memSeek(thandle_t handle, toff_t off, int whence)
{
    MemBuffer *mem = reinterpret_cast<MemBuffer *>(handle);
    toff_t newpos;
    switch (whence)
    {
    case SEEK_SET:
        newpos = off;
        break;
    case SEEK_CUR:
        newpos = mem->pos + off;
        break;
    case SEEK_END:
        newpos = mem->size + off;
        break;
    default:
        return -1;
    }
    if (newpos < 0 || newpos > mem->size)
        return -1;
    mem->pos = newpos;
    return newpos;
}

static int memClose(thandle_t /*handle*/)
{
    return 0;
}

static toff_t memSize(thandle_t handle)
{
    MemBuffer *mem = reinterpret_cast<MemBuffer *>(handle);
    return mem->size;
}

static tsize_t memWrite(thandle_t /*handle*/, void * /*buf*/, tsize_t /*size*/)
{
    return 0;
}

static int memMap(thandle_t /*handle*/, void ** /*pData*/, toff_t * /*pSize*/)
{
    return 0;
}

static void memUnmap(thandle_t /*handle*/, void * /*pData*/, toff_t /*size*/)
{
}

bool readTiffFloat(const FileEntry &entry, std::vector<float> &image, uint32_t &width, uint32_t &height, TiffHeaderInfo &headerInfo)
{
    MemBuffer memBuffer;
    memBuffer.data = reinterpret_cast<const unsigned char *>(entry.data.data());
    memBuffer.size = entry.data.size();
    memBuffer.pos = 0;

    TIFF *tif = TIFFClientOpen("InMemoryTIFF", "r",
                               reinterpret_cast<thandle_t>(&memBuffer),
                               memRead, memWrite, memSeek, memClose,
                               memSize, memMap, memUnmap);
    if (!tif)
    {
        std::cerr << "TIFFClientOpen failed: " << entry.name << std::endl;
        return false;
    }

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
    {
        std::cerr << "TIFFGetField failed: " << entry.name << std::endl;
        TIFFClose(tif);
        return false;
    }

    uint16_t bitsPerSample = 0, samplesPerPixel = 0, sampleFormat = 0;
    if (!TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample))
    {
        bitsPerSample = 8;
    }
    if (!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel))
    {
        samplesPerPixel = 1;
    }
    if (!TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat))
    {
        sampleFormat = SAMPLEFORMAT_UINT;
    }
    if (samplesPerPixel != 1)
    {
        std::cerr << "Only single sample per pixel images are supported." << std::endl;
        TIFFClose(tif);
        return false;
    }

    // ヘッダー情報の取得
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &headerInfo.compression);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &headerInfo.photometric);
    TIFFGetField(tif, TIFFTAG_ORIENTATION, &headerInfo.orientation);
    TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &headerInfo.planarConfig);
    TIFFGetField(tif, TIFFTAG_XRESOLUTION, &headerInfo.xResolution);
    TIFFGetField(tif, TIFFTAG_YRESOLUTION, &headerInfo.yResolution);
    TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &headerInfo.resolutionUnit);

    char *dateTime = nullptr;
    if (TIFFGetField(tif, TIFFTAG_DATETIME, &dateTime) && dateTime)
        headerInfo.dateTime = dateTime;

    char *software = nullptr;
    if (TIFFGetField(tif, TIFFTAG_SOFTWARE, &software) && software)
        headerInfo.software = software;

    char *description = nullptr;
    if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &description) && description)
        headerInfo.description = description;

    char *artist = nullptr;
    if (TIFFGetField(tif, TIFFTAG_ARTIST, &artist) && artist)
        headerInfo.artist = artist;

    char *copyright = nullptr;
    if (TIFFGetField(tif, TIFFTAG_COPYRIGHT, &copyright) && copyright)
        headerInfo.copyright = copyright;

    const size_t npixels = width * height;
    image.resize(npixels);

    for (uint32_t row = 0; row < height; row++)
    {
        if (sampleFormat == SAMPLEFORMAT_IEEEFP && bitsPerSample == 32)
        {
            if (TIFFReadScanline(tif, &image[row * width], row, 0) != 1)
            {
                TIFFClose(tif);
                return false;
            }
        }
        else if (sampleFormat == SAMPLEFORMAT_UINT)
        {
            if (bitsPerSample == 8)
            {
                std::vector<uint8_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else if (bitsPerSample == 16)
            {
                std::vector<uint16_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else if (bitsPerSample == 32)
            {
                std::vector<uint32_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else
            {
                std::cerr << "Unsupported bits per sample: " << bitsPerSample << std::endl;
                TIFFClose(tif);
                return false;
            }
        }
        else if (sampleFormat == SAMPLEFORMAT_INT)
        {
            if (bitsPerSample == 8)
            {
                std::vector<int8_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else if (bitsPerSample == 16)
            {
                std::vector<int16_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else if (bitsPerSample == 32)
            {
                std::vector<int32_t> buffer(width);
                if (TIFFReadScanline(tif, buffer.data(), row, 0) != 1)
                {
                    TIFFClose(tif);
                    return false;
                }
                for (uint32_t col = 0; col < width; col++)
                {
                    image[row * width + col] = static_cast<float>(buffer[col]);
                }
            }
            else
            {
                TIFFClose(tif);
                return false;
            }
        }
        else
        {
            TIFFClose(tif);
            return false;
        }
    }
    TIFFClose(tif);
    return true;
}

bool readTiffFloat(const FileEntry &entry, std::vector<float> &image, uint32_t &width, uint32_t &height)
{
    TiffHeaderInfo dummyHeader;
    return readTiffFloat(entry, image, width, height, dummyHeader);
}

bool writeTiffInt32Aligned(const std::string &file,
                           const std::vector<float> &img,
                           uint32_t w, uint32_t h,
                           const TiffHeaderInfo &hdr)
{
    TIFF *tif = TIFFOpen(file.c_str(), "w+");
    if (!tif)
    {
        std::cerr << "open fail\n";
        return false;
    }

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_INT);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, hdr.photometric);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, hdr.planarConfig);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, h);
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, hdr.xResolution);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, hdr.yResolution);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, hdr.resolutionUnit);
    if (!hdr.dateTime.empty())
        TIFFSetField(tif, TIFFTAG_DATETIME, hdr.dateTime.c_str());
    if (!hdr.software.empty())
        TIFFSetField(tif, TIFFTAG_SOFTWARE, hdr.software.c_str());
    if (!hdr.description.empty())
        TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, hdr.description.c_str());

    int fd = TIFFFileno(tif);
#ifdef _WIN32
    __int64 cur = _lseeki64(fd, 0, SEEK_END);
#else
    off_t cur = lseek(fd, 0, SEEK_END);
#endif
    long pad = (4096 - (cur % 4096)) % 4096;
    if (pad)
    {
        std::vector<char> zeros(pad, 0);
#ifdef _WIN32
        _write(fd, zeros.data(), pad);
#else
        write(fd, zeros.data(), pad);
#endif
    }

    std::vector<int32_t> row(w);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
            row[x] = static_cast<int32_t>(img[y * w + x]);
        TIFFWriteScanline(tif, row.data(), y, 0);
    }

    TIFFWriteDirectory(tif);
    TIFFClose(tif);

    return true;
}

bool writeTiffInt32(const std::string &filename, const std::vector<float> &image, uint32_t width, uint32_t height)
{
    TiffHeaderInfo defaultHeader;
    return writeTiffInt32Aligned(filename, image, width, height, defaultHeader);
}

bool writeTiffInt32WithOriginalHeader(const std::string &filename,
                                      const std::vector<float> &image,
                                      uint32_t width, uint32_t height,
                                      const FileEntry &originalTiffEntry)
{
    std::vector<char> tiffData = originalTiffEntry.data;

    MemBuffer memBuffer;
    memBuffer.data = reinterpret_cast<const unsigned char *>(originalTiffEntry.data.data());
    memBuffer.size = originalTiffEntry.data.size();
    memBuffer.pos = 0;

    TIFF *originalTif = TIFFClientOpen("OriginalTIFF", "r",
                                       reinterpret_cast<thandle_t>(&memBuffer),
                                       memRead, memWrite, memSeek, memClose,
                                       memSize, memMap, memUnmap);
    if (!originalTif)
    {
        std::cerr << "Failed to open original TIFF for reading" << std::endl;
        return false;
    }

    uint32_t stripCount = TIFFNumberOfStrips(originalTif);
    std::vector<uint64_t> stripOffsets(stripCount);
    std::vector<uint64_t> stripByteCounts(stripCount);

    for (uint32_t i = 0; i < stripCount; i++)
    {
        stripOffsets[i] = TIFFGetStrileOffset(originalTif, i);
        stripByteCounts[i] = TIFFGetStrileByteCount(originalTif, i);
    }

    TIFFClose(originalTif);

    std::vector<int32_t> int32Data(width * height);
    for (size_t i = 0; i < image.size(); i++)
    {
        int32Data[i] = static_cast<int32_t>(image[i]);
    }

    size_t dataSize = int32Data.size() * sizeof(int32_t);

    if (stripCount > 0 && stripOffsets[0] + dataSize <= tiffData.size())
    {
        std::memcpy(&tiffData[stripOffsets[0]], int32Data.data(), dataSize);
    }
    else
    {
        std::cerr << "Strip offset or data size mismatch" << std::endl;
        return false;
    }

    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Failed to create output file: " << filename << std::endl;
        return false;
    }

    outFile.write(tiffData.data(), tiffData.size());
    outFile.close();

    return true;
}

void mergeTiffFilesWithLibTiff(const std::vector<FileEntry> &entries, 
                              const std::string &prefix_with_run, 
                              const std::string &outputFolder, 
                              const int s_img, const int e_img, 
                              const int integ_frame_num)
{
    fs::create_directories(outputFolder);

    int incre_num = e_img - s_img + 1;
    int inc_set = static_cast<int>(std::round(double(incre_num) / integ_frame_num));

    uint32_t width = 0, height = 0;
    bool size_initialized = false;

    const FileEntry *originalTiffEntry = nullptr;

    std::vector<std::vector<float>> merged_images(inc_set);

    std::unordered_map<std::string, const FileEntry *> file_map;
    for (const auto &entry : entries)
    {
        file_map[entry.name] = &entry;
    }

    for (int t = 0; t < integ_frame_num; t++)
    {
        for (int i = 0; i < inc_set; i++)
        {
            int idx = s_img + i * integ_frame_num + t;
            std::string i_num = zeroPad(idx, 5);
            std::string input_name = prefix_with_run + i_num + ".tif";

            auto it = file_map.find(input_name);
            if (it == file_map.end())
            {
                continue;
            }

            std::vector<float> img;
            uint32_t imgWidth = 0, imgHeight = 0;

            if (!readTiffFloat(*(it->second), img, imgWidth, imgHeight))
            {
                continue;
            }

            if (!size_initialized)
            {
                width = imgWidth;
                height = imgHeight;
                size_initialized = true;
                originalTiffEntry = it->second;

                for (int j = 0; j < inc_set; j++)
                {
                    merged_images[j].assign(width * height, 0.0f);
                }
            }

            if (img.size() != width * height)
            {
                std::cerr << "Image size mismatch: " << input_name << std::endl;
                continue;
            }

            for (size_t p = 0; p < img.size(); p++)
            {
                merged_images[i][p] += img[p];
            }
        }
    }

    for (int i = 0; i < inc_set; i++)
    {
        if (merged_images[i].empty())
        {
            std::cerr << "Failed to initialize group " << zeroPad(i + 1, 5) << std::endl;
            continue;
        }

        float threshold = -1.0f * integ_frame_num;
        for (size_t p = 0; p < merged_images[i].size(); p++)
        {
            if (merged_images[i][p] == threshold)
                merged_images[i][p] = -1.0f;
            else if (merged_images[i][p] < threshold)
                merged_images[i][p] = -2.0f;
        }

        std::string output_name = outputFolder + "/" + prefix_with_run + zeroPad(s_img / 10 + i + 1, 5) + ".tif";
        if (originalTiffEntry)
        {
            if (!writeTiffInt32WithOriginalHeader(output_name, merged_images[i], width, height, *originalTiffEntry))
            {
                std::cerr << "TIFF output failed: " << output_name << std::endl;
            }
        }
        else
        {
            std::cerr << "No original TIFF entry available for: " << output_name << std::endl;
        }
    }
}

void extractTiffFilesFromMemory(const std::vector<FileEntry> &entries, const std::string &outputFolder)
{
    fs::create_directories(outputFolder);

    for (const auto &entry : entries)
    {
        std::string filename = entry.name;
        size_t dotPos = filename.find_last_of(".");
        if (dotPos != std::string::npos)
        {
            std::string extension = filename.substr(dotPos + 1);
            for (char &c : extension)
            {
                c = std::tolower(c);
            }

            if (extension == "tif" || extension == "tiff")
            {
                std::string outputPath = outputFolder + "/" + filename;

                std::ofstream outFile(outputPath, std::ios::binary);
                if (!outFile)
                {
                    std::cerr << "Failed to create output file: " << outputPath << std::endl;
                    continue;
                }
                outFile.write(entry.data.data(), entry.data.size());
                outFile.close();
            }
        }
    }
}

