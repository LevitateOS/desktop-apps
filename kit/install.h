// The page of a running release install (`recab update --json`,
// `recupdate apply --json`): the steps as they come, the progress of the
// current step, then the result. Used by the Recab app and the Updater.
//
// It reads the JSON lines (events on standard output):
//   {"event":"step","n":2,"of":5,"text":"Copying the booted root"}
//   {"event":"progress","done":123,"total":456}
//   {"event":"done","restart_needed":true,"version":"1.1","serial":4}
//   {"event":"failed","error":"<one line>"}   (and a non-zero exit)
#pragma once

#include "page.h"

#include <QJsonObject>

class QLabel;
class QProgressBar;
class QVBoxLayout;
class Run;
class StepMark;

// The words the app puts on the page; `%1` in `doneText` is the version.
struct InstallWords {
    QString title, subtitle;
    QString doneTitle, doneText;
    QString restartButton;   // "Restart now", shown when the run said a restart is needed
    QString failedTitle, failedNote; // the note says what state things are in
};

class InstallPage : public Page {
    Q_OBJECT
public:
    using Page::Page;

    // Starts the run (events on standard output) and shows it live; the result
    // replaces the steps the moment the command ends. The run belongs to the page.
    void follow(Run *run, const InstallWords &words);
    bool running() const { return run_ != nullptr; }
    bool finished() const { return finished_; }
    // What "Copy for agent" copies: the reason, the steps, then every line the command wrote.
    QString outputText() const;
    // For a check: every label and button of the page.
    QString pageText() const;

signals:
    // The command ended. `restartNeeded` and `version` as `done` said them.
    void ended(bool ok, bool restartNeeded, const QString &version);
    void restartNow();
    void back();

private:
    void onEvent(const QJsonObject &event);
    void finish(bool ok, const QString &reason, const QString &version, bool restartNeeded);
    void addStep(int n, int of, const QString &words);

    InstallWords words_;
    Run *run_ = nullptr;
    QVBoxLayout *column_ = nullptr, *steps_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *where_ = nullptr, *amount_ = nullptr;
    QList<StepMark *> marks_;
    QStringList stepWords_, lines_;
    QString reason_;
    bool finished_ = false, done_ = false, restartNeeded_ = false;
    QString version_;
};
