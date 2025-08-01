#pragma once

#ifdef _MSC_VER
    #include <string>
#endif

#include <filesystem>
#include <fstream>
#include <vector>
#include <string.h>
#include <set>

class deinterleaver
{
protected:
    static constexpr int CD_SECTOR_SIZE = 2352;
    static constexpr int XA_DATA_SIZE   = 2336;

    struct FileInfo
    {
        std::string fileName;
        int sectorBlock;
        int sectorCount     = 0;
        int sectorStride    = 0;
        int nullTermination = 0;
        int filenum;
        int channel;
        std::streamoff begOff;
        std::streamoff endOff;
    };

private:
    int offset;
    int inputSectorSize;
    const std::filesystem::path inputPath;

    static constexpr int FILENUM_OFFSET = 0x10;
    static constexpr int CHANNEL_OFFSET = 0x11;
    static constexpr int SUBMODE_OFFSET = 0x12;
    unsigned char buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02};

    // Virtual function to handle non-standard null sectors.
    virtual bool isInvalid() const
    {
        // If channel number is 0xFF, it's a non-standard null sector.
        return buffer[CHANNEL_OFFSET] == 0xFF;
    }

    bool isAudio() const
    {
        return buffer[SUBMODE_OFFSET] & 0x04; // 0x04 = AUDIO_MASK
    }

    void parse(std::ifstream &inputFile, std::streamoff &currentOff, const uintmax_t &fileSize, std::set<std::streamoff> &processedSectors)
    {
        FileInfo &entry = entries.emplace_back();
        entry.filenum   = buffer[FILENUM_OFFSET];
        entry.channel   = buffer[CHANNEL_OFFSET];
        entry.begOff    = currentOff - inputSectorSize;
        entry.fileName  = std::to_string(entries.size() - 1);
        /*entry.fileName  = inputPath.stem().string() + "_"
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
        bool eof = false;
        do {
            if (!isAudio() || (eof && [&]{unsigned char empty[2324] {};
                                return !memcmp(buffer + 0x18, empty, sizeof(empty));}()))
                entry.nullTermination++;
            else if (entry.nullTermination || eof)
                break;
            else
            {
                if (buffer[SUBMODE_OFFSET] & 0x80) // 0x80 = EOF_MASK
                    eof = true;
                //outputFile.write(reinterpret_cast<const char*>(buffer) + offset, sectorSize);
            }

            entry.sectorCount++;
            currentOff = inputFile.tellg();
            processedSectors.insert(currentOff);
            //if (currentOff >= 99219120)                                       //
                //currentOff = *std::prev(std::prev(processedSectors.end()));   // Debug breakpoint
            //currentOff = *std::prev(processedSectors.end());                  //

            if (entry.sectorStride)
            {
                inputFile.seekg(skipSize, std::ios::cur);
                if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                {
                    inputFile.clear();
                    break;
                }
                // Set file number and submode to standard null sector values
                if (isInvalid())
                {
                    buffer[FILENUM_OFFSET] = entry.filenum;
                    buffer[SUBMODE_OFFSET] = 0x00;
                }
            }
            else // Calculate sectorStride and skipSize
            {
                if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                {
                    inputFile.clear();
                    break;
                }
                // Check if the next sector has different channel or submode.
                if (entry.channel != buffer[CHANNEL_OFFSET] || !isAudio())
                {
                    do {
                        entry.sectorStride++;
                        if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                        {
                            inputFile.clear();
                            break;
                        }
                    } while (entry.channel != buffer[CHANNEL_OFFSET] || !isAudio());

                    entry.sectorBlock = entry.sectorCount;
                    skipSize = entry.sectorStride * inputSectorSize;
                }
            }
        } while (entry.filenum == buffer[FILENUM_OFFSET] &&
                (entry.channel == buffer[CHANNEL_OFFSET] ||
                !(buffer[SUBMODE_OFFSET] & 0x7F))); // 0 or 0x80, standard null sector values

        entry.endOff = currentOff;
        //outputFile.close();
        //printf("Done\n");

        if (entry.sectorStride)
            inputFile.seekg(entry.begOff + inputSectorSize, std::ios::beg);
        else
            inputFile.seekg(currentOff, std::ios::beg);
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
                     /*<< "," << entry.begOff / inputSectorSize << "-" << entry.endOff / inputSectorSize - entry.sectorStride - 1*/ << "\n";
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

        std::streamoff currentOff;
        std::set<std::streamoff> processedSectors;
        printf("Analyzing %s...   0%%", inputPath.filename().string().c_str());
        while (inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
        {
            if (isInvalid() || !isAudio())
                continue;

            currentOff = inputFile.tellg();

            // Skip sectors that has already been processed.
            if (auto prev = processedSectors.find(currentOff); prev != processedSectors.end())
            {
                for (auto it = prev; ++it != processedSectors.end(); )
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
            parse(inputFile, currentOff, fileSize, processedSectors);
        }
        inputFile.close();
        printf("\b\b\b\b100%%\n");

        if (entries.empty())
            printf("No valid entries found.\n");
    }

    // outputDir must be a directory (not a file).
    // sectorSize must be 2336 or 2352 to change the output size.
    void deinterleave(const std::filesystem::path outputDir, int sectorSize = 0)
    {
        if (entries.empty())
            return;

        if (!sectorSize)
            sectorSize = inputSectorSize;
        const int outOffset = CD_SECTOR_SIZE - sectorSize;

        if (!std::filesystem::exists(outputDir))
            std::filesystem::create_directories(outputDir);

        std::ifstream inputFile(inputPath, std::ios::binary);
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

            inputFile.seekg(entry.begOff, std::ios::beg);
            std::streamoff limit = entry.endOff - (entry.nullTermination * (entry.sectorStride + 1) * inputSectorSize);
            while (inputFile.tellg() < limit)
            {
                int i = 0;
                do {
                    if (!inputFile.read(reinterpret_cast<char*>(buffer) + offset, inputSectorSize))
                    {
                        inputFile.clear();
                        break;
                    }
                    outputFile.write(reinterpret_cast<const char*>(buffer) + outOffset, sectorSize);
                } while (++i < entry.sectorBlock);
                inputFile.seekg(entry.sectorStride * inputSectorSize, std::ios::cur);
            }

            outputFile.close();
            printf("Done\n");
        }

        createManifest(outputDir, inputPath.stem().string() + ".csv");
    }
};
