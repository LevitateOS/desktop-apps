// The only place that runs `recab` and the restart. Reading goes through as
// the desktop user; copying and choosing the root to start go through the root
// helper (pkexec), which shows the desktop's own password prompt.
#pragma once

#include "run.h"
#include "status.h"

#include <QObject>

class RecabClient : public QObject {
    Q_OBJECT
public:
    explicit RecabClient(QObject *parent = nullptr);

    // `recab status --json`, as a run that does not hold the window up.
    // `asRoot`: through the root helper, when the person cannot read the boot
    // entries themselves.
    Run *read(bool asRoot = false);
    // Reads the answer. `permission` is set when the read failed for lack of rights.
    bool understand(Run *finished, bool ok, Status *into, QString *error, bool *permission = nullptr);

    // `recab copy`: make the other root a copy of the booted root. Its own
    // words on standard error are the progress.
    Run *copy();
    // `recab boot-other`, or with `cancel` `recab boot-other --cancel`.
    Run *bootOther(bool cancel);
    // `recab update --json`: fetch the release, copy the booted root into the
    // other, install the release there, start the other root once. Through the
    // root helper; its JSON lines are on standard output (the page follows them).
    Run *update();
    Run *reboot();

    // Whether changes go through a root helper (pkexec), which asks for the
    // administrator password. Without one (the stand-in) they run as the person.
    bool asksPassword() const { return !rootHelper_.isEmpty(); }
    // What a command is, as it is typed: "recab copy".
    QString commandWords(const QStringList &arguments) const;

private:
    Run *asRoot(const QString &program, const QStringList &arguments);
    QString program_, rootHelper_, reboot_;
};
