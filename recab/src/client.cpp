#include "client.h"

#include <QJsonDocument>

RecabClient::RecabClient(QObject *parent)
    : QObject(parent)
    , program_(qEnvironmentVariable("RECAB_GUI_RECAB", "recab"))
    , rootHelper_(qEnvironmentVariable("RECAB_GUI_ROOT_HELPER", "pkexec"))
    , reboot_(qEnvironmentVariable("RECAB_GUI_REBOOT", "reboot"))
{
}

Run *RecabClient::asRoot(const QString &program, const QStringList &arguments)
{
    if (rootHelper_.isEmpty())
        return new Run(program, arguments, this);
    return new Run(rootHelper_, QStringList{program} + arguments, this);
}

Run *RecabClient::read(bool root)
{
    const QStringList arguments{"status", "--json"};
    return root ? asRoot(program_, arguments) : new Run(program_, arguments, this);
}

bool RecabClient::understand(Run *finished, bool ok, Status *into, QString *error, bool *permission)
{
    if (permission)
        *permission = false;
    if (!ok) {
        *error = tr("`%1 status --json` failed: %2").arg(program_, finished->problem());
        if (permission)
            *permission = finished->failure()["class"] == QLatin1String("permission")
                          || finished->problem().contains(QLatin1String("Permission denied"));
        return false;
    }
    error->clear();
    *into = Status::fromJson(QJsonDocument::fromJson(finished->answer()).object(), error);
    return error->isEmpty();
}

Run *RecabClient::copy()
{
    return asRoot(program_, {"copy"});
}

Run *RecabClient::bootOther(bool cancel)
{
    QStringList arguments{"boot-other"};
    if (cancel)
        arguments << "--cancel";
    return asRoot(program_, arguments + QStringList{"--json"});
}

Run *RecabClient::update()
{
    return asRoot(program_, {"update", "--json"});
}

Run *RecabClient::reboot()
{
    return asRoot(reboot_, {});
}

QString RecabClient::commandWords(const QStringList &arguments) const
{
    return QStringLiteral("recab ") + arguments.join(QLatin1Char(' '));
}
