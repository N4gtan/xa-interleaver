#pragma once

#ifdef _MSC_VER
#include <string>
#endif

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <set>

class deinterleaver
{
public:
    struct FileInfo
    {
        std::string fileName;
        int sectorChunk;
        int sectorCount;
        int sectorStride;
        int nullTermination;
        uint8_t filenum;
        uint8_t channel;
        alignas(int) uint8_t nullSubheader[4];
        std::streamoff begOff;
        std::streamoff endOff;
    };
    std::vector<FileInfo> entries;

    // inputPath must be an interleaved .xa or .str file. CD image files may have unexpected results.
    explicit deinterleaver(const std::filesystem::path &inputPath) : inputPath(inputPath)
    {
        std::ifstream inputFile(inputPath, std::ios::binary);

        if (!inputFile.read(reinterpret_cast<char *>(buffer) + FILENUM_OFFSET, 12))
        {
            fprintf(stderr, "Error: Cannot read \"%s\". %s\n", inputPath.filename().string().c_str(), strerror(errno));
            return;
        }
        inputFile.seekg(0, std::ios::beg);

        const unsigned long long fileSize = std::filesystem::file_size(inputPath);
        if (fileSize % CD_SECTOR_SIZE == 0 && memcmp(buffer + FILENUM_OFFSET, buffer, 12) == 0)
            inputSectorSize = CD_SECTOR_SIZE;
        else if (fileSize % XA_DATA_SIZE == 0)
            inputSectorSize = XA_DATA_SIZE;
        else
        {
            fprintf(stderr, "Error: File \"%s\" is not aligned to 2336/2352 bytes.\n", inputPath.filename().string().c_str());
            errno = EINVAL;
            return;
        }
        offset = CD_SECTOR_SIZE - inputSectorSize;

        std::streamoff currentOff;
        std::set<std::streamoff> processedSectors;
        printf("Analyzing %s...   0%%", inputPath.filename().string().c_str());
        while (inputFile.read(reinterpret_cast<char *>(buffer) + offset, inputSectorSize))
        {
            if (!isAudio() || isNull())
                continue;

            currentOff = inputFile.tellg();

            // Skip sectors that has already been processed.
            if (auto prev = processedSectors.find(currentOff); prev != processedSectors.end())
            {
                for (auto it = prev; ++it != processedSectors.end();)
                {
                    if (*it > *prev + inputSectorSize)
                        break;
                    prev = it;
                }
                if (*prev != currentOff)
                    inputFile.seekg(*prev, std::ios::beg);
                //processedSectors.erase(processedSectors.begin(), std::next(prev));
                continue;
            }

            printf("\b\b\b\b%3llu%%", (currentOff * 100) / fileSize);
            parse(inputFile, currentOff, processedSectors);
        }
        inputFile.close();
        printf("\b\b\b\b100%%\n");

        if (entries.empty())
            printf("No valid entries found.\n");
    }

    // outputDir must be a directory (not a file).
    // sectorSize must be 2336 or 2352 to change the output size.
    void deinterleave(const std::filesystem::path &outputDir, int sectorSize = 0)
    {
        if (entries.empty())
            return;

        if (sectorSize == 0)
            sectorSize = inputSectorSize;
        const int outOffset = CD_SECTOR_SIZE - sectorSize;

        if (!std::filesystem::exists(outputDir))
            std::filesystem::create_directories(outputDir);

        std::ifstream inputFile(inputPath, std::ios::binary);
        std::string namePrefix = inputPath.stem().u8string() + "_";
        size_t namePadWidth = std::max<size_t>(std::to_string(entries.size() - 1).length(), 2);
        for (FileInfo &entry : entries)
        {
            entry.fileName = namePrefix + std::string(namePadWidth - entry.fileName.length(), '0') + std::move(entry.fileName) + ".xa";
            std::ofstream outputFile(outputDir / std::filesystem::u8path(entry.fileName), std::ios::binary);
            if (!outputFile)
            {
                fprintf(stderr, "Error: Cannot write \"%s\". %s\n", entry.fileName.c_str(), strerror(errno));
                return;
            }
            else
                printf("Deinterleaving %s... ", entry.fileName.c_str());

            inputFile.seekg(entry.begOff, std::ios::beg);
            std::streamoff limit = entry.endOff - (entry.nullTermination * (entry.sectorStride + 1) * inputSectorSize);
            do {
                int i = 0;
                do {
                    if (!inputFile.read(reinterpret_cast<char *>(buffer) + offset, inputSectorSize))
                    {
                        inputFile.clear();
                        goto END;
                    }
                    outputFile.write(reinterpret_cast<const char *>(buffer) + outOffset, sectorSize);
                } while (++i < entry.sectorChunk);
                inputFile.seekg(entry.sectorStride * inputSectorSize, std::ios::cur);
            } while (inputFile.tellg() < limit);

        END:
            outputFile.close();
            printf("Done\n");
        }

        createManifest(outputDir, inputPath.stem().string() + ".csv");
    }

protected:
    int inputSectorSize = 0;
    static constexpr int CD_SECTOR_SIZE = 2352;
    static constexpr int XA_DATA_SIZE   = 2336;
    static constexpr int FILENUM_OFFSET = 0x10;
    static constexpr int CHANNEL_OFFSET = 0x11;
    static constexpr int SUBMODE_OFFSET = 0x12;

private:
    int offset = 0;
    const std::filesystem::path inputPath;
    static constexpr int SOUND_GROUP_HEAD = 16;
    static constexpr int SOUND_GROUP_SIZE = 128;

    uint8_t buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};
    uint8_t emptyBuffer[SOUND_GROUP_SIZE - SOUND_GROUP_HEAD]{};

    bool isSilent() const
    {
        return isNull(sizeof(emptyBuffer));
    }

    bool isNull(const int bytes = SOUND_GROUP_HEAD) const
    {
        int groups = ((buffer[SUBMODE_OFFSET] & 0x20) != 0 ? 18 : 16) * SOUND_GROUP_SIZE; // 0x20 = FORM2_MASK
        for (int i = bytes != SOUND_GROUP_HEAD ? 0x28 : 0x18; i < groups; i += SOUND_GROUP_SIZE) // 0x28/0x18 = DATA_OFFSET
        {
            if (memcmp(buffer + i, emptyBuffer, bytes) != 0)
                return false;
        }
        return true;
    }

    bool isAudio() const
    {
        return buffer[SUBMODE_OFFSET] & 0x04; // 0x04 = AUDIO_MASK
    }

    void parse(std::ifstream &inputFile, std::streamoff &currentOff, std::set<std::streamoff> &processedSectors)
    {
        FileInfo entry{};
        entry.filenum = buffer[FILENUM_OFFSET];
        entry.channel = buffer[CHANNEL_OFFSET];
        entry.begOff  = currentOff - inputSectorSize;

        int skipSize;
        bool eof = buffer[SUBMODE_OFFSET] & 0x80; // 0x80 = EOF_MASK
        bool silent = isSilent();
        do {
            entry.sectorCount++;
            currentOff = inputFile.tellg();
            processedSectors.insert(currentOff);
            //if (currentOff >= 99219120)                                       //
                //currentOff = *std::prev(std::prev(processedSectors.end()));   // Debug breakpoint
            //currentOff = *std::prev(processedSectors.end());                  //

            if (entry.sectorStride > 0)
            {
                inputFile.seekg(skipSize, std::ios::cur);
                if (!inputFile.read(reinterpret_cast<char *>(buffer) + offset, inputSectorSize))
                {
                    inputFile.clear();
                    goto END;
                }
                if (processedSectors.find(inputFile.tellg()) != processedSectors.end())
                    goto END;
            }
            else
            {
                if (!inputFile.read(reinterpret_cast<char *>(buffer) + offset, inputSectorSize))
                {
                    inputFile.clear();
                    goto END;
                }
                // Check if the next sector has different channel or submode.
                if (!eof && (processedSectors.find(inputFile.tellg()) != processedSectors.end() ||
                    entry.channel != buffer[CHANNEL_OFFSET] || !isAudio()))
                {
                    // Calculate sectorStride and skipSize
                    do {
                        entry.sectorStride++;
                        if (!inputFile.read(reinterpret_cast<char *>(buffer) + offset, inputSectorSize))
                        {
                            inputFile.clear();
                            goto END;
                        }
                    } while (processedSectors.find(inputFile.tellg()) != processedSectors.end() ||
                             entry.channel != buffer[CHANNEL_OFFSET] || !isAudio());

                    entry.sectorChunk = entry.sectorCount;
                    skipSize = entry.sectorStride * inputSectorSize;
                }
            }

            if (entry.filenum != buffer[FILENUM_OFFSET])
                goto END;
            else if ((entry.nullTermination > 0 && entry.nullSubheader[1] == buffer[CHANNEL_OFFSET] &&
                     (entry.nullSubheader[2] | 0x80) == (buffer[SUBMODE_OFFSET] | 0x80) && isNull()) ||
                    (entry.nullTermination == 0 && isNull()))
            {
                if (entry.nullTermination++ == 0)
                    memcpy(&entry.nullSubheader, &buffer[FILENUM_OFFSET], sizeof(entry.nullSubheader));
            }
            else if (entry.nullTermination || eof ||
                     entry.channel != buffer[CHANNEL_OFFSET])
                goto END;
            else
            {
                eof = buffer[SUBMODE_OFFSET] & 0x80; // 0x80 = EOF_MASK
                if (silent)
                    silent = isSilent();
            }
        } while (true);

    END:
        entry.endOff = currentOff;

        if (entry.sectorStride > 0)
            currentOff = entry.begOff + inputSectorSize;

        inputFile.seekg(currentOff, std::ios::beg);

        // Skip silence-only files.
        if (silent)
            return;

        entry.fileName = std::to_string(entries.size());
        entries.push_back(std::move(entry));
    }

    // Virtual function to fill the manifest as needed.
    virtual void createManifest(const std::filesystem::path &outputDir, const std::string &fileName)
    {
        FILE *manifest = fopen((outputDir / fileName).string().c_str(), "wb");
        if (!manifest)
        {
            fprintf(stderr, "Error: Cannot write manifest \"%s\". %s\n", fileName.c_str(), strerror(errno));
            return;
        }

        fprintf(manifest, "chunk,type,file,null_termination,xa_file_number,xa_channel_number" /*",xa_null_subheader"*/ ",sector_beg-end\n");
        for (const FileInfo &entry : entries)
        {
            fprintf(manifest, "%d,%s,%s,%d,%hhu,%hhu" /*",0x%02X%02X%02X%02X"*/ ",%lld-%lld\n", entry.sectorChunk, inputSectorSize == XA_DATA_SIZE ? "xa" : "xacd",
                    entry.fileName.c_str(), entry.nullTermination, entry.filenum, entry.channel,
                    /*entry.nullSubheader[0], entry.nullSubheader[1], entry.nullSubheader[2], entry.nullSubheader[3],*/
                    (long long)entry.begOff / inputSectorSize, (long long)(entry.endOff / inputSectorSize) - (entry.nullTermination * (entry.sectorStride + 1)) - 1);
        }
        fclose(manifest);
    }
};
