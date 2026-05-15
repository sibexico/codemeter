# codemeter

Command-line tool that counts lines of code recursively in a directory.

## Behavior

- Default mode ignores comment-only lines.
- Use `-c` to include comment-only lines.
- Use `-f` to count only selected file extensions.
- Use `-x` to exclude selected file/folder names.
- Supports both directory and single-file targets.
- Runs on Linux and Windows.

## Usage

```bash
codemeter [-c] [-f ext1,ext2,...] [-x name1,name2,...] [path]
```

Examples:

```bash
codemeter .
codemeter -c .
codemeter -f cpp,hpp,h /home/user/project
codemeter -x vendor,backup,build /home/user/project
```

## Build

Linux or MinGW with Makefile:

```bash
make
```

Linux (GCC/Clang):

```bash
gcc -std=c23 -O2 -Wall -Wextra -pedantic codemeter.c -o codemeter
```

Windows (MinGW-w64 GCC):

```powershell
gcc -std=c23 -O2 -Wall -Wextra codemeter.c -o codemeter.exe
```
