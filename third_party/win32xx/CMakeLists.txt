cmake_minimum_required(VERSION 3.20)

# WIN32XX_VERSION carries the full build version. CI injects the
# four-component value 9.6.1.{buildnumber} via -DWIN32XX_VERSION=...; a local
# build that does not pass the option falls back to the base release version.
if(NOT DEFINED WIN32XX_VERSION OR WIN32XX_VERSION STREQUAL "")
    set(WIN32XX_VERSION "9.6.1")
endif()

project(win32xx VERSION "${WIN32XX_VERSION}" LANGUAGES CXX)

# Win32++ is a header-only framework, so the CMake target only needs to expose
# the include directory. Consumers keep using the existing headers:
#   #include "wxx_wincore.h"
add_library(win32xx INTERFACE)
add_library(win32xx::win32xx ALIAS win32xx)

target_include_directories(win32xx
        INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Win32++ is a TCHAR-based framework. This package compiles consumers in
# Unicode by default: linking win32xx::win32xx defines UNICODE and _UNICODE so
# the Windows API and Win32++ resolve to their wide-character variants.
target_compile_definitions(win32xx
        INTERFACE
        UNICODE
        _UNICODE)

# Win32++ is a Windows GUI framework, so it declares the system import
# libraries it needs. Consumers then only have to link win32xx::win32xx and do
# not need to repeat this list. Most of these are already default libraries for
# the common toolchains; comctl32 (common controls) is the one that usually is
# not, so listing them here keeps consumers correct regardless of toolchain.
target_link_libraries(win32xx
        INTERFACE
        comctl32
        comdlg32
        gdi32
        user32
        advapi32
        shell32
        ole32
        oleaut32
        uuid
        winspool)

# Win32++ places all of its classes in the Win32xx namespace, but by default it
# also injects 'using namespace Win32xx;' globally (see wxx_setup.h). That
# global using is what collides with ATL and WTL, which declare identically
# named classes (CWnd, CString, CDC, CRect, ...). Enable this option to skip the
# global using; consumers then qualify Win32++ types as Win32xx::CWnd and the
# three frameworks coexist. The option is OFF by default for source
# compatibility with existing single-framework projects.
option(WIN32XX_NO_USING_NAMESPACE
        "Skip 'using namespace Win32xx;' so Win32++ can coexist with ATL/WTL" OFF)
if (WIN32XX_NO_USING_NAMESPACE)
    target_compile_definitions(win32xx INTERFACE NO_USING_NAMESPACE)
endif()
