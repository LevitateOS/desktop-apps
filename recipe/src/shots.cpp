#include "shots.h"

#include "before.h"
#include "frame.h"
#include "helpstory.h"
#include "look.h"
#include "sourceview.h"

#include "pages.h"
#include "parts.h"
#include "sources.h"
#include "storage.h"
#include "window.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QPalette>
#include <QPlainTextEdit>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <optional>

static void waitUntilIdle(Window &window, int seconds = 10)
{
    QElapsedTimer clock;
    clock.start();
    while (window.busy() && clock.elapsed() < seconds * 1000)
        settle(50);
    settle(150);
}


namespace {

using Save = std::function<void(QWidget &, const QString &)>;
using Expect = std::function<void(const QString &, bool)>;

void clickButton(QWidget *inside, const QString &words)
{
    for (QPushButton *button : inside->findChildren<QPushButton *>())
        if (QString(button->text()).remove(QLatin1Char('&')) == words && button->isVisible())
            button->click();
}

// The buttons a person can press in a window now.
QStringList buttonTexts(QWidget &inside)
{
    QStringList buttons;
    for (const QPushButton *button : inside.findChildren<QPushButton *>())
        if (button->isVisible())
            buttons << QString(button->text()).remove(QLatin1Char('&'));
    return buttons;
}

// The Before step: press its one button ("Install", "Update"), as a click would.
void goOn(Window &window, const QString &button)
{
    settle(100); // a page drawn just now is shown once the event loop has run
    clickButton(window.beforePage(), button);
}

// What `recipe` was asked to change, each as the arguments it got, from the
// stand-in's log (RECIPE_STANDIN_LOG, else invocations.log in its state
// folder). Reads (list, installed, updates, storage) are left out.
struct Asked {
    QString route; // "system", "store" or "me", as the stand-in told them apart
    QStringList arguments;
};

QList<Asked> changesAsked()
{
    QString path = qEnvironmentVariable("RECIPE_STANDIN_LOG");
    if (path.isEmpty())
        path = QDir(qEnvironmentVariable("RECIPE_STANDIN_STATE")).filePath("invocations.log");
    QFile file(path);
    QList<Asked> found;
    if (!file.open(QIODevice::ReadOnly))
        return found;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        Asked asked{object["route"].toString(), {}};
        for (const QJsonValue &argument : object["args"].toArray())
            asked.arguments << argument.toString();
        for (const QString &argument : asked.arguments) {
            if (argument.startsWith("--"))
                continue;
            if (argument == "install" || argument == "update" || argument == "remove")
                found << asked;
            break;
        }
    }
    return found;
}

// What `recipe` answers to a read, asked of the same stand-in and store the window uses,
// for numbers the pages must equal.
QJsonObject answerOf(const QStringList &arguments)
{
    QProcess run;
    run.start(qEnvironmentVariable("RECIPE_GUI_RECIPE"),
              arguments + QStringList{"--package-root", qEnvironmentVariable("RECIPE_GUI_PACKAGE_ROOT")});
    run.waitForFinished(120000);
    return QJsonDocument::fromJson(run.readAllStandardOutput()).object();
}

// Every command that changes a store, from the stand-in's log.
QList<Asked> loggedCommands()
{
    QString path = qEnvironmentVariable("RECIPE_STANDIN_LOG");
    if (path.isEmpty())
        path = QDir(qEnvironmentVariable("RECIPE_STANDIN_STATE")).filePath("invocations.log");
    QFile file(path);
    QList<Asked> found;
    if (!file.open(QIODevice::ReadOnly))
        return found;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        Asked asked{object["route"].toString(), {}};
        for (const QJsonValue &argument : object["args"].toArray())
            asked.arguments << argument.toString();
        found << asked;
    }
    return found;
}

// How many times `recipe` was run with all of these words among its arguments, from the stand-in's log.
int timesAsked(const QStringList &words)
{
    int found = 0;
    for (const Asked &each : loggedCommands()) {
        bool all = true;
        for (const QString &word : words)
            all = all && each.arguments.contains(word);
        found += all;
    }
    return found;
}

// How many processes run a command line with these words (the stand-in recipe, stopped in the middle).
int processesRunning(const QString &pattern)
{
    QProcess pgrep;
    pgrep.start(QStringLiteral("pgrep"), {QStringLiteral("-f"), pattern});
    pgrep.waitForFinished(10000);
    return pgrep.exitCode() == 0 ? 1 : 0;
}

// Everything a person can read in a window now: labels, buttons and the rows of lists.
QString wordsOn(QWidget &window)
{
    QStringList words;
    for (const QLabel *label : window.findChildren<QLabel *>())
        if (label->isVisible())
            words << label->text();
    for (const QAbstractButton *button : window.findChildren<QAbstractButton *>())
        if (button->isVisible())
            words << button->text();
    for (const QTreeWidget *tree : window.findChildren<QTreeWidget *>()) {
        if (!tree->isVisible())
            continue;
        for (int row = 0; row < tree->topLevelItemCount(); ++row)
            for (int column = 0; column < tree->columnCount(); ++column)
                if (!tree->isColumnHidden(column))
                    words << tree->topLevelItem(row)->text(column);
    }
    return words.join(QLatin1Char('\n'));
}

// Environment a pass changes and puts back afterwards.
class Environment {
public:
    void set(const char *name, const QString &value)
    {
        keep(name);
        qputenv(name, value.toUtf8());
    }
    void unset(const char *name)
    {
        keep(name);
        qunsetenv(name);
    }
    ~Environment()
    {
        for (auto it = was_.begin(); it != was_.end(); ++it) {
            if (!it.value())
                qunsetenv(it.key().constData());
            else
                qputenv(it.key().constData(), *it.value());
        }
    }
private:
    void keep(const char *name)
    {
        if (!was_.contains(name))
            was_.insert(name, qEnvironmentVariableIsSet(name) ? std::optional<QByteArray>(qgetenv(name)) : std::nullopt);
    }
    QMap<QByteArray, std::optional<QByteArray>> was_;
};

// What the window is as a frame: the rail, the pages in it and what the
// Browse page's search and kinds still do. Pictures `f*`.
void frameShots(Window &window, const Save &save, const Expect &expect)
{
    Frame *frame = window.frame();
    for (const char *title : {"Browse", "Installed", "Updates", "Sources", "Storage", "Help"})
        expect(QStringLiteral("the rail has %1").arg(title), frame->entryTitles().contains(QString::fromLatin1(title)));
    expect(QStringLiteral("Storage follows Sources in the rail"),
           frame->entryTitles().mid(0, 5) == QStringList({"Browse", "Installed", "Updates", "Sources", "Storage"}));
    expect(QStringLiteral("Help is the last entry, the frame's own"), frame->entryTitles().last() == "Help");
    expect(QStringLiteral("the window is 1100 by 700"), window.size() == QSize(1100, 700));

    window.chooseSource(QString());
    window.browsePage()->searchBox()->clear();
    window.showBrowse();
    expect(QStringLiteral("Browse is marked in the rail"), frame->selected() == "browse");
    const qsizetype all = window.browsePage()->listed().size();
    int listed = 0;
    for (const Recipe &recipe : window.catalog().recipes)
        listed += recipe.listed();
    expect(QStringLiteral("Browse lists every listed recipe (%1)").arg(listed), all == listed && all > 3);
    save(window, "f1-browse");

    // Search still filters, and says so in the header.
    window.browsePage()->searchBox()->setText("htop");
    settle(100);
    expect(QStringLiteral("a search lists htop and fewer than all"),
           window.browsePage()->listed().contains("htop") && window.browsePage()->listed().size() < all);
    save(window, "f1b-browse-search");
    window.browsePage()->searchBox()->setText("zzzz-nothing");
    settle(100);
    expect(QStringLiteral("a search that matches nothing lists nothing"), window.browsePage()->listed().isEmpty());
    window.browsePage()->searchBox()->clear();
    settle(100);
    expect(QStringLiteral("clearing the search lists all again"), window.browsePage()->listed().size() == all);

    // A kind still filters.
    const QStringList kinds = window.catalog().categories();
    expect(QStringLiteral("the catalog has kinds"), kinds.size() >= 2);
    if (kinds.size() >= 2) {
        const QString kind = kinds.first();
        QStringList want;
        for (const Recipe &recipe : window.catalog().recipes)
            if (recipe.listed() && recipe.category == kind)
                want << recipe.name;
        window.showBrowse(kind);
        expect(QStringLiteral("a kind lists exactly the recipes of that kind (%1)").arg(kind),
               window.browsePage()->listed() == want && want.size() < all);
        expect(QStringLiteral("the kinds box shows it"),
               window.browsePage()->categoryBox()->currentData().toString() == kind);
        save(window, "f1c-browse-kind");
        window.showBrowse();
        expect(QStringLiteral("all kinds again"), window.browsePage()->listed().size() == all);
    }

    window.showInstalled("gimp");
    expect(QStringLiteral("Installed is marked in the rail"), frame->selected() == "installed");
    save(window, "f2-installed");

    window.showUpdates("element");
    expect(QStringLiteral("Updates is marked in the rail"), frame->selected() == "updates");
    expect(QStringLiteral("the Updates badge is the number of updates (%1)").arg(window.catalog().updates.size()),
           window.catalog().updates.size() > 0 && frame->badge("updates") == window.catalog().updates.size()
               && !frame->badgeWarns("updates"));
    save(window, "f3-updates");

    window.showSources();
    expect(QStringLiteral("Sources is marked in the rail"), frame->selected() == "sources");
    save(window, "f4-sources");

    // The clicks: choosing an entry shows its page.
    frame->choose(QStringLiteral("installed"));
    expect(QStringLiteral("choosing Installed shows it"), frame->selected() == "installed");
    frame->choose(QStringLiteral("browse"));
    expect(QStringLiteral("choosing Browse shows the list"), frame->selected() == "browse" && !window.browsePage()->listed().isEmpty());

    // A change just made says so for a few seconds; the line under it is where the recipes are from.
    frame->flash(QString());
    const QString status = frame->statusText();
    expect(QStringLiteral("the status line says where the recipes are from and when (%1)").arg(status),
           status.contains(" read at ") && status.contains("source"));
}

void helpShots(Window &window, const Save &save, const Expect &expect)
{
    helpstory::Plan plan;
    plan.app = Frame::Recipe;
    plan.window = &window;
    plan.frame = window.frame();
    plan.firstEntry = QStringLiteral("browse");
    plan.idle = [&] { waitUntilIdle(window, 30); };
    plan.pageShown = [&](const QString &key) {
        QWidget *page = key == QLatin1String("browse")      ? static_cast<QWidget *>(window.findChild<BrowsePage *>())
                        : key == QLatin1String("installed") ? static_cast<QWidget *>(window.findChild<InstalledPage *>())
                        : key == QLatin1String("updates")   ? static_cast<QWidget *>(window.findChild<UpdatesPage *>())
                        : key == QLatin1String("sources")   ? static_cast<QWidget *>(window.findChild<SourcesPage *>())
                        : key == QLatin1String("storage")   ? static_cast<QWidget *>(window.findChild<StoragePage *>())
                                                            : nullptr;
        return page && page->isVisible();
    };
    helpstory::run(plan, save, expect, "h");
    window.showBrowse();
    settle(100);
}

// The two places software can go, in windows of their own: an A/B system
// (`recab.immutable` on a scratch command line), where "for everyone" is `recipe --system` into the
// other root, and a system with one root, where it is `recipe --system` into the running root (the
// stand-in's RECIPE_STANDIN_AB=0 answers as that route does). Each has its own stand-in state, and the
// stand-in logs every command's arguments, which are checked here.
void placesShots(const QString &folder, const Save &save, const Expect &expect)
{
    const QString recipe = qEnvironmentVariable("RECIPE_GUI_RECIPE");
    if (recipe.isEmpty()) {
        expect(QStringLiteral("the two places need RECIPE_GUI_RECIPE, see README.md"), false);
        return;
    }
    const QString scratch = QDir(folder).filePath("places");
    QDir(scratch).removeRecursively();
    QDir().mkpath(scratch);
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) >= 0;
    };
    write(scratch + "/locked-cmdline", "BOOT_IMAGE=/vmlinuz root=/dev/vda1 ro recab.immutable quiet\n");
    write(scratch + "/open-cmdline", "BOOT_IMAGE=/vmlinuz root=/dev/vda1 ro quiet\n");
    auto kinds = [](const QStringList &arguments, const QStringList &want) { return arguments == want; };
    auto asked = [](qsizetype from) { return changesAsked().mid(from); };
    // Words that belong to a system with two roots, and must not appear on one with a single root.
    auto abWords = [](const QString &words) {
        for (const char *word : {"other root", "running root", "root A", "root B", "A/B"})
            if (words.contains(QString::fromLatin1(word), Qt::CaseInsensitive))
                return QString::fromLatin1(word);
        return QString();
    };

    // ---- An A/B system.
    {
        Environment env;
        env.unset("RECIPE_GUI_PACKAGE_ROOT");
        env.set("RECIPE_GUI_CMDLINE", scratch + "/locked-cmdline");
        env.set("RECIPE_STANDIN_STATE", scratch + "/locked-state");
        env.unset("RECIPE_STANDIN_LOG");
        env.unset("RECIPE_GUI_SYSTEM_ROOT");
        env.unset("RECIPE_STANDIN_AB");
        env.unset("RECIPE_STANDIN_NOT_DONE");
        // A helper that notes what it was given and runs it: only changes for everyone go through it.
        env.set("RECIPE_GUI_ROOT_HELPER", QFileInfo(recipe).absolutePath() + "/helper-log");
        env.set("HELPER_LOG", scratch + "/locked-helper.log");

        Window window;
        window.resize(1100, 700);
        window.show();
        waitUntilIdle(window, 30);
        const Catalog &catalog = window.catalog();
        const Places places = window.places();
        expect(QStringLiteral("an A/B system is told from its command line, and names its roots"),
               places.ab && !places.one && catalog.twoPlaces && catalog.ab && catalog.bootedRoot == "A" && catalog.otherRoot == "B");
        expect(QStringLiteral("it holds the person's own software and the booted root's, which says only name and version"),
               catalog.installedItem("tree", Place::Me) && catalog.installedItem("gimp", Place::Computer)
                   && !catalog.installedItem("gimp", Place::Computer)->inStore
                   && catalog.installedItem("gimp", Place::Computer)->version == "3.0.4"
                   && !catalog.installedItem("gimp", Place::Computer)->sizeBytes);
        expect(QStringLiteral("the other root starts as a copy of the booted one: nothing is only in it"),
               !catalog.installedItem("gimp", Place::Other) && !catalog.installedItem("element", Place::Other)
                   && !catalog.installedItem("gimp", Place::Computer)->leavesOther);
        expect(QStringLiteral("updates: the person's store by `recipe updates`, the booted root's from the collection's versions"),
               catalog.update("tree", Place::Me) && !catalog.update("tree", Place::Me)->fromCollection
                   && catalog.update("gimp", Place::Computer) && catalog.update("gimp", Place::Computer)->fromCollection);
        expect(QStringLiteral("only the person's store has storage figures"),
               catalog.stores[int(Place::Me)].has && !catalog.stores[int(Place::Computer)].has);
        expect(QStringLiteral("Recipe said this system's C library, and the status line is not about a next system"),
               catalog.hostLibc == "glibc" && !window.frame()->statusText().contains("next system"));
        qsizetype before = changesAsked().size();

        // Install: where it goes, with the sentences for the place picked. "For me" is the default.
        window.showBrowse();
        window.askInstall("htop");
        BeforePage *page = window.beforePage();
        expect(QStringLiteral("Before: both places, and for me is picked (%1)").arg(page->pageText().left(60)),
               page->isVisible() && page->choiceNames() == QStringList({"For me", "For everyone"})
                   && page->chosen() == placeKey(Place::Me) && page->pageText().contains("Install htop for me?")
                   && page->pageText().contains("Nothing has started. This is what will happen if you go on."));
        page->choose(placeKey(Place::Computer));
        expect(QStringLiteral("for everyone on an A/B system: written into the other root, active when it is started, the booted one untouched"),
               page->pageText().contains("Install htop for everyone?")
                   && page->pageText().contains("Written into the other root (B), not into the running root. It becomes active, for everyone, when root B is started.")
                   && page->pageText().contains("Asks for the administrator password.")
                   && page->pageText().contains("The running root is not touched.")
                   && !page->pageText().contains("next system") && !page->pageText().contains("restart"));
        expect(QStringLiteral("it says what the catalog says: where it is fetched from and the sandbox"),
               page->pageText().contains("is fetched from") && page->pageText().contains("inside the sandbox")
                   && page->pageText().contains("The sandbox refuses anything else."));
        expect(QStringLiteral("it does not say how much is downloaded or used; recipe does not report that"),
               !page->pageText().contains("is downloaded") && !page->pageText().contains("is used"));
        expect(QStringLiteral("the foot names the source and offers the recipe"),
               page->pageText().contains("From ") && page->pageText().contains("Read the recipe"));
        save(window, "p1-before-install-choice");
        page->choose(placeKey(Place::Me));
        expect(QStringLiteral("choosing For me: your own folder, at once, and the roots are not mentioned"),
               page->chosen() == placeKey(Place::Me) && page->pageText().contains("Install htop for me?")
                   && page->pageText().contains("Into your own folder. It works at once, for you only.")
                   && !page->pageText().contains("The running root is not touched."));
        save(window, "p2-before-install-for-me");
        page->choose(placeKey(Place::Computer));
        goOn(window, "Install");
        waitUntilIdle(window, 30);
        QList<Asked> now = asked(before);
        expect(QStringLiteral("for everyone on A/B: `recipe --system install NAME` through the root helper"),
               now.size() == 1 && now[0].route == "system"
                   && kinds(now[0].arguments, {"--system", "install", "levitate:htop", "--machine-events"}));
        expect(QStringLiteral("the root helper was given that command"), QFile(scratch + "/locked-helper.log").exists());
        expect(QStringLiteral("it is in the other root and not in the booted one"),
               window.catalog().installedItem("htop", Place::Other) && !window.catalog().installedItem("htop", Place::Computer));
        expect(QStringLiteral("the status line said it was written into the other root (B), active when B is started (%1)")
                   .arg(window.frame()->statusText()),
               window.frame()->statusText().contains("htop was written into the other root (B). It becomes active when root B is started."));
        before = changesAsked().size();
        // The program page: it is in the other root, not active now, and cannot be opened.
        window.showRecipe("htop");
        settle(100);
        {
            const QString there = wordsOn(window);
            expect(QStringLiteral("its page says it is in the other root (B), active when B is started, and offers no Open (%1)").arg(buttonTexts(window).join("|")),
                   there.contains("In the other root (B)") && there.contains("active when root B is started")
                       && !buttonTexts(window).contains("Open") && !buttonTexts(window).contains("Install"));
        }
        save(window, "p10-program-in-other-root");

        // For me: the person's own store, no root helper, no --system, no --package-root.
        auto helperLines = [&] {
            QFile helper(scratch + "/locked-helper.log");
            return helper.open(QIODevice::ReadOnly) ? helper.readAll().count('\n') : 0;
        };
        const qsizetype helperBefore = helperLines();
        window.askInstall("krita");
        page->choose(placeKey(Place::Me));
        goOn(window, "Install");
        waitUntilIdle(window, 30);
        now = asked(before);
        const qsizetype helperAfter = helperLines();
        expect(QStringLiteral("for me: `recipe install NAME` with no store named and no --system"),
               now.size() == 1 && now[0].route == "me" && kinds(now[0].arguments, {"install", "levitate:krita", "--machine-events"}));
        expect(QStringLiteral("for me goes through no root helper"), helperAfter == helperBefore);
        expect(QStringLiteral("it is there at once, in the person's store, and said so (%1)").arg(window.frame()->statusText()),
               window.catalog().installedItem("krita", Place::Me) && window.catalog().installedItem("krita", Place::Me)->inStore
                   && window.frame()->statusText().contains("Installed Krita") && !window.frame()->statusText().contains("other root"));
        // "Open" starts the program from the profile of the store it is in (the person's own: its bin is not
        // on PATH), and the one its menu entry names, not the first program the package put in bin.
        {
            const QString meProfile = scratch + "/locked-state/me/profile";
            const QString startedLog = scratch + "/locked-state/started.log";
            auto started = [&] {
                QFile file(startedLog);
                return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
            };
            QFile::remove(startedLog);
            window.showRecipe("krita");
            settle(150);
            const InstalledItem *mine = window.catalog().installedItem("krita", Place::Me);
            expect(QStringLiteral("recipe named the person's own profile for it, and the programs and menu entry it put there"),
                   mine && mine->profile == meProfile && mine->programs == QStringList({"kritarunner", "krita"})
                       && mine->menu == QStringList({"krita"}));
            expect(QStringLiteral("the page offers Open"), buttonTexts(window).contains("Open"));
            clickButton(window.recipePage(), "Open");
            waitFor([&] { return started().contains("krita started from"); }, 10);
            expect(QStringLiteral("Open started krita (the menu entry's program, not kritarunner) from the person's profile, with its bin first on PATH (%1)").arg(started().trimmed()),
                   started().contains("krita started from " + meProfile + "/bin/krita, PATH begins with " + meProfile + "/bin")
                       && !started().contains("kritarunner started"));
            expect(QStringLiteral("the status line says it was started (%1)").arg(window.frame()->statusText()),
                   window.frame()->statusText() == "Started Krita");
            save(window, "p8-open-for-me");
            // The program is not there any more: the window says what is true.
            QFile::remove(meProfile + "/bin/krita");
            QFile::remove(meProfile + "/bin/kritarunner");
            clickButton(window.recipePage(), "Open");
            settle(200);
            expect(QStringLiteral("when it cannot be started the status line says which program is missing and where (%1)").arg(window.frame()->statusText()),
                   window.frame()->statusText().contains("Krita could not be started: krita, which the menu entry krita starts, in " + meProfile + "/bin is not there."));
            save(window, "p9-open-failed");
        }
        before = changesAsked().size();

        // Update where the software is: no choice, only the statement.
        window.showUpdates("gimp", int(Place::Computer));
        window.askUpdate("gimp", Place::Computer);
        expect(QStringLiteral("an update has no choice, only the statement of where it is"),
               page->choiceNames() == QStringList({"For everyone"}) && page->pageText().contains("Where it is")
                   && page->pageText().contains("Update GIMP to 3.0.6?")
                   && page->pageText().contains("GIMP goes from version 3.0.4 to 3.0.6."));
        expect(QStringLiteral("what the booted root's version was allowed to use is not known, and the page says so"),
               page->pageText().contains("is not known here, so the two cannot be compared"));
        save(window, "p3-before-update-ab");
        goOn(window, "Update");
        waitUntilIdle(window, 30);
        now = asked(before);
        expect(QStringLiteral("updating system software: `recipe --system update NAME`"),
               now.size() == 1 && now[0].route == "system"
                   && kinds(now[0].arguments, {"--system", "update", "levitate:gimp", "--machine-events"}));
        expect(QStringLiteral("the other root holds the new version beside the booted root's"),
               window.catalog().installedItem("gimp", Place::Other) && window.catalog().installedItem("gimp", Place::Other)->version == "3.0.6"
                   && window.catalog().installedItem("gimp", Place::Computer)->version == "3.0.4");
        before = changesAsked().size();
        // The recipe's page for software in the booted root and the other one: buttons that say where.
        window.showRecipe("gimp");
        settle(100);
        {
            const QString there = wordsOn(window);
            expect(QStringLiteral("what only the other root holds, and the booted root's list, offer no Open and say why"),
                   !buttonTexts(window).contains("Open") && there.contains("It cannot be opened until then.")
                       && there.contains("This window cannot open it: the list of the running root names the software and its version, not its programs."));
            window.open("gimp", Place::Other);
            expect(QStringLiteral("asking anyway says why (%1)").arg(window.frame()->statusText()),
                   window.frame()->statusText().contains("only in the other root (B), so it cannot be opened until root B is started"));
        }
        save(window, "p3b-recipe-two-roots");

        // The person's own update, with what `recipe updates` said changed in the recipe.
        window.askUpdate("tree", Place::Me);
        expect(QStringLiteral("an update for me names where and compares nothing it cannot: tree was recorded without needs"),
               page->pageText().contains("Update tree to 2.3.2?") && page->pageText().contains("For me"));
        goOn(window, "Update");
        waitUntilIdle(window, 30);
        now = asked(before);
        expect(QStringLiteral("updating for me: `recipe update NAME`, nothing else"),
               now.size() == 1 && now[0].route == "me" && kinds(now[0].arguments, {"update", "local:tree", "--machine-events"}));
        before = changesAsked().size();

        // What is installed, from both places and both roots; what a store cannot do is not offered for the booted root's.
        window.showInstalled("gimp", int(Place::Computer));
        settle(100);
        QStringList buttons;
        for (QPushButton *button : window.findChildren<QPushButton *>())
            if (button->isVisible())
                buttons << button->text();
        expect(QStringLiteral("a booted-root row offers Remove (%1)").arg(buttons.join("|")),
               buttons.contains("Remove…"));
        const QString installed = wordsOn(window);
        for (const char *where : {"For me", "For everyone", "In the other root (B)", "active now", "active when root B is started"})
            expect(QStringLiteral("Installed says %1").arg(where), installed.contains(QString::fromLatin1(where)));
        expect(QStringLiteral("Installed does not talk of a next system or a restart"),
               !installed.contains("next system") && !installed.contains("restart"));
        expect(QStringLiteral("no page made the window wider than it was (%1)").arg(window.width()), window.width() == 1100);
        save(window, "p4-installed-both-roots");
        // The base is one row for the system itself: only a way to the release page, no Update or Remove.
        {
            window.showInstalled("base", int(Place::Computer));
            settle(100);
            const InstalledItem *base = window.catalog().installedItem("base", Place::Computer);
            const QStringList on = buttonTexts(window);
            expect(QStringLiteral("the base is one row for the system, with Check for updates and nothing that changes it (%1)").arg(on.join("|")),
                   base && base->base && base->serial == 4 && wordsOn(window).contains(", the base system")
                       && on.contains("Check for updates") && !on.contains("Remove…") && !on.filter("Update to").size()
                       && !window.catalog().update("base", Place::Computer));
            save(window, "p4b-installed-base");
            // A shared part comes with the software that needs it, and stays while that is installed.
            window.showInstalled("desktop-libs", int(Place::Computer));
            settle(100);
            const QString shared = wordsOn(window);
            expect(QStringLiteral("a library of a system group is a shared part: it comes with what needs it and cannot be removed (%1)").arg(buttonTexts(window).join("|")),
                   shared.contains("comes with Plasma desktop") && shared.contains("cannot be removed while Plasma desktop is installed")
                       && !buttonTexts(window).contains("Remove…"));
            window.showInstalled("plasma", int(Place::Computer));
            settle(100);
            expect(QStringLiteral("what needs it is said on the row of the software that is needed (%1)").arg(wordsOn(window).left(80)),
                   window.catalog().neededBy("desktop-libs", Place::Other) == QStringList({"plasma"})
                       && buttonTexts(window).contains("Remove…"));
            // The last desktop of the root the change goes into: said before the button, and removing stays allowed.
            window.askRemove("plasma", Place::Computer);
            settle(200);
            QPointer<RemoveDialog> last = window.removeQuestion();
            expect(QStringLiteral("removing the last desktop says so before the button"),
                   last && last->isVisible() && window.catalog().lastDesktop("plasma")
                       && wordsOn(*last).contains("This is the last desktop in the other root (B). After it is removed the computer starts to a text login.")
                       && wordsOn(*last).contains("recipe --system install NAME") && buttonTexts(*last).contains("Remove"));
            if (last)
                save(*last, "p4c-remove-last-desktop");
            clickButton(last, "Cancel");
            settle(100);
            // Software of the system itself is only ever installed into the system: no choice of place.
            window.askInstall("lxqt");
            BeforePage *system = window.beforePage();
            expect(QStringLiteral("a system group offers no choice of place and says why in one sentence (%1)").arg(system->choiceNames().join("|")),
                   system->isVisible() && system->choiceNames() == QStringList({"The system"})
                       && system->pageText().contains("It becomes part of the system, so it is written into the other root (B) and is active when root B is started.")
                       && !system->pageText().contains("For me") && system->pageText().contains("Install LXQt desktop?")
                       && !window.catalog().installPlaces("lxqt").contains(Place::Me));
            expect(QStringLiteral("a library a system group needs is system software too"),
                   window.catalog().intoSystemOnly("desktop-libs") && window.catalog().intoSystemOnly("plasma")
                       && !window.catalog().intoSystemOnly("htop"));
            save(window, "p1b-before-install-system-group");
            clickButton(system, "Cancel");
            settle(100);
        }
        window.showUpdates();
        save(window, "p5-updates-both-roots");
        expect(QStringLiteral("what the other root holds already is not offered as an update again (%1)").arg(window.catalog().updates.size()),
               !window.catalog().update("gimp", Place::Computer) && window.catalog().update("element", Place::Computer));
        window.showBrowse();
        settle(100);
        expect(QStringLiteral("Browse says software only the other root holds is in root B"), wordsOn(window).contains("In root B"));
        save(window, "p7-browse-both-roots");

        // A system change that was wiped from the other root and could not be written again: said with its error
        // and "Copy for agent"; nothing is suggested for the booted root.
        {
            Environment notDone;
            notDone.set("RECIPE_STANDIN_NOT_DONE", "htop:the recipe failed; nothing was written to the other root");
            window.refresh();
            waitUntilIdle(window, 30);
            window.showInstalled();
            settle(100);
            const QString words = wordsOn(window);
            expect(QStringLiteral("a not done entry: named with Recipe's error (%1)").arg(window.catalog().notDone.size()),
                   window.catalog().notDone.size() == 1 && window.catalog().notDone[0].package == "htop"
                       && words.contains("Not done yet: htop")
                       && words.contains("the recipe failed; nothing was written to the other root")
                       && words.contains("Recipe tries again by itself the next time you change system software."));
            expect(QStringLiteral("it offers Copy for agent, and no way to change the booted root (%1)").arg(buttonTexts(window).join("|")),
                   buttonTexts(window).contains("Copy for agent") && !words.contains("Change the running root"));
            save(window, "p11-installed-not-done");
            const qsizetype ran = changesAsked().size();
            QApplication::clipboard()->clear();
            clickButton(window.installedPage(), "Copy for agent");
            const QString copied = QApplication::clipboard()->text();
            expect(QStringLiteral("Copy for agent puts the issue, the facts and Recipe's error on the clipboard and starts nothing"),
                   copied.contains("The issue:\n") && copied.contains("The facts:\n")
                       && copied.contains("the recipe failed; nothing was written to the other root") && changesAsked().size() == ran);
        }
        window.refresh();
        waitUntilIdle(window, 30);
        expect(QStringLiteral("when Recipe lists nothing not done, the entry is gone"), window.catalog().notDone.isEmpty());

        // Remove for me, and from the system.
        window.removeNow("krita", Place::Me);
        waitUntilIdle(window, 30);
        now = asked(before);
        expect(QStringLiteral("removing for me: `recipe remove NAME` and no store, no --system"),
               now.size() == 1 && now[0].route == "me" && kinds(now[0].arguments, {"remove", "krita", "--machine-events"}));
        before = changesAsked().size();
        window.removeNow("element", Place::Computer);
        waitUntilIdle(window, 30);
        now = asked(before);
        expect(QStringLiteral("removing system software: `recipe --system remove NAME`"),
               now.size() == 1 && now[0].route == "system"
                   && kinds(now[0].arguments, {"--system", "remove", "element", "--machine-events"}));
        expect(QStringLiteral("the booted root still holds it, and says the other root no longer does"),
               window.catalog().installedItem("element", Place::Computer) && window.catalog().installedItem("element", Place::Computer)->leavesOther);
        expect(QStringLiteral("the status line said where it was taken out (%1)").arg(window.frame()->statusText()),
               window.frame()->statusText().contains("Removed Element from the other root (B). It stays in the running root (A) until the other root is started."));

        // Installed, but the system's caches could not be rebuilt: not a failed install.
        {
            Environment effects;
            effects.set("RECIPE_STANDIN_EFFECTS", "failed");
            before = changesAsked().size();
            window.askInstall("lxqt");
            goOn(window, "Install");
            waitUntilIdle(window, 30);
            settle(200);
            const QString said = window.runPage()->pageText();
            const QString on = said;
            expect(QStringLiteral("it is written into the other root; the command's answer says the effects failed (%1)").arg(window.runPage()->answer()["effects"].toObject()["status"].toString()),
                   window.catalog().installedItem("lxqt", Place::Other) && window.runPage()->answer()["effects"].toObject()["status"] == "failed");
            expect(QStringLiteral("the page says it is installed and that only the caches failed, with the error and Copy for agent, and no Try again (%1)").arg(on.left(100)),
                   window.runPage()->isVisible() && said.contains("Installed LXQt desktop")
                       && said.contains("LXQt desktop is installed, but one step after it did not finish")
                       && said.contains("The system's caches (menus, icons and fonts) could not be rebuilt")
                       && said.contains("The next system change tries again.") && said.contains("glib-compile-schemas: cannot write")
                       && on.contains("Copy for agent") && !on.contains("Try again") && !said.contains("Stopped at this step"));
            expect(QStringLiteral("the status line says so too (%1)").arg(window.frame()->statusText()),
                   window.frame()->statusText().contains("LXQt desktop is installed, but the system's caches could not be rebuilt"));
            save(window, "p13-installed-caches-failed");
            QApplication::clipboard()->clear();
            clickButton(window.runPage(), "Copy for agent");
            expect(QStringLiteral("Copy for agent puts the issue, the facts and Recipe's error on the clipboard"),
                   QApplication::clipboard()->text().contains("The issue:\n") && QApplication::clipboard()->text().contains("this is not a failed install")
                       && QApplication::clipboard()->text().contains("glib-compile-schemas: cannot write"));
        }
        // And the same install when the caches are rebuilt: the page says what was done, and nothing else.
        {
            before = changesAsked().size();
            window.askInstall("printing");
            goOn(window, "Install");
            waitUntilIdle(window, 30);
            settle(200);
            const QString said = window.runPage()->pageText();
            expect(QStringLiteral("the run page lists what was written, and the caches rebuilt (%1)").arg(said.left(120)),
                   window.catalog().installedItem("printing", Place::Other)
                       && said.contains("Written into the system: Printing") && said.contains("The system's caches (menus, icons and fonts) were rebuilt.")
                       && !said.contains("Settings files") && !said.contains("Accounts created") && !said.contains("One step after it did not finish"));
        }
    }

    // ---- A system whose command line says A/B but whose `recipe --system installed` answers as a computer with one
    // root does (`{root, packages}`): what `recipe` answers decides, and no A/B wording is shown.
    {
        Environment env;
        env.unset("RECIPE_GUI_PACKAGE_ROOT");
        env.set("RECIPE_GUI_CMDLINE", scratch + "/locked-cmdline");
        env.set("RECIPE_STANDIN_STATE", scratch + "/plain-state");
        env.unset("RECIPE_STANDIN_LOG");
        env.set("RECIPE_STANDIN_AB", "0");
        env.unset("RECIPE_GUI_SYSTEM_ROOT");
        env.set("RECIPE_GUI_ROOT_HELPER", QFileInfo(recipe).absolutePath() + "/helper-log");
        env.set("HELPER_LOG", scratch + "/plain-helper.log");

        Window window;
        window.resize(1100, 700);
        window.show();
        waitUntilIdle(window, 30);
        expect(QStringLiteral("`--system installed` answered as one root does (`{root, packages}`): read with no A/B"),
               !window.catalog().ab && window.catalog().bootedRoot.isEmpty() && window.catalog().notDone.isEmpty()
                   && window.catalog().installedItem("tree", Place::Me) && !window.catalog().installedItem("anything", Place::Other)
                   && window.catalog().installedItem("gimp", Place::Computer) && !window.catalog().installedItem("gimp", Place::Computer)->inStore);
        qsizetype before = changesAsked().size();
        window.askInstall("htop");
        BeforePage *page = window.beforePage();
        page->choose(placeKey(Place::Computer));
        expect(QStringLiteral("Before says nothing of roots (%1)").arg(abWords(page->pageText())),
               abWords(page->pageText()).isEmpty() && page->pageText().contains("Install htop for everyone?"));
        goOn(window, "Install");
        waitUntilIdle(window, 30);
        const QList<Asked> now = asked(before);
        expect(QStringLiteral("the change still goes through `--system` and the helper"),
               now.size() == 1 && now[0].route == "system" && kinds(now[0].arguments, {"--system", "install", "levitate:htop", "--machine-events"}));
        expect(QStringLiteral("it is installed for everyone, in effect at once, and said so plainly (%1)").arg(window.frame()->statusText()),
               window.catalog().installedItem("htop", Place::Computer) && !window.catalog().installedItem("htop", Place::Computer)->inStore
                   && !window.catalog().installedItem("htop", Place::Other)
                   && window.frame()->statusText().contains("htop was written into the system. It is active at once")
                   && abWords(window.frame()->statusText()).isEmpty());
        for (const char *shown : {"installed", "updates", "storage", "recipe"}) {
            if (QLatin1String(shown) == "installed")
                window.showInstalled("htop", int(Place::Computer));
            else if (QLatin1String(shown) == "updates")
                window.showUpdates();
            else if (QLatin1String(shown) == "storage")
                window.showStorage();
            else
                window.showRecipe("htop");
            settle(100);
            expect(QStringLiteral("the %1 page has no A/B word (%2)").arg(QString::fromLatin1(shown), abWords(wordsOn(window))),
                   abWords(wordsOn(window)).isEmpty());
        }
        save(window, "p12-no-ab-recipe-page");
    }

    // ---- A system with one root and no recab on its command line: "for everyone" is `recipe --system` into the
    // running root, as on A/B; it is active at once, so there is no other root and no A/B word.
    {
        Environment env;
        env.unset("RECIPE_GUI_PACKAGE_ROOT");
        env.set("RECIPE_GUI_CMDLINE", scratch + "/open-cmdline");
        env.set("RECIPE_STANDIN_STATE", scratch + "/open-state");
        env.unset("RECIPE_STANDIN_LOG");
        env.set("RECIPE_STANDIN_AB", "0");
        env.unset("RECIPE_STANDIN_EFFECTS");
        env.set("RECIPE_GUI_SYSTEM_ROOT", scratch + "/system-store");
        env.set("RECIPE_GUI_ROOT_HELPER", QFileInfo(recipe).absolutePath() + "/helper-log");
        env.set("HELPER_LOG", scratch + "/open-helper.log");

        Window window;
        window.resize(1100, 700);
        window.show();
        waitUntilIdle(window, 30);
        expect(QStringLiteral("a system with one root holds the person's store and the system's software (names and versions), and no A/B"),
               !window.places().ab && !window.catalog().ab && window.catalog().twoPlaces && window.catalog().installedItem("gimp", Place::Computer)
                   && !window.catalog().installedItem("gimp", Place::Computer)->inStore
                   && !window.catalog().stores[int(Place::Computer)].has && window.catalog().installedItem("tree", Place::Me)
                   && window.catalog().installedItem("base", Place::Computer) && window.catalog().installedItem("base", Place::Computer)->base);
        qsizetype before = changesAsked().size();
        window.askInstall("htop");
        BeforePage *page = window.beforePage();
        expect(QStringLiteral("for me is the default here too"), page->chosen() == placeKey(Place::Me));
        page->choose(placeKey(Place::Computer));
        expect(QStringLiteral("for everyone, one root: into this computer's system, at once, and no word of roots (%1)").arg(abWords(page->pageText())),
               page->pageText().contains("Written into this computer's system, for everyone. It is active at once; programs that are already running may need a restart.")
                   && abWords(page->pageText()).isEmpty() && !page->pageText().contains("next system"));
        goOn(window, "Install");
        waitUntilIdle(window, 30);
        QList<Asked> now = changesAsked().mid(before);
        expect(QStringLiteral("one root: `recipe --system install NAME` through the helper, and no --package-root"),
               now.size() == 1 && now[0].route == "system"
                   && kinds(now[0].arguments, {"--system", "install", "levitate:htop", "--machine-events"})
                   && QFile(scratch + "/open-helper.log").exists());
        expect(QStringLiteral("it is in effect at once for everyone: it shows as for everyone, and the status line says so (%1)").arg(window.frame()->statusText()),
               window.catalog().installedItem("htop", Place::Computer) && !window.catalog().installedItem("htop", Place::Other)
                   && window.frame()->statusText().contains("htop was written into the system. It is active at once; programs that are already running may need a restart."));
        before = changesAsked().size();

        // Software that no store stands behind: the list names the software and its version only. A program with no recipe still has its receipt page, and can be removed.
        window.showInstalled("fd", int(Place::Computer));
        settle(100);
        expect(QStringLiteral("a program with no recipe, in the system's list, offers its receipt and Remove (%1)").arg(buttonTexts(window).join("|")),
               !window.catalog().recipe("fd") && buttonTexts(window).contains("Its receipt") && buttonTexts(window).contains("Remove…"));
        clickButton(window.installedPage(), "Its receipt");
        settle(150);
        expect(QStringLiteral("its page offers Remove"),
               window.recipePage()->isVisible() && buttonTexts(window).contains("Remove…"));
        save(window, "18d-program-no-recipe-receipt");

        // A system group: no choice of place, one sentence why; then what the change reports.
        window.askInstall("lxqt");
        expect(QStringLiteral("a system group offers no choice and says it becomes part of the system, written at once (%1)").arg(page->choiceNames().join("|")),
               page->choiceNames() == QStringList({"The system"})
                   && page->pageText().contains("It becomes part of the system, so it is written into this computer's root and is active at once; programs that are already running may need a restart.")
                   && !page->pageText().contains("For me") && abWords(page->pageText()).isEmpty());
        save(window, "p1c-before-install-system-group-one-root");
        before = changesAsked().size();
        goOn(window, "Install");
        waitUntilIdle(window, 30);
        settle(200);
        now = changesAsked().mid(before);
        expect(QStringLiteral("it is `recipe --system install lxqt` and nothing else"),
               now.size() == 1 && now[0].route == "system" && kinds(now[0].arguments, {"--system", "install", "levitate:lxqt", "--machine-events"}));
        {
            // What the change reports, as sentences, only what is not empty (desktop-libs is written with it
            // when the root lacks it; the stand-in's root has it already, so it is "already there").
            const QString said = window.runPage()->pageText();
            expect(QStringLiteral("the run page lists what was written and what was already there, without anything empty (%1)").arg(said.left(160)),
                   window.catalog().installedItem("lxqt", Place::Computer) && window.catalog().installedItem("desktop-libs", Place::Computer)
                       && said.contains("Written into the system: LXQt desktop.")
                       && said.contains("Already there, so not written again: Desktop libraries.")
                       && said.contains("Settings files that were changed on this computer were left as they are: /etc/lxqt/lxqt.conf.")
                       && said.contains("The new versions of settings files were written beside them, ending in .recipe-new: /etc/lxqt/lxqt.conf.recipe-new.")
                       && said.contains("Accounts created: user lxqt.") && said.contains("State folders made: /var/lib/lxqt.")
                       && said.contains("The system's caches (menus, icons and fonts) were rebuilt.")
                       && said.contains("Written into the system. It is active at once; programs that are already running may need a restart."));
            save(window, "p14-installed-result");
        }
        window.showRecipe("lxqt");
        settle(100);
        expect(QStringLiteral("the installed group's page says what needs what, and there is no choice of place to offer"),
               wordsOn(window).contains("Where it is") && !wordsOn(window).contains("Not installed")
                   && !buttonTexts(window).contains("Install"));
        save(window, "p1d-system-group-installed");

        // What needs software keeps it: Remove is not offered, and the page says what needs it.
        window.showInstalled("desktop-libs", int(Place::Computer));
        settle(100);
        {
            const QString there = wordsOn(window);
            expect(QStringLiteral("a shared part is listed as coming with the groups that need it, and Remove is not offered (%1)").arg(buttonTexts(window).join("|")),
                   there.contains("comes with LXQt desktop, Plasma desktop") && there.contains("cannot be removed while LXQt desktop, Plasma desktop are installed")
                       && !buttonTexts(window).contains("Remove…"));
            save(window, "p6b-installed-shared-part");
            window.askRemove("desktop-libs", Place::Computer);
            settle(100);
            expect(QStringLiteral("asking anyway removes nothing and says why (%1)").arg(window.frame()->statusText()),
                   !window.removeQuestion() && window.frame()->statusText().contains("is needed by LXQt desktop, Plasma desktop"));
        }
        // The base: one row for the system itself.
        window.showInstalled("base", int(Place::Computer));
        settle(100);
        expect(QStringLiteral("the base is one row, with a way to the release page and no Remove or Update (%1)").arg(buttonTexts(window).join("|")),
               wordsOn(window).contains(", the base system") && buttonTexts(window).contains("Check for updates")
                   && !buttonTexts(window).contains("Remove…") && !buttonTexts(window).filter("Update to").size());
        // Not the last desktop while another stays; the last one says so, and removing it is still allowed.
        window.askRemove("lxqt", Place::Computer);
        settle(200);
        expect(QStringLiteral("while another desktop stays the question says nothing of a last desktop"),
               window.removeQuestion() && !wordsOn(*window.removeQuestion()).contains("last desktop")
                   && wordsOn(*window.removeQuestion()).contains("It is taken out of this computer's system, for everyone, at once."));
        clickButton(window.removeQuestion(), "Cancel");
        before = changesAsked().size();
        window.removeNow("lxqt", Place::Computer);
        waitUntilIdle(window, 30);
        now = changesAsked().mid(before);
        expect(QStringLiteral("removing it is `recipe --system remove lxqt`, and the status line says it is gone at once (%1)").arg(window.frame()->statusText()),
               now.size() == 1 && now[0].route == "system" && kinds(now[0].arguments, {"--system", "remove", "lxqt", "--machine-events"})
                   && !window.catalog().installedItem("lxqt", Place::Computer)
                   && window.frame()->statusText().contains("Removed LXQt desktop from the system. It is gone at once"));
        window.askRemove("plasma", Place::Computer);
        settle(200);
        expect(QStringLiteral("the last desktop of the root says so before the button, and Remove stays offered (%1)").arg(window.removeQuestion() ? wordsOn(*window.removeQuestion()).left(120) : QString()),
               window.removeQuestion() && window.catalog().lastDesktop("plasma")
                   && wordsOn(*window.removeQuestion()).contains("This is the last desktop in this computer. After it is removed the computer starts to a text login.")
                   && wordsOn(*window.removeQuestion()).contains("recipe --system install NAME")
                   && buttonTexts(*window.removeQuestion()).contains("Remove"));
        if (window.removeQuestion())
            save(*window.removeQuestion(), "p4d-remove-last-desktop-one-root");
        clickButton(window.removeQuestion(), "Cancel");

        window.showInstalled("htop", int(Place::Computer));
        expect(QStringLiteral("Installed on one root has no A/B word (%1)").arg(abWords(wordsOn(window))), abWords(wordsOn(window)).isEmpty());
        save(window, "p6-installed-one-root");

        // Installed, but the caches could not be rebuilt: the software is in, and the page says so.
        {
            Environment effects;
            effects.set("RECIPE_STANDIN_EFFECTS", "failed");
            window.askInstall("printing");
            goOn(window, "Install");
            waitUntilIdle(window, 30);
            settle(200);
            const QString said = window.runPage()->pageText();
            const QString on = said;
            expect(QStringLiteral("only the caches failed: it is installed, the page says so with Recipe's error and Copy for agent, and offers no Try again (%1)").arg(on.left(100)),
                   window.catalog().installedItem("printing", Place::Computer) && window.runPage()->isVisible()
                       && said.contains("Printing is installed, but one step after it did not finish")
                       && said.contains("The next system change tries again.") && said.contains("glib-compile-schemas: cannot write")
                       && on.contains("Copy for agent") && !on.contains("Try again") && !said.contains("Stopped at this step")
                       && !said.contains("Written into this computer's root"));
            save(window, "p13b-installed-caches-failed-one-root");
        }
    }

    // ---- A system that is not one a recipe is made for (ABOUT.systems): the same one line, and the recipe can still be read.
    {
        Environment env;
        env.unset("RECIPE_GUI_PACKAGE_ROOT");
        env.set("RECIPE_GUI_CMDLINE", scratch + "/open-cmdline");
        env.set("RECIPE_STANDIN_STATE", scratch + "/other-os-state");
        env.unset("RECIPE_STANDIN_LOG");
        env.set("RECIPE_STANDIN_AB", "0");
        env.unset("RECIPE_STANDIN_EFFECTS");
        env.set("RECIPE_STANDIN_HOST_OS", "acornos");
        env.set("RECIPE_GUI_ROOT_HELPER", QFileInfo(recipe).absolutePath() + "/helper-log");
        env.set("HELPER_LOG", scratch + "/other-os-helper.log");

        Window window;
        window.resize(1100, 700);
        window.show();
        waitUntilIdle(window, 30);
        const QString line = window.catalog().unavailableLine("steam");
        expect(QStringLiteral("on another system steam is not available for ABOUT.systems, and one line says why, without the way to force it (%1)").arg(line),
               !window.catalog().unavailable("steam").isEmpty() && window.catalog().unavailable("steam").contains("To install it anyway")
                   && line == "steam: it is made for the systems levitateos (os-release ID); this system is acornos (ID=acornos), which it does not list."
                   && !window.catalog().unavailable("plasma").isEmpty() && window.catalog().unavailable("lxqt").isEmpty());
        window.showBrowse();
        window.browsePage()->searchBox()->setText("steam");
        settle(150);
        expect(QStringLiteral("Browse says it in a line and offers no Install (%1)").arg(buttonTexts(window).join("|")),
               wordsOn(window).contains(line) && wordsOn(window).contains("Not available") && !buttonTexts(window).contains("Install"));
        window.browsePage()->searchBox()->clear();
        window.showRecipe("steam");
        settle(100);
        expect(QStringLiteral("its page says it, offers Read it and no Install (%1)").arg(buttonTexts(window).join("|")),
               wordsOn(window).contains("Not available on this system. " + line) && buttonTexts(window).contains("Read it")
                   && !buttonTexts(window).contains("Install"));
        save(window, "a5-unavailable-systems");
        const qsizetype before = changesAsked().size();
        window.askInstall("plasma");
        settle(100);
        expect(QStringLiteral("asking anyway starts nothing and says why"),
               !window.beforePage()->isVisible() && changesAsked().size() == before
                   && window.frame()->statusText().contains("is not available on this system"));
    }
}


// The program page in three states: not installed, installed, and installed with
// an update.
void programPages(Window &window, const Save &save, const Expect &expect)
{
    auto buttonsOn = [&](QWidget &inside) {
        QStringList buttons;
        for (const QPushButton *button : inside.findChildren<QPushButton *>())
            if (button->isVisible())
                buttons << button->text();
        return buttons;
    };
    const Catalog &catalog = window.catalog();

    // Not installed: what it is, what it may use, where it is from, and Install.
    expect(QStringLiteral("htop is not installed here (its install was stopped)"), !catalog.installedItem("htop"));
    window.showRecipe("htop");
    settle(100);
    QString words = wordsOn(window);
    QStringList buttons = buttonsOn(window);
    expect(QStringLiteral("not installed: it says so and offers Install, nothing to remove (%1)").arg(buttons.join("|")),
           words.contains("Not installed") && buttons.contains("Install") && !buttons.contains("Remove…"));
    expect(QStringLiteral("it says what it adds, what it may use, where it is from and offers the recipe"),
           words.contains("It adds") && words.contains("the program htop") && words.contains("What it may use")
               && words.contains("github.com (port 443)") && words.contains("The recipe") && buttons.contains("Read it")
               && words.contains("from "));
    save(window, "15a-program-not-installed");

    // Installed, nothing newer (Element, updated earlier in the story).
    const InstalledItem *mail = catalog.installedItem("element");
    expect(QStringLiteral("element is installed and has no update"), mail && !catalog.update("element"));
    window.showRecipe("element");
    settle(100);
    words = wordsOn(window);
    buttons = buttonsOn(window);
    expect(QStringLiteral("installed: its version, Open and Remove; nothing to install again (%1)").arg(buttons.join("|")),
           mail && words.contains("Version " + mail->version) && buttons.contains("Open")
               && buttons.contains("Remove…") && !buttons.contains("Install") && words.contains("Where it is"));
    save(window, "15b-program-installed");

    // Installed with an update.
    const InstalledItem *gimp = catalog.installedItem("gimp");
    const UpdateItem *newer = catalog.update("gimp");
    expect(QStringLiteral("gimp has an update"), gimp && newer);
    window.showRecipe("gimp");
    settle(100);
    words = wordsOn(window);
    buttons = buttonsOn(window);
    expect(QStringLiteral("installed with an update: Update to the new version and Remove (%1)").arg(buttons.join("|")),
           gimp && newer && buttons.contains("Update to " + newer->to) && buttons.contains("Remove…")
               && words.contains(newer->to + " is ready."));
    save(window, "15c-program-installed-with-update");
}

// Storage: what the stand-in says, and what was done so far.
void storageBefore(Window &window, const Save &save, const Expect &expect)
{
    const QJsonObject said = answerOf({"storage", "--json"});
    const qint64 active = said["active_bytes"].toInteger();
    window.showStorage();
    settle(100);
    const QString words = wordsOn(window);
    expect(QStringLiteral("Storage is marked in the rail"), window.frame()->selected() == "storage");
    expect(QStringLiteral("Storage says what is installed takes, as recipe reports it (%1)").arg(bytesText(active)),
           words.contains(QStringLiteral("Installed software takes %1").arg(bytesText(active))));
    expect(QStringLiteral("and says what recipe does not report"), words.contains("Recipe reports no sizes for downloads or builds"));
    // The history: what the story did to this store so far, newest first.
    const QJsonArray history = answerOf({"history", "--json"})["history"].toArray();
    expect(QStringLiteral("recipe's history has the story's changes (%1)").arg(history.size()), history.size() >= 3);
    const QStringList lines = window.storagePage()->historyLines();
    expect(QStringLiteral("the page lists as many as recipe reports (%1)").arg(lines.size()), lines.size() == history.size());
    expect(QStringLiteral("newest first: the update of Element, then the install of Krita (%1)").arg(lines.join(" | ")),
           lines.size() >= 2 && lines[0] == "Updated Element from 1.12.3 to 1.12.4, from levitate"
               && lines[1] == "Installed Krita 5.2.13, from levitate");
    save(window, "15d-storage");
}

// What the audit of the window found and the stories did not reach: software whose recipe is gone,
// the page for a name nothing carries, the remove question, a short change with a page of its own
// (running, stopped, not finished, tried again), closing the window while something runs. Pictures
// `17*` and `18*`, `19*`; "Open" for the person's own store is in the places pass (`p8`, `p9`).
void gapShots(Window &window, const Save &save, const Expect &expect)
{
    // ---- Software whose recipe is gone: what its receipt says, and what still works without one.
    expect(QStringLiteral("fd is installed and no recipe for it is in the collection or the sources"),
           window.catalog().installedItem("fd") && !window.catalog().recipe("fd"));
    window.showInstalled("fd");
    settle(100);
    QString words = wordsOn(window);
    QStringList buttons = buttonTexts(window);
    expect(QStringLiteral("Installed says its recipe is gone, what the receipt lists and where it came from, and offers what works (%1)").arg(buttons.join("|")),
           words.contains("Its recipe is gone: no recipe for it is in the collection or the sources any more.")
               && words.contains("Its receipt lists 2 files.")
               && words.contains("It came from the source oldmirror, which is not among the sources any more.")
               && buttons.contains("Its receipt") && buttons.contains("Remove…")
               && !buttons.contains("Open") && !buttons.contains("Install"));
    save(window, "18a-installed-no-recipe");
    clickButton(window.installedPage(), "Its receipt");
    settle(150);
    words = wordsOn(window);
    buttons = buttonTexts(window);
    expect(QStringLiteral("its page shows the receipt: name, version, files, source and commit, the recipe file that is not there (%1)").arg(buttons.join("|")),
           window.recipePage()->isVisible() && words.contains("Its recipe is gone") && words.contains("What its receipt says")
               && words.contains("bin/fd") && words.contains("share/man/man1/fd.1") && words.contains("2 files")
               && words.contains("The source oldmirror, which is not among the sources any more, at commit e41c7a0")
               && words.contains("/usr/share/recipe/recipes/fd.rhai") && words.contains("The file is not there any more."));
    expect(QStringLiteral("and offers remove, never install or a recipe to read"),
           buttons.contains("Remove…") && !buttons.contains("Install") && !buttons.contains("Read it"));
    save(window, "18b-program-no-recipe");
    // The back link leads to what is installed.
    for (ClickLabel *label : window.recipePage()->findChildren<ClickLabel *>())
        if (label->text() == QStringLiteral("‹ Installed"))
            emit label->clicked();
    settle(100);
    expect(QStringLiteral("the page's back link returns to Installed"), window.installedPage()->isVisible());
    expect(QStringLiteral("Updates says recipe could not look it up"),
           window.catalog().updateProblems.join("|").contains("fd: no recipe for it is in the collection or the sources any more"));

    // ---- A name no recipe has and nothing installed carries.
    window.showRecipe("no-such-recipe");
    settle(100);
    words = wordsOn(window);
    expect(QStringLiteral("its page says there is no recipe by that name, why it might be so, and leads back"),
           words.contains("No recipe named no-such-recipe") && words.contains("No source offers a recipe by that name, and nothing installed carries it.")
               && words.contains("‹ All software"));
    save(window, "18c-no-recipe-named");

    // ---- The remove question, then the change with a page of its own.
    const int removed = timesAsked({"remove", "fd"});
    window.showInstalled("fd");
    settle(100);
    clickButton(window.installedPage(), "Remove…");
    settle(200);
    QPointer<RemoveDialog> question = window.removeQuestion();
    expect(QStringLiteral("Remove… asks first, in a window of its own; nothing has run"),
           question && question->isVisible() && timesAsked({"remove", "fd"}) == removed);
    if (question) {
        const QString asked = wordsOn(*question);
        expect(QStringLiteral("for software with no recipe it still says what happens (%1)").arg(asked.left(80)),
               asked.contains("Remove fd 10.2.0?") && asked.contains("It is no longer installed.")
                   && asked.contains("What it wrote in your home folder stays."));
        save(*question, "17a-remove-question");
        clickButton(question, "Cancel");
    }
    settle(200);
    expect(QStringLiteral("Cancel removes nothing and closes the question"),
           timesAsked({"remove", "fd"}) == removed && window.catalog().installedItem("fd") && (!question || !question->isVisible()));

    qputenv("RECIPE_STANDIN_STOP_AT", "fd:remove");
    clickButton(window.installedPage(), "Remove…");
    settle(200);
    clickButton(window.removeQuestion(), "Remove");
    waitFor([&] { return timesAsked({"remove", "fd"}) > removed; }, 30);
    settle(300);
    expect(QStringLiteral("Remove ran `recipe remove fd`, and the change has a page: what it does, Stop, and the status line says what runs (%1; %2)")
               .arg(buttonTexts(window).join("|"), window.frame()->statusText()),
           timesAsked({"remove", "fd"}) == removed + 1 && window.changePage()->isVisible() && window.busy()
               && wordsOn(window).contains("Removing fd") && buttonTexts(window).contains("Stop")
               && wordsOn(window).contains("Stopping it before it ends leaves fd installed. You can remove it again from Installed.")
               && window.frame()->statusText().contains("Removing fd"));
    save(window, "17b-change-running");
    clickButton(window.changePage(), "Stop");
    waitUntilIdle(window, 30);
    words = wordsOn(window);
    buttons = buttonTexts(window);
    expect(QStringLiteral("stopped: the page says so, that fd 10.2.0 is still installed, and offers Try again and Back (%1)").arg(buttons.join("|")),
           words.contains("You stopped it") && words.contains("fd 10.2.0 is still installed.") && buttons.contains("Try again")
               && buttons.contains("Back") && !buttons.contains("Stop") && window.catalog().installedItem("fd") && !window.busy());
    save(window, "17c-change-stopped");

    qunsetenv("RECIPE_STANDIN_STOP_AT");
    qputenv("RECIPE_STANDIN_FAIL", "remove:fd");
    clickButton(window.changePage(), "Try again");
    waitUntilIdle(window, 30);
    words = wordsOn(window);
    buttons = buttonTexts(window);
    expect(QStringLiteral("not finished: the headline, recipe's own message, the command's own output, what is true, and the way on (%1)").arg(buttons.join("|")),
           timesAsked({"remove", "fd"}) == removed + 2 && window.changePage()->isVisible()
               && words.contains("Removing fd did not finish")
               && words.contains("cannot switch the active profile of") && words.contains("Permission denied")
               && words.contains("fd 10.2.0 is still installed.")
               && buttons.contains("Try again") && buttons.contains("Back") && window.catalog().installedItem("fd") && !window.busy());
    save(window, "17d-change-failed");
    qunsetenv("RECIPE_STANDIN_FAIL");
    clickButton(window.changePage(), "Try again");
    waitUntilIdle(window, 30);
    expect(QStringLiteral("Try again ran the same command once more and it worked: fd is gone, Installed is drawn again, the status line says so"),
           timesAsked({"remove", "fd"}) == removed + 3 && !window.catalog().installedItem("fd") && window.installedPage()->isVisible()
               && window.frame()->statusText().contains("Removed fd"));
    window.showStorage();
    settle(100);
    expect(QStringLiteral("and the history has it, on top"), window.storagePage()->historyLines().value(0) == "Removed fd 10.2.0");

    // A failed change that is not a package's: the lists could not be fetched.
    qputenv("RECIPE_STANDIN_FAIL", "refresh");
    window.showUpdates();
    settle(100);
    clickButton(window.updatesPage(), "Check for updates");
    waitUntilIdle(window, 30);
    words = wordsOn(window);
    expect(QStringLiteral("fetching that did not finish says so, with recipe's own words (%1)").arg(words.left(60)),
           window.changePage()->isVisible() && words.contains("Fetching the lists did not finish")
               && words.contains("could not fetch the list of levitate: connection timed out")
               && words.contains("The recipes are as they were when they were last read."));
    save(window, "17e-fetch-failed");
    qunsetenv("RECIPE_STANDIN_FAIL");
    clickButton(window.changePage(), "Back");
    waitUntilIdle(window, 30);
    expect(QStringLiteral("Back returns to the page the person was on"), window.updatesPage()->isVisible());

    // ---- Closing the window while something runs: asked first, with what is true.
    {
        qputenv("RECIPE_STANDIN_STOP_AT", "htop:build");
        const int installs = timesAsked({"install", "levitate:htop"});
        Window other;
        other.show();
        waitUntilIdle(other, 30);
        other.startInstall("htop", Place::Computer);
        waitFor([&] { return timesAsked({"install", "levitate:htop"}) > installs; }, 30);
        settle(500);
        expect(QStringLiteral("an install runs, stopped in the middle of its build"),
               processesRunning("install levitate:htop --machine-events") == 1 && other.busy());
        other.close();
        settle(300);
        QPointer<QMessageBox> box = other.closeQuestion();
        expect(QStringLiteral("closing the window asks first, and the window stays"), box && box->isVisible() && other.isVisible());
        if (box) {
            const QString asked = wordsOn(*box);
            expect(QStringLiteral("it says what stops, what is kept, that nothing was changed and that it can be started again (%1)").arg(asked.left(60)),
                   asked.contains("Installing htop is still running.") && asked.contains("If you close the window now it is stopped.")
                       && asked.contains("What is already fetched and built is kept.") && asked.contains("Nothing on this computer is changed.")
                       && asked.contains("You can start it again from htop's page whenever you like.")
                       && buttonTexts(*box).contains("Stop it and close") && buttonTexts(*box).contains("Keep the window open"));
            save(*box, "19a-close-install");
            clickButton(box, "Keep the window open");
        }
        settle(300);
        expect(QStringLiteral("keeping the window open leaves the install running"),
               other.isVisible() && other.busy() && processesRunning("install levitate:htop --machine-events") == 1);
        other.close();
        settle(300);
        box = other.closeQuestion();
        expect(QStringLiteral("closing again asks again"), box && box->isVisible());
        if (box)
            clickButton(box, "Stop it and close");
        waitFor([&] { return !other.isVisible(); }, 20);
        settle(200);
        expect(QStringLiteral("stopping it ended the install, and the window closed"),
               !other.isVisible() && processesRunning("install levitate:htop --machine-events") == 0);
    }
    {
        qputenv("RECIPE_STANDIN_STOP_AT", "thunderbird:remove");
        const int removes = timesAsked({"remove", "thunderbird"});
        Window other;
        other.show();
        waitUntilIdle(other, 30);
        const QString shown = other.catalog().recipe("thunderbird") ? other.catalog().recipe("thunderbird")->shown() : QStringLiteral("thunderbird");
        other.removeNow("thunderbird", Place::Computer);
        waitFor([&] { return timesAsked({"remove", "thunderbird"}) > removes; }, 30);
        settle(400);
        other.close();
        settle(300);
        QPointer<QMessageBox> box = other.closeQuestion();
        expect(QStringLiteral("closing during a remove asks too, and says what stopping it leaves"),
               box && box->isVisible() && wordsOn(*box).contains(QStringLiteral("Removing %1 is still running.").arg(shown))
                   && wordsOn(*box).contains(QStringLiteral("Stopping it before it ends leaves %1 installed. You can remove it again from Installed.").arg(shown)));
        if (box) {
            save(*box, "19b-close-remove");
            clickButton(box, "Stop it and close");
        }
        waitFor([&] { return !other.isVisible(); }, 20);
        settle(200);
        bool still = false;
        for (const QJsonValue &each : answerOf({"installed", "--json"})["packages"].toArray())
            still = still || each.toObject()["name"] == "thunderbird";
        expect(QStringLiteral("it stopped and the window closed; Thunderbird is still installed"),
               !other.isVisible() && still && processesRunning("remove thunderbird --machine-events") == 0);
        qunsetenv("RECIPE_STANDIN_STOP_AT");
    }
    window.showBrowse();
    settle(100);
}

// How a run ends. The recipe's last event ("done" or "failed") ends it on the page at once: the
// window must not keep saying "Installing…" with a running bar and a Stop button until the command
// has exited, and until the lists have been read again. Both are slowed here: the stand-in lingers
// after its last event (as a recipe that tears its sandbox down slowly does), and its reads are slow.
// Pictures `l1`-`l3`.
void lagShots(Window &window, const Save &save, const Expect &expect)
{
    window.showBrowse();
    settle(100);
    Environment env;
    env.set("RECIPE_STANDIN_SPEED", "0");
    env.set("RECIPE_STANDIN_LINGER", "5");
    env.set("RECIPE_STANDIN_SLOW_READS", "1");
    expect(QStringLiteral("claude-code is not installed yet"), !window.catalog().installedItem("claude-code"));
    window.startInstall("claude-code", Place::Computer);
    QElapsedTimer clock;
    clock.start();
    // The page says the run is over as soon as the recipe's last event came.
    waitFor([&] { return window.runPage()->stateWords().startsWith("Done"); }, 30);
    const qint64 over = clock.elapsed();
    const bool alive = processesRunning(QStringLiteral("install levitate:claude-code"));
    const QString words = wordsOn(window);
    expect(QStringLiteral("the page showed the end after %1 ms, well before the command exited (5000 ms of lingering)").arg(over),
           over < 3000 && alive);
    expect(QStringLiteral("the command was still alive then"), alive);
    expect(QStringLiteral("no running bar, no Stop, no \"step 3 of 3\" any more (%1)").arg(buttonTexts(window).join("|")),
           !window.runPage()->showsRunning() && !buttonTexts(window).contains("Stop")
               && !words.contains("step 3 of 3") && !window.frame()->statusText().contains("step"));
    expect(QStringLiteral("it says what happened and what comes next (%1)").arg(window.runPage()->stateWords()),
           words.contains("Installed Claude Code") && window.runPage()->stateWords() == "Done. Reading what is installed now…");
    expect(QStringLiteral("every step check is ticked, none still running"),
           !words.contains("Fetching Claude Code") && !words.contains("Building Claude Code"));
    expect(QStringLiteral("the window said it was installed, while the lists are still being read (busy: %1)").arg(window.busy()),
           window.frame()->statusText().contains("Installed Claude Code") && window.busy());
    expect(QStringLiteral("the output is the run's own, up to the end"),
           window.runPage()->findChild<QPlainTextEdit *>() && window.runPage()->findChild<QPlainTextEdit *>()->toPlainText().contains("recipe: diagnostics:"));
    save(window, "l1-run-over-command-lingering");
    waitUntilIdle(window, 60);
    expect(QStringLiteral("then the lists were read, and the program's page shows it installed"),
           window.catalog().installedItem("claude-code") && window.recipePage()->isVisible()
               && buttonTexts(window).contains("Remove…"));
    save(window, "l2-installed-after-reading");
    waitFor([] { return !processesRunning(QStringLiteral("install levitate:claude-code")); }, 20);

    // A failure ends the page the same way: the facts and "Copy for agent" are there while the command lingers.
    env.set("RECIPE_STANDIN_REFUSE_NEW", "onlyoffice");
    window.startInstall("onlyoffice", Place::Computer);
    waitFor([&] { return buttonTexts(window).contains("Copy for agent"); }, 30);
    expect(QStringLiteral("a failed run shows its end at once too: no Stop, the way on (%1)").arg(buttonTexts(window).join("|")),
           buttonTexts(window).contains("Try again") && !buttonTexts(window).contains("Stop")
               && processesRunning(QStringLiteral("install levitate:onlyoffice")));
    save(window, "l3-failed-command-lingering");
    waitUntilIdle(window, 60);
    waitFor([] { return !processesRunning(QStringLiteral("install levitate:onlyoffice")); }, 20);
    window.showBrowse();
    settle(100);
}

// What `recipe info` says the file is, asked of the same stand-in and store the window uses.
QString infoFile(const QString &qualified)
{
    QProcess run;
    run.start(qEnvironmentVariable("RECIPE_GUI_RECIPE"),
              {"info", qualified, "--package-root", qEnvironmentVariable("RECIPE_GUI_PACKAGE_ROOT")});
    run.waitForFinished(60000);
    const QRegularExpressionMatch found = QRegularExpression(QStringLiteral("^Recipe:\\s+(\\S.*?)\\s*$"), QRegularExpression::MultilineOption)
                                              .match(QString::fromUtf8(run.readAllStandardOutput()));
    return found.hasMatch() ? found.captured(1) : QString();
}

// The recipe's file, read-only, with Rhai highlighting and where it came from. Pictures `v1`-`v3`.
void sourceShots(Window &window, const Save &save, const Expect &expect)
{
    window.showRecipe("htop");
    settle(100);
    expect(QStringLiteral("the recipe's page offers to read it"), buttonTexts(window).contains("Read it"));
    clickButton(window.recipePage(), "Read it");
    waitFor([&] { return window.sourceViewer() && window.sourceViewer()->isVisible(); }, 15);
    SourceViewer *viewer = window.sourceViewer();
    expect(QStringLiteral("a viewer opened"), viewer && viewer->isVisible());
    if (!viewer)
        return;
    const QString real = infoFile(window.catalog().qualified("htop"));
    expect(QStringLiteral("it names the file `recipe info` names (%1)").arg(viewer->pathWords()),
           !real.isEmpty() && viewer->pathWords() == real && viewer->pathWords().endsWith("/htop.rhai"));
    QFile file(real);
    const bool opened = file.open(QIODevice::ReadOnly);
    expect(QStringLiteral("it shows that file's own text, whole"), opened && viewer->code() == QString::fromUtf8(file.readAll()));
    expect(QStringLiteral("it is read-only, and has no way to save or run anything (%1)").arg(buttonTexts(*viewer).join("|")),
           viewer->readOnly() && buttonTexts(*viewer) == QStringList({"Close"}));
    expect(QStringLiteral("it says which source it is from, and where (%1)").arg(viewer->fromWords()),
           viewer->fromWords().contains("LevitateOS recipes") && viewer->fromWords().contains("levitate")
               && viewer->fromWords().contains("https://raw.githubusercontent.com/LevitateOS/recipes/"));
    expect(QStringLiteral("Rhai is highlighted: keywords (%1)").arg(viewer->highlighted("keyword").join(",")),
           viewer->highlighted("keyword").contains("fn") && viewer->highlighted("keyword").contains("let")
               && viewer->highlighted("keyword").contains("if") && viewer->highlighted("keyword").contains("return")
               && viewer->highlighted("keyword").contains("const"));
    expect(QStringLiteral("strings, numbers, calls and both kinds of comment (%1 | %2 | %3 | %4)")
               .arg(viewer->highlighted("string").join(","), viewer->highlighted("number").join(","),
                    viewer->highlighted("call").join(","), viewer->highlighted("comment").join(",")),
           viewer->highlighted("string").contains("\"tool\"") && viewer->highlighted("number").contains("3")
               && viewer->highlighted("call").contains("print") && viewer->highlighted("call").contains("install")
               && viewer->highlighted("comment").contains("// the stand-in's recipe for htop")
               && viewer->highlighted("comment").contains("/* what a recipe says about itself */"));
    expect(QStringLiteral("code in a string is not read as keywords"),
           !viewer->highlighted("keyword").contains("putting"));
    save(*viewer, "v1-source-viewer");
    viewer->close();
    settle(100);

    // The Before step offers the same file.
    window.askInstall("htop");
    settle(100);
    emit window.beforePage()->linkClicked();
    waitFor([&] { return window.sourceViewer() && window.sourceViewer()->isVisible(); }, 15);
    expect(QStringLiteral("the Before step's \"Read the recipe\" opens the same viewer"),
           window.sourceViewer() && window.sourceViewer()->pathWords() == real);
    if (window.sourceViewer())
        window.sourceViewer()->close();
    settle(100);
    clickButton(window.beforePage(), "Cancel");

    // When Recipe does not say where the file is, the viewer says so and shows no code.
    {
        Environment env;
        env.set("RECIPE_STANDIN_INFO", "nofile");
        window.viewSource("htop");
        waitFor([&] { return window.sourceViewer() && window.sourceViewer()->isVisible(); }, 15);
        expect(QStringLiteral("no file named: the viewer says so, and guesses nothing (%1)").arg(window.sourceViewer() ? window.sourceViewer()->problemWords() : QString()),
               window.sourceViewer() && window.sourceViewer()->problemWords().contains("did not say which file it is")
                   && window.sourceViewer()->code().isEmpty() && window.sourceViewer()->pathWords().isEmpty());
        if (window.sourceViewer()) {
            save(*window.sourceViewer(), "v2-source-no-file");
            window.sourceViewer()->close();
        }
    }
    window.viewFile("/nonexistent/missing.rhai", QString());
    expect(QStringLiteral("a named file that cannot be read says why"),
           window.sourceViewer() && window.sourceViewer()->problemWords().contains("could not be read"));
    if (window.sourceViewer())
        window.sourceViewer()->close();
    settle(100);
    window.showBrowse();
}

// "Use your own recipes, and your own agent": the words on Sources, and the flow that ends with the
// folder added and the window saying it reads recipes from there. Pictures `o1`-`o2`.
void ownRecipesShots(Window &window, const QString &folder, const Save &save, const Expect &expect)
{
    window.showSources();
    settle(100);
    const QString words = wordsOn(window);
    expect(QStringLiteral("Sources teaches: the recipes root, a folder of one's own, adding it, and where it ends"),
           words.contains("Use your own recipes, and your own agent") && words.contains("recipes root")
               && words.contains("one .rhai file for each") && words.contains("recipe source add --path ~/my-recipes")
               && words.contains("recipe source list") && words.contains("recipe source remove NAME")
               && words.contains("Browse shows them under it") && buttonTexts(window).contains("Add my recipe folder…"));
    expect(QStringLiteral("it names the folder Recipe reads now, so an agent can be pointed at it"),
           words.contains("The folders of recipes Recipe reads now: "));
    expect(QStringLiteral("it says nothing about an agent inside this window, or a choice of agents"),
           !words.contains("Ask an agent") && !words.contains("Choose an agent"));
    save(window, "o1-sources-own-recipes");

    const QString mine = QDir(folder).filePath("my-recipes");
    QDir().mkpath(mine);
    const qsizetype before = changesAsked().size();
    window.addSource(mine);
    waitUntilIdle(window, 30);
    expect(QStringLiteral("`recipe source add --path FOLDER` was asked"),
           timesAsked({"source", "add", "--path", mine}) == 1 && loggedCommands().size() > before);
    expect(QStringLiteral("the folder is a source now, and its recipes are offered"),
           window.catalog().origin("my-recipes") && window.catalog().countFrom("my-recipes") == 2
               && window.catalog().recipe("zellij"));
    settle(150);
    expect(QStringLiteral("Sources shows it, and the window says it reads recipes from there (%1)").arg(window.frame()->statusText()),
           window.frame()->selected() == "sources" && wordsOn(window).contains(mine)
               && window.frame()->statusText().contains("Recipe now reads recipes from my-recipes (2 recipes)."));
    save(window, "o2-sources-folder-added");
    window.removeSource("my-recipes", false);
    waitUntilIdle(window, 30);
    expect(QStringLiteral("and it can be removed again"), !window.catalog().origin("my-recipes"));
    window.showBrowse();
}

// Per-OS availability and the proprietary flag. A recipe whose `ABOUT.libc` this system's C library cannot
// run is not offered, with the reason in one line; a recipe whose licence is `proprietary …` shows that flag
// before the install. Pictures `a1`-`a4`.
void availabilityShots(Window &window, const Save &save, const Expect &expect)
{
    // The flag, before anything is installed (a glibc system).
    expect(QStringLiteral("this system says glibc, and steam is installable"),
           window.catalog().hostLibc == "glibc" && window.catalog().unavailable("steam").isEmpty());
    window.showBrowse();
    window.browsePage()->searchBox()->setText("steam");
    settle(150);
    expect(QStringLiteral("Browse flags steam as proprietary, with Install (%1)").arg(buttonTexts(window).join("|")),
           wordsOn(window).contains("Proprietary software") && buttonTexts(window).contains("Install"));
    window.browsePage()->searchBox()->clear();
    window.showRecipe("steam");
    settle(100);
    expect(QStringLiteral("its page shows the flag and the licence before Install"),
           wordsOn(window).contains("Proprietary software. Its licence: proprietary (Steam Subscriber Agreement).")
               && buttonTexts(window).contains("Install"));
    save(window, "a1-proprietary-recipe-page");
    window.askInstall("steam");
    settle(100);
    BeforePage *page = window.beforePage();
    expect(QStringLiteral("the Before step says it first, in What happens (%1)").arg(page->pageText().left(120).replace('\n', '|')),
           page->isVisible() && page->pageText().contains("Steam is proprietary software. Its licence: proprietary (Steam Subscriber Agreement).")
               && page->pageText().indexOf("is proprietary software") < page->pageText().indexOf("is fetched from"));
    save(window, "a2-proprietary-before");
    clickButton(page, "Cancel");
    window.askInstall("htop");
    settle(100);
    expect(QStringLiteral("free software carries no such flag"), !window.beforePage()->pageText().contains("proprietary"));
    clickButton(window.beforePage(), "Cancel");

    // A system whose C library is musl: steam and claude-code declare glibc, htop declares both.
    {
        Environment env;
        env.set("RECIPE_STANDIN_HOST_LIBC", "musl");
        Window other;
        other.resize(1100, 700);
        other.show();
        waitUntilIdle(other, 30);
        const Catalog &catalog = other.catalog();
        const QString why = catalog.unavailable("steam");
        expect(QStringLiteral("on musl: steam is not available, and one line says why (%1)").arg(why),
               catalog.hostLibc == "musl"
                   && why == "steam: its build is linked against glibc; this system's C library is musl, so as packaged its "
                             "program would find no loader. To install it anyway: recipe install --any-libc steam");
        expect(QStringLiteral("claude-code too; htop, which declares glibc or musl, and recipes that declare nothing, are"),
               !catalog.unavailable("claude-code").isEmpty() && catalog.unavailable("htop").isEmpty()
                   && catalog.unavailable("krita").isEmpty());
        other.showBrowse();
        other.browsePage()->searchBox()->setText("steam");
        settle(150);
        QString words = wordsOn(other);
        // Browse and the page say the reason as one line: without the command that installs it anyway.
        const QString line = catalog.unavailableLine("steam");
        expect(QStringLiteral("Browse says why in a line and offers no Install for it (%1)").arg(buttonTexts(other).join("|")),
               line == "steam: its build is linked against glibc; this system's C library is musl, so as packaged its program would find no loader."
                   && words.contains(line) && !words.contains("To install it anyway") && words.contains("Not available")
                   && !buttonTexts(other).contains("Install"));
        save(other, "a3-unavailable-browse");
        other.browsePage()->searchBox()->clear();
        other.showRecipe("steam");
        settle(100);
        words = wordsOn(other);
        expect(QStringLiteral("its page says it is not available on this system, why, and has no Install (%1)").arg(buttonTexts(other).join("|")),
               words.contains("Not available on this system. " + line) && !buttonTexts(other).contains("Install")
                   && words.contains("Proprietary software.") && buttonTexts(other).contains("Read it"));
        save(other, "a4-unavailable-recipe-page");
        const qsizetype before = changesAsked().size();
        other.askInstall("steam");
        settle(100);
        expect(QStringLiteral("asking for it anyway starts nothing and says why (%1)").arg(other.frame()->statusText()),
               !other.beforePage()->isVisible() && changesAsked().size() == before && other.frame()->statusText().contains(line));
        other.askInstall("htop");
        settle(100);
        expect(QStringLiteral("what it can run is still offered"), other.beforePage()->isVisible());
        clickButton(other.beforePage(), "Cancel");
    }
    window.showBrowse();
}

} // namespace

int tryRecipe(Window &window, const QString &name, const QString &folder, bool keep, bool readOnly)
{
    QDir().mkpath(folder);
    auto save = [&folder](QWidget &widget, const QString &title) {
        settle(200);
        return widget.grab().save(QDir(folder).filePath(title + ".png"));
    };
    window.show();
    waitUntilIdle(window, 60);
    bool ok = window.catalog().recipe(name) != nullptr;
    window.showBrowse();
    ok &= save(window, "real-1-browse");
    window.showRecipe(name);
    ok &= save(window, "real-2-recipe");
    // The Before step reads nothing and starts nothing; it is shown in both modes.
    window.askInstall(name);
    ok &= save(window, "real-2b-before");
    if (readOnly) {
        window.showInstalled(name);
        ok &= save(window, "real-5-installed");
        // What `recipe updates` and `recipe storage` said, for the one who runs this.
        QTextStream out(stdout);
        for (const UpdateItem &update : window.catalog().updates)
            out << "update: " << update.name << " " << update.from << " -> " << update.to << ", "
                << (update.changesKnown ? QString::number(update.changes.size()) + " changes in what it may use"
                                        : QStringLiteral("not compared"))
                << "\n";
        for (const QString &problem : window.catalog().updateProblems)
            out << "not looked up: " << problem << "\n";
        out << "installed takes " << window.catalog().activeBytes << " bytes\n";
        if (!window.catalog().updates.isEmpty()) {
            window.showUpdates(name);
            ok &= save(window, "real-7-updates");
        }
        // Storage and the history, against what `recipe storage --json` and `recipe history --json`
        // print for the same store; and the version of every installed package against `installed --json`.
        window.showStorage();
        ok &= save(window, "real-8-storage");
        const QString shown = wordsOn(window);
        const QJsonObject storage = answerOf({"storage", "--json"});
        const QJsonArray history = answerOf({"history", "--json"})["history"].toArray();
        const QStringList lines = window.storagePage()->historyLines();
        auto check = [&](const QString &what, bool holds) {
            out << (holds ? "same: " : "DIFFERENT: ") << what << "\n";
            ok &= holds;
        };
        out << "recipe storage: active " << storage["active_bytes"].toInteger() << "\n";
        check(QStringLiteral("Storage shows what is installed takes %1").arg(bytesText(storage["active_bytes"].toInteger())),
              shown.contains(bytesText(storage["active_bytes"].toInteger())));
        out << "recipe history: " << history.size() << " entries, newest " << history.first().toObject()["what"].toString() << "\n";
        check(QStringLiteral("the history lists %1 entries, newest first (%2)").arg(history.size()).arg(lines.join(" | ")),
              lines.size() == history.size());
        const QJsonArray packages = answerOf({"installed", "--json"})["packages"].toArray();
        for (const QJsonValue &value : packages) {
            const QJsonObject package = value.toObject();
            const InstalledItem *item = window.catalog().installedItem(package["name"].toString());
            out << "recipe installed: " << package["name"].toString() << " " << package["version"].toString()
                << "\n";
            check(QStringLiteral("%1 is at the version `installed --json` says").arg(package["name"].toString()),
                  item && item->version == package["version"].toString());
        }
        return ok ? 0 : 1;
    }

    goOn(window, QStringLiteral("Install"));
    settle(20000);
    if (window.busy())
        ok &= save(window, "real-3-running");
    waitUntilIdle(window, 900);
    ok &= save(window, "real-4-after");
    const bool installed = window.catalog().installedItem(name) != nullptr;
    window.showInstalled(name);
    ok &= save(window, "real-5-installed");
    if (installed && !keep) {
        window.removeNow(name);
        waitUntilIdle(window, 120);
        window.showInstalled();
        ok &= save(window, "real-6-removed");
        ok &= window.catalog().installedItem(name) == nullptr;
    }
    return ok && installed ? 0 : 1;
}

int takeShots(Window &window, const QString &folder)
{
    bool ok_ = true; // the guard's check, before the first picture
    QDir().mkpath(folder);
    auto save = [&folder](QWidget &widget, const QString &name) {
        settle(200);
        return widget.grab().save(QDir(folder).filePath(name + ".png"));
    };
    window.show();
    // A picture run that is not told of its stand-ins refuses to start.
    {
        auto refused = [&](const QString &what, bool holds) {
            if (!holds) {
                QTextStream(stdout) << "not as expected: " << what << "\n";
                ok_ = false;
            }
        };
        helpstory::guardRefuses(refused, folder, "RECIPE_GUI_RECIPE");
    }
    // Before recipe has answered: what a person sees while the catalog is read.
    bool ok = ok_ && window.grab().save(QDir(folder).filePath("0-reading.png"));
    waitUntilIdle(window);
    ok &= !window.catalog().recipes.isEmpty();

    QTextStream said(stdout);
    // This window is on one explicit store (RECIPE_GUI_PACKAGE_ROOT): one place, no choice.
    if (!window.places().one) {
        said << "not as expected: this pass needs RECIPE_GUI_PACKAGE_ROOT, see README.md\n";
        ok = false;
    }

    window.showBrowse();
    ok &= save(window, "1-browse");

    window.showRecipe("htop");
    ok &= save(window, "2-recipe");

    // Before an install: what will happen, with no choice because there is one place.
    window.askInstall("htop");
    {
        BeforePage *page = window.beforePage();
        auto hold = [&](const QString &what, bool holds) {
            if (!holds) {
                said << "not as expected: " << what << "\n";
                said.flush();
                ok = false;
            }
        };
        hold("the Before page says nothing has started", page->isVisible()
                 && page->pageText().contains("Nothing has started. This is what will happen if you go on."));
        hold("one explicit store is one place, so no choice: a statement", page->choiceNames().size() == 1
                 && !page->pageText().contains("For me") && page->pageText().contains("It is the only place this window changes."));
        hold("it says what happens from the catalog: fetched from, built, in the sandbox, what it needs",
             page->pageText().contains("htop 3.5.3 is fetched from github.com and release-assets.githubusercontent.com and built on this computer, inside the sandbox.")
                 && page->pageText().contains("Needed only to build it: ncurses."));
        hold("it lists what the recipe may use and that the sandbox refuses the rest",
             page->pageText().contains("github.com (port 443), release-assets.githubusercontent.com (port 443). Nothing else.")
                 && page->pageText().contains("Memory") && page->pageText().contains("Time")
                 && page->pageText().contains("The sandbox refuses anything else."));
        hold("it does not say how much is downloaded or how much disk is used: recipe does not report either",
             !page->pageText().contains("is downloaded") && !page->pageText().contains("is used"));
        hold("no running system to mention: this is not a locked computer", !page->pageText().contains("The running root is not touched."));
        ok &= save(window, "2b-before-install");
        // Cancel comes back to where the person was; nothing was run.
        const qsizetype asked = changesAsked().size();
        clickButton(page, "Cancel");
        hold("Cancel starts nothing and comes back to the recipe", changesAsked().size() == asked && !page->isVisible()
                 && window.frame()->selected() == "browse");
    }
    qputenv("RECIPE_STANDIN_SPEED", "0");
    qputenv("RECIPE_STANDIN_STOP_AT", "htop:build");
    const qsizetype beforeInstall = changesAsked().size();
    window.askInstall("htop");
    goOn(window, QStringLiteral("Install"));
    // Until the stand-in recipe has been asked, and has stopped where it was told to.
    waitFor([&] { return changesAsked().size() > beforeInstall; }, 30);
    settle(300);
    {
        const QList<Asked> now = changesAsked().mid(beforeInstall);
        QStringList want{"install", "levitate:htop", "--machine-events", "--package-root", qEnvironmentVariable("RECIPE_GUI_PACKAGE_ROOT")};
        if (now.size() != 1 || now[0].arguments != want || now[0].route != "store") {
            said << "not as expected: the install asked recipe for " << (now.isEmpty() ? QString() : now[0].arguments.join(' ')) << "\n";
            said.flush();
            ok = false;
        }
    }
    ok &= save(window, "3-installing");
    window.stopRun();
    waitUntilIdle(window);

    qunsetenv("RECIPE_STANDIN_STOP_AT");
    // Before an update: where the software is, and what the newer recipe may use that the old one could not.
    window.askUpdate("element", Place::Computer);
    ok &= save(window, "4b-before-update");
    {
        BeforePage *page = window.beforePage();
        const bool holds = page->pageText().contains("Update Element to 1.12.4?")
                           && page->pageText().contains("Element goes from version 1.12.3 to 1.12.4.")
                           && page->pageText().contains("New: static.element.io (port 443).")
                           && page->pageText().contains("Download") && page->choiceNames().size() == 1;
        if (!holds) {
            said << "not as expected: the update's Before page names the version change and what the recipe may use now\n";
            ok = false;
        }
    }
    goOn(window, QStringLiteral("Update"));
    waitUntilIdle(window);
    ok &= save(window, "4-stopped");
    {
        // The refused connection: recipe reports a numeric address, and the page words it as one.
        const QString words = wordsOn(window);
        QRegularExpression address(QStringLiteral(R"(the address (\d+\.\d+\.\d+\.\d+), port (\d+), which is not on the recipe's list)"));
        const bool numeric = address.match(words).hasMatch();
        if (!numeric || words.contains("cdn.example.net")) {
            said << "not as expected: the stopped run names the address recipe reports, in words, not a host name\n";
            ok = false;
        }
        if (!words.contains("Recipe reports the address the recipe tried to reach, not a host name.")) {
            said << "not as expected: the facts say that the address is not a host name\n";
            ok = false;
        }
        // Where recipe keeps the log is a fact of the stop; the output itself is under "Show the output".
        if (!words.contains("Recipe keeps the log and the retained record of this run in ")
            || words.contains("of the run's output")) {
            said << "not as expected: a stopped update names the log and keeps the output behind its button\n";
            ok = false;
        }
    }

    window.showInstalled("gimp");
    ok &= save(window, "5-installed");

    const Recipe *recipe = window.catalog().recipe("gimp");
    const InstalledItem *item = window.catalog().installedItem("gimp");
    if (recipe && item) {
        RemoveDialog dialog(*recipe, *item, window.catalog().takenOutWords(*item), QString(), &window);
        dialog.show();
        ok &= save(dialog, "6-remove");
        dialog.close();
    }

    window.showUpdates("element");
    ok &= save(window, "7-updates");
    {
        QTextStream said(stdout);
        auto hold = [&](const QString &what, bool holds) {
            if (!holds) {
                said << "not as expected: " << what << "\n";
                said.flush();
                ok = false;
            }
        };
        const UpdateItem *update = window.catalog().update("element");
        hold("the update of Element says what the newer recipe may use that the installed one may not",
             update && update->changesKnown && update->asksMore() && !update->changes.isEmpty()
                 && update->changes.first().added() == QStringList{"static.element.io:443"});
    }

    // A first install the sandbox refuses: the failure offers "Copy for agent", which puts the issue, the facts
    // and the whole output on the clipboard, and nothing else.
    qputenv("RECIPE_STANDIN_REFUSE_NEW", "krita");
    window.startInstall("krita", Place::Computer);
    waitUntilIdle(window);
    {
        const QString facts = wordsOn(window);
        const bool given = facts.contains("Copy for agent")
                           && facts.contains("Recipe keeps the log and the retained record of this run in ")
                           && !facts.contains("The output of the run: ");
        if (!given) {
            said << "not as expected: a stopped install carries Copy for agent and the log's place\n";
            ok = false;
        }
        ok &= save(window, "8a-failure-facts");
        QApplication::clipboard()->clear();
        const qsizetype beforeCopy = changesAsked().size();
        clickButton(window.runPage(), "Copy for agent");
        const QString copied = QApplication::clipboard()->text();
        if (!copied.contains("The issue:\n") || !copied.contains("The facts:\n")
            || !copied.contains("recipe: fetching krita 5.2.13 from")
            || !copied.contains("Recipe keeps the log and the retained record of this run in ")
            || changesAsked().size() != beforeCopy) {
            said << "not as expected: Copy for agent puts the issue, the facts and the output on the clipboard and starts nothing\n";
            ok = false;
        }
    }
    qunsetenv("RECIPE_STANDIN_REFUSE_NEW");
    window.startInstall("krita", Place::Computer);
    waitUntilIdle(window);
    ok &= save(window, "8-installed-again");
    // Element's source has since published a recipe that stays on its list: the update goes through.
    qputenv("RECIPE_STANDIN_FIXED", "element");
    window.startInstall("element", Place::Computer);
    waitUntilIdle(window);

    // Sources: the page, a second source added, its list fetched again, only its recipes shown.
    QTextStream out(stdout);
    auto expect = [&](const QString &what, bool holds) {
        if (!holds) {
            out << "not as expected: " << what << "\n";
            out.flush();
            ok = false;
        }
    };
    window.addSource("https://github.com/acme/recipes");
    waitUntilIdle(window);
    expect("an added source is listed", window.catalog().origin("acme") != nullptr);
    expect("its recipes are offered", window.catalog().recipe("zellij") != nullptr);
    window.fetchSources("acme");
    waitUntilIdle(window);
    window.showSources();
    ok &= save(window, "9-sources");
    window.chooseSource("acme");
    window.showBrowse();
    ok &= save(window, "10-one-source");
    window.chooseSource(QString());

    window.removeSource("acme", false);
    waitUntilIdle(window);
    expect("a removed source is gone", window.catalog().origin("acme") == nullptr);
    window.refresh();
    waitUntilIdle(window);

    window.showInstalled("gimp");
    {
        auto save_ = [&](QWidget &widget, const QString &name) { ok &= save(widget, name); };
        programPages(window, save_, expect);
        storageBefore(window, save_, expect);
    }

    // The frame: the rail.
    auto saved = [&](QWidget &widget, const QString &name) { ok &= save(widget, name); };
    auto held = [&](const QString &what, bool holds) { expect(what, holds); };
    frameShots(window, saved, held);
    gapShots(window, saved, held);
    helpShots(window, saved, held);
    lagShots(window, saved, held);
    sourceShots(window, saved, held);
    ownRecipesShots(window, folder, saved, held);
    availabilityShots(window, saved, held);
    placesShots(folder, saved, held);
    out << (ok ? "all flows ended where they should\n" : "some flows did not end where they should\n");
    return ok ? 0 : 1;
}
