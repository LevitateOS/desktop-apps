#include "install.h"

#include "parts.h"
#include "run.h"
#include "words.h"

#include <QHBoxLayout>
#include <climits>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

void InstallPage::follow(Run *run, const InstallWords &words)
{
    words_ = words;
    run_ = run;
    run_->setParent(this);
    run_->setEventsOnStdout(true);
    finished_ = done_ = restartNeeded_ = false;
    reason_.clear();
    version_.clear();
    marks_.clear();
    stepWords_.clear();
    lines_.clear();

    column_ = fresh();
    column_->addWidget(heading(words.title));
    if (!words.subtitle.isEmpty())
        column_->addWidget(dim(text(words.subtitle, true)));
    steps_ = new QVBoxLayout;
    steps_->setSpacing(6);
    column_->addLayout(steps_);
    bar_ = new QProgressBar;
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    column_->addWidget(bar_);
    amount_ = dim(text(QString(), true));
    column_->addWidget(amount_);
    where_ = nullptr;
    column_->addStretch();

    connect(run_, &Run::event, this, &InstallPage::onEvent);
    connect(run_, &Run::output, this, [this](const QString &line) { lines_ << line; });
    connect(run_, &Run::finished, this, [this](bool ok) {
        Run *run = run_;
        run_ = nullptr;
        run->deleteLater();
        // A `done` or `failed` line says how it ended; the exit code has the last word on success.
        if (ok && done_) {
            finish(true, QString(), version_, restartNeeded_);
            return;
        }
        QString reason = run->failure()["error"].toString();
        if (reason.isEmpty())
            reason = run->problem();
        if (reason.isEmpty())
            reason = ok ? tr("The command ended without saying that it was done.") : tr("The command stopped without saying why.");
        finish(false, reason, QString(), false);
    });
    run_->start();
}

void InstallPage::onEvent(const QJsonObject &e)
{
    const QString kind = e["event"].toString();
    if (kind == QLatin1String("step")) {
        addStep(e["n"].toInt(), e["of"].toInt(), e["text"].toString());
    } else if (kind == QLatin1String("progress")) {
        const qint64 done = qint64(e["done"].toDouble()), total = qint64(e["total"].toDouble());
        if (total > 0 && bar_) {
            // Bytes can pass 2 GiB: the bar counts in KiB.
            bar_->setRange(0, int(qMin<qint64>(total / 1024, INT_MAX)));
            bar_->setValue(int(qMin<qint64>(qMin(done, total) / 1024, INT_MAX)));
            if (amount_)
                amount_->setText(tr("%1 of %2").arg(bytesText(done), bytesText(total)));
        }
    } else if (kind == QLatin1String("done")) {
        done_ = true;
        restartNeeded_ = e["restart_needed"].toBool();
        version_ = e["version"].toString();
    } else if (kind == QLatin1String("failed")) {
        reason_ = e["error"].toString();
    }
}

void InstallPage::addStep(int n, int of, const QString &words)
{
    if (!steps_)
        return;
    // The step before is over.
    if (!marks_.isEmpty())
        marks_.last()->setState(StepMark::Done);
    auto *row = new QHBoxLayout;
    auto *mark = new StepMark(n);
    mark->setState(StepMark::Running);
    marks_ << mark;
    row->addWidget(mark);
    const QString line = of > 0 ? tr("Step %1 of %2: %3").arg(n).arg(of).arg(words) : words;
    stepWords_ << line;
    row->addWidget(text(line, true), 1);
    steps_->addLayout(row);
    // A new step starts with no figure for how far it is.
    if (bar_) {
        bar_->setRange(0, 0);
        if (amount_)
            amount_->clear();
    }
}

void InstallPage::finish(bool ok, const QString &reason, const QString &version, bool restartNeeded)
{
    finished_ = true;
    reason_ = ok ? QString() : (reason.isEmpty() ? reason_ : reason);
    if (!marks_.isEmpty())
        marks_.last()->setState(ok ? StepMark::Done : StepMark::Failed);
    bar_->hide();
    amount_->hide();
    QWidget *verdict = nullptr;
    if (ok) {
        auto *v = new Panel;
        v->column()->addWidget(strong(words_.doneTitle));
        v->column()->addWidget(text(words_.doneText.contains(QLatin1String("%1")) ? words_.doneText.arg(version) : words_.doneText, true));
        verdict = v;
    } else {
        auto *v = new Verdict;
        v->setFrameShape(QFrame::NoFrame);
        v->column()->addWidget(strong(words_.failedTitle));
        v->column()->addWidget(text(reason_, true));
        if (!words_.failedNote.isEmpty())
            v->column()->addWidget(text(words_.failedNote, true));
        verdict = v;
    }
    column_->insertWidget(column_->indexOf(bar_), verdict);
    auto *buttons = new QHBoxLayout;
    if (!ok)
        buttons->addWidget(copyForAgentButton([this] { return outputText(); }));
    buttons->addStretch();
    if (ok && restartNeeded && !words_.restartButton.isEmpty()) {
        auto *restart = new QPushButton(words_.restartButton);
        restart->setDefault(true);
        connect(restart, &QPushButton::clicked, this, &InstallPage::restartNow);
        buttons->addWidget(restart);
    }
    auto *leave = new QPushButton(ok && restartNeeded ? tr("Later") : tr("Back"));
    if (!(ok && restartNeeded && !words_.restartButton.isEmpty()))
        leave->setDefault(true);
    connect(leave, &QPushButton::clicked, this, &InstallPage::back);
    buttons->addWidget(leave);
    column_->addLayout(buttons);
    emit ended(ok, restartNeeded, version);
}

QString InstallPage::outputText() const
{
    QStringList parts;
    if (!reason_.isEmpty())
        parts << reason_;
    if (!stepWords_.isEmpty())
        parts << stepWords_.join(QLatin1Char('\n'));
    if (!lines_.isEmpty())
        parts << lines_.join(QLatin1Char('\n'));
    return parts.join(QStringLiteral("\n\n"));
}

QString InstallPage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<InstallPage *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<InstallPage *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}
