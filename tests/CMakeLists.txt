add_executable(ShutDownTests
    test_scheduler.cpp
    ../src/SettingsStore.cpp
)
target_include_directories(ShutDownTests PRIVATE ../src)
target_link_libraries(ShutDownTests PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME ShutDownTests COMMAND ShutDownTests)
set_tests_properties(ShutDownTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
