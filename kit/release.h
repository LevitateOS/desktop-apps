// "Check for updates": what the system release is and whether a newer one
// exists, as `recupdate` says it. One shared piece for the four apps.
//
//  - `ReleaseCheck` holds the answer. At start it reads
//    /var/lib/recupdate/available.json (what the daily check wrote), so a
//    release the daily check found shows without any network; `check()` runs
//    `recupdate check --json` (no privileges) and replaces it.
//  - `ReleaseCard` shows the answer: checking, up to date, a newer release
//    (version, date, download size, link to the notes), or the failure with
//    "Copy for agent". The app that can install adds one button to it.
//
// Nothing here installs anything. Nothing here is read on the window's thread
// but the answer to a finished command: the file is read in a thread of its own.
#pragma once

#include "parts.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>

class QPushButton;
class Run;

struct ReleaseState {
    enum Kind { NotChecked, Checking, UpToDate, Available, Failed };
    Kind kind = NotChecked;
    QString os;                 // "levitateos"
    QString installedVersion;   // "1.0"
    qint64 installedSerial = 0;
    // Set when a newer release exists.
    QString version, date, notes;
    qint64 serial = 0, downloadBytes = 0;
    QDateTime checkedAt;        // invalid when the answer did not say
    bool fromFile = false;      // the daily check's answer, not one asked for now
    // An earlier `recupdate apply` on this mutable system did not finish
    // (`"interrupted"` in the answer): running it again finishes it.
    bool interrupted = false;
    QString interruptedVersion;
    qint64 interruptedSerial = 0;
    QString error;              // Failed: what the check said

    // "LevitateOS 1.0"; "release 1.0" when the system is not named.
    QString installedWords() const;
};

class ReleaseCheck : public QObject {
    Q_OBJECT
public:
    explicit ReleaseCheck(QObject *parent = nullptr);

    const ReleaseState &state() const { return state_; }
    bool checking() const { return run_ != nullptr; }
    // What the daily check's file said when it was last read.
    const ReleaseState &cached() const { return cached_; }

    // Runs `recupdate check --json` (RECUPDATE_BIN names the program). Ignored while one runs.
    void check();
    // Reads what the daily check wrote (RECUPDATE_STATE_DIR/available.json) and
    // the release this system runs (RECUPDATE_RELEASE_FILE, else
    // /usr/lib/levitate/release.json), in a thread of its own, and uses them
    // unless something newer is already known. Called at start, on F5 and when
    // the window comes back. `fileRead()` follows every time, whatever was found.
    void readFile();
    // The release this system runs, as the release file says it (empty version: unknown).
    const ReleaseState &installed() const { return installed_; }
    // What "Copy for agent" copies for a failed check.
    QString forAgent() const;

    // The answer of `recupdate check --json`; an empty `error` means it was understood.
    static ReleaseState parse(const QByteArray &json, QString *error);
    static QString program();
    static QString filePath();
    static QString releaseFilePath();

signals:
    void changed();
    // The files were read (and used, when they said anything).
    void fileRead();

private:
    void useFiles(const QByteArray &available, const QByteArray &release);
    ReleaseState state_, cached_, installed_;
    Run *run_ = nullptr;
};

// The program that installs a release on this system: the Recab app on an A/B
// system (`/usr/bin/recab` exists), the Updater on a mutable one.
// LEVITATE_AB overrides the look: "1" is an A/B system, "0" a mutable one.
enum class ReleaseInstaller { Recab, Updater };
ReleaseInstaller releaseInstaller();

class ReleaseCard : public Card {
    Q_OBJECT
public:
    explicit ReleaseCard(ReleaseCheck *check, QWidget *parent = nullptr);

    // A button shown while a newer release is there (the app that installs puts
    // "Install 1.1 into the other root" on it); `words` containing %1 get the
    // version. Empty `words` is no button.
    void setAction(const QString &words);
    // A plain line under the release, shown with it ("Installed from the Recab app.").
    void setNote(const QString &words);
    // A newer release that is already installed somewhere that is not running yet
    // (into the other root): while the answer is that release, `note` is said in
    // place of the action button.
    void setInstalledNotRunning(const QString &version, const QString &note);
    // While something else is going (an install), the buttons are off.
    void setBusy(bool busy);
    QPushButton *checkButton() const { return check_; }
    QPushButton *actionButton() const { return action_; }
    // For a check: every label and button of the card.
    QString pageText() const;

signals:
    void actionClicked();

private:
    void draw();
    ReleaseCheck *model_;
    QWidget *body_ = nullptr;
    QPushButton *check_ = nullptr, *action_ = nullptr;
    QString actionWords_, note_, doneVersion_, doneNote_;
    bool busy_ = false;
};
