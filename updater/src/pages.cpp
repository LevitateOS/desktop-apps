#include "pages.h"

#include "parts.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

void MainPage::show(ReleaseCheck *check, const QString &restartFor, bool installedBefore)
{
    const ReleaseState &s = check->state();
    QVBoxLayout *column = fresh();
    column->addWidget(heading(tr("Updater")));

    // An earlier install that did not finish comes first: the first thing offered is to finish it.
    if (s.interrupted) {
        auto *v = new Verdict;
        v->setFrameShape(QFrame::NoFrame);
        v->setMaximumWidth(560);
        v->column()->addWidget(strong(tr("An earlier update did not finish")));
        v->column()->addWidget(text(tr("Release %1 was being installed when it stopped. Until it is finished, some files of this "
                                       "system are new and some are old.")
                                        .arg(s.interruptedVersion),
                                    true));
        auto *row = new QHBoxLayout;
        auto *finish = new QPushButton(tr("Finish the update"));
        finish->setDefault(true);
        connect(finish, &QPushButton::clicked, this, &MainPage::finish);
        row->addWidget(finish);
        row->addStretch();
        v->column()->addLayout(row);
        column->addWidget(v);
    }

    if (!restartFor.isEmpty()) {
        auto *panel = new Panel;
        panel->setMaximumWidth(560);
        panel->column()->addWidget(strong(tr("Restart to use release %1").arg(restartFor)));
        panel->column()->addWidget(text(tr("Release %1 is installed. Programs that are running keep their old files until they "
                                           "are restarted.")
                                            .arg(restartFor),
                                        true));
        auto *row = new QHBoxLayout;
        auto *restart = new QPushButton(tr("Restart now"));
        restart->setDefault(true);
        connect(restart, &QPushButton::clicked, this, &MainPage::restartNow);
        row->addWidget(restart);
        row->addStretch();
        panel->column()->addLayout(row);
        column->addWidget(panel);
    }

    column->addWidget(strong(tr("Installed release")));
    const ReleaseState &now = check->installed();
    const QString installed = !now.installedVersion.isEmpty() ? now.installedWords() : s.installedVersion.isEmpty() ? QString() : s.installedWords();
    auto *shown = text(installed.isEmpty() ? tr("The release this system runs could not be read.") : installed, true);
    column->addWidget(shown);

    column->addWidget(strong(tr("Updates")));
    card_ = new ReleaseCard(check);
    card_->setMaximumWidth(560);
    card_->setAction(tr("Install %1"));
    column->addWidget(card_);
    connect(card_, &ReleaseCard::actionClicked, this, &MainPage::install);

    if (!installedBefore) {
        auto *panel = new Panel;
        panel->setMaximumWidth(560);
        panel->column()->addWidget(strong(tr("This system has one root")));
        panel->column()->addWidget(text(tr("There is no second root to fall back to. The previous kernel stays as a second boot entry."), true));
        column->addWidget(panel);
    }
    column->addStretch();
}

void MainPage::showProblem(const QString &title, const QString &reason)
{
    card_ = nullptr;
    QVBoxLayout *column = fresh();
    column->addWidget(heading(title));
    column->addWidget(text(reason, true));
    auto *buttons = new QHBoxLayout;
    auto *leave = new QPushButton(tr("Back"));
    leave->setDefault(true);
    connect(leave, &QPushButton::clicked, this, &MainPage::back);
    buttons->addWidget(leave);
    buttons->addWidget(copyForAgentButton([reason] { return reason; }));
    buttons->addStretch();
    column->addLayout(buttons);
    column->addStretch();
}

QString MainPage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<MainPage *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<MainPage *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}
