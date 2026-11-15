// The page a person sees the first time an app opens: what the app is for, the
// things worth knowing first (Help's ideas that are marked for it), and one
// button that goes on to the app. The frame owns it and shows it in place of
// the app's own page, once per person and per app; Help says how to see it
// again.
#pragma once

#include "frame.h"
#include "page.h"

class FirstRunPage : public Page {
    Q_OBJECT
public:
    explicit FirstRunPage(Frame::App app, QWidget *parent = nullptr);

    // Whether this person has not seen the page for this app yet, and noting
    // that it was shown. Both use QSettings (organisation "LevitateOS", file
    // `firstrun`, one key per app) and do nothing in a picture run, where a
    // story asks for the page itself: `wanted` is then false, `markSeen` writes
    // nothing.
    static bool wanted(Frame::App app);
    static void markSeen(Frame::App app);

    // For a check.
    QString pageText() const;
    bool press(const QString &words);

signals:
    // The one button: go on to the app.
    void goOn();
    // The link: Help.
    void openHelp();

private:
    void draw();

    Frame::App app_;
};
