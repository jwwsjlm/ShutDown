add_executable(ShutDownTests
    test_scheduler.cpp
    ../src/SettingsStore.cpp
    ../src/ShutdownExecutor.cpp
    ../src/ShutdownScheduler.cpp
    ../src/TaskSchedulerFallback.cpp
)
target_include_directories(ShutDownTests PRIVATE ../src)
target_link_libraries(ShutDownTests PRIVATE advapi32 user32 shell32)
add_test(NAME ShutDownTests COMMAND ShutDownTests)

add_executable(ShutDownUpdateTests
    test_update_manager.cpp
    ../src/UpdateManager.cpp
)
target_include_directories(ShutDownUpdateTests PRIVATE ../src)
target_link_libraries(ShutDownUpdateTests PRIVATE winhttp bcrypt shell32)
add_test(NAME ShutDownUpdateTests COMMAND ShutDownUpdateTests)
