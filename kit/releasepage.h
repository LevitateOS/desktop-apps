// The page of "Check for updates" in Recipe and Recdirstat, owned by the frame
// (like Help): the answer of `recupdate`, and where a newer release is installed
// from. It installs nothing: it offers a button that starts the app that does
// (the Recab app on an A/B system, the Updater on a mutable one).
#pragma once

#include "frame.h"
#include "page.h"
#include "release.h"

class ReleasePage : public Page {
    Q_OBJECT
public:
    explicit ReleasePage(QWidget *parent = nullptr);

    // The page was opened by the person: draws it and checks.
    void open();
    // F5 and coming back to the window: reads the daily check's file again.
    void refresh();
    ReleaseCheck *check() { return &check_; }
    // For a check: every label and button of the page.
    QString pageText() const;

signals:
    void back();
    void startApp(Frame::App app);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void draw();
    ReleaseCheck check_;
    ReleaseCard *card_ = nullptr;
};
