#pragma once

#include <string>

class AppLogger {
public:
    static void initialize();
    static void write(const std::wstring &level, const std::wstring &message);
};
