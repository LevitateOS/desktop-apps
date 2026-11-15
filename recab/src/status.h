// What `recab status --json` says (schema 11): which root is booted and
// locked, which is the other and unlocked, where the other is mounted,
// whether the other can be started, and which root starts at the next boot.
#pragma once

#include <QJsonObject>
#include <QString>

struct Status {
    QString booted, other; // "A" or "B"
    QString otherMount;    // empty: the other root is not mounted
    bool otherBootable = false;
    QString bootDefault;   // empty: the boot default names neither root
    QString nextOnce;      // empty: no one-time pick

    // The root that starts at the next boot: the one-time pick, else the boot default.
    QString startsNext() const { return nextOnce.isEmpty() ? bootDefault : nextOnce; }
    // Reads recab's answer. An empty `error` means it was understood.
    static Status fromJson(const QJsonObject &object, QString *error);
};
