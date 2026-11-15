#pragma once

#include "page.h"

#include <QMap>

class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class Run;
class StepMark;

// Something long is being done: its steps, how far it is, its output. It reads
// the events of one Run (plan, step, progress, say). "Copy for agent" puts the
// whole output on the clipboard.
class WorkPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // `stopNote` says what stopping means; by default, that what is done is kept.
    void begin(const QString &title, const QString &subtitle, Run *run, const QString &stopNote = QString());
    // Whether the person pressed Stop on the run shown.
    bool stopped() const;
    // Whether the run ended and the page says why (it stopped).
    bool saysWhy() const { return saysWhy_; }
    // What "Copy for agent" copies: the failure's reason, then the whole output.
    QString outputText() const;
signals:
    void done(bool ok);
    void back();
private:
    void onEvent(const QJsonObject &event);
    void onFinished(bool ok);
    void showVerdict(const QString &title, const QString &words);

    Run *run_ = nullptr;
    QVBoxLayout *column_ = nullptr;
    QHBoxLayout *steps_ = nullptr;
    QMap<QString, StepMark *> marks_;
    QMap<QString, QString> titles_;
    QLabel *state_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QPushButton *stop_ = nullptr;
    QLabel *stopNote_ = nullptr;
    QString reason_; // why the run failed, once it has
    bool saysWhy_ = false;
};
