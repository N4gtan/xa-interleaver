# xa-interleaver
`⠀xa-interleaver ` builds a mixed audio xa file, supported by psx, from a manifest file with single audio xa files.

`xa-deinterleaver` dumps a mixed audio xa file into single audio xa files and generates a manifest file of them.

`⠀⠀xa-replacer   ` replaces a mixed audio xa file stream with a given single audio xa file.
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
xa-interleaver <input> [stride] [size] [output]
```
```
xa-deinterleaver <input> [size] [output]
```

### Commands
>[!CAUTION]
>All arguments are positional so, to set an `<output>`, you need to set the previous ones.

>[!TIP]
>On Windows, can simply drag and drop a supported file into an exe to run it with the default options.

Required:
```
<input>     For interleave can be a .csv, .txt, or any text file properly formated.
            For deinterleave can be any mixed audio file. A .bin CD image file works, but is not recommended.
```
Optional:
```
[stride]    (Only for interleave) Stride of sectors to interleave, 2/4/8/16/32. Defaults to 8
[size]      Optional output file sector size (2336 or 2352). Defaults to input file (or the first file in the manifest) sector size
[output]    Optional output dir/file path. Defaults to input dir/file path
```
Examples:
```
xa-interleaver path/to/input.csv 8 2336 path/to/output.xa
xa-deinterleaver path/to/input.xa 2352 path/to/output/
```

### xa-replacer
```
xa-replacer <target> <source> [sector]
Example:
xa-replacer path/to/target.str path/to/source.xa 7
```
>[!CAUTION]
>Files containing non-media data might cause the tool to misidentify null data sectors as null audio sectors.

## Manifest
The manifest text file must be in the following format:
```
chunk,type,file,null_trailing,xa_file_number,xa_channel_number,xa_null_subheader
```
| Field             | Value            | Information                                                                                     |
|-------------------|------------------|-------------------------------------------------------------------------------------------------|
|`chunk`            |`1`-`<stride>`    |Sector chunk size to write between strides. Cannot be higher than the `<stride>` command.        |
|`type`             |`xa` `xacd` `null`|`xa` for 2336 sector file. `xacd` for 2352 sector file. `null` for empty sectors.                |
|`file`             |`my file.xa`      |File name or path. Can be a relative or absolute path, but without "quotes". (omitted for `null`)|
|`null_trailing`    |`0` or more       |***OPTIONAL*** number of trailing null sectors after the end of the file. Defaults to `0`.       |
|`xa_file_number`   |`0`-`255`         |***OPTIONAL*** subheader file number identifier. Defaults to `file` original value (RAW copy).   |
|`xa_channel_number`|`0`-`254`         |***OPTIONAL*** subheader channel number identifier. Defaults to `file` original value (RAW copy).|
|`xa_null_subheader`|`0xFFFFFFFF`      |***OPTIONAL*** subheader value for `null_trailing` sectors. Defaults to 0x`xa_file_number`000000.|

>[!NOTE]
>The deinterleaver's `sector_beg-end` and `stride` fields are purely informative and do not affect interleaving.
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
>
>Or to replace the audio of an image or .str file, simply use `xa-replacer`

## Compile
A C++ compiler (MSVC, GCC, Clang) is required.

### MSVC:
```
cl /std:c++17 /O2 /EHsc /Fexa-replacer xa-replacer.cxx
cl /std:c++17 /O2 /EHsc /Fexa-interleaver xa-interleaver.cxx
cl /std:c++17 /O2 /EHsc /Fexa-deinterleaver xa-deinterleaver.cxx
```
### GCC:
```
g++ -std=c++17 -O2 -o xa-replacer xa-replacer.cxx
g++ -std=c++17 -O2 -o xa-interleaver xa-interleaver.cxx
g++ -std=c++17 -O2 -o xa-deinterleaver xa-deinterleaver.cxx
```
### Clang:
```
clang++ -std=c++17 -O2 -o xa-replacer xa-replacer.cxx
clang++ -std=c++17 -O2 -o xa-interleaver xa-interleaver.cxx
clang++ -std=c++17 -O2 -o xa-deinterleaver xa-deinterleaver.cxx
```
