cmake_minimum_required(VERSION 3.16)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(SHUTDOWN_BASE_VERSION 2.1.4)
set(SHUTDOWN_VERSION_OVERRIDE "" CACHE STRING "Override version, normally passed by release CI from the git tag.")

set(SHUTDOWN_EFFECTIVE_VERSION "${SHUTDOWN_BASE_VERSION}")
if (NOT "${SHUTDOWN_VERSION_OVERRIDE}" STREQUAL "")
    set(SHUTDOWN_EFFECTIVE_VERSION "${SHUTDOWN_VERSION_OVERRIDE}")
elseif (DEFINED ENV{SHUTDOWN_VERSION_OVERRIDE} AND NOT "$ENV{SHUTDOWN_VERSION_OVERRIDE}" STREQUAL "")
    set(SHUTDOWN_EFFECTIVE_VERSION "$ENV{SHUTDOWN_VERSION_OVERRIDE}")
endif()
if (NOT "${SHUTDOWN_EFFECTIVE_VERSION}" MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid SHUTDOWN version '${SHUTDOWN_EFFECTIVE_VERSION}'. Expected MAJOR.MINOR.PATCH.")
endif()

project(ShutDown VERSION ${SHUTDOWN_EFFECTIVE_VERSION} LANGUAGES CXX)
message(STATUS "ShutDown version: ${PROJECT_VERSION}")

if (NOT WIN32)
    message(FATAL_ERROR "ShutDown now targets Windows and Win32++ only")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(third_party/win32xx)

set(SHUTDOWN_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")
set(SHUTDOWN_RC_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.h")
set(SHUTDOWN_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
set(SHUTDOWN_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
set(SHUTDOWN_FILE_VERSION "${PROJECT_VERSION}.0")
configure_file(resources/app.rc.in "${SHUTDOWN_RC_FILE}" @ONLY)

set(SHUTDOWN_SOURCES
    src/main.cpp
    src/AppLogger.cpp
    src/SettingsStore.cpp
    src/ShutdownExecutor.cpp
    src/ShutdownScheduler.cpp
    src/TaskSchedulerFallback.cpp
    src/UpdateManager.cpp
    src/MainWindow.cpp
    "${SHUTDOWN_RC_FILE}"
)

add_executable(ShutDown WIN32 ${SHUTDOWN_SOURCES})
set_source_files_properties("${SHUTDOWN_RC_FILE}" PROPERTIES
    OBJECT_DEPENDS "${SHUTDOWN_MANIFEST_FILE};${SHUTDOWN_ICON_FILE};${SHUTDOWN_RC_INCLUDE};${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
)
target_include_directories(ShutDown PRIVATE src third_party/win32xx/include)
target_link_libraries(ShutDown PRIVATE win32xx::win32xx comctl32 shell32 advapi32 user32 gdi32 ole32 uxtheme dwmapi winhttp)
if(MSVC)
    target_link_options(ShutDown PRIVATE "/MANIFEST:NO")
endif()
target_compile_definitions(ShutDown PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WINVER=0x0601
    _WIN32_WINNT=0x0601
    NTDDI_VERSION=0x06010000
    SHUTDOWN_VERSION="${PROJECT_VERSION}"
)
if (MINGW)
    target_compile_options(ShutDown PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    # MinGW uses a Unicode CRT entry point for wWinMain.
    target_link_options(ShutDown PRIVATE -municode)
endif()
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
