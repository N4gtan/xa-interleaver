#include "libxa_deinterleaver.hxx"
#define VER "VERSION"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("xa-deinterleaver " VER " by N4gtan\n\n");
        printf("    Usage: xa-deinterleaver <input> <2336/2352> <output>\n\n");
        printf("    Input: XA interleaved file\n");
        printf("2336/2352: Output file sector size (2336 or 2352). Defaults to input file sector size\n");
        printf("   Output: Optional output file(s) path. Defaults to input file path\n");
        return EXIT_SUCCESS;
    }

    const std::filesystem::path inputFile = argv[1];
    const int sectorSize = argc >= 3 ? atoi(argv[2]) : 0;
    const std::filesystem::path outputDir = argc >= 4 ? argv[3] : inputFile.parent_path() / inputFile.stem();

    deinterleaver(inputFile).deinterleave(outputDir, sectorSize);

    printf("Process complete.\n");

    return EXIT_SUCCESS;
}
