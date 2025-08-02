# xa-interleaver
`xa-interleaver  ` builds a mixed audio xa file, supported by psx, from a manifest file with single audio xa files.

`xa-deinterleaver` dumps a mixed audio xa file into single audio xa files and generates a manifest file of them.
>[!IMPORTANT]
>What's the difference between other tools? Well, this **CAN** interleave in succession with a custom stride pattern.
>
>Many games have lots of files interleaved into a single file (e.g. in stride of 8) and other tools can't achieve that.
>
>Those are limited to the standart limited structure like the old Movie Converter (MC32.EXE).

>[!NOTE]
>The interleaver tool does not regenerate ECC/EDC for the output.

## Download
[Releases](../../releases/latest) for Windows, Linux and macOS (built by github CI)

## Usage
```
xa-interleaver <input> <stride> <2336/2352> <output>
```
```
xa-deinterleaver <input> <2336/2352> <output>
```

### Commands
>[!CAUTION]
>All arguments are positional so, to set an `<output>`, you need to set the previous ones.

>[!TIP]
>On Windows, can simply drag and drop a supported file into an exe to run it with the default options.

Required:
```
<input>     For interleave can be a .csv, .txt, or any text file properly formated.
            For deinterleave can be any mixed audio file. A CD image file (bin/iso) can work, but is not recommended.
```
Optional:
```
<stride>    (Only for interleave) Stride of sectors to interleave, 2/4/8/16/32. Defaults to 8
<2336/2352> Output file sector size (2336 or 2352). Defaults to input file (or the first file in the manifest) sector size
<output>    Optional output file(s) path. Defaults to input file path
```
Examples:
```
xa-interleaver path/to/input.csv 8 2336 path/to/output.xa
xa-deinterleaver path/to/input.xa 2352 path/to/output/
```
## Manifest
The manifest text file must be in the following format:
```
sectors,type,file,null_termination,xa_file_number,xa_channel_number
```
| Field             | Value            | Information                                                                                   |
|-------------------|------------------|-----------------------------------------------------------------------------------------------|
|`sectors`          |`1`-`<stride>`    |Sector block size to write between strides. Cannot be higher than the `<stride>` command.      |
|`type`             |`xa` `xacd` `null`|`xa` for 2336 sector file. `xacd` for 2352 sector file. `null` for empty sectors.              |
|`file`             |`my file.xa`      |File name or path. Can be a relative or absolute path, but without "quotes".                   |
|`null_termination` |`0` or more       |Number of null-termination sectors after the end of the file.                                  |
|`xa_file_number`   |`0`-`255`         |Subheader file number identifier. Can be omitted to keep the file original value (RAW copy).   |
|`xa_channel_number`|`0`-`254`         |Subheader channel number identifier. Can be omitted to keep the file original value (RAW copy).|

>[!NOTE]
><details>
><summary>Example.csv manifest for Megaman X6 BGM.XA:</summary>
>
>```
>1,xa,BGM_00.xa,20,1,0
>1,xa,BGM_01.xa,20,1,1
>1,xa,BGM_02.xa,20,1,2
>1,xa,BGM_03.xa,20,1,3
>1,xa,BGM_04.xa,20,1,4
>1,xa,BGM_05.xa,20,1,5
>1,xa,BGM_06.xa,20,1,6
>1,xa,BGM_07.xa,20,1,7
>1,xa,BGM_08.xa,20,1,7
>1,xa,BGM_09.xa,20,1,6
>1,xa,BGM_10.xa,20,1,5
>1,xa,BGM_11.xa,20,1,4
>1,xa,BGM_12.xa,20,1,3
>1,xa,BGM_13.xa,20,1,2
>1,xa,BGM_14.xa,20,1,1
>1,xa,BGM_15.xa,20,1,0
>1,xa,BGM_16.xa,20,1,4
>1,xa,BGM_17.xa,20,1,6
>1,xa,BGM_18.xa,20,1,5
>1,xa,BGM_19.xa,20,1,3
>1,xa,BGM_20.xa,20,1,7
>1,xa,BGM_21.xa,20,1,2
>1,xa,BGM_22.xa,20,1,1
>1,xa,BGM_23.xa,20,1,0
>1,xa,BGM_24.xa,20,1,0
>1,xa,BGM_25.xa,20,1,1
>1,xa,BGM_26.xa,20,1,1
>1,xa,BGM_27.xa,20,1,1
>1,xa,BGM_28.xa,20,1,0
>1,xa,BGM_29.xa,20,1,1
>```
></details>

>[!TIP]
>To create an interleaved .str file: (the video stream must be WITHOUT audio)
><details>
><summary>ExampleSTR.csv</summary>
>
>```
>7,xa,video without audio.str
>1,xa,audio file.xa,0,1,1
>```
></details>
>Or to reemplace the audio of an .str file, the best way is using the Movie Converter 3.4 (MC32.EXE)
>
>On the Video+Sound section put the desired .str and .xa files leaving all default but the Frame Rate
>
>15 for NTSC(USA/JAP) and 12.5 for PAL(EUR)
>
>Then, to change the filenum or channel, the following trick can be done with the interleaver:
><details>
><summary>TrickSTR.csv</summary>
>
>```
>1,xa,mc32 output.str,0,1
>```
></details>

## Compile
A C++ compiler (MSVC, GCC, Clang) is required.

### MSVC:
```
cl /std:c++17 /O2 /EHsc /Fexa-interleaver xa-interleaver.cxx
cl /std:c++17 /O2 /EHsc /Fexa-deinterleaver xa-deinterleaver.cxx
```
### GCC:
```
g++ -std=c++17 -O2 -o xa-interleaver xa-interleaver.cxx
g++ -std=c++17 -O2 -o xa-deinterleaver xa-deinterleaver.cxx
```
### Clang:
```
clang++ -std=c++17 -O2 -o xa-interleaver xa-interleaver.cxx
clang++ -std=c++17 -O2 -o xa-deinterleaver xa-deinterleaver.cxx
```
