// Based on asiekierka's code (https://github.com/ChenThread/candyk-psx/blob/master/toolsrc/xainterleave/xainterleave.c)
// But completely rewritten by me (Nagtan)

#pragma once

#ifdef _MSC_VER
#undef fseeko
#undef strtok_r
#undef strncasecmp
#define fseeko _fseeki64
#define strtok_r strtok_s
#define strncasecmp _strnicmp
#endif

#include <filesystem>
#include <vector>
#include <cstring>
#include <optional>
#include <utility>

class interleaver
{
public:
    struct FileInfo
    {
        std::filesystem::path filePath;
        int sectorChunk = 1;
        int sectorCount;
        int sectorSize;
        int nullTermination;
        std::optional<uint8_t> filenum;
        std::optional<uint8_t> channel;
        alignas(int) uint8_t nullSubheader[4];
        int begSec;
        int endSec;
    };
    std::vector<FileInfo> entries;

    // inputPath must be a .csv or .txt or any UTF-8 encoded text file with the appropriate format.
    // sectorStride must be like the psx XA format (2/4/8/16/32).
    explicit interleaver(const std::filesystem::path &inputPath, const int sectorStride) : sectorStride(sectorStride)
    {
        std::unique_ptr<FILE, decltype(&fclose)> inputFile(fopen(inputPath.string().c_str(), "r"), &fclose);
        if (!inputFile)
        {
            fprintf(stderr, "Error: Cannot read \"%s\". %s\n", inputPath.filename().string().c_str(), strerror(errno));
            return;
        }

        char *field;
        char *saveptr;
        char line[1024];
        int fillTarget = sectorStride;
        std::vector<int> slots(sectorStride);

        while (fgets(line, sizeof(line), inputFile.get()))
        {
            FileInfo entry{};
            entry.sectorChunk = atoi(strtok_r(line, ",", &saveptr));
            if (entry.sectorChunk < 1)
                continue;

            if (entry.sectorChunk > sectorStride ||
                (fillTarget > 0 ? fillTarget -= entry.sectorChunk : fillTarget) < 0)
            {
                fprintf(stderr, "Error: Consecutive chunks exceed %d\n", sectorStride);
                std::vector<FileInfo>().swap(entries);
                return;
            }

            if (!(field = strtok_r(NULL, ",", &saveptr)) ||
                strncasecmp(field, "null", 4) == 0)
                entry.sectorSize = entry.endSec = -1;
            else if (strncasecmp(field, "xacd", 4) == 0)
                entry.sectorSize = CD_SECTOR_SIZE;
            else if (strncasecmp(field, "xa", 2) == 0)
                entry.sectorSize = XA_DATA_SIZE;
            else
            {
                fprintf(stderr, "Warning: Unknown type \"%s\" at line %zu\n", field, entries.size() + 1);
                continue;
            }

            int idle = 0;
            entry.begSec = INT32_MAX;
            for (int s = 0; s <= sectorStride - entry.sectorChunk; ++s)
            {
                int maxBase = 0;
                for (int i = 0; i < entry.sectorChunk; ++i)
                    maxBase = std::max(maxBase, slots[s + i] - (s + i));

                if (maxBase + s < entry.begSec)
                {
                    idle = s;
                    entry.begSec = maxBase + s;
                }
            }

            int nextBase = INT32_MAX / 2;
            if (entry.sectorSize > 0)
            {
                if ((field = strtok_r(NULL, ",", &saveptr)))
                {
                    field[strcspn(field, "\r\n")] = 0;
                    entry.filePath = inputPath.parent_path() / std::filesystem::u8path(field);
                    FILE *test = fopen(entry.filePath.string().c_str(), "rb");
                    if (!test)
                    {
                        fprintf(stderr, "Error: Cannot read \"%s\" at line %zu. %s\n", entry.filePath.string().c_str(), entries.size() + 1, strerror(errno));
                        std::vector<FileInfo>().swap(entries);
                        return;
                    }
                    fclose(test);

                    const uintmax_t fileSize = std::filesystem::file_size(entry.filePath);
                    if (fileSize == 0 ||
                        fileSize % entry.sectorSize != 0)
                    {
                        fprintf(stderr, "Error: Invalid type for \"%s\" at line %zu\n", field, entries.size() + 1);
                        std::vector<FileInfo>().swap(entries);
                        return;
                    }
                    entry.sectorCount = fileSize / entry.sectorSize;
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
                            while ((field = strtok_r(NULL, ",", &saveptr)))
                            {
                                if (strncasecmp(field, "0x", 2) != 0)
                                    continue;
                                field += 2;
                                sscanf(field, "%2hhx%2hhx%2hhx%2hhx", &entry.nullSubheader[0], &entry.nullSubheader[1], &entry.nullSubheader[2], &entry.nullSubheader[3]);
                                break;
                            }
                        }
                    }
                }

                const int sectorCount = entry.sectorCount - 1;
                entry.endSec = entry.begSec + sectorCount + sectorCount / entry.sectorChunk * (sectorStride - entry.sectorChunk);
                nextBase = (entry.endSec / sectorStride + 1 + entry.nullTermination) * sectorStride;
            }

            for (int i = 0; i < entry.sectorChunk; ++i)
                slots[idle + i] = nextBase + idle + i;

            entries.push_back(std::move(entry));
        }
    }
    virtual ~interleaver() = default;

    // outputFile must be opened in read and write binary (+b) mode.
    // sectorSize must be 2336 or 2352 to change the output size.
    void interleave(FILE *outputFile, int sectorSize = 0)
    {
        uint8_t buffer[CD_SECTOR_SIZE]{};
        uint8_t emptyBuffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};
        std::vector<std::tuple<FILE *, std::unique_ptr<char []>, int>> inputFiles(sectorStride);
        std::vector<FileInfo> workingEntries = entries;

        int can_read = 0;
        int sectorsToFill = sectorStride;
        for (const auto &entry : workingEntries)
        {
            if (entry.sectorSize > 0 &&
                !entry.filePath.empty())
            {
                if (can_read < sectorStride)
                {
                    auto &[inputFile, stdiBuf, _] = inputFiles[can_read];
                    stdiBuf.reset(new char[STDIO_IOFBF_SIZE]);
                    inputFile = fopen(entry.filePath.string().c_str(), "rb");
                    setvbuf(inputFile, stdiBuf.get(), _IOFBF, STDIO_IOFBF_SIZE);
                }
                if (sectorSize == 0)
                    sectorSize = entry.sectorSize;
                can_read++;
            }
            sectorsToFill -= entry.sectorChunk;
        }
        const int outOffset = CD_SECTOR_SIZE - sectorSize;

        // Fill with null sectors to achieve the desired stride
        if (sectorsToFill > 0 &&
            sectorsToFill < sectorStride)
        {
            for (int i = workingEntries.size() - 1; sectorsToFill > 0; ++i, --sectorsToFill)
            {
                FileInfo &e = workingEntries.emplace_back();
                e.filenum   = workingEntries[i].filenum;
            }
        }

        while (can_read > 0)
        {
            for (int i = 0; i < sectorStride && i < workingEntries.size(); ++i)
            {
                FileInfo &entry = workingEntries[i];
                auto &[inputFile, stdiBuf, currentSector] = inputFiles[i];

                for (int is = 0; is < entry.sectorChunk; ++is)
                {
                    if (inputFile != nullptr &&
                        fread(buffer + (CD_SECTOR_SIZE - entry.sectorSize), 1, entry.sectorSize, inputFile) == entry.sectorSize)
                    {
                        currentSector++;
                        buffer[FILENUM_OFFSET + 4] = buffer[FILENUM_OFFSET] = entry.filenum.value_or(buffer[FILENUM_OFFSET]);
                        buffer[CHANNEL_OFFSET + 4] = buffer[CHANNEL_OFFSET] = entry.channel.value_or(buffer[CHANNEL_OFFSET]);
                        fwrite(buffer + outOffset, 1, sectorSize, outputFile);
                    }
                    else
                    {
                        emptyBuffer[FILENUM_OFFSET] = buffer[FILENUM_OFFSET];
                        nullCustomizer(emptyBuffer, entry);
                        memcpy(&emptyBuffer[FILENUM_OFFSET], &entry.nullSubheader, sizeof(entry.nullSubheader));
                        memcpy(&emptyBuffer[FILENUM_OFFSET + 4], &entry.nullSubheader, sizeof(entry.nullSubheader));
                        fwrite(emptyBuffer + outOffset, 1, sectorSize, outputFile);
                    }
                }

                if (inputFile != nullptr &&
                    currentSector >= entry.sectorCount &&
                    entry.nullTermination-- <= 0)
                {
                    can_read--;
                    fclose(std::exchange(inputFile, nullptr));
                    printf("Interleaving %s... Done\n", entry.filePath.filename().string().c_str());
                    if (sectorStride < workingEntries.size())
                    {
                        currentSector = 0;
                        entry = std::move(workingEntries[sectorStride]);
                        inputFile = fopen(entry.filePath.string().c_str(), "rb");
                        setvbuf(inputFile, stdiBuf.get(), _IOFBF, STDIO_IOFBF_SIZE);
                        workingEntries.erase(workingEntries.begin() + sectorStride);
                    }
                    else
                        stdiBuf.reset();
                }
            }
        }

        fseeko(outputFile, -2334, SEEK_END);
        int EOFbit = fgetc(outputFile) | 0x80;
        fseeko(outputFile, -2334, SEEK_END);
        fputc(EOFbit, outputFile);
        fseeko(outputFile, -2330, SEEK_END);
        fputc(EOFbit, outputFile);
        fseeko(outputFile, 0, SEEK_END);
    }

protected:
    static constexpr int CD_SECTOR_SIZE = 2352;
    static constexpr int XA_DATA_SIZE   = 2336;
    static constexpr int FILENUM_OFFSET = 0x10;
    static constexpr int CHANNEL_OFFSET = 0x11;

private:
    const int sectorStride;
    static constexpr int STDIO_IOFBF_SIZE = 1024 * 1024; // 1MiB

    // Virtual function to fill null sectors as needed.
    virtual void nullCustomizer(uint8_t *emptyBuffer, FileInfo &entry)
    {
        if (*reinterpret_cast<int *>(entry.nullSubheader) != 0)
            return;
        entry.nullSubheader[0] = entry.filenum.value_or(emptyBuffer[FILENUM_OFFSET]);
        //entry.nullSubheader[1] = entry.nullTermination >= 0 ? entry.channel.value_or(0) : 0;
        //entry.nullSubheader[2] = 0x48;
        //entry.nullSubheader[3] = 0x00;
    }
};
