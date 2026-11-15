#include "help.h"

#include "parts.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

// Where each sentence below comes from is in the README of the app it is about
// ("What a person sees", and the "Not done" list); a sentence that no README or
// piece of code makes true is not here.

const int readingWidth = 720;

QString appName(Frame::App app)
{
    return app == Frame::Recipe ? QObject::tr("Recipe") : app == Frame::Recab ? QObject::tr("Recab") : QObject::tr("Recdirstat");
}

// ---- Recab

HelpText systemText()
{
    HelpText t;
    t.name = QObject::tr("Recab");
    t.purpose = QObject::tr(
        "Recab shows the two roots of this computer, A and B. The booted root is locked; the other root is unlocked, "
        "and changes are made there.");
    t.never = QObject::tr(
        "It never copies a root or restarts the computer by itself. Before a copy, it says what will be overwritten and "
        "waits for you.");
    t.ideas = {
        {QObject::tr("The booted root and the other root"),
         QObject::tr("The booted root is locked: it cannot be changed while it runs."),
         QObject::tr("The other root is unlocked and mounted for changes. Start the other root and the two swap: it "
                     "becomes the locked one."),
         true}};
    t.how = {
        {QObject::tr("Start over from the booted root"),
         QObject::tr("“Make the other root a copy of the booted root” overwrites the other root, after asking."),
         QString(), QString()},
        {QObject::tr("Start the other root"),
         QObject::tr("“Start the other root at the next boot” picks it once; “Restart now” restarts the computer."),
         QString(), QString()}};
    t.wrong = {
        {QObject::tr("A run stopped"),
         QObject::tr("The page says why, and its output ends with the reason. “Copy for agent” puts the whole output on "
                     "the clipboard, to paste into your own agent."),
         QString(), QString()}};
    t.others = {{Frame::Recipe, QObject::tr("Find, install, update and remove software.")},
                {Frame::Recdirstat, QObject::tr("See how full the disk is and find mess, and compare the two roots where "
                                          "there are two.")}};
    return t;
}

// ---- Recipe

HelpText recipeText()
{
    HelpText t;
    t.name = QObject::tr("Recipe");
    t.purpose = QObject::tr(
        "Recipe is where you find software, see what its recipe will do and what it may use, install it, update it, go "
        "back a version and remove it. Programs are fetched and built by Recipe, inside a sandbox.");
    t.never = QObject::tr(
        "It installs or updates nothing until you have read what will happen, on the page that says so, and pressed its "
        "button.");
    t.ideas = {
        {QObject::tr("Recipes"),
         QObject::tr("A recipe says how a program is fetched and built, what it needs and what it may use."),
         QObject::tr("A program's page shows what its recipe says: where it is from, what comes with it and what it may "
                     "use. The page that says what will happen lists it again before anything runs."),
         true},
        {QObject::tr("The sandbox and what a recipe may use"),
         QObject::tr("A recipe runs inside a sandbox, and may use only what it declares."),
         QObject::tr("It declares the network addresses it may reach, the most it may download, memory, disk while it "
                     "builds and time. The sandbox refuses anything else. You see the list before an install."),
         true},
        {QObject::tr("The two places software goes"),
         QObject::tr("Software goes either for you alone or for the whole computer."),
         QObject::tr("“For me” goes into your own folder, takes effect at once, is only for you and asks for no "
                     "password. “For this computer” is for everyone and asks for the administrator's password; on a "
                     "locked computer it goes into the other root and takes effect once that root is started. The page before an "
                     "install says which place, and lets you choose when both are open."),
         true}};
    t.how = {
        {QObject::tr("Find and install a program"),
         QObject::tr("Search, or pick a kind, then open a program and choose “Install”. A page says what will happen; "
                     "nothing starts until you press “Install”."),
         QStringLiteral("browse"), QObject::tr("Browse")},
        {QObject::tr("See what is installed, and remove it"),
         QObject::tr("Installed lists what is installed and in which place. “Open” starts a program from the place it is "
                     "installed in. A program can be removed; Remove asks first in its own small window. A program whose "
                     "recipe is gone shows its receipt instead, with what still works without a recipe."),
         QStringLiteral("installed"), QObject::tr("Installed")},
        {QObject::tr("Update"),
         QObject::tr("Updates lists the installed programs that a newer recipe would change, and what the newer recipe may "
                     "use that is different. “Check for updates” fetches the lists of recipes first. Each update shows the "
                     "page that says what will happen."),
         QStringLiteral("updates"), QObject::tr("Updates")},
        {QObject::tr("Go back a version, or keep a program at its version"),
         QObject::tr("Open the program from Installed. “Go back” puts an earlier version in use again; “Keep at this "
                     "version” stops updates until you press “Let it update again”. Software in a locked computer's "
                     "running system has no earlier versions."),
         QStringLiteral("installed"), QObject::tr("Installed")},
        {QObject::tr("See what takes room"),
         QObject::tr("Storage shows what the earlier versions and what is installed take, and what was done to the "
                     "stores. “Clear earlier versions…” deletes all earlier versions, after asking."),
         QStringLiteral("storage"), QObject::tr("Storage")},
        {QObject::tr("See where recipes come from"),
         QObject::tr("Sources lists them. “Fetch again” reads one again; “Add a source…” takes an address or a folder."),
         QStringLiteral("sources"), QObject::tr("Sources")}};
    t.wrong = {
        {QObject::tr("An install or update stopped"),
         QObject::tr("The page says what stopped and shows what Recipe recorded. “Try again” repeats it, and “Copy for "
                     "agent” puts the whole output on the clipboard, to paste into your own agent. Removing, going back, "
                     "keeping at a version, sources, fetching and clearing have a page of their own with Stop; if one does "
                     "not finish it shows Recipe's message and “Try again”."),
         QString(), QString()}};
    t.others = {{Frame::Recab, QObject::tr("Make the other root a copy of the booted root, start it at the next boot, and "
                                            "restart.")},
                {Frame::Recdirstat, QObject::tr("See how full the disk is, find mess and set it aside, and compare the two "
                                          "roots where there are two.")}};
    return t;
}

// ---- Recdirstat

HelpText diskText()
{
    HelpText t;
    t.name = QObject::tr("Recdirstat");
    t.purpose = QObject::tr(
        "Recdirstat shows how full the place for your files is, where the mess is hiding, and, where the computer has two roots, how alike they are. "
        "What belongs to Recipe is listed, but Recdirstat leaves it alone.");
    t.never = QObject::tr(
        "It changes nothing on your disk: it only reads and shows.");
    t.ideas = {
        {QObject::tr("Mess"),
         QObject::tr("Mess is what the rules name as leftovers."),
         QObject::tr("Caches, build leftovers, the trash, old downloads, big files nobody has touched for a year, and "
                     "places you pick yourself. The rules look only at your home folder and Recipe's own folders, which are "
                     "listed but never handed over. “Never suggest this” keeps a "
                     "place off the list; “Suggest it again” on Mess puts it back."),
         true}};
    t.how = {
        {QObject::tr("See how full the disk is"),
         QObject::tr("The disk says how much room is left, what could be freed, every partition of the disk and whose "
                     "room it is."),
         QStringLiteral("disk"), QObject::tr("The disk")},
        {QObject::tr("Find what takes the room"),
         QObject::tr("Browse shows a folder as a list and as a map. “Biggest files” and “Not used for a year” list single "
                     "files of 1 MiB or more."),
         QStringLiteral("browse"), QObject::tr("Browse")},
        {QObject::tr("Hand mess to your agent"),
         QObject::tr("Tick the places, then choose “Copy for agent”, which puts the ticked places and the facts about "
                     "them on the clipboard, to paste into your own agent."),
         QStringLiteral("mess"), QObject::tr("Mess")},
        {QObject::tr("Compare the two roots"),
         QObject::tr("On a computer with two roots, choose “Compare”. A page says first that both roots are read and that "
                     "it takes minutes."),
         QStringLiteral("systems"), QObject::tr("The two roots")}};
    t.others = {{Frame::Recipe, QObject::tr("Find, install, update and remove software.")},
                {Frame::Recab, QObject::tr("Make the other root a copy of the booted root, start it at the next boot, and "
                                            "restart.")}};
    return t;
}

// Whether a widget is on the page now: Page hides the old body while it waits to be deleted.
bool onPage(const QWidget *widget, const QWidget *page)
{
    for (const QWidget *w = widget; w && w != page; w = w->parentWidget())
        if (w->isHidden() && w->testAttribute(Qt::WA_WState_ExplicitShowHide))
            return false;
    return true;
}

// ---- Drawing

QLabel *sectionHead(const QString &words)
{
    QLabel *head = strong(words);
    QFont font = head->font();
    font.setPointSizeF(font.pointSizeF() * 1.2);
    head->setFont(font);
    return head;
}

} // namespace

HelpText helpText(Frame::App app)
{
    return app == Frame::Recipe ? recipeText() : app == Frame::Recab ? systemText() : diskText();
}

HelpPage::HelpPage(Frame::App app, QWidget *parent)
    : Page(parent)
    , app_(app)
{
    draw({});
}

void HelpPage::draw(const QStringList &targets)
{
    targets_ = targets;
    const HelpText t = helpText(app_);
    QVBoxLayout *column = fresh();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *page = new QVBoxLayout(body);
    page->setContentsMargins(0, 0, 12, 0);
    page->setSpacing(10);
    scroll->setWidget(body);
    column->addWidget(scroll, 1);

    // One column no wider than a line can be read in, at the left.
    auto *wide = new QWidget;
    wide->setMaximumWidth(readingWidth);
    auto *lines = new QVBoxLayout(wide);
    lines->setContentsMargins(0, 0, 0, 0);
    lines->setSpacing(10);
    auto *rowOfWide = new QHBoxLayout;
    rowOfWide->addWidget(wide, 1);
    rowOfWide->addStretch(1);
    page->addLayout(rowOfWide);
    page->addStretch();

    lines->addWidget(heading(tr("Help")));
    lines->addWidget(dim(text(tr("What %1 is for, the ideas behind it, and where to look when something is wrong.").arg(t.name),
                              true)));

    // A link is only drawn when its page exists.
    auto exists = [&](const QString &target) {
        return targets.contains(target);
    };
    auto link = [&](QVBoxLayout *into, const HelpRow &row) {
        if (row.target.isEmpty())
            return;
        ClickLabel *label = linkLabel(tr("Go to %1").arg(row.page));
        label->setAccessibleName(tr("Go to %1").arg(row.page));
        connect(label, &ClickLabel::clicked, this, [this, target = row.target] { emit go(target); });
        into->addWidget(label, 0, Qt::AlignLeft);
    };
    auto rows = [&](const QList<HelpRow> &list) {
        auto *panel = new Panel;
        QVBoxLayout *inside = panel->column();
        inside->setSpacing(4);
        bool first = true;
        for (const HelpRow &row : list) {
            // A thing to do on a page the app does not have is not offered at all.
            if (!row.target.isEmpty() && !exists(row.target))
                continue;
            if (!first)
                inside->addWidget(rule());
            first = false;
            inside->addWidget(strong(row.title));
            inside->addWidget(text(row.words, true));
            link(inside, row);
        }
        return panel;
    };

    // ---- What it is for
    lines->addSpacing(6);
    lines->addWidget(sectionHead(tr("What %1 is for").arg(t.name)));
    lines->addWidget(text(t.purpose, true));
    lines->addWidget(text(t.never, true));

    // ---- The ideas
    lines->addSpacing(8);
    lines->addWidget(sectionHead(tr("The ideas you need")));
    {
        auto *panel = new Panel;
        QVBoxLayout *inside = panel->column();
        inside->setSpacing(4);
        bool first = true;
        for (const HelpIdea &idea : t.ideas) {
            if (!first)
                inside->addWidget(rule());
            first = false;
            inside->addWidget(strong(idea.title));
            inside->addWidget(text(idea.first + QLatin1Char(' ') + idea.more, true));
        }
        lines->addWidget(panel);
    }

    // ---- How do I
    lines->addSpacing(8);
    lines->addWidget(sectionHead(tr("How do I…")));
    lines->addWidget(rows(t.how));

    // ---- When something is wrong
    if (!t.wrong.isEmpty()) {
        lines->addSpacing(8);
        lines->addWidget(sectionHead(tr("When something is wrong")));
        lines->addWidget(rows(t.wrong));
    }

    // ---- The other apps
    lines->addSpacing(8);
    lines->addWidget(sectionHead(tr("The other two apps")));
    {
        auto *panel = new Panel;
        QVBoxLayout *inside = panel->column();
        inside->setSpacing(8);
        for (const auto &other : t.others) {
            auto *row = new QHBoxLayout;
            row->setSpacing(12);
            auto *words = new QVBoxLayout;
            words->setSpacing(1);
            words->addWidget(strong(appName(other.first)));
            words->addWidget(text(other.second, true));
            row->addLayout(words, 1);
            auto *open = new QPushButton(tr("Open %1").arg(appName(other.first)));
            connect(open, &QPushButton::clicked, this, [this, app = other.first] { emit startApp(app); });
            row->addWidget(open, 0, Qt::AlignVCenter);
            inside->addLayout(row);
        }
        lines->addWidget(panel);
    }

    // ---- The first page
    lines->addSpacing(8);
    {
        auto *row = new QHBoxLayout;
        auto *again = new QPushButton(tr("Show the first page again"));
        connect(again, &QPushButton::clicked, this, &HelpPage::firstPageAgain);
        row->addWidget(again);
        row->addWidget(dim(text(tr("The page you saw when you first opened %1.").arg(t.name))), 1);
        lines->addLayout(row);
    }
}

QString HelpPage::pageText() const
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

QStringList HelpPage::links() const
{
    QStringList words;
    for (ClickLabel *label : findChildren<ClickLabel *>())
        if (onPage(label, this))
            words << label->text();
    return words;
}

bool HelpPage::scrollDown()
{
    for (QScrollArea *area : findChildren<QScrollArea *>()) {
        if (!onPage(area, this))
            continue;
        QScrollBar *bar = area->verticalScrollBar();
        if (bar->value() >= bar->maximum())
            return false;
        bar->setValue(qMin(bar->maximum(), bar->value() + area->viewport()->height() - 40));
        return true;
    }
    return false;
}

bool HelpPage::press(const QString &words)
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
