// Original code and idea by asiekierka (https://github.com/ChenThread/candyk-psx/blob/master/toolsrc/xainterleave/xainterleave.c)
// But completely rewrited by me (Nagtan)

#pragma once

#ifdef _MSC_VER
    #define strtok_r strtok_s
    #define strncasecmp _strnicmp
    #include <string>
#endif

#include <filesystem>
#include <fstream>
#include <vector>
#include <string.h>
#include <optional>

class interleaver
{
protected:
    static constexpr int CD_SECTOR_SIZE = 2352;
    static constexpr int XA_DATA_SIZE   = 2336;

    struct FileInfo
    {
        int sectorChunk = 1;
        int sectorSize;
        std::filesystem::path filePath;
        int nullTermination;
        std::optional<uint8_t> filenum;
        std::optional<uint8_t> channel;
        alignas(int) uint8_t nullSubheader[4];

        uintmax_t fileSize;
        int begSec;
        int endSec;
    };

private:
    const int sectorStride;
    static constexpr int FILENUM_OFFSET = 0x10;
    static constexpr int CHANNEL_OFFSET = 0x11;

    // Virtual function to fill null sectors as needed.
    virtual void nullCustomizer(unsigned char *emptyBuffer, FileInfo &entry)
    {
        if (*reinterpret_cast<int*>(entry.nullSubheader) != 0)
            return;
        entry.nullSubheader[0] = entry.filenum.value_or(emptyBuffer[FILENUM_OFFSET]);
        //entry.nullSubheader[1] = entry.nullTermination >= 0 ? entry.channel.value_or(0) : 0;
        //entry.nullSubheader[2] = 0x48;
        //entry.nullSubheader[3] = 0x00;
    }

public:
    std::vector<FileInfo> entries;

    // inputPath must be a .csv or .txt or any text file with the appropriate format.
    // sectorStride must be like the psx XA format (2/4/8/16/32).
    interleaver(const std::filesystem::path inputPath, const int sectorStride) : sectorStride(sectorStride)
    {
        std::ifstream inputFile(inputPath);
        if (!inputFile.is_open())
        {
            fprintf(stderr, "Error: Cannot read \"%s\". %s\n", inputPath.filename().string().c_str(), strerror(errno));
            return;
        }

        char *field;
        char *saveptr;
        std::string line;
        size_t lastMinSec = 0;
        int div_check = sectorStride;

        while (std::getline(inputFile, line))
        {
            FileInfo entry {};
            entry.sectorChunk = atoi(strtok_r(line.data(), ",", &saveptr));
            if (entry.sectorChunk < 1)
                continue;
            else if (entry.sectorChunk > sectorStride ||
                    div_check < 0)
            {
                fprintf(stderr, "Error: Consecutive sectors are not divisible by %d\n", sectorStride);
                std::vector<FileInfo>().swap(entries);
                return;
            }

            if (!(field = strtok_r(NULL, ",", &saveptr)) ||
                !strncasecmp(field, "null", 4))
                entry.sectorSize = 0;
            else if (!strncasecmp(field, "xacd", 4))
                entry.sectorSize = CD_SECTOR_SIZE;
            else if (!strncasecmp(field, "xa", 2))
                entry.sectorSize = XA_DATA_SIZE;
            else
            {
                fprintf(stderr, "Warning: Unknown type \"%s\" at line %zu\n", field, entries.size() + 1);
                continue;
            }

            if (entry.sectorSize)
            {
                if ((field = strtok_r(NULL, ",", &saveptr)))
                {
                    entry.filePath = inputPath.parent_path() / field;
                    std::ifstream input(entry.filePath, std::ios::binary);
                    if (!input.is_open())
                    {
                        fprintf(stderr, "Error: Cannot read \"%s\" at line %zu. %s\n", entry.filePath.string().c_str(), entries.size() + 1, strerror(errno));
                        std::vector<FileInfo>().swap(entries);
                        return;
                    }
                    input.close();

                    entry.fileSize = std::filesystem::file_size(entry.filePath);
                    if (!entry.fileSize ||
                        entry.fileSize % entry.sectorSize)
                    {
                        fprintf(stderr, "Error: Invalid type for \"%s\" at line %zu\n", field, entries.size() + 1);
                        std::vector<FileInfo>().swap(entries);
                        return;
                    }
                }
                else
                {
                    fprintf(stderr, "Error: Empty file name at line %zu\n", entries.size() + 1);
                    std::vector<FileInfo>().swap(entries);
                    return;
                }

                if ((field = strtok_r(NULL, ",", &saveptr)))
                {
                    entry.nullTermination = atoi(field);
                    if ((field = strtok_r(NULL, ",", &saveptr)))
                    {
                        entry.filenum = atoi(field);
                        if ((field = strtok_r(NULL, ",", &saveptr)))
                        {
                            entry.channel = atoi(field);
                            if ((field = strtok_r(NULL, ",", &saveptr)) &&
                                !strncasecmp(field, "0x", 2))
                            {
                                field += 2;
                                sscanf(field, "%2hhx%2hhx%2hhx%2hhx", &entry.nullSubheader[0], &entry.nullSubheader[1], &entry.nullSubheader[2], &entry.nullSubheader[3]);
                            }
                        }
                    }
                }

                if (div_check - entry.sectorChunk >= 0)
                {
                    entry.begSec = entries.size();
                    entry.endSec = (entry.fileSize / entry.sectorSize + entry.nullTermination) * sectorStride + entry.begSec;
                }
                else
                {
                    size_t minSec = SIZE_MAX;
                    for (const auto &e : entries)
                    {
                        if (e.endSec > lastMinSec &&
                            e.endSec < minSec)
                            minSec = e.endSec;
                    }
                    entry.begSec = lastMinSec = minSec;
                    entry.endSec = (entry.fileSize / entry.sectorSize + entry.nullTermination) * sectorStride + entry.begSec;
                }
            }

            if (div_check)
                div_check -= entry.sectorChunk;
            entries.push_back(std::move(entry));
        }
    }

    // outputFile must be opened in read and write binary (w+b) mode.
    // sectorSize must be 2336 or 2352 to change the output size.
    void interleave(std::fstream &outputFile, int sectorSize = 0)
    {
        unsigned char buffer[CD_SECTOR_SIZE];
        unsigned char emptyBuffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};
        std::vector<std::ifstream> inputFiles(sectorStride);
        std::vector<FileInfo> workingEntries = entries;

        int can_read = 0;
        //int sectorsToFill = sectorStride;
        for (const auto &entry : workingEntries)
        {
            if (entry.sectorSize &&
                !entry.filePath.empty())
            {
                if (can_read < sectorStride)
                    inputFiles[can_read].open(entry.filePath, std::ios::binary);
                if (!sectorSize)
                    sectorSize = entry.sectorSize;
                can_read++;
            }
            //sectorsToFill -= entry.sectorChunk;
        }
        const int outOffset = CD_SECTOR_SIZE - sectorSize;

        /*/ Fill with null sectors to achieve the desired stride
        if (sectorsToFill > 0 &&
            sectorsToFill < sectorStride)
        {
            for (int i = workingEntries.size() - 1; sectorsToFill > 0; ++i, --sectorsToFill)
            {
                FileInfo &e = workingEntries.emplace_back();
                e.filenum = workingEntries[i].filenum;
            }
        }*/

        while (can_read > 0)
        {
            for (int i = 0; i < sectorStride && i < workingEntries.size(); ++i)
            {
                FileInfo &entry = workingEntries[i];
                std::ifstream &inputFile = inputFiles[i];

                for (int is = 0; is < entry.sectorChunk; ++is)
                {
                    if (entry.sectorSize &&
                        inputFile.read(reinterpret_cast<char*>(buffer) + (CD_SECTOR_SIZE - entry.sectorSize), entry.sectorSize))
                    {
                        buffer[FILENUM_OFFSET + 4] = buffer[FILENUM_OFFSET] = entry.filenum.value_or(buffer[FILENUM_OFFSET]);
                        buffer[CHANNEL_OFFSET + 4] = buffer[CHANNEL_OFFSET] = entry.channel.value_or(buffer[CHANNEL_OFFSET]);
                        outputFile.write(reinterpret_cast<const char*>(buffer) + outOffset, sectorSize);
                    }
                    else
                    {
                        emptyBuffer[FILENUM_OFFSET] = buffer[FILENUM_OFFSET];
                        nullCustomizer(emptyBuffer, entry);
                        memcpy(&emptyBuffer[FILENUM_OFFSET], &entry.nullSubheader, sizeof(entry.nullSubheader));
                        memcpy(&emptyBuffer[FILENUM_OFFSET + 4], &entry.nullSubheader, sizeof(entry.nullSubheader));
                        outputFile.write(reinterpret_cast<const char*>(emptyBuffer) + outOffset, sectorSize);
                    }
                }

                if (inputFile.is_open() &&
                    inputFile.tellg() >= entry.fileSize &&
                    entry.nullTermination-- <= 0)
                {
                    inputFiles[i].close();
                    printf("Interleaving %s... Done\n", entry.filePath.filename().string().c_str());
                    if (sectorStride < workingEntries.size())
                    {
                        workingEntries[i] = workingEntries[sectorStride];
                        workingEntries.erase(workingEntries.begin() + sectorStride);
                        inputFiles[i].open(entry.filePath, std::ios::binary);
                    }
                    can_read--;
                }
            }
        }

        outputFile.seekg(-2334, std::ios::end);
        int EOFbit = outputFile.get() | 0x80;
        outputFile.seekp(-2334, std::ios::end);
        outputFile.put(EOFbit);
        outputFile.seekp(3, std::ios::cur);
        outputFile.put(EOFbit);
    }
};
