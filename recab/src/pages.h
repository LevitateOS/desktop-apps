// The two pages of the window: the two roots with their three actions, and
// the output of what was run.
#pragma once

#include "page.h"
#include "status.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class RootsPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // The two roots as recab said them.
    void show(const Status &status);
    // recab could not be read. `asAdministrator`: offer to read it as administrator.
    void showProblem(const QString &problem, bool asAdministrator);

    // For a check: the buttons as drawn, and the one-line reason under a disabled one.
    QPushButton *copyButton() const { return copy_; }
    QPushButton *startButton() const { return start_; }
    QPushButton *restartButton() const { return restart_; }
    QString startReason() const;
    QString pageText() const;

signals:
    void copyOther();
    void startOther();
    void doNotStartOther();
    void restartNow();
    void readAgain();
    void readAsAdministrator();

private:
    QPushButton *copy_ = nullptr, *start_ = nullptr, *restart_ = nullptr;
    QLabel *reason_ = nullptr;
};

// What a command wrote while it ran, and how it ended.
class OutputPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // Starts showing a run: `title` over it, the lines it writes as they come.
    void begin(const QString &title, const QString &subtitle);
    void add(const QString &line);
    // It ended well: `result` is what it said.
    void succeeded(const QString &result);
    // It ended badly: `reason` is what it said.
    void failed(const QString &title, const QString &reason);
    // What "Copy for agent" copies: the reason, then the whole output.
    QString outputText() const;
    bool finished() const { return finished_; }
    QString pageText() const;

signals:
    void back();

private:
    void ended(const QString &title, const QString &words, bool bad);
    QVBoxLayout *column_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *state_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QString reason_;
    bool finished_ = false;
};
