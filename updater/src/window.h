// The one window: the installed release, "Check for updates", and the install.
#pragma once

#include "before.h"
#include "client.h"
#include "install.h"
#include "release.h"

#include <QMainWindow>

#include <functional>

class MainPage;
class QStackedWidget;
class Refresher;

class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QWidget *parent = nullptr);

    // Installs the release found, or finishes an interrupted one; each asks first
    // (the Before page) unless `askFirst` is false.
    void install(bool askFirst = true);
    void finishInterrupted(bool askFirst = true);
    void restart(bool askFirst = true);
    // Reads the files again, as F5 does.
    void refresh();

private:
    void applyNow(const QString &version, bool resuming);
    void ask(const BeforePage::Content &content, std::function<void()> go);
    void showMain();
    BeforePage::Content installContent(const QString &title, const QString &go, const QString &first) const;

    UpdaterClient client_;
    ReleaseCheck *updates_;
    Refresher *refresher_;
    QStackedWidget *stack_;
    MainPage *main_;
    InstallPage *install_;
    BeforePage *before_;
    std::function<void()> go_;
    bool read_ = false, installing_ = false;
    QString restartFor_;      // installed in this session, waits for a restart
    bool installedBefore_ = false;
};
