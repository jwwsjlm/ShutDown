cmake_minimum_required(VERSION 3.16)

project(ShutDown VERSION 2.0.0 LANGUAGES CXX)

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
target_include_directories(ShutDown PRIVATE src third_party/win32xx/include)
target_link_libraries(ShutDown PRIVATE win32xx::win32xx winhttp bcrypt comctl32 shell32 advapi32 user32 gdi32 ole32)
target_compile_definitions(ShutDown PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SHUTDOWN_VERSION="${PROJECT_VERSION}")
if (MSVC)
    target_compile_options(ShutDown PRIVATE /utf-8)
endif()
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
