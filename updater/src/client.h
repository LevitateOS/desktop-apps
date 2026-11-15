// The only place the Updater runs `recupdate` to change anything, and the
// restart. Checking is the kit's (`ReleaseCheck`, as the person). Applying and
// restarting go through the root helper (pkexec), which shows the desktop's own
// password prompt.
#pragma once

#include "run.h"

#include <QObject>

class UpdaterClient : public QObject {
    Q_OBJECT
public:
    explicit UpdaterClient(QObject *parent = nullptr);

    // `recupdate apply --json`, through the root helper. Resumes by itself when an
    // earlier one was interrupted. The JSON lines are on standard output.
    Run *apply();
    Run *reboot();

    bool asksPassword() const { return !rootHelper_.isEmpty(); }
    QString commandWords(const QStringList &arguments) const;

private:
    Run *asRoot(const QString &program, const QStringList &arguments);
    QString program_, rootHelper_, reboot_;
};
