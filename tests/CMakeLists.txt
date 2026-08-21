find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(ShutDownTests
    test_scheduler.cpp
    ../src/SettingsStore.cpp
)
target_include_directories(ShutDownTests PRIVATE ../src)
target_link_libraries(ShutDownTests PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME ShutDownTests COMMAND ShutDownTests)
