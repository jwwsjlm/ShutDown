#pragma once

#include <string>

class ShutdownExecutor {
public:
    static bool execute(bool force, std::wstring *errorMessage = nullptr);
};
