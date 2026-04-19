#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

#define VER "VERSION"
#define XA_DATA_SIZE 2336
#define CD_SECTOR_SIZE 2352
#define SUBHEAD_OFFSET 16
#ifdef _MSC_VER
#define fseeko _fseeki64
#endif

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
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("xa-injector " VER " by N4gtan\n\n"
               " Usage: xa-injector <target> <source> <sector> <stride>\n\n"
               "Target: File to be modified (in-place)\n"
               "Source: Deinterleaved XA file to inject\n"
               "Sector: LBA to start the injection\n"
               "Stride: Stride of sectors to interleave. Defaults to 8\n");
        return 0;
    }

    const std::filesystem::path tgtPath = argv[1];
    const std::filesystem::path srcPath = argv[2];
    const int sector = atoi(argv[3]);
    const int stride = argc >= 5 ? atoi(argv[4]) : 8;

    uint8_t buffer[CD_SECTOR_SIZE] {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::unique_ptr<FILE, decltype(&fclose)> tgtFile(fopen(tgtPath.string().c_str(), "r+b"), &fclose);
    std::unique_ptr<FILE, decltype(&fclose)> srcFile(fopen(srcPath.string().c_str(), "rb"), &fclose);

    // Retrieve sector sizes and offsets
    uintmax_t tgtSize, srcSize;
    const int tgtSectorSize = check_file(tgtFile.get(), tgtPath, tgtSize, buffer);
    const int srcSectorSize = check_file(srcFile.get(), srcPath, srcSize, buffer);
    const int dataOffset = tgtSectorSize - XA_DATA_SIZE;
    const int buffOffset = CD_SECTOR_SIZE - srcSectorSize;

    // Validate target available space
    if ((sector + (srcSize / srcSectorSize - 1) * stride + 1) * tgtSectorSize > tgtSize)
    {
        fprintf(stderr, "Error: Injection will exceed target file size.\n");
        return 1;
    }

    // Reset source file pointer since check_file advanced it
    fseeko(srcFile.get(), 0, SEEK_SET);
    fseeko(tgtFile.get(), static_cast<int64_t>(sector) * tgtSectorSize + dataOffset, SEEK_SET);

    const int skipSize = (stride - 1) * tgtSectorSize + dataOffset;
    while (fread(buffer + buffOffset, 1, srcSectorSize, srcFile.get()) == srcSectorSize)
    {
        fwrite(buffer + SUBHEAD_OFFSET, 1, XA_DATA_SIZE, tgtFile.get());
        fseeko(tgtFile.get(), skipSize, SEEK_CUR);
    }

    printf("Process complete.\n");
    return 0;
}
