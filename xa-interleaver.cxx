#include "libxa_interleaver.hxx"
#define VER "VERSION"

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        printf("xa-interleaver " VER " by N4gtan\n\n"
               " Usage: xa-interleaver <input> [stride] [size] [output]\n\n"
               " Input: Manifest .csv file (or any text file with the appropriate format)\n"
               "Stride: Optional stride of sectors to interleave (2/4/8/16/32). Defaults to 8\n"
               "  Size: Optional output file sector size (2336 or 2352). Defaults to first file sector size\n"
               "Output: Optional output file path. Defaults to input file path\n");
        return EXIT_SUCCESS;
    }

    const std::filesystem::path inputFile = argv[1];
    const int sectorStride = argc >= 3 ? atoi(argv[2]) : 8;
    const int sectorSize = argc >= 4 ? atoi(argv[3]) : 0;

    interleaver files(inputFile, sectorStride);
    if (files.entries.empty())
    {
        if (!errno)
            fprintf(stderr, "Invalid manifest\n");
        return EXIT_FAILURE;
    }

    const std::filesystem::path outputPath = argc >= 5 ? argv[4] : inputFile.parent_path() / inputFile.stem() += "_NEW.XA";
    FILE *outputFile = fopen(outputPath.string().c_str(), "w+b");
    if (!outputFile)
    {
        fprintf(stderr, "Error: Cannot write \"%s\". %s\n", outputPath.string().c_str(), strerror(errno));
        return EXIT_FAILURE;
    }
    std::unique_ptr<char[]> stdoBuf(new char[1024 * 1024]);
    setvbuf(outputFile, stdoBuf.get(), _IOFBF, 1024 * 1024);

    files.interleave(outputFile, sectorSize);
    fclose(outputFile);

    printf("Process complete.\n");

    return EXIT_SUCCESS;
}
