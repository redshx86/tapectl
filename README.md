
## Description

Backup your files to tape drive in common format without using bloated software.

OS: Windows 2000 / XP / 7

This is WIP project. Use with care!

## Tape quick tutorial

To backup directory, pack it into some archive and write to the tape:

`tapectl -w folder.zip`

Data will be padded with 00's to nearest multiple of block size. When block size not set, program uses device default block size for padding. You can check current block size with drive info command:

`tapectl -i`

Block size can be set with `-k` command. For example, command

`tapectl -k 32K -w folder.zip`

sets block size to 32768 bytes then writes folder.zip to the tape.

Tape drives usually have hardware data compression and you probably want to disable it when archiving already compressed data. Use `-C` command to enable or disable hardware compression:

`tapectl -C off -k 32K -w folder.zip`

To restore your file, rewind with `-o` and read file with `-r`:

`tapectl -o -r restored.zip`

To read data from tape, exactly same block size should be set as when writing data. Otherwise driver will probably report error.

To append other file, seek to the end of data with `-e`, write filemark (special block for marking end of file or separating files) with `-m` and write data with `-w`. Options can be combined to single group:

`tapectl -emw other.zip`

Note: written data padded with 00's to nearest multiple of block size. For example, `-w file1.zip file2.zip` writes to the tape `<file1.zip> <zero padding> <file2.zip> <zero padding>`. Most unpackers can't parse this as single archive. You normally should use filemarks to separate files. Program emits warning before writing multiple files without filemark between them. However program doesn't check for filemark when adding files to the tape with some existing data. You can use `-epf` (seek to end of data, move 1 block back and try to skip filemark) to check if filemark present at the end of data. It should succeed if last block is filemark and give error otherwise. Use `-mw <file>` to put filemark before appending data.

To restore this file immediately, backward seek to filemark with `-b`, skip filemark with `-f` and read data with `-r`:

`tapectl -bfr another.zip`

You can use count parameter with `-f`. For example, to read third file from the tape:

`tapectl -of 2 -r third.zip`

It's possible to give `-r` or `-w` list of filenames to read or write multiple files from the tape:

`tapectl -or first.zip second.zip third.zip`

When using `-W`, filemark added after each written file. For example,

`tapectl -W first.zip second.zip -w third.zip`

is equivalent of 

`tapectl -w first.zip -m -w second.zip -m -w third.zip`

Check full/remaining capacity of tape and eject tape.

`tapectl -cJ`

## Command line reference

Use "-" or "/" for command line options. Add quotes around parameter if it begins with some of this characters e.g. `-w "-test-.zip"`.

Command line options can be combined to groups (e.g. `-ew file.zip` is equivalent of `-e -w file.zip`). If group contains parametrized options, only first of them can use following parameters and others will use default value. For example `-rb file1.zip file2.zip` is equivalent to `-r file1.zip -r file2.zip -b 1`, `-rw file1.zip file2.zip` is equivalent to `-r file1.zip -r file2.zip -w` (which is invalid because `-w` requires a file name) and `-fb 1 1` is equivalent to `-f 1 1 -b` (which is also invalid).

You can use size modifiers for numbers if this applicable, e.g. `-k 32k`, `-I "2 M"` or `-G 16GB`. For boolean parameters either on/off, yes/no, true/false or 1/0 can be used.

### Drive commands

`-t <N>`, `-t <tapeN>` or `-t \\.\TapeN`
Select tape device to be used.

`-C <on/off>`
Switch data compression on/off.

`-E <on/off>`
Switch ECC on/off. Most drives have ECC always on.

`-D <on/off>`
Switch data padding on/off (none of my drives supports this).

`-R <on/off>`
Switch setmark reporting on/off (none of my drives supports this).

`-Z <N>[k/M/G]`
Set EOT warning zone size (none of my drives supports this).

`-k <N>[k]`
Set block size. Seems that Windows doesn't support block size > 64K.

`-x`, `-u`
Lock or unlock media ejection with button on the drive (still can be forced with long pressing).

`-i`
Display drive and media information. Add `-v` to also list drive features or `-V` to also print description for each feature.

### Tape commands

`-L`
Load tape. Drive specific: can load cartridge, load tape from cartridge or do nothing.

`-J`
Unload tape. Drive specific: usually rewinds and unloads tape and ejects cartridge.

`-c`
Display media full capacity and remaining capacity if supported.

`-T`
Tension tape (rewind tape back and forth), not usually supported.

### Positioning commands

`-l`
Display (list) current position. If device supports partitions, it displays absolute and logical position.

`-o`
Rewind tape ("origin").

`-e`
Seek to the end of data.

`-a <absolute block>`
Seek to absolute block number. If device doesn't support partitions it usually same as '-s <logical block>'.

`-s [partition.]<logical block>`
Seek to logical block number. If partition omitted, position will be set within current partition. If device doesn't support partitions it usually same as `-a <absolute block>`.

`-n [count]`, `-p [count]`
Seek count blocks forward or backwards. If count omitted, it seeks to next or previous block.

`-f [count]`, `-b [count]`
Seek count filemarks forward or backwards. If count omitted, it seeks to next or previous filemark. Seeking stops past filemark in conforming direction (i.e. `-f` stops after filemark and `-b` stops before filemark in order of block numbers).

`-F [count]`, `-B [count]`
Same as previous but for setmarks (none of my drives support setmarks).

### Data reading

`-r <filename>`
Read data from the tape to specific file from current position until end of data or filemark reached or error occurred. When filemark encountered, current position will be after filemark. You must set correct block size before data can be read. You can read multiple files by passing multiple filenames as parameter e.g. `-r file1.zip file2.zip file3.zip`

### Writing commands

Using following commands can destroy existing data on your tape. Usually writing something data to the tape sets EOD mark to current position, making following data inaccessible. If you pass some writing commands, program will ask confirmation one time before program starts any operation (unless overwrite forced with -Y switch).

`-w <filename>`, `-W <filename>`
Write file to the tape at current position. If block size not set, drive default block size used for padding/alignment. `-W` also adds a filemark after data. You can pass multiple filenames to this commands (e.g. `-W file1.zip file2.zip -w file3.zip` writes 3 files with filemarks between them).

`-m`
Write filemark at current position.

`-M`
Write setmark at current position (rarely used and supported).

`-t`
Truncate data at current position so following data can't be more accessed normal way. `-ot` truncates at origin (logically erasing all data on the tape).

`-X`
Fully erase data on tape. This operation can take long time (same as writing whole tape).

### Buffering options

`-G <N>[M/G]`
Set buffer size for reading/writing data. Defaults to 128 MB. Buffers larger than 512 MB allocated in user pages (unswappable physical pages, memory lock privilege required). You can use any buffer size as long as you have enough free RAM (64-bit OS not required).

`-I <N>[k/M]`
Set I/O block size for reading/writing data. Defaults to 1 MB. Rounded up to block size for tape access and to 4 KB for file access.

`-Q <N>`
Set I/O queue depth. When N == 0, I/O operations done using regular I/O. When N > 0, overlapped I/O used and up to N operations can be issued concurrently to device driver. This setting is common to file and tape drive access.

`-U`
Use Windows buffering. By default files and tape drive opened with `FILE_FLAG_NO_BUFFERING`. This key removes flag and files opened with `FILE_FLAG_SEQUENTIAL_SCAN`.

### Display options

`-h`, `-H`, `-?`
Show quick reference and exit.

`-v`, `-V`
Verbose and very verbose output.

`-q`
Minimize output. Only error messages and explicitly requested information will be displayed.

`-S`
Show list of operations and ask user for confirmation before starting doing anything.

`-N`
Check commands and exit before executing any operations. Use `-SN` to check that your command line does exactly what you want. Device still will be opened for doing extra checks (such as for support of given commands by the device). Use `-SNy` to omit extra checks and just display list commands to be executed.

`-y`
Skip sanity checks before executing operations.

`-Y`
Don't ask for confirmation before overwriting data on tape or disk.

`-P`
By default program uses 11-second timeout as prompt for data overwriting. This gives you extra time to think when executing command interactively and prevent stucking in batch files. You can change this to standard Y/N prompt by this switch.

## Configuration file

Any options can be made permanent by adding it to configuration file. Configuration file should have same name as executable but with .cfg extension (tapectl.cfg by default). Each non-empty line, not starting with ';' or '#' parsed same way as command line before actual command line.

## References

Fast CRC32 algorithm used in program:
https://create.stephan-brumme.com/crc32/
