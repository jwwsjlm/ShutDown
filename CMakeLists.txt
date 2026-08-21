cmake_minimum_required(VERSION 3.16)

project(ShutDown VERSION 1.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer Qt 6 when available (the current Qt installer usually exposes Qt 6),
# while retaining compatibility with Qt 5.15 installations.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(SHUTDOWN_SOURCES
    src/main.cpp
    src/AppLogger.cpp
    src/UpdateManager.cpp
    src/MainWindow.cpp
    src/ShutdownScheduler.cpp
    src/ShutdownExecutor.cpp
    src/TaskSchedulerFallback.cpp
    src/SettingsStore.cpp
)

set(SHUTDOWN_HEADERS
    src/MainWindow.h
    src/AppLogger.h
    src/UpdateManager.h
    src/ShutdownScheduler.h
    src/ShutdownExecutor.h
    src/TaskSchedulerFallback.h
    src/SettingsStore.h
)

add_executable(ShutDown WIN32
    ${SHUTDOWN_SOURCES}
    ${SHUTDOWN_HEADERS}
    resources/app.qrc
    resources/app.rc
)

target_link_libraries(ShutDown PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
target_include_directories(ShutDown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ShutDown PRIVATE SHUTDOWN_VERSION="${PROJECT_VERSION}")

if (TARGET Qt${QT_VERSION_MAJOR}::QWindowsIntegrationPlugin)
    target_link_libraries(ShutDown PRIVATE Qt${QT_VERSION_MAJOR}::QWindowsIntegrationPlugin)
endif()

# All user-facing strings in the Qt UI are UTF-8.  MSVC otherwise uses the
# active Windows code page for source files without a BOM, which turns Chinese
# string literals into mojibake in the packaged Windows build.  Keep both the
# input and execution character sets explicit for every supported toolchain.
if (MSVC)
    target_compile_options(ShutDown PRIVATE /utf-8)
elseif (MINGW)
    target_compile_options(ShutDown PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if (WIN32)
    target_compile_definitions(ShutDown PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0601 _WIN32_WINNT=0x0601
    )
    target_link_libraries(ShutDown PRIVATE advapi32 user32 shell32)
endif()

install(TARGETS ShutDown RUNTIME DESTINATION bin)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
