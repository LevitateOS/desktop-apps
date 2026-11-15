#include "shots.h"

#include "frame.h"
#include "help.h"
#include "helpstory.h"
#include "look.h"
#include "window.h"
#include "words.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include <QTextStream>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include "browse.h"
#include "disk.h"
#include "pages.h"
#include "rules.h"
#include "systems.h"

#include <functional>
#include <limits>

#include <sys/statvfs.h>

static void waitUntilIdle(Window &window, int seconds)
{
    QElapsedTimer clock;
    clock.start();
    settle(100);
    while (window.busy() && clock.elapsed() < seconds * 1000)
        settle(50);
    settle(200);
}

namespace {

using Save = std::function<void(const QString &)>;
using Expect = std::function<void(const QString &, bool)>;

QPushButton *buttonNamed(QWidget *inside, const QString &words)
{
    for (QPushButton *button : inside->findChildren<QPushButton *>())
        if (button->text() == words && button->isVisible())
            return button;
    return nullptr;
}

// "Open Recab…" starts the Recab app (the stand-in named by LEVITATE_APP_SYSTEM makes a mark).
void openRecabShots(Window &window, const Expect &expect)
{
    const QString mark = qEnvironmentVariable("DISK_STANDIN_SYSTEM_MARK");
    if (mark.isEmpty() || qEnvironmentVariable("LEVITATE_APP_SYSTEM").isEmpty()) {
        expect(QStringLiteral("the Recab app could be started: LEVITATE_APP_SYSTEM and DISK_STANDIN_SYSTEM_MARK, see README.md"), false);
        return;
    }
    window.frame()->choose(QStringLiteral("systems"));
    settle(100);
    QFile::remove(mark);
    if (QPushButton *open = buttonNamed(&window, QStringLiteral("Open Recab…")))
        open->click();
    waitFor([&] { return QFileInfo::exists(mark); }, 10);
    expect(QStringLiteral("\"Open Recab…\" starts the Recab app"), QFileInfo::exists(mark));
    QFile::remove(mark);
}

// The pages the frame owns: Help for Recdirstat, and the page a person
// sees the first time. The checks are `../kit/helpstory.*`; this says which page
// of Recdirstat each target is.
void helpShots(Window &window, const Save &save, const Expect &expect)
{
    helpstory::Plan plan;
    plan.app = Frame::Recdirstat;
    plan.window = &window;
    plan.frame = window.frame();
    plan.firstEntry = QStringLiteral("disk");
    plan.idle = [&] { waitUntilIdle(window, 300); };
    plan.pageShown = [&](const QString &key) {
        QWidget *page = key == QLatin1String("disk")      ? static_cast<QWidget *>(window.diskPage())
                        : key == QLatin1String("browse")  ? static_cast<QWidget *>(window.browsePage())
                        : key == QLatin1String("mess")    ? static_cast<QWidget *>(window.messPage())
                        : key == QLatin1String("systems") ? static_cast<QWidget *>(window.systemsPage())
                                                          : nullptr;
        return page && page->isVisible();
    };
    helpstory::run(plan, [&](QWidget &, const QString &name) { save(name); }, expect, "h");
    window.frame()->choose(QStringLiteral("disk"));
    waitUntilIdle(window, 300);
}

} // namespace

namespace {

bool near(qint64 a, qint64 b)
{
    // The disk is shared with other programs while the check runs, so what was
    // measured a moment ago may differ from what statvfs says now.
    return qAbs(a - b) <= (256ll << 20);
}

// Everything a person can read on a page: its labels, buttons and list rows.
QString pageText(QWidget *page)
{
    QStringList words;
    for (QLabel *label : page->findChildren<QLabel *>())
        if (label->isVisible())
            words << label->text();
    for (QAbstractButton *button : page->findChildren<QAbstractButton *>())
        if (button->isVisible())
            words << button->text();
    for (QTabBar *tabs : page->findChildren<QTabBar *>())
        for (int tab = 0; tab < tabs->count(); ++tab)
            words << tabs->tabText(tab);
    for (QTreeWidget *tree : page->findChildren<QTreeWidget *>()) {
        for (int column = 0; column < tree->columnCount(); ++column)
            words << tree->headerItem()->text(column);
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            for (int column = 0; column < tree->columnCount(); ++column)
                words << (*it)->text(column);
    }
    return words.join(QLatin1Char('\n'));
}

qint64 statFree(const QString &path)
{
    struct statvfs st;
    return ::statvfs(path.toLocal8Bit().constData(), &st) == 0 ? qint64(st.f_bavail) * qint64(st.f_frsize) : -1;
}

qint64 statTotal(const QString &path)
{
    struct statvfs st;
    return ::statvfs(path.toLocal8Bit().constData(), &st) == 0 ? qint64(st.f_blocks) * qint64(st.f_frsize) : -1;
}

qint64 sumOf(const QList<Finding> &found)
{
    qint64 total = 0;
    for (const Finding &finding : found)
        total += finding.bytes;
    return total;
}

bool listsPath(const Window &window, const QString &path)
{
    for (const Finding &finding : window.found())
        if (finding.path == path)
            return true;
    return false;
}

Finding findingNamed(const Window &window, const QString &tail)
{
    for (const Finding &finding : window.found())
        if (finding.path.endsWith(tail))
            return finding;
    return {};
}

Node *nodeAt(Node *top, const QString &path)
{
    if (top->path() == path)
        return top;
    for (const auto &child : top->children)
        if (Node *found = nodeAt(child.get(), path))
            return found;
    return nullptr;
}

// The first page: the disk, measured. Leftovers, Recipe's share, the
// partitions from the lsblk stand-in, whose room it is.
void diskShots(Window &window, const Save &save, const Expect &expect, QTextStream &out)
{
    Frame *frame = window.frame();
    DiskPage *page = window.diskPage();
    const QString home = window.home();
    expect(QStringLiteral("the window opens on The disk"), frame->selected() == "disk" && window.showingDisk());
    const QString words = pageText(page);
    // The page's figures are what it measured when it drew; statvfs now may differ a little (the disk is shared).
    const DiskInfo &drawn = page->shown();
    const qint64 free = drawn.home.free, total = drawn.home.total;
    const bool nearly = free < (5ll << 30) || free * 10 < total;
    out << "the disk: " << bytesText(free) << " free of " << bytesText(total) << (nearly ? " (nearly full)" : " (has room)") << "\n";
    expect(QStringLiteral("the figures are statvfs's (free %1 and %2, total %3 and %4)").arg(free).arg(statFree(home)).arg(total).arg(statTotal(home)),
           drawn.home.known && near(free, statFree(home)) && total == statTotal(home));
    expect(QStringLiteral("the heading is true of the measure (%1)").arg(page->headingText()),
           page->headingText() == (nearly ? QStringLiteral("The place for your files is nearly full")
                                          : QStringLiteral("The place for your files has room")));
    expect(QStringLiteral("it says how much is left of how much (%1 of %2)").arg(bytesText(free), bytesText(total)),
           words.contains(QStringLiteral("%1 is left of %2.").arg(bytesText(free), bytesText(total))));
    expect(QStringLiteral("the window's own measure is statvfs's"),
           window.space().known && near(window.space().free, statFree(home)) && window.space().total == statTotal(home));

    // The leftovers row is the Mess list: the same figure in the same number of places.
    const qint64 mess = sumOf(window.found());
    expect(QStringLiteral("the Known leftovers row equals the Mess list (%1 in %2 places)").arg(bytesText(mess)).arg(window.found().size()),
           words.contains(QStringLiteral("Known leftovers")) && words.contains(bytesText(mess))
               && words.contains(counted(window.found().size(), QStringLiteral("in %1 place"), QStringLiteral("in %1 places"))));
    // Recipe's earlier versions, asked of the stand-in `recipe storage --json`.
    const qint64 earlier = qEnvironmentVariable("DISK_STANDIN_EARLIER", QString::number(6ll << 30)).toLongLong();
    expect(QStringLiteral("Kept by Recipe says what Recipe says (%1)").arg(bytesText(earlier)),
           earlier > 0 && words.contains(QStringLiteral("Kept by Recipe")) && words.contains(bytesText(earlier)));
    const QString mark = qEnvironmentVariable("DISK_STANDIN_RECIPE_MARK");
    if (mark.isEmpty() || qEnvironmentVariable("LEVITATE_APP_RECIPE").isEmpty()) {
        expect(QStringLiteral("the Recipe app could be started: LEVITATE_APP_RECIPE and DISK_STANDIN_RECIPE_MARK, see README.md"), false);
    } else {
        QFile::remove(mark);
        if (QPushButton *open = buttonNamed(page, QStringLiteral("Open Recipe…")))
            open->click();
        waitFor([&] { return QFileInfo::exists(mark); }, 10);
        expect(QStringLiteral("\"Open Recipe…\" starts the Recipe app"), QFileInfo::exists(mark));
        QFile::remove(mark);
    }
    expect(QStringLiteral("there is no row for what cannot be measured (logs and crash records)"),
           !words.contains(QStringLiteral("Logs and crash records")));

    // Every partition of the disk the home folder is on, from lsblk's answer.
    const DiskLayout &layout = window.layout();
    expect(QStringLiteral("the disk was found in lsblk's answer (%1: %2)").arg(layout.disk, layout.problem),
           layout.found && layout.disk == QStringLiteral("/dev/vda") && layout.parts.size() == 6);
    expect(QStringLiteral("the other disk's partition is not listed"), !words.contains(QStringLiteral("backup")));
    for (const QString &phrase :
         {QStringLiteral("Your files and what programs keep"), QStringLiteral("Start-up files"),
          QStringLiteral("Partition “scratch”"), QStringLiteral("Swap space")})
        expect(QStringLiteral("a partition row says \"%1\"").arg(phrase), words.contains(phrase));
    expect(QStringLiteral("sizes are lsblk's (20 GiB, 512 MiB)"), words.contains(QStringLiteral("20 GiB")) && words.contains(QStringLiteral("512 MiB")));
    // The home folder's partition is measured where it is mounted.
    const Partition *homePart = nullptr;
    for (const Partition &part : layout.parts)
        if (part.path == QStringLiteral("/dev/vda4"))
            homePart = &part;
    expect(QStringLiteral("the home partition's used and free are statvfs's (%1 free)").arg(bytesText(homePart ? homePart->free : 0)),
           homePart && near(homePart->free, statFree(home)) && homePart->used == homePart->total - homePart->free
               && homePart->total == total && words.contains(bytesText(homePart->free)));
    // The other system sits on a partition recab names; it is not mounted, so its use is not read.
    const Partition *other = nullptr;
    for (const Partition &part : layout.parts)
        if (part.path == QStringLiteral("/dev/vda3"))
            other = &part;
    expect(QStringLiteral("the other system's use is not read (not mounted)"),
           other && other->used < 0 && words.contains(QStringLiteral("not mounted, so not read")));

    // Whose room it is: the kinds the scan tells apart add up to what it read.
    qint64 sliced = 0;
    for (const Slice &slice : composition(window.tree().get(), window.found(), home))
        sliced += slice.bytes;
    expect(QStringLiteral("whose room it is adds up to what the scan read (%1 and %2)").arg(bytesText(sliced), bytesText(window.scannedBytes())),
           sliced == window.scannedBytes() && words.contains(QStringLiteral("Whose room it is"))
               && words.contains(QStringLiteral("Your files,")));
    save("1-disk");
}

// Browse: going in and up, search, the two extra views, and what can be done with a place.
void browseShots(Window &window, const QString &folder, const Save &save, const Expect &expect, QTextStream &out)
{
    Frame *frame = window.frame();
    BrowsePage *page = window.browsePage();
    frame->choose(QStringLiteral("browse"));
    settle(200);
    expect(QStringLiteral("Browse is marked and shows"), frame->selected() == "browse" && window.showingBrowse());
    const QString home = window.home();
    expect(QStringLiteral("Browse starts in the home folder (%1)").arg(page->folder() ? page->folder()->path() : QString()),
           page->folder() && page->folder()->path() == home && page->view() == BrowsePage::View::Folders);
    const QStringList top = page->listed();
    out << "browse, in the home folder: " << top.join(", ") << "\n";
    for (const QString &name : {QStringLiteral("Videos"), QStringLiteral("Projects"), QStringLiteral(".cache"), QStringLiteral("Downloads")})
        expect(QStringLiteral("the home folder lists %1").arg(name), top.contains(name));
    save("2a-browse");

    // Going in, and up.
    Node *projects = nodeAt(page->folder(), home + QStringLiteral("/Projects"));
    page->pick(projects);
    expect(QStringLiteral("choosing Projects points at it and allows going in"),
           page->pointed() == projects && buttonNamed(page, QStringLiteral("Go in")) && buttonNamed(page, QStringLiteral("Go in"))->isEnabled());
    buttonNamed(page, QStringLiteral("Go in"))->click();
    settle(200);
    expect(QStringLiteral("Go in shows Projects (%1)").arg(page->folder()->path()), page->folder() == projects);
    expect(QStringLiteral("its folders are listed"), page->listed().contains(QStringLiteral("shop")) && page->listed().contains(QStringLiteral("site"))
                                                        && page->listed().contains(QStringLiteral("viewer")));
    expect(QStringLiteral("the path can be clicked: Projects is named"), pageText(page).contains(QStringLiteral("Projects")));
    save("2b-browse-in");
    // Search filters this folder only.
    page->setSearch(QStringLiteral("sh"));
    settle(100);
    expect(QStringLiteral("search keeps what matches (%1)").arg(page->listed().join(", ")), page->listed() == QStringList{QStringLiteral("shop")});
    page->setSearch(QStringLiteral("nothing-is-called-this"));
    settle(100);
    expect(QStringLiteral("search says when nothing matches"), page->listed().isEmpty() && pageText(page).contains(QStringLiteral("No folder here has")));
    page->setSearch(QString());
    settle(100);
    expect(QStringLiteral("clearing the search lists the folders again"), page->listed().size() >= 3);
    buttonNamed(page, QStringLiteral("Up"))->click();
    settle(100);
    expect(QStringLiteral("Up goes back to the home folder"), page->folder() && page->folder()->path() == home);

    // The two other views, over the home folder.
    page->setView(BrowsePage::View::Biggest);
    settle(100);
    const QStringList biggest = page->listed();
    expect(QStringLiteral("Biggest files starts with the biggest file (%1)").arg(biggest.value(0)),
           biggest.value(0) == QStringLiteral("Videos/wedding.mkv") && biggest.contains(QStringLiteral("Music/album.flac")));
    qint64 last = std::numeric_limits<qint64>::max();
    bool sorted = true;
    for (const QString &name : biggest) {
        Node *node = nodeAt(page->folder(), home + QLatin1Char('/') + name);
        sorted = sorted && node && node->bytes <= last;
        last = node ? node->bytes : 0;
    }
    expect(QStringLiteral("and they run from big to small"), sorted && biggest.size() > 5);
    save("2c-browse-biggest");
    page->setView(BrowsePage::View::Unused);
    settle(100);
    expect(QStringLiteral("Not used for a year lists only what is that old (%1)").arg(page->listed().join(", ")),
           page->listed() == QStringList{QStringLiteral("Videos/screen-recording-2024.mkv")});
    save("2d-browse-unused");
    page->setView(BrowsePage::View::Folders);

    // What can be done with a place. Copy, and Open in Files through a stand-in.
    Node *shop = nodeAt(page->folder(), home + QStringLiteral("/Projects/shop"));
    page->goTo(projects);
    page->pick(shop);
    buttonNamed(page, QStringLiteral("Copy the path"))->click();
    expect(QStringLiteral("Copy the path puts the path on the clipboard"), QApplication::clipboard()->text() == shop->path());
    const QString filesMark = qEnvironmentVariable("DISK_STANDIN_FILES_MARK");
    if (filesMark.isEmpty() || qEnvironmentVariable("DISK_GUI_FILES").isEmpty()) {
        expect(QStringLiteral("Open in Files could be checked: DISK_GUI_FILES and DISK_STANDIN_FILES_MARK, see README.md"), false);
    } else {
        QFile::remove(filesMark);
        buttonNamed(page, QStringLiteral("Open in Files"))->click();
        waitFor([&] { return QFileInfo::exists(filesMark); }, 10);
        QFile mark(filesMark);
        const QString asked = mark.open(QIODevice::ReadOnly) ? QString::fromUtf8(mark.readAll()) : QString();
        expect(QStringLiteral("Open in Files hands the desktop's file manager the folder (%1)").arg(asked), asked == shop->path());
        QFile::remove(filesMark);
    }
    Node *target = nodeAt(page->folder(), home + QStringLiteral("/Projects/shop/target"));
    page->goTo(shop);
    page->pick(target);
    expect(QStringLiteral("a place on the list says what it is and offers to take it off or never suggest it"),
           pageText(page).contains(QStringLiteral("Made by the Rust compiler")) && buttonNamed(page, QStringLiteral("Never suggest this"))
               && buttonNamed(page, QStringLiteral("Take off the list")) && !buttonNamed(page, QStringLiteral("Add to the list")));
    save("2e-browse-place");
    page->pick(nodeAt(page->folder(), home + QStringLiteral("/Projects/shop/src")));
    expect(QStringLiteral("a place that is not on it offers to add it, and not the others"),
           buttonNamed(page, QStringLiteral("Add to the list")) && !buttonNamed(page, QStringLiteral("Never suggest this")));

    // Add to the list, then take it off again.
    const int before = int(window.found().size());
    buttonNamed(page, QStringLiteral("Add to the list"))->click();
    settle(100);
    expect(QStringLiteral("Add to the list puts it on the list and moves the badge (%1 and %2)").arg(before + 1).arg(frame->badge("mess")),
           window.found().size() == before + 1 && frame->badge("mess") == before + 1 && buttonNamed(page, QStringLiteral("Take off the list")));
    buttonNamed(page, QStringLiteral("Take off the list"))->click();
    settle(100);
    expect(QStringLiteral("Take off the list takes it off again"), window.found().size() == before && frame->badge("mess") == before);

    // Never suggest: the place leaves the list, and stays off it at the next look.
    const QString never = home + QStringLiteral("/.cache/mozilla");
    expect(QStringLiteral("the Firefox cache is on the list to begin with"), listsPath(window, never));
    page->goTo(nodeAt(window.tree().get(), home + QStringLiteral("/.cache")));
    page->pick(nodeAt(window.tree().get(), never));
    buttonNamed(page, QStringLiteral("Never suggest this"))->click();
    settle(200);
    expect(QStringLiteral("Never suggest this takes it off the list at once"), !listsPath(window, never) && window.found().size() == before - 1
                                                                                && frame->badge("mess") == before - 1);
    expect(QStringLiteral("and keeps it in the person's config folder (%1)").arg(neverFile()),
           window.never().contains(never) && readNever().contains(never) && QFileInfo::exists(neverFile()));
    save("2f-browse-never");
    window.lookAgain();
    waitUntilIdle(window, 300);
    expect(QStringLiteral("a second look does not suggest it again"), !listsPath(window, never) && window.found().size() == before - 1
                                                                       && frame->badge("mess") == before - 1);
    frame->choose(QStringLiteral("mess"));
    settle(150);
    expect(QStringLiteral("Mess says what you told it"), pageText(window.messPage()).contains(QStringLiteral("Never suggest: 1 place"))
                                                           && pageText(window.messPage()).contains(QStringLiteral("Suggest it again")));
    save("3c-mess-told");
    frame->choose(QStringLiteral("browse"));
    // It is put back at the end of the story: the config folder is the person's.
    window.suggestAgain(never);
    waitUntilIdle(window, 300);
    expect(QStringLiteral("Suggest it again puts it back, and the config file no longer names it"),
           listsPath(window, never) && !readNever().contains(never) && window.found().size() == before);
    Q_UNUSED(folder)
}

// Mess: the list in the mock-up's words, the ticks, the panel for a place.
void messShots(Window &window, const Save &save, const Expect &expect)
{
    Frame *frame = window.frame();
    MessPage *page = window.messPage();
    frame->choose(QStringLiteral("mess"));
    settle(200);
    expect(QStringLiteral("Mess is marked and shows"), frame->selected() == "mess" && window.showingMess());
    const QString words = pageText(page);
    expect(QStringLiteral("the heading counts the places and their size"),
           words.contains(QStringLiteral("%1 look like mess, %2")
                              .arg(counted(window.found().size(), QStringLiteral("%1 place"), QStringLiteral("%1 places")),
                                   bytesText(sumOf(window.found())))));
    expect(QStringLiteral("it says what each kind is, and the columns are the mock-up's"),
           words.contains(QStringLiteral("made again by the next build")) && words.contains(QStringLiteral("What and where"))
               && words.contains(QStringLiteral("Last used")) && words.contains(QStringLiteral("Verdict")));
    expect(QStringLiteral("it no longer says this window removes nothing"), !words.contains(QStringLiteral("removes nothing itself")));
    const Finding shop = findingNamed(window, QStringLiteral("/Projects/shop/target"));
    page->pick(shop.node);
    const QString panel = pageText(page);
    expect(QStringLiteral("choosing a place says what it is and every action is there"),
           panel.contains(QStringLiteral("Made by the Rust compiler")) && buttonNamed(page, QStringLiteral("Show in Browse"))
               && buttonNamed(page, QStringLiteral("Open in Files")) && buttonNamed(page, QStringLiteral("Take off the list"))
               && buttonNamed(page, QStringLiteral("Never suggest this")) && buttonNamed(page, QStringLiteral("Copy for agent")));
    expect(QStringLiteral("the foot counts what is ticked and what is safe to remove"),
           panel.contains(QStringLiteral("ticked: ")) && panel.contains(QStringLiteral("is safe to remove")));
    save("3a-mess");
    // "Show in Browse" goes to the place's folder, with the place pointed at.
    buttonNamed(page, QStringLiteral("Show in Browse"))->click();
    settle(150);
    expect(QStringLiteral("Show in Browse opens the folder and points at the place"),
           window.showingBrowse() && window.browsePage()->pointed() == shop.node && window.browsePage()->folder() == shop.node->parent);
    frame->choose(QStringLiteral("mess"));
    // Ticking only what is safe to remove leaves nothing to check first.
    page->tickOnlySure();
    bool allSure = !page->chosen().isEmpty();
    for (const Finding &finding : page->chosen())
        allSure = allSure && finding.sure && finding.kind != kindPicked;
    expect(QStringLiteral("\"Tick only what is safe to remove\" leaves only places that are"), allSure);
    window.lookAgain(); // a second look ticks everything again
    waitUntilIdle(window, 300);
    int leftAlone = 0;
    for (const Finding &finding : window.found())
        leftAlone += finding.kind == kindRecipe;
    expect(QStringLiteral("a second look ticks everything except Recipe's own folders, which are never ticked (%1 of %2)")
               .arg(leftAlone).arg(window.found().size()),
           leftAlone > 0 && page->chosen().size() == window.found().size() - leftAlone);
    const QString listed = pageText(page);
    expect(QStringLiteral("the list says Recipe's folders are left alone, and not that they are cleared through Recipe"),
           listed.contains(QStringLiteral("left alone")) && !listed.contains(QStringLiteral("through Recipe")));
    // The panel for one of them says what is true.
    for (const Finding &finding : window.found())
        if (finding.kind == kindRecipe) {
            page->pick(finding.node);
            break;
        }
    const QString recipePanel = pageText(page);
    expect(QStringLiteral("Recipe's folder says it belongs to Recipe, that Recdirstat leaves it alone and that Recipe has no command for it yet"),
           recipePanel.contains(QStringLiteral("belongs to Recipe")) && recipePanel.contains(QStringLiteral("Recdirstat leaves it alone"))
               && recipePanel.contains(QStringLiteral("Recipe has no command to clear it yet")) && !recipePanel.contains(QStringLiteral("through Recipe")));
}



// The page before anything was compared says the truth about the password: it is asked for only
// when comparing goes through a root helper.
void passwordShots(Window &window, const Expect &expect)
{
    if (window.comparison())
        return; // a kept comparison is shown instead, and says nothing of it
    const QString words = pageText(window.systemsPage());
    const bool helper = !qEnvironmentVariable("DISK_GUI_ROOT_HELPER").isEmpty();
    expect(QStringLiteral("the page of the two roots says the truth about the password (%1)").arg(helper ? QStringLiteral("asked") : QStringLiteral("none asked")),
           helper ? words.contains(QStringLiteral("needs the administrator's password"))
                  : (words.contains(QStringLiteral("no password is asked")) && !words.contains(QStringLiteral("administrator's password"))));
}

// Comparing says what it does first; Cancel changes nothing; the button starts it.
void compareShots(Window &window, const Save &save, const Expect &expect)
{
    const bool had = window.comparison() != nullptr;
    window.askCompare();
    settle(250);
    const QString words = pageText(window.beforePage());
    expect(QStringLiteral("comparing asks first, on the Before page, under The two roots"),
           window.showingBefore() && window.frame()->selected() == "systems" && words.contains(QStringLiteral("Nothing has started."))
               && !window.comparing());
    expect(QStringLiteral("it says both roots are read from start to end, neither is changed, it takes a few minutes, and the command that runs"),
           words.contains(QStringLiteral("read from start to end")) && words.contains(QStringLiteral("Neither root is changed"))
               && words.contains(QStringLiteral("a few minutes")) && words.contains(QStringLiteral("compare --json --machine-events")));
    // The stories run as the person, so no password is asked, and the page says so.
    expect(QStringLiteral("it says the truth about the password (%1)").arg(qEnvironmentVariable("DISK_GUI_ROOT_HELPER").isEmpty() ? "none asked" : "asked"),
           qEnvironmentVariable("DISK_GUI_ROOT_HELPER").isEmpty() ? words.contains(QStringLiteral("no password is asked"))
                                                                  : words.contains(QStringLiteral("administrator password")));
    save("5a-before-compare");
    if (QPushButton *cancel = buttonNamed(window.beforePage(), QStringLiteral("Cancel")))
        cancel->click();
    settle(200);
    expect(QStringLiteral("Cancel compares nothing: The two roots shows, nothing runs, the kept comparison is as it was"),
           window.showingSystems() && !window.comparing() && !window.busy() && (window.comparison() != nullptr) == had);
    window.askCompare();
    settle(250);
    if (QPushButton *go = buttonNamed(window.beforePage(), QStringLiteral("Compare")))
        go->click();
    else
        expect(QStringLiteral("the Before page has a \"Compare\" button"), false);
}

} // namespace

namespace {

// Whether the window has the page of the two roots is decided by two things:
// recab can be run, and the kernel command line has `recab.immutable`. Checked
// on its own for every combination, with command lines written here.
void twoRootsShots(Window &window, const QString &folder, const Expect &expect)
{
    const QString recab = qEnvironmentVariable("DISK_GUI_RECAB");
    auto cmdline = [&](const QString &name, const QByteArray &words) {
        const QString path = QDir(folder).filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(words);
        return path;
    };
    const QString ab = cmdline("cmdline-ab", "BOOT_IMAGE=/vmlinuz root=PARTUUID=1 ro recab.immutable quiet\n");
    const QString option = cmdline("cmdline-option", "root=PARTUUID=1 ro recab.immutable=1\n");
    const QString single = cmdline("cmdline-single", "BOOT_IMAGE=/vmlinuz root=PARTUUID=1 rw quiet\n");
    const QString near = cmdline("cmdline-near", "root=PARTUUID=1 ro notrecab.immutable recab.immutable-not\n");
    expect(QStringLiteral("two roots: recab runs and the command line has recab.immutable"), Window::hasTwoRoots(recab, ab));
    expect(QStringLiteral("two roots: recab.immutable=value counts as the option"), Window::hasTwoRoots(recab, option));
    expect(QStringLiteral("one root: recab runs but the command line lacks recab.immutable"), !Window::hasTwoRoots(recab, single));
    expect(QStringLiteral("one root: only words that merely contain recab.immutable"), !Window::hasTwoRoots(recab, near));
    expect(QStringLiteral("one root: the command line has recab.immutable but there is no recab to run"),
           !Window::hasTwoRoots(QStringLiteral("/nonexistent/recab"), ab) && !Window::hasTwoRoots(QStringLiteral("recab-not-installed-anywhere"), ab));
    expect(QStringLiteral("one root: the command line cannot be read"), !Window::hasTwoRoots(recab, QDir(folder).filePath("nonexistent")));
    // And the window itself followed the same decision, with the command line it was given.
    const QString given = qEnvironmentVariable("DISK_GUI_CMDLINE");
    expect(QStringLiteral("the window has the page of the two roots exactly when its own command line says so (%1)").arg(given),
           window.twoRoots() == Window::hasTwoRoots(recab, given) && (window.systemsPage() != nullptr) == window.twoRoots());
}

} // namespace

int takeShots(Window &window, const QString &folder)
{
    QDir().mkpath(folder);
    QTextStream out(stdout);
    bool ok = true;
    auto expect = [&](const QString &what, bool holds) {
        if (!holds) {
            out << "not as expected: " << what << "\n";
            ok = false;
        }
    };
    // Every picture is of the window at its size: no page may make it wider or taller.
    auto save = [&](const QString &name) {
        settle(200);
        expect(QStringLiteral("%1 fits the window (%2 x %3)").arg(name).arg(window.width()).arg(window.height()),
               window.width() == 1100 && window.height() == 700);
        ok &= window.grab().save(QDir(folder).filePath(name + ".png"));
    };
    window.show();
    // A picture run that is not told of its stand-ins refuses to start.
    helpstory::guardRefuses(expect, folder, "DISK_GUI_RECAB");
    waitUntilIdle(window, 600);
    qint64 mess = 0;
    for (const Finding &finding : window.found())
        mess += finding.bytes;
    out << "looked at " << bytesText(window.scannedBytes()) << ", " << window.found().size() << " places of mess, "
        << bytesText(mess) << "\n";
    for (const Finding &finding : window.found())
        out << "  " << bytesText(finding.bytes) << "  " << finding.what << "  " << finding.path << "\n";
    Frame *frame = window.frame();
    expect(QStringLiteral("the window is 1100 x 700"), window.width() == 1100 && window.height() == 700);
    twoRootsShots(window, folder, expect);
    expect(QStringLiteral("the rail offers The disk, Browse, Mess, %1Help, nothing else (%2)")
               .arg(window.twoRoots() ? QStringLiteral("The two roots, ") : QString(), frame->entryTitles().join(QStringLiteral(", "))),
           frame->entryTitles() == (window.twoRoots() ? QStringList({"The disk", "Browse", "Mess", "The two roots", "Help"})
                                                      : QStringList({"The disk", "Browse", "Mess", "Help"})));
    expect(QStringLiteral("the Mess badge is the number of places listed (%1 and %2)")
               .arg(frame->badge("mess")).arg(window.found().size()),
           frame->badge("mess") == window.found().size() && window.found().size() > 0);
    expect(QStringLiteral("the status line says when the disk was looked at (%1)").arg(frame->statusText()),
           frame->statusText().startsWith(QStringLiteral("Looked at the disk at ")));
    diskShots(window, save, expect, out);
    // "Look again" reads everything again, and the page is as true after it.
    if (QPushButton *again = buttonNamed(window.diskPage(), QStringLiteral("Look again")))
        again->click();
    settle(100);
    waitUntilIdle(window, 300);
    expect(QStringLiteral("Look again reads again and the page shows the same leftovers"),
           pageText(window.diskPage()).contains(bytesText(sumOf(window.found()))) && window.layout().found);
    if (window.twoRoots()) {
        helpShots(window, save, expect);
    } else {
        // Help is the same page; its row about the two roots has no page to go to, so it is not there.
        frame->choose(Frame::helpKey());
        settle(300);
        const QString help = frame->help()->pageText();
        expect(QStringLiteral("Help has no row about comparing the two roots, and no link to a page that is not there"),
               help.contains(QStringLiteral("How do I")) && !help.contains(QStringLiteral("Compare the two roots"))
                   && !frame->help()->links().contains(QStringLiteral("Go to The two roots")));
        save("h-help-one-root");
        frame->choose(QStringLiteral("disk"));
    }
    browseShots(window, folder, save, expect, out);
    messShots(window, save, expect);

    const QString picked = window.pickBiggestUnnamed();
    if (!picked.isEmpty())
        out << "picked by hand: " << picked << "\n";
    frame->choose(QStringLiteral("mess"));
    expect(QStringLiteral("what was picked by hand is in the Mess list"),
           pageText(window.messPage()).contains(QStringLiteral("Picked by you")) && listsPath(window, picked));
    save("3-picked");
    if (!window.twoRoots()) {
        // One root: no page, and recab was never asked anything.
        const QString log = qEnvironmentVariable("DISK_STANDIN_LOG");
        expect(QStringLiteral("without two roots recab was never run (%1)").arg(log),
               !log.isEmpty() && !QFileInfo::exists(log) && !window.showingSystems());
        window.refresh();
        settle(300);
        expect(QStringLiteral("not even when reading again"), !QFileInfo::exists(log));
        out << (ok ? "all as expected\n" : "some things were not as expected\n");
        return ok ? 0 : 1;
    }
    openRecabShots(window, expect);

    // The two roots: compare.
    frame->choose(QStringLiteral("systems"));
    waitUntilIdle(window, 30);
    ok &= window.state().known;
    expect(QStringLiteral("The two roots is marked and shows"), frame->selected() == "systems" && window.showingSystems());
    passwordShots(window, expect);
    {
        const QString said = pageText(window.systemsPage());
        expect(QStringLiteral("before comparing, the page says which root is booted and which is the other (%1)").arg(said.left(120)),
               window.state().booted == "A" && window.state().other == "B" && said.contains(QStringLiteral("Root A"))
                   && said.contains(QStringLiteral("Booted — locked")) && said.contains(QStringLiteral("Root B"))
                   && said.contains(QStringLiteral("Other — unlocked")) && said.contains(QStringLiteral("have not been compared")));
    }
    save("5-systems");
    // Slowly once, to see the two sides fill while they are read.
    const QByteArray speed = qgetenv("DISK_STANDIN_SPEED");
    qputenv("DISK_STANDIN_SPEED", "1");
    compareShots(window, save, expect);
    settle(3200);
    ok &= window.comparing();
    save("5b-comparing");
    qputenv("DISK_STANDIN_SPEED", speed);
    waitUntilIdle(window, 300);
    const Comparison *compared = window.comparison();
    if (!compared) {
        out << "the two roots could not be compared\n";
        save("6-compared");
        return 1;
    }
    out << "the two roots: " << compared->count() << " differences, " << bytesText(compared->differingBytes())
        << "; " << compared->sameFiles << " files the same\n";
    expect(QStringLiteral("the two maps and the list fit the window without making it wider (%1 x %2)")
               .arg(window.width()).arg(window.height()),
           window.width() == 1100 && window.height() == 700);
    {
        const QString said = pageText(window.systemsPage());
        expect(QStringLiteral("the comparison names the roots: A booted and locked, B the other and unlocked"),
               said.contains(QStringLiteral("Root A")) && said.contains(QStringLiteral("Booted — locked."))
                   && said.contains(QStringLiteral("Root B")) && said.contains(QStringLiteral("Other — unlocked."))
                   && said.contains(QStringLiteral("Root A is booted and locked; root B is the other root and unlocked.")));
    }
    save("6-compared");
    if (!compared->identical()) {
        // "Copy for agent" puts the differences on the clipboard, and nothing else happens.
        QApplication::clipboard()->clear();
        QPushButton *copy = buttonNamed(window.systemsPage(), QStringLiteral("Copy for agent"));
        expect(QStringLiteral("the comparison has a Copy for agent button"), copy != nullptr);
        if (copy)
            copy->click();
        expect(QStringLiteral("it copies the differences to the clipboard"),
               QApplication::clipboard()->text() == compared->copyText() && QApplication::clipboard()->text().contains(compared->issue()));
        expect(QStringLiteral("and starts nothing"), !window.busy());
    }
    // The partition of the other system now says what its files took.
    frame->choose(QStringLiteral("disk"));
    waitUntilIdle(window, 60);
    expect(QStringLiteral("The disk says what the other system's files took when compared"),
           pageText(window.diskPage()).contains(QStringLiteral("its files took %1 when compared").arg(bytesText(compared->other.bytes))));
    expect(QStringLiteral("and its partitions are named as the booted root and the other root"),
           pageText(window.diskPage()).contains(QStringLiteral("Root A — booted, locked"))
               && pageText(window.diskPage()).contains(QStringLiteral("Root B — other, unlocked")));
    save("6b-disk-compared");
    // A comparison made while the other root was booted is out of date.
    qputenv("DISK_STANDIN_SCENARIO", "booted-b");
    frame->choose(QStringLiteral("systems"));
    window.readState();
    waitUntilIdle(window, 30);
    expect(QStringLiteral("after the roots swapped, the kept comparison says it is out of date"),
           window.stale() && pageText(window.systemsPage()).contains(QStringLiteral("Another root is booted now than when this was compared")));
    save("6c-stale");
    // recab cannot be read.
    qputenv("DISK_STANDIN_SCENARIO", "failing");
    window.readState();
    waitUntilIdle(window, 30);
    expect(QStringLiteral("a status that fails: the state is not known, and the kept comparison stays on the page"),
           !window.state().known && window.comparison() != nullptr);
    qputenv("DISK_STANDIN_SCENARIO", "normal");
    frame->choose(QStringLiteral("mess"));
    expect(QStringLiteral("Mess is marked and shows again"), frame->selected() == "mess" && window.showingMess());
    out << (ok ? "all as expected\n" : "some things were not as expected\n");
    return ok ? 0 : 1;
}
