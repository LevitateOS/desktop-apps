#include "status.h"

#include <QObject>

Status Status::fromJson(const QJsonObject &object, QString *error)
{
    Status s;
    const int schema = object["schema_version"].toInt();
    if (schema != 11) {
        *error = QObject::tr("recab answered in a form this window does not know (schema %1; it reads schema 11).")
                     .arg(schema);
        return s;
    }
    s.booted = object["booted"].toString();
    s.other = object["other"].toString();
    if (s.booted.isEmpty() || s.other.isEmpty()) {
        *error = QObject::tr("recab did not say which root is booted and which is the other.");
        return s;
    }
    s.otherMount = object["other_mount"].toString();
    s.otherBootable = object["other_bootable"].toBool();
    s.bootDefault = object["boot_default"].toString();
    s.nextOnce = object["next_boot_once"].toString();
    return s;
}
