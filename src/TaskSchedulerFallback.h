#pragma once

#include <QDateTime>
#include <QString>

class TaskSchedulerFallback {
public:
    static QString taskName();
    static bool create(const QDateTime &when, bool force, QString *errorMessage = nullptr);
    static bool remove(QString *errorMessage = nullptr);
    static bool exists();
};
