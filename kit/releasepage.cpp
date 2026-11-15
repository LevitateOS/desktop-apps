#include "releasepage.h"

#include "parts.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

ReleasePage::ReleasePage(QWidget *parent)
    : Page(parent)
{
    connect(&check_, &ReleaseCheck::changed, this, &ReleasePage::draw);
    draw();
}

void ReleasePage::open()
{
    draw();
    check_.check();
}

void ReleasePage::refresh()
{
    check_.readFile();
}

void ReleasePage::showEvent(QShowEvent *event)
{
    Page::showEvent(event);
    // What the daily check found shows without the network.
    check_.readFile();
    if (window())
        window()->installEventFilter(this);
}

bool ReleasePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::WindowActivate && isVisible())
        refresh();
    return Page::eventFilter(watched, event);
}

void ReleasePage::draw()
{
    const bool ab = releaseInstaller() == ReleaseInstaller::Recab;
    QVBoxLayout *column = fresh();
    column->addWidget(heading(tr("Updates to this system")));
    column->addWidget(dim(text(tr("This asks whether a newer release of the whole system exists. It installs nothing. "
                                  "The updates of single programs are on the Updates page of Recipe."),
                               true)));
    card_ = new ReleaseCard(&check_);
    card_->setMaximumWidth(560);
    column->addWidget(card_);

    const ReleaseState &s = check_.state();
    if (s.kind == ReleaseState::Available) {
        auto *panel = new Panel;
        panel->setMaximumWidth(560);
        panel->column()->addWidget(strong(tr("Where it is installed from")));
        panel->column()->addWidget(text(ab ? tr("Release %1 is installed from the Recab app. It puts the release into the other root, "
                                                "and the next start uses that root. The root you are using is not changed.")
                                                 .arg(s.version)
                                           : tr("Release %1 is installed from the Updater. This system has one root, so the Updater "
                                                "changes it in place and asks for a restart at the end.")
                                                 .arg(s.version),
                                        true));
        auto *go = new QPushButton(ab ? tr("Open Recab") : tr("Open Updater"));
        go->setDefault(true);
        connect(go, &QPushButton::clicked, this, [this, ab] { emit startApp(ab ? Frame::Recab : Frame::Updater); });
        auto *row = new QHBoxLayout;
        row->addWidget(go);
        row->addStretch();
        panel->column()->addLayout(row);
        column->addWidget(panel);
    }
    auto *foot = new QHBoxLayout;
    auto *leave = new QPushButton(tr("Back"));
    connect(leave, &QPushButton::clicked, this, &ReleasePage::back);
    foot->addWidget(leave);
    foot->addStretch();
    column->addLayout(foot);
    column->addStretch();
}

QString ReleasePage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<ReleasePage *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<ReleasePage *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}
