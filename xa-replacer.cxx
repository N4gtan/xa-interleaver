#include "libxa_deinterleaver.hxx"

#define VER "VERSION"
#define XA_DATA_SIZE 2336
#define CD_SECTOR_SIZE 2352
#define SUBHEAD_OFFSET 16

int check_file(FILE *fp, const std::filesystem::path &path, uintmax_t &fileSize, uint8_t *buffer)
{
    if (!fp)
    {
        fprintf(stderr, "Error: Cannot open file \"%s\".\n", path.filename().string().c_str());
        exit(1);
    }

    fileSize = std::filesystem::file_size(path);
    if (fread(buffer + SUBHEAD_OFFSET, 1, XA_DATA_SIZE, fp) == XA_DATA_SIZE)
    {
        if (fileSize % CD_SECTOR_SIZE == 0 && memcmp(buffer + SUBHEAD_OFFSET, buffer, 12) == 0)
            return CD_SECTOR_SIZE;
        if (fileSize % XA_DATA_SIZE == 0)
            return XA_DATA_SIZE;
    }

    fprintf(stderr, "Error: File \"%s\" is not aligned to 2336/2352 bytes.\n", path.filename().string().c_str());
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("xa-replacer " VER " by N4gtan\n\n"
               " Usage: xa-replacer <target> <source> [sector]\n\n"
               "Target: File to be modified (in-place)\n"
               "Source: Deinterleaved XA file to inject\n"
               "Sector: Starting LBA of the track to replace\n"
               "        (If omitted, a track list will be displayed to choose from)\n");
        return EXIT_SUCCESS;
    }

    const std::filesystem::path tgtPath = argv[1];
    const std::filesystem::path srcPath = argv[2];

    const std::vector<deinterleaver::FileInfo> entries = std::move(deinterleaver(tgtPath).entries);
    if (errno)
        return EXIT_FAILURE;

    const auto &entry = [&]() -> const deinterleaver::FileInfo &
    {
        if (argc >= 4)
        {
            int sector = 0;
            const int ret = sscanf(argv[3], "%d", &sector);
            const auto it = std::find_if(entries.begin(), entries.end(), [sector](const auto &entry) { return entry.begSec == sector; });
            if (ret == 0 || it == entries.end())
            {
                fprintf(stderr, "Error: There is no stream that starts at sector %s.\n", argv[3]);
                exit(EXIT_FAILURE);
            }
            return *it;
        }
        else
        {
            size_t track = 0;
            printf("Track AudioSectors NullSectors Beg-End_AudioSector\n");
            for (const auto &entry : entries)
                printf("#%-5zu%-13d%-12d%d-%d\n", track++, entry.sectorCount, entry.nullTrailing, entry.begSec, entry.endSec);

            printf("Enter track number: #");
            const int ret = scanf("%zu", &track);
            if (ret == 0 || track >= entries.size())
            {
                fprintf(stderr, "Error: That was not a valid track.\n");
                exit(EXIT_FAILURE);
            }
            return entries[track];
        }
    }();

    uint8_t buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::unique_ptr<FILE, decltype(&fclose)> tgtFile(fopen(tgtPath.string().c_str(), "r+b"), &fclose);
    std::unique_ptr<FILE, decltype(&fclose)> srcFile(fopen(srcPath.string().c_str(), "rb"), &fclose);

    // Retrieve sector sizes and offsets
    uintmax_t tgtSize, srcSize;
    const int tgtSectorSize = check_file(tgtFile.get(), tgtPath, tgtSize, buffer);
    const int srcSectorSize = check_file(srcFile.get(), srcPath, srcSize, buffer);
    const int dataOffset    = tgtSectorSize  - XA_DATA_SIZE;
    const int buffOffset    = CD_SECTOR_SIZE - srcSectorSize;

    // Validate target available space
    const int srcSectorCount = srcSize / srcSectorSize;
    if (srcSectorCount > entry.sectorCount + entry.nullTrailing)
    {
        fprintf(stderr, "Error: Source exceeds target stream size by %d sector(s).\n", srcSectorCount - (entry.sectorCount + entry.nullTrailing));
        return EXIT_FAILURE;
    }

    // Check eof bit and reset file pointers
    fseeko(tgtFile.get(), static_cast<int64_t>(entry.endSec) * tgtSectorSize + dataOffset + 2, SEEK_SET);
    uint8_t eofBit = fgetc(tgtFile.get()) & 0x80;
    fseeko(srcFile.get(), 0, SEEK_SET);
    fseeko(tgtFile.get(), static_cast<int64_t>(entry.begSec) * tgtSectorSize + dataOffset, SEEK_SET);

    // Write data
    auto processSector = [&](auto tag) -> void
    {
        [[maybe_unused]] size_t __ = fread(buffer + buffOffset, 1, srcSectorSize, srcFile.get());
        buffer[SUBHEAD_OFFSET + 4] = buffer[SUBHEAD_OFFSET]     = entry.filenum;
        buffer[SUBHEAD_OFFSET + 5] = buffer[SUBHEAD_OFFSET + 1] = entry.channel;
        if constexpr (tag)
            buffer[SUBHEAD_OFFSET + 6] = buffer[SUBHEAD_OFFSET + 2] = (buffer[SUBHEAD_OFFSET + 2] & 0x7F) | eofBit;
        fwrite(buffer + SUBHEAD_OFFSET, 1, XA_DATA_SIZE, tgtFile.get());
    };

    const int skipSize = entry.sectorStride * tgtSectorSize + dataOffset;
    for (int i = 1; i < srcSectorCount; ++i)
    {
        processSector(std::false_type{});
        fseeko(tgtFile.get(), skipSize, SEEK_CUR);
    }
    processSector(std::true_type{});

    // Null filler
    int sectorsToFill = entry.sectorCount - srcSectorCount;
    if (sectorsToFill > 0)
    {
        printf("Warning: Source is smaller than target stream size by %d sector(s).\n"
               "         Write null sector(s) as filler? <Y/n> ", sectorsToFill);

        const uint8_t key = getchar();
        if (std::tolower(key) != 'n')
        {
            memcpy(&buffer[SUBHEAD_OFFSET], &entry.nullSubheader, sizeof(entry.nullSubheader));
            memcpy(&buffer[SUBHEAD_OFFSET + 4], &entry.nullSubheader, sizeof(entry.nullSubheader));
            memset(&buffer[SUBHEAD_OFFSET + 8], entry.nullSubheader[2] == 0xFF ? 0xFF : 0, sizeof(buffer) - (SUBHEAD_OFFSET + 8));
            while (sectorsToFill-- > 0)
            {
                fseeko(tgtFile.get(), skipSize, SEEK_CUR);
                fwrite(buffer + SUBHEAD_OFFSET, 1, XA_DATA_SIZE, tgtFile.get());
            }
        }
    }

    printf("Process complete.\n");
    return EXIT_SUCCESS;
}
