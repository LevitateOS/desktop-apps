#include "firstrun.h"

#include "help.h"
#include "look.h"
#include "parts.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QString keyOf(Frame::App app)
{
    return app == Frame::Recipe ? QStringLiteral("recipe") : app == Frame::Recab ? QStringLiteral("system")
                                                                                 : QStringLiteral("disk");
}

bool onPage(const QWidget *widget, const QWidget *page)
{
    for (const QWidget *w = widget; w && w != page; w = w->parentWidget())
        if (w->isHidden() && w->testAttribute(Qt::WA_WState_ExplicitShowHide))
            return false;
    return true;
}

} // namespace

bool FirstRunPage::wanted(Frame::App app)
{
    if (pictureMode())
        return false;
    QSettings settings(QStringLiteral("LevitateOS"), QStringLiteral("firstrun"));
    return !settings.value(keyOf(app), false).toBool();
}

void FirstRunPage::markSeen(Frame::App app)
{
    if (pictureMode())
        return;
    QSettings settings(QStringLiteral("LevitateOS"), QStringLiteral("firstrun"));
    settings.setValue(keyOf(app), true);
    settings.sync();
}

FirstRunPage::FirstRunPage(Frame::App app, QWidget *parent)
    : Page(parent)
    , app_(app)
{
    draw();
}

void FirstRunPage::draw()
{
    const HelpText t = helpText(app_);
    QVBoxLayout *column = fresh();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *page = new QVBoxLayout(body);
    page->setContentsMargins(0, 0, 12, 0);
    page->setSpacing(12);
    scroll->setWidget(body);
    column->addWidget(scroll, 1);

    auto *wide = new QWidget;
    wide->setMaximumWidth(720);
    auto *lines = new QVBoxLayout(wide);
    lines->setContentsMargins(0, 0, 0, 0);
    lines->setSpacing(12);
    auto *at = new QHBoxLayout;
    at->addWidget(wide, 1);
    at->addStretch(1);
    page->addLayout(at);
    page->addStretch();

    lines->addWidget(heading(tr("Welcome to %1").arg(t.name)));
    lines->addWidget(text(t.purpose, true));
    lines->addWidget(text(t.never, true));

    // ---- Worth knowing first
    {
        QLabel *head = strong(tr("Worth knowing first"));
        QFont font = head->font();
        font.setPointSizeF(font.pointSizeF() * 1.2);
        head->setFont(font);
        lines->addWidget(head);
        auto *panel = new Panel;
        QVBoxLayout *inside = panel->column();
        inside->setSpacing(4);
        bool first = true;
        for (const HelpIdea &idea : t.ideas) {
            if (!idea.forFirstRun)
                continue;
            if (!first)
                inside->addWidget(rule());
            first = false;
            inside->addWidget(strong(idea.title));
            inside->addWidget(text(idea.first, true));
        }
        lines->addWidget(panel);
    }

    // ---- On: outside the scrolled part, so the one button is always in view.
    column->addWidget(rule());
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(14);
        auto *on = new QPushButton(tr("Go on to %1").arg(t.name));
        on->setDefault(true);
        connect(on, &QPushButton::clicked, this, &FirstRunPage::goOn);
        row->addWidget(on);
        ClickLabel *help = linkLabel(tr("Read Help first"));
        connect(help, &ClickLabel::clicked, this, &FirstRunPage::openHelp);
        row->addWidget(help);
        row->addStretch();
        row->addWidget(dim(text(tr("This page is shown once. Help has “Show the first page again”."))));
        column->addLayout(row);
    }
}

QString FirstRunPage::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (onPage(label, this))
            all << label->text();
    for (QAbstractButton *button : findChildren<QAbstractButton *>())
        if (onPage(button, this))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}

bool FirstRunPage::press(const QString &words)
{
    for (ClickLabel *label : findChildren<ClickLabel *>()) {
        if (label->text() == words && onPage(label, this)) {
            emit label->clicked();
            return true;
        }
    }
    for (QAbstractButton *button : findChildren<QAbstractButton *>()) {
        if (button->text() == words && button->isEnabled() && onPage(button, this)) {
            button->click();
            return true;
        }
    }
    return false;
}
