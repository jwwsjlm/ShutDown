#pragma once

#include "NativeTypes.h"

class SettingsStore {
public:
    static void saveTask(const PersistedTask &task);
    static PersistedTask loadTask();
    static void clearTask();
    static bool hasTask();
};
