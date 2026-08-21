cmake_minimum_required(VERSION 3.16)

project(ShutDown VERSION 2.0.6 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "ShutDown now targets Windows and Win32++ only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party/win32xx)

set(SHUTDOWN_SOURCES
    src/main.cpp
    src/AppLogger.cpp
    src/SettingsStore.cpp
    src/ShutdownExecutor.cpp
    src/ShutdownScheduler.cpp
    src/TaskSchedulerFallback.cpp
    src/UpdateManager.cpp
    src/MainWindow.cpp
    resources/app.rc
)

add_executable(ShutDown WIN32 ${SHUTDOWN_SOURCES})
set_source_files_properties(resources/app.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.h"
)
target_include_directories(ShutDown PRIVATE src third_party/win32xx/include)
target_link_libraries(ShutDown PRIVATE win32xx::win32xx winhttp bcrypt comctl32 shell32 advapi32 user32 gdi32 ole32 uxtheme)
target_compile_definitions(ShutDown PRIVATE UNICODE _UNICODE NOMINMAX SHUTDOWN_VERSION="${PROJECT_VERSION}")
if (MSVC)
    target_compile_options(ShutDown PRIVATE /utf-8)
endif()
if (MINGW)
    target_compile_options(ShutDown PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    # MinGW uses a Unicode CRT entry point for wWinMain.
    target_link_options(ShutDown PRIVATE -municode)
endif()
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
