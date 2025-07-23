#pragma once

#ifdef _MSC_VER
    #include <string>
#endif

#include <filesystem>
#include <fstream>
#include <vector>
#include <string.h>

class deinterleaver
{
protected:
    static constexpr int CD_SECTOR_SIZE = 2352;
    static constexpr int XA_DATA_SIZE = 2336;

    struct FileInfo
    {
        std::string fileName;
        int sectorBlock;
        int sectorCount = 0;
        int sectorStride = 0;
        int nullTermination = 0;
        int filenum;
        int channel;
        int begSec;
        std::streampos endPos;
    };

private:
    unsigned char buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};
    int offset;
    int inputSectorSize;
    const std::filesystem::path inputPath;

    std::streampos parse(const uintmax_t &fileSize, std::ifstream &inputFile, int &maxPos)
    {
        std::streampos begPos = inputFile.tellg();
        if (buffer[0x12] & 0x04)
        {
            FileInfo &entry = entries.emplace_back();
            entry.filenum = buffer[0x10];
            entry.channel = buffer[0x11];
            entry.begSec = (begPos -= inputSectorSize) / inputSectorSize;
            entry.fileName = std::to_string(entries.size() - 1);
            /*entry.fileName = inputPath.stem().string() + "_"
                            + std::string(std::max(static_cast<int>(sizeof("00") - entry.fileName.length()), 0), '0')
                            + std::move(entry.fileName) + ".xa";

            if (!std::filesystem::exists(outputDir))
                std::filesystem::create_directories(outputDir);
            std::ofstream outputFile(outputDir / entry.fileName, std::ios::binary);
            if (!outputFile)
            {
                printf("Error: Cannot write output file.\n");
                exit(EXIT_FAILURE);
            }
            else
                printf("Deinterleaving %s... ", entry.fileName.c_str());*/

            int skipSize;
            while (entry.filenum == buffer[0x10] &&
                   (entry.channel == buffer[0x11] ||
                    !(buffer[0x12] & 0x7F)))
            {
                if (!(buffer[0x12] & 0x04))
                    entry.nullTermination++;
                else if (entry.nullTermination)
                    break;
                //else
                    //outputFile.write(reinterpret_cast<const char*>(buffer) + offset, sectorSize);

                entry.sectorCount++;
                if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                {
                    inputFile.clear();
                    break;
                }

                if (entry.channel != buffer[0x11] ||
                    !(buffer[0x12] & 0x04))
                {
                    if (entry.sectorStride)
                    {
                        inputFile.seekg(skipSize, std::ios::cur);
                        if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                        {
                            inputFile.clear();
                            break;
                        }
                    }
                    else
                    {
                        while (entry.channel != buffer[0x11] ||
                               !(buffer[0x12] & 0x04))
                        {
                            entry.sectorStride++;
                            if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                            {
                                inputFile.clear();
                                break;
                            }
                        }

                        entry.sectorBlock = entry.sectorCount;
                        skipSize = (entry.sectorStride - entry.sectorBlock) * inputSectorSize;
                    }
                }
            }
            entry.endPos = inputFile.tellg();
            if (entry.endPos < fileSize)
                entry.endPos -= inputSectorSize;
            if (entry.endPos > maxPos)
                maxPos = entry.endPos;

            //outputFile.close();
            //printf("Done\n");

            if (entry.sectorStride)
                inputFile.seekg(begPos += inputSectorSize, std::ios::beg);
        }
        return inputFile.tellg();
    }

    // Virtual function to fill the manifest as needed.
    virtual void createManifest(const std::filesystem::path &outputDir, const std::string &fileName)
    {
        std::ofstream manifest(outputDir / fileName);
        if (!manifest)
        {
            fprintf(stderr, "Error: Cannot write manifest file.\n");
            return;
        }

        for (const FileInfo &entry : entries)
        {
            manifest << entry.sectorBlock << "," << (inputSectorSize == XA_DATA_SIZE ? "xa" : "xacd")
                     << "," << entry.fileName << "," << entry.nullTermination << "," << entry.filenum << "," << entry.channel
                     /*<< "," << entry.begSec << "-" << entry.endPos / inputSectorSize - entry.sectorStride - 1*/ << "\n";
        }
        manifest.close();
    }

public:
    std::vector<FileInfo> entries;

    // inputPath must be an interleaved .xa or .str file. CD image files may have unexpected results.
    deinterleaver(const std::filesystem::path inputPath) : inputPath(inputPath)
    {
        std::ifstream inputFile(inputPath, std::ios::binary);

        unsigned char sync[12];
        if (!inputFile.read(reinterpret_cast<char*>(sync), sizeof(sync)))
        {
            fprintf(stderr, "Error: Cannot read input file.\n");
            return;
        }
        inputFile.seekg(0, std::ios::beg);

        const uintmax_t fileSize = std::filesystem::file_size(inputPath);
        inputSectorSize = !(fileSize % CD_SECTOR_SIZE) && !memcmp(sync, buffer, sizeof(sync)) ? CD_SECTOR_SIZE : XA_DATA_SIZE;
        offset = CD_SECTOR_SIZE - inputSectorSize;

        int index = 0;
        int maxPos = 0;
        std::streampos currentPos;
        while (inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
        {
            currentPos = parse(fileSize, inputFile, maxPos);

            if (currentPos < maxPos &&
                currentPos > (entries[index].begSec + entries[index].sectorStride) * inputSectorSize)
            {
                std::streampos minPos = fileSize;
                for (const auto &entry : entries)
                {
                    if (entry.endPos > currentPos &&
                        entry.endPos < minPos)
                        minPos = entry.endPos;
                }
                if (minPos == maxPos)
                    index = entries.size();

                inputFile.seekg(minPos, std::ios::beg);
            }
        }
        inputFile.close();

        if (entries.empty())
            printf("No valid entries found in the input file.\n");
    }

    // outputDir must be a directory (not a file).
    // sectorSize must be 2336 or 2352 to change the output size.
    void deinterleave(const std::filesystem::path outputDir, int sectorSize = 0)
    {
        if (entries.empty())
            return;

        std::ifstream inputFile(inputPath, std::ios::binary);
        if (!sectorSize)
            sectorSize = inputSectorSize;
        const int outOffset = CD_SECTOR_SIZE - sectorSize;

        if (!std::filesystem::exists(outputDir))
            std::filesystem::create_directories(outputDir);

        std::string namePrefix = inputPath.stem().string() + "_";
        size_t namePadWidth = std::max(std::to_string(entries.size() - 1).length(), (std::size_t)2);
        for (FileInfo &entry : entries)
        {
            entry.fileName = namePrefix + std::string(namePadWidth - entry.fileName.length(), '0') + std::move(entry.fileName) + ".xa";
            std::ofstream outputFile(outputDir / entry.fileName, std::ios::binary);
            if (!outputFile)
            {
                fprintf(stderr, "Error: Cannot write output file.\n");
                return;
            }
            else
                printf("Deinterleaving %s... ", entry.fileName.c_str());

            inputFile.seekg(entry.begSec * inputSectorSize, std::ios::beg);
            std::streampos limit(entry.endPos - std::streampos(entry.nullTermination * (entry.sectorStride + 1) * inputSectorSize));
            while (inputFile.tellg() < limit)
            {
                for (int i = 0; i == 0 || i < entry.sectorBlock; ++i)
                {
                    if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                    {
                        inputFile.clear();
                        break;
                    }
                    outputFile.write(reinterpret_cast<const char*>(buffer) + outOffset, sectorSize);
                }
                inputFile.seekg(entry.sectorStride * inputSectorSize, std::ios::cur);
            }

            outputFile.close();
            printf("Done\n");
        }

        createManifest(outputDir, inputPath.stem().string() + ".csv");
    }
};
