add_executable(ShutDownTests
    test_scheduler.cpp
    ../src/SettingsStore.cpp
)
target_include_directories(ShutDownTests PRIVATE ../src)
target_link_libraries(ShutDownTests PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME ShutDownTests COMMAND ShutDownTests)
set_tests_properties(ShutDownTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(ShutDownUpdateTests
    test_update_manager.cpp
    ../src/UpdateManager.cpp
)
target_include_directories(ShutDownUpdateTests PRIVATE ../src)
target_link_libraries(ShutDownUpdateTests PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
add_test(NAME ShutDownUpdateTests COMMAND ShutDownUpdateTests)
set_tests_properties(ShutDownUpdateTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
