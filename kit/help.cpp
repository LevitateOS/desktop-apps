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
        "Recab is where you change and update this computer's locked system. The running system cannot be "
        "changed while it runs, so a change is made in a second system beside it, and takes effect when you "
        "restart into it.");
    t.never = QObject::tr(
        "It never starts a change or a restart by itself. Before one, it says what will happen and waits for you.");
    // The 15 minutes are recab's own wait for start-up (`recab trial`, its default timeout), as Overview says.
    t.ideas = {
        {QObject::tr("The running system and the next system"),
         QObject::tr("The running system is locked: it cannot be changed while it runs."),
         QObject::tr("A change goes into the next system, a copy made beside it when you make a change. It takes "
                     "effect only after you restart into it. Your files are the same in both: home is shared by both "
                     "systems."),
         true},
        {QObject::tr("Finishing"),
         QObject::tr("Finishing checks and closes the next system, so that it can be started."),
         QObject::tr("Until it is finished, a restart cannot start it. On the other pages, the strip under them has "
                     "“Finish and restart…”, which goes to “The next system”."),
         false},
        {QObject::tr("The restart on trial"),
         QObject::tr("After you restart into the next system it is on trial: it is not kept yet."),
         QObject::tr("recab keeps it by itself once start-up has finished with nothing failed. It waits up to 15 "
                     "minutes for that. If start-up fails, the computer goes back to the system from before by "
                     "itself. You need not wait: Overview has “Keep it now” and “Go back now…”."),
         true},
        {QObject::tr("Coming back"),
         QObject::tr("The system from before stays as your way back."),
         QObject::tr("After a new system is kept, the system from before stays beside it until you make the next "
                     "change, which uses its place. Going back restarts into it; “Getting back” has the button."),
         true}};
    t.how = {
        {QObject::tr("Install software"),
         QObject::tr("Software is installed with Recipe. “Install software…” starts Recipe, where what you install for "
                     "this computer goes into the next system."),
         QStringLiteral("next"), QObject::tr("The next system")},
        {QObject::tr("Finish the next system and restart into it"),
         QObject::tr("Choose “Finish…”, then “Restart now” or “Start it at the next restart”. Each shows what will "
                     "happen and waits for you."),
         QStringLiteral("next"), QObject::tr("The next system")},
        {QObject::tr("Keep the new system, or go back"),
         QObject::tr("Getting back has three rows: go back to the system from before, keep the running system, and "
                     "cancel a restart that is set."),
         QStringLiteral("back"), QObject::tr("Getting back")},
        {QObject::tr("Take a newer release"),
         QObject::tr("When recab knows of a newer release, Overview says so, and “See it…” opens the page that follows "
                     "it from the check to the next system. “Check for a new release” asks recab. Whether one is found "
                     "depends on recab being set up with a place to get releases from. When recab will not merge a "
                     "release by itself, the page offers what recab offers: the original release to adopt, a package to "
                     "reproduce, or a failed system to replace. Each says what will happen first."),
         QStringLiteral("overview"), QObject::tr("Overview")},
        {QObject::tr("See what has happened"),
         QObject::tr("History lists what recab recorded: when, what, who and how it ended. “Save as a file…” keeps it "
                     "as text."),
         QStringLiteral("history"), QObject::tr("History")}};
    t.wrong = {
        {QObject::tr("A run stopped"),
         QObject::tr("The page says why, and its output ends with the reason. “Copy for agent” puts the whole output on "
                     "the clipboard, to paste into your own agent."),
         QString(), QString()},
        {QObject::tr("The desktop does not start"),
         QObject::tr("Getting back has the steps: which system to pick in the list at start-up, and how to make that "
                     "choice stay. “Save as a file…” keeps them as text. A next system that fails to start properly is "
                     "left by itself: the computer restarts into the system it came from."),
         QStringLiteral("back"), QObject::tr("Getting back")},
        {QObject::tr("recab says a system needs recovery"),
         QObject::tr("Overview gives recab's own reason and lists the steps recab offers, each with its own verdict. "
                     "While recab says a system needs recovery, nothing else is offered. A restart or a command that "
                     "failed says which step had been made before it."),
         QStringLiteral("overview"), QObject::tr("Overview")}};
    t.others = {{Frame::Recipe, QObject::tr("Find, install, update and remove software.")},
                {Frame::Recdirstat, QObject::tr("See how full the disk is, find mess, and compare the two "
                                          "systems.")}};
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
                     "locked computer it goes into the next system and takes effect after a restart. The page before an "
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
    t.others = {{Frame::Recab, QObject::tr("Change and update the locked system, finish and restart into it, and go "
                                            "back.")},
                {Frame::Recdirstat, QObject::tr("See how full the disk is, find mess and set it aside, and compare the two "
                                          "systems.")}};
    return t;
}

// ---- Recdirstat

HelpText diskText()
{
    HelpText t;
    t.name = QObject::tr("Recdirstat");
    t.purpose = QObject::tr(
        "Recdirstat shows how full the place for your files is, where the mess is hiding, and how alike the two systems are. "
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
         true},
        {QObject::tr("The two systems"),
         QObject::tr("A locked computer keeps two systems: the running one and the other beside it."),
         QObject::tr("“The two systems” reads both from start to end, which takes minutes, and changes neither. "
                     "If the other system can only be read by the administrator, it asks for the administrator's "
                     "password first. It says how alike they are and which folders "
                     "differ. Changing, updating and going back are done in "
                     "Recab."),
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
        {QObject::tr("Compare the two systems"),
         QObject::tr("Choose “Compare”. A page says first that both systems are read and that it takes minutes."),
         QStringLiteral("systems"), QObject::tr("The two systems")}};
    t.others = {{Frame::Recipe, QObject::tr("Find, install, update and remove software.")},
                {Frame::Recab, QObject::tr("Change and update the locked system, finish and restart into it, and go "
                                            "back.")}};
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
