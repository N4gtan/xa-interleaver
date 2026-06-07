#pragma once

#ifdef _MSC_VER
#undef fseeko
#define fseeko _fseeki64
#include <string>
#endif

#include <filesystem>
#include <vector>
#include <cstring>
#include <algorithm>

class deinterleaver
{
public:
    struct FileInfo
    {
        std::string fileName;
        int sectorChunk = 1;
        int sectorCount;
        int sectorStride;
        int nullTermination;
        uint8_t filenum;
        uint8_t channel;
        alignas(int) uint8_t nullSubheader[4];
        int begSec;
        int endSec;
    };
    std::vector<FileInfo> entries;

    // inputPath must be an interleaved .xa or .str file. CD image files may have unexpected results.
    explicit deinterleaver(const std::filesystem::path &inputPath) : inputPath(inputPath)
    {
        std::unique_ptr<FILE, decltype(&fclose)> inputFile(fopen(inputPath.string().c_str(), "rb"), &fclose);
        if (!inputFile)
        {
            fprintf(stderr, "Error: Cannot read \"%s\". %s\n", inputPath.filename().string().c_str(), strerror(errno));
            return;
        }
        std::unique_ptr<char []> stdiBuf(new char[STDIO_IOFBF_SIZE]);
        setvbuf(inputFile.get(), stdiBuf.get(), _IOFBF, STDIO_IOFBF_SIZE);

        if (fread(buffer + FILENUM_OFFSET, 1, XA_DATA_SIZE, inputFile.get()) != XA_DATA_SIZE)
        {
        not_aligned:
            fprintf(stderr, "Error: File \"%s\" is not aligned to 2336/2352 bytes.\n", inputPath.filename().string().c_str());
            errno = EINVAL;
            return;
        }
        fseeko(inputFile.get(), 0, SEEK_SET);

        const uintmax_t fileSize = std::filesystem::file_size(inputPath);
        if (fileSize % CD_SECTOR_SIZE == 0 && memcmp(buffer + FILENUM_OFFSET, buffer, 12) == 0)
            inputSectorSize = CD_SECTOR_SIZE;
        else if (fileSize % XA_DATA_SIZE == 0)
            inputSectorSize = XA_DATA_SIZE;
        else
            goto not_aligned;
        offset = CD_SECTOR_SIZE - inputSectorSize;
        const intmax_t totalSectors = fileSize / inputSectorSize;

        intmax_t currentSector = 0;
        std::vector<bool> processedSectors(totalSectors, false);
        printf("Analyzing %s...   0%%", inputPath.filename().string().c_str());
        while (fread(buffer + offset, 1, inputSectorSize, inputFile.get()) == inputSectorSize)
        {
            if (!isAudio() || isNull())
            {
                currentSector++;
                continue;
            }

            // Skip sectors that has already been processed.
            if (processedSectors[currentSector])
            {
                auto nextUnprocessed = std::find(processedSectors.begin() + currentSector, processedSectors.end(), false);
                currentSector = std::distance(processedSectors.begin(), nextUnprocessed);
                fseeko(inputFile.get(), currentSector * inputSectorSize, SEEK_SET);
                continue;
            }

            printf("\b\b\b\b%3jd%%", (currentSector * 100) / totalSectors);
            currentSector = parse(inputFile.get(), currentSector, processedSectors);
        }
        printf("\b\b\b\b100%%\n");

        if (entries.empty())
            printf("No valid entries found.\n");
        else
        {
            std::string namePrefix = inputPath.stem().u8string() + "_";
            size_t namePadWidth = std::max<size_t>(std::to_string(entries.size() - 1).length(), 2);
            for (FileInfo &entry : entries)
                entry.fileName = namePrefix + std::string(namePadWidth - entry.fileName.length(), '0') + std::move(entry.fileName) + ".xa";
        }
    }
    virtual ~deinterleaver() = default;

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

        std::unique_ptr<char []> stdiBuf(new char[STDIO_IOFBF_SIZE]);
        std::unique_ptr<char []> stdoBuf(new char[STDIO_IOFBF_SIZE]);

        std::unique_ptr<FILE, decltype(&fclose)> inputFile(fopen(inputPath.string().c_str(), "rb"), &fclose);
        setvbuf(inputFile.get(), stdiBuf.get(), _IOFBF, STDIO_IOFBF_SIZE);

        for (const FileInfo &entry : entries)
        {
            FILE *outputFile = fopen((outputDir / std::filesystem::u8path(entry.fileName)).string().c_str(), "wb");
            if (!outputFile)
            {
                fprintf(stderr, "Error: Cannot write \"%s\". %s\n", entry.fileName.c_str(), strerror(errno));
                return;
            }
            else
                printf("Deinterleaving %s... ", entry.fileName.c_str());
            setvbuf(outputFile, stdoBuf.get(), _IOFBF, STDIO_IOFBF_SIZE);

            int curSec = entry.begSec;
            fseeko(inputFile.get(), static_cast<int64_t>(curSec) * inputSectorSize, SEEK_SET);
            do {
                int i = 0;
                do {
                    if (fread(buffer + offset, 1, inputSectorSize, inputFile.get()) != inputSectorSize)
                        goto END;

                    fwrite(buffer + outOffset, 1, sectorSize, outputFile);
                } while (++curSec <= entry.endSec && ++i < entry.sectorChunk);
                fseeko(inputFile.get(), entry.sectorStride * inputSectorSize, SEEK_CUR);
            } while ((curSec += entry.sectorStride) <= entry.endSec);

        END:
            fclose(outputFile);
            printf("Done\n");
        }

        createManifest(outputDir, inputPath.stem().string() + ".csv", sectorSize == XA_DATA_SIZE ? "xa" : "xacd");
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
    static constexpr int STDIO_IOFBF_SIZE = 1024 * 1024; // 1MiB

    uint8_t buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};
    uint8_t emptyBuffer[SOUND_GROUP_SIZE - SOUND_GROUP_HEAD]{};

    bool isSilent() const
    {
        return isNull(sizeof(emptyBuffer));
    }

    bool isNull(const int bytes = SOUND_GROUP_HEAD) const
    {
        if (buffer[SUBMODE_OFFSET] == 0xFF)
            return true;
        
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

    intmax_t parse(FILE *inputFile, intmax_t currentSector, std::vector<bool> &processedSectors)
    {
        FileInfo entry{};
        entry.filenum = buffer[FILENUM_OFFSET];
        entry.channel = buffer[CHANNEL_OFFSET];
        entry.begSec  = currentSector;

        int skipSize;
        int chunksRead = 0;
        bool eof = buffer[SUBMODE_OFFSET] & 0x80; // 0x80 = EOF_MASK
        bool silent = isSilent();
        do {
            entry.sectorCount++;
            //entry.endSec = currentSector;
            processedSectors[currentSector++] = true;
            //if (currentSector >= 42185)                                                                              //
                //for (; currentSector > 0 && !processedSectors[--currentSector];);                                    // Debug breakpoint
            //for (currentSector = processedSectors.size(); currentSector > 0 && !processedSectors[--currentSector];); //

            if (entry.sectorStride > 0)
            {
                // If we finished reading a full chunk, apply the stride gap
                if (++chunksRead == entry.sectorChunk)
                {
                    chunksRead = 0;
                    currentSector += entry.sectorStride;
                    if (currentSector >= processedSectors.size() || processedSectors[currentSector])
                        goto END;

                    fseeko(inputFile, skipSize, SEEK_CUR);
                }
                if (fread(buffer + offset, 1, inputSectorSize, inputFile) != inputSectorSize)
                    goto END;
            }
            else
            {
                if (fread(buffer + offset, 1, inputSectorSize, inputFile) != inputSectorSize)
                    goto END;

                // Check if the next sector has different channel or submode.
                if (!eof && entry.sectorCount < 32 && (processedSectors[currentSector] ||
                    entry.channel != buffer[CHANNEL_OFFSET] || !isAudio()))
                {
                    // Calculate sectorStride and skipSize
                    do {
                        currentSector++;
                        entry.sectorStride++;
                        if (fread(buffer + offset, 1, inputSectorSize, inputFile) != inputSectorSize)
                            goto END;

                    } while (processedSectors[currentSector] ||
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
        const int nullSectors = entry.nullTermination / entry.sectorChunk * entry.sectorStride + entry.nullTermination;
        entry.endSec = currentSector - entry.sectorStride - nullSectors - 1;

        if (entry.sectorStride > 0)
            currentSector = entry.begSec + 1;

        fseeko(inputFile, currentSector * inputSectorSize, SEEK_SET);

        // Skip silence-only files.
        if (silent)
            return currentSector;

        entry.fileName = std::to_string(entries.size());
        entries.push_back(std::move(entry));
        return currentSector;
    }

    // Virtual function to fill the manifest as needed.
    virtual void createManifest(const std::filesystem::path &outputDir, const std::string &fileName, const char *type)
    {
        FILE *manifest = fopen((outputDir / fileName).string().c_str(), "wb");
        if (!manifest)
        {
            fprintf(stderr, "Error: Cannot write manifest \"%s\". %s\n", fileName.c_str(), strerror(errno));
            return;
        }

        fprintf(manifest, "chunk,type,file,null_termination,xa_file_number,xa_channel_number" /*",xa_null_subheader"*/ ",sector_beg-end,stride\n");
        for (const FileInfo &entry : entries)
        {
            fprintf(manifest, "%d,%s,%s,%d,%hhu,%hhu" /*",0x%02X%02X%02X%02X"*/ ",%d-%d,%d\n", entry.sectorChunk, type,
                    entry.fileName.c_str(), entry.nullTermination, entry.filenum, entry.channel,
                    /*entry.nullSubheader[0], entry.nullSubheader[1], entry.nullSubheader[2], entry.nullSubheader[3],*/
                    entry.begSec, entry.endSec, entry.sectorChunk + entry.sectorStride);
        }
        fclose(manifest);
    }
};
