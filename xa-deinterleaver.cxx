#include "libxa_deinterleaver.hxx"
#define VER "VERSION"

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        printf("xa-deinterleaver " VER " by N4gtan\n\n"
               " Usage: xa-deinterleaver <input> <size> <output>\n\n"
               " Input: XA interleaved file\n"
               "  Size: Optional output file sector size (2336 or 2352). Defaults to input file sector size\n"
               "Output: Optional output directory path. Defaults to input directory path\n");
        return EXIT_SUCCESS;
    }

    const std::filesystem::path inputFile = argv[1];
    const int sectorSize = argc >= 3 ? atoi(argv[2]) : 0;
    const std::filesystem::path outputDir = argc >= 4 ? argv[3] : inputFile.parent_path() / inputFile.stem();

    deinterleaver(inputFile).deinterleave(outputDir, sectorSize);
    if (errno)
        return EXIT_FAILURE;

    printf("Process complete.\n");

    return EXIT_SUCCESS;
}
