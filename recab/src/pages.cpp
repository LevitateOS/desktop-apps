#include "pages.h"

#include "parts.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// One root: its letter, whether it is booted, and what is true of it.
Card *rootCard(const QString &letter, bool booted, const QStringList &lines)
{
    auto *card = new Card(booted ? Card::On : Card::Plain);
    QVBoxLayout *inside = card->column();
    inside->setSpacing(6);
    QLabel *name = heading(QObject::tr("Root %1").arg(letter));
    inside->addWidget(name);
    inside->addWidget(strong(booted ? QObject::tr("Booted — locked") : QObject::tr("Other — unlocked")));
    for (const QString &line : lines)
        inside->addWidget(text(line, true));
    inside->addStretch();
    return card;
}

QPushButton *action(const QString &words)
{
    auto *button = new QPushButton(words);
    button->setMinimumHeight(34);
    return button;
}

} // namespace

void RootsPage::show(const Status &s)
{
    QVBoxLayout *column = fresh();
    column->addWidget(heading(tr("The two roots")));

    QStringList booted, other;
    if (s.startsNext() == s.booted)
        booted << (s.nextOnce.isEmpty() ? tr("Starts at the next boot.") : tr("Starts at the next boot, once."));
    if (s.startsNext() == s.other)
        other << (s.nextOnce.isEmpty() ? tr("Starts at the next boot.") : tr("Starts at the next boot, once."));
    other << (s.otherMount.isEmpty() ? tr("Not mounted.") : tr("Mounted at %1.").arg(s.otherMount));
    if (!s.otherBootable)
        other << tr("It cannot be started: a copy into it did not finish.");

    auto *roots = new QHBoxLayout;
    roots->setSpacing(14);
    // The booted root first, whichever letter it has.
    roots->addWidget(rootCard(s.booted, true, booted), 1);
    roots->addWidget(rootCard(s.other, false, other), 1);
    column->addLayout(roots);
    if (s.startsNext().isEmpty())
        column->addWidget(dim(text(tr("Neither root is the boot default."), true)));

    column->addSpacing(6);
    copy_ = action(tr("Make the other root a copy of the booted root"));
    connect(copy_, &QPushButton::clicked, this, &RootsPage::copyOther);
    column->addWidget(copy_);

    const bool once = !s.nextOnce.isEmpty();
    start_ = action(once ? tr("Do not start the other root at the next boot") : tr("Start the other root at the next boot"));
    connect(start_, &QPushButton::clicked, this, [this, once] { emit(once ? doNotStartOther() : startOther()); });
    // Taking the pick back is always possible; picking needs a root that can be started.
    start_->setEnabled(once || s.otherBootable);
    column->addWidget(start_);
    reason_ = dim(text(tr("Root %1 cannot be started: a copy into it did not finish. Make it a copy of the booted root first.")
                           .arg(s.other)));
    reason_->setVisible(!start_->isEnabled());
    column->addWidget(reason_);

    restart_ = action(tr("Restart now"));
    connect(restart_, &QPushButton::clicked, this, &RootsPage::restartNow);
    column->addWidget(restart_);
    column->addStretch();
    // Buttons that read like one column are not stretched across the window.
    for (QPushButton *button : {copy_, start_, restart_})
        button->setMaximumWidth(460);
}

void RootsPage::showProblem(const QString &problem, bool asAdministrator)
{
    copy_ = start_ = restart_ = nullptr;
    reason_ = nullptr;
    QVBoxLayout *column = fresh();
    column->addWidget(heading(tr("The two roots could not be read")));
    column->addWidget(text(problem, true));
    auto *buttons = new QHBoxLayout;
    auto *again = new QPushButton(tr("Try again"));
    connect(again, &QPushButton::clicked, this, &RootsPage::readAgain);
    buttons->addWidget(again);
    if (asAdministrator) {
        // recab reads the boot entries, which this person may not.
        auto *root = new QPushButton(tr("Read it as administrator"));
        root->setDefault(true);
        connect(root, &QPushButton::clicked, this, &RootsPage::readAsAdministrator);
        buttons->addWidget(root);
    } else {
        again->setDefault(true);
    }
    buttons->addWidget(copyForAgentButton([problem] { return problem; }));
    buttons->addStretch();
    column->addLayout(buttons);
    column->addStretch();
}

QString RootsPage::startReason() const
{
    return reason_ && !reason_->isHidden() ? reason_->text() : QString();
}

QString RootsPage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<RootsPage *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<RootsPage *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------- Output

void OutputPage::begin(const QString &title, const QString &subtitle)
{
    finished_ = false;
    reason_.clear();
    column_ = fresh();
    column_->addWidget(heading(title));
    column_->addWidget(dim(text(subtitle, true)));
    bar_ = new QProgressBar;
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    column_->addWidget(bar_);
    state_ = dim(text(tr("Starting"), true));
    column_->addWidget(state_);
    auto *head = new QHBoxLayout;
    head->addWidget(strong(tr("Output")));
    head->addStretch();
    head->addWidget(copyForAgentButton([this] { return outputText(); }));
    column_->addLayout(head);
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    column_->addWidget(output_, 1);
}

void OutputPage::add(const QString &line)
{
    if (!output_ || finished_)
        return;
    output_->appendPlainText(line);
    state_->setText(line);
}

void OutputPage::succeeded(const QString &result)
{
    ended(tr("It is done"), result, false);
}

void OutputPage::failed(const QString &title, const QString &reason)
{
    reason_ = reason;
    // A page that was not begun (the command never started) is made now.
    if (!output_)
        begin(title, QString());
    ended(title, reason, true);
}

void OutputPage::ended(const QString &title, const QString &words, bool bad)
{
    finished_ = true;
    bar_->hide();
    state_->hide();
    QWidget *verdict = nullptr;
    if (bad) {
        auto *v = new Verdict;
        v->setFrameShape(QFrame::NoFrame);
        v->column()->addWidget(strong(title));
        v->column()->addWidget(text(words, true));
        verdict = v;
    } else {
        auto *v = new Panel;
        v->column()->addWidget(strong(title));
        v->column()->addWidget(text(words, true));
        verdict = v;
    }
    column_->insertWidget(1, verdict);
    auto *buttons = new QHBoxLayout;
    auto *leave = new QPushButton(tr("Back"));
    leave->setDefault(true);
    connect(leave, &QPushButton::clicked, this, &OutputPage::back);
    buttons->addStretch();
    buttons->addWidget(leave);
    column_->addLayout(buttons);
}

QString OutputPage::outputText() const
{
    const QString lines = output_ ? output_->toPlainText() : QString();
    return reason_.isEmpty() ? lines : tr("%1\n\n%2").arg(reason_, lines);
}

QString OutputPage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<OutputPage *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<OutputPage *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}
