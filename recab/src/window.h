// The one window: the two roots and what can be done with them.
#pragma once

#include "before.h"
#include "client.h"
#include "install.h"
#include "release.h"
#include "status.h"

#include <QMainWindow>

#include <functional>

class OutputPage;
class QStackedWidget;
class Refresher;
class RootsPage;

class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QWidget *parent = nullptr);

    // Each of these shows the Before page first, which says what will happen;
    // `askFirst = false` does it at once (the checks' direct calls).
    void copyOther(bool askFirst = true);
    void restart(bool askFirst = true);
    // "Install <version> into the other root": `recab update`, with its steps and
    // progress shown as they come; it asks first (the Before page).
    void installUpdate(bool askFirst = true);
    // Starts the other root at the next boot, once; and takes that back.
    void startOther();
    void doNotStartOther();

    // Reads the two roots again, as F5 does.
    void refresh();

    // For the checks.
    const Status &status() const { return status_; }
    RootsPage *rootsPage() const { return roots_; }
    OutputPage *outputPage() const { return output_; }
    BeforePage *beforePage() const { return before_; }
    InstallPage *installPage() const { return install_; }
    ReleaseCheck *releaseCheck() const { return updates_; }
    bool showingInstall() const;
    bool showingRoots() const;
    bool showingOutput() const;
    bool showingBefore() const;
    bool busy() const { return run_ != nullptr || reading_ != nullptr || installing_; }
    // The status line, as read.
    QString statusLine() const;

private:
    // Reads the roots without holding the window up, then does `then`. When
    // they cannot be read, the page says why and `then` is not done.
    void reload(std::function<void()> then = nullptr);
    void showRoots();
    // Runs a command: its words are the output page's, and `then` runs when it has ended.
    void work(Run *run, const QString &title, const QString &subtitle, std::function<void(bool, const QString &)> then);
    void askBefore(const BeforePage::Content &content, std::function<void()> go);

    RecabClient client_;
    Status status_;
    Run *run_ = nullptr, *reading_ = nullptr;
    bool read_ = false;    // the roots were read at least once
    bool asRoot_ = false;  // reading needs the administrator's rights here
    QString note_;         // what the last action did, said in the status line until the next read by hand
    Refresher *refresher_;
    ReleaseCheck *updates_;
    bool installing_ = false;
    QString installedVersion_, installedNote_; // a release put into the other root, not running yet

    QStackedWidget *stack_;
    RootsPage *roots_;
    OutputPage *output_;
    BeforePage *before_;
    InstallPage *install_;
    std::function<void()> go_;
};
