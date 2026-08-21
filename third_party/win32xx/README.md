# Win32++

[![build](https://github.com/tinysec/win32xx/actions/workflows/ci.yaml/badge.svg)](https://github.com/tinysec/win32xx/actions)
[![usage](https://img.shields.io/badge/usage-FetchContent-blue)](#use-from-another-cmake-project)

## Introduction

Win32++ is an open-source C++ library that simplifies the development of
Windows API-based applications. It provides a C++ interface (classes,
inheritance, templates) while staying lightweight and close to the Windows API.

This repository packages Win32++ 9.6.1 as a header-only CMake dependency. The
9.6.1 release keeps to a pre-C++11 dialect, so it builds with older toolchains
such as the WDK7 (VS2008) compiler. The CMake target is:

```cmake
win32xx::win32xx
```

Linking this target adds the Win32++ include directory to consumers, so the
existing Win32++ headers keep working:

```cpp
#include "wxx_wincore.h"
#include "wxx_frame.h"
```

Linking `win32xx::win32xx` also defines `UNICODE` and `_UNICODE`, so consumers
build in Unicode by default and the TCHAR-based Win32++ API resolves to its
wide-character variants.

Win32++ requires a Windows C++ toolchain.

## Use From Another CMake Project

Use `FetchContent` to load this repository from GitHub. Pin whichever version
granularity fits your needs:

```cmake
include(FetchContent)

FetchContent_Declare(
        win32xx
        GIT_REPOSITORY https://github.com/tinysec/win32xx.git
        GIT_TAG v9.6.1)         # floating tag and release alias; immutable
                                # v9.6.1.{build} tags/releases also exist
FetchContent_MakeAvailable(win32xx)

target_link_libraries(your_target PRIVATE win32xx::win32xx)
```

Version tags and releases published by CI:

- `v9.6.1.{buildnumber}` - immutable, full four-component build tag and release
- `v9.6.1` - floating three-component tag and GitHub release alias, re-pointed
  to the latest build

This repository is intended to be consumed with `FetchContent`; it does not
provide CMake install or package configuration support.

## Build With The WDK7 Toolchain

`cmake/wdk7.cmake` is the Windows Driver Kit 7.1 (7600.16385.1) CMake toolchain
file. Consumers that target WDK7 configure with it directly:

```bat
cmake -S . -B build-wdk7 -G "NMake Makefiles" ^
  -DCMAKE_TOOLCHAIN_FILE=cmake/wdk7.cmake ^
  -DWDK7_ARCH=amd64
```

Set `WDK7_ROOT` (or the `W7BASE` / `WDK7_ROOT` environment variable) when the
WDK7 tree is not at a default install path. `WDK7_ARCH` accepts `i386` or
`amd64`. `cmake/wdk7.cmake` is a verbatim copy of the canonical toolchain at
<https://github.com/tinysec/setup-wdk7/blob/master/cmake/wdk7.cmake> and must
not be edited locally — sync it from the canonical source instead.

## License

The source code of Win32++ is licensed under the
[MIT License](https://opensource.org/license/mit). See `license.txt`.

## Upstream

- SourceForge: <https://sourceforge.net/projects/win32-framework/>
- Upstream GitHub: <https://github.com/DavidNash2024/Win32xx>
