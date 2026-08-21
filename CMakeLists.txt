cmake_minimum_required(VERSION 3.16)

project(ShutDown VERSION 1.0.0 LANGUAGES CXX)

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

if (WIN32)
    target_compile_definitions(ShutDown PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ShutDown PRIVATE advapi32 user32 shell32)
endif()

install(TARGETS ShutDown RUNTIME DESTINATION bin)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
