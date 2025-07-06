#include "libxa_interleaver.hxx"
#define VER "VERSION"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("xa-interleaver " VER " by N4gtan\n\n");
        printf("    Usage: xa-interleaver <input> <stride> <2336/2352> <output>\n\n");
        printf("    Input: Manifest .csv file (or any text file with the appropriate format)\n");
        printf("   Stride: Stride of sectors to interleave. Defaults to 8\n");
        printf("2336/2352: Output file sector size (2336 or 2352). Defaults to first file sector size\n");
        printf("   Output: Optional output file path. Defaults to input file path\n");
        return EXIT_SUCCESS;
    }

    const std::filesystem::path inputFile = argv[1];
    const int sectorStride = argc >= 3 ? atoi(argv[2]) : 8;
    const int sectorSize = argc >= 4 ? atoi(argv[3]) : 0;

    interleaver files(inputFile, sectorStride);
    if (files.entries.empty())
    {
        fprintf(stderr, "Invalid manifest\n");
        return EXIT_FAILURE;
    }

    const std::filesystem::path outputPath = argc >= 4 ? argv[3] : inputFile.stem() += "_NEW.XA";
    std::fstream output(outputPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    files.interleave(output, sectorSize);

    output.close();

    printf("Process complete.\n");

    return EXIT_SUCCESS;
}
