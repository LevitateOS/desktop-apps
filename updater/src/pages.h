// The two pages of the window: the first screen, and a problem (the restart did not work).
#pragma once

#include "page.h"
#include "release.h"

class QPushButton;

class MainPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // The first screen. `restartFor` is the release installed in this session that
    // waits for a restart (empty: none). `installedBefore`: an install finished on
    // this system through this app before, so the one-root sentence is not said again here.
    void show(ReleaseCheck *check, const QString &restartFor, bool installedBefore);
    // The restart did not work.
    void showProblem(const QString &title, const QString &reason);
    // For a check: every label and button of the page.
    QString pageText() const;
    ReleaseCard *card() const { return card_; }

signals:
    void install();
    void finish();
    void restartNow();
    void back();

private:
    ReleaseCard *card_ = nullptr;
};
