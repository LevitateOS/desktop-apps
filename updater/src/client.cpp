#include "client.h"

#include "release.h"

UpdaterClient::UpdaterClient(QObject *parent)
    : QObject(parent)
    , program_(ReleaseCheck::program()) // RECUPDATE_BIN
    , rootHelper_(qEnvironmentVariable("UPDATER_GUI_ROOT_HELPER", "pkexec"))
    , reboot_(qEnvironmentVariable("UPDATER_GUI_REBOOT", "reboot"))
{
}

Run *UpdaterClient::asRoot(const QString &program, const QStringList &arguments)
{
    if (rootHelper_.isEmpty())
        return new Run(program, arguments, this);
    return new Run(rootHelper_, QStringList{program} + arguments, this);
}

Run *UpdaterClient::apply()
{
    return asRoot(program_, {"apply", "--json"});
}

Run *UpdaterClient::reboot()
{
    return asRoot(reboot_, {});
}

QString UpdaterClient::commandWords(const QStringList &arguments) const
{
    return QStringLiteral("recupdate ") + arguments.join(QLatin1Char(' '));
}
