#include "shots.h"

#include "frame.h"
#include "helpstory.h"
#include "look.h"
#include "parts.h"
#include "nextsystem.h"
#include "pages.h"
#include "window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QPushButton>
#include <QTextStream>
#include <QTreeWidget>

#include <csignal>
#include <functional>

namespace {

void waitUntilIdle(Window &window, int seconds = 30)
{
    QElapsedTimer clock;
    clock.start();
    while (window.busy() && clock.elapsed() < seconds * 1000)
        settle(50);
    settle(150);
}

// A change in the next system, made the way a person's own tool would make it: the window has no way to ask
// for one. `which` is "nvidia" or "ssh" (the older stand-in also takes "fix-failed"). The stand-in is the
// program named in RECAB_GUI_RECAB; the real shape needs its preparation first, which it reads back.
void makeChange(Window &window, const char *which, const QString &actor = QStringLiteral("tool"))
{
    const QString recab = qEnvironmentVariable("RECAB_GUI_RECAB");
    auto run = [&](const QStringList &arguments) {
        QProcess process;
        process.start(recab, arguments);
        process.waitForFinished(120000);
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    if (!window.status().real) {
        run({"make-change", QLatin1String(which)});
    } else {
        if (window.status().preparationId.isEmpty()) {
            run({"--json", "prepare"});
            window.refresh();
            waitUntilIdle(window);
        }
        const QStringList cas{"--preparation", window.status().preparationId, "--revision",
                              QString::number(window.status().preparationRevision), "--actor", actor};
        if (QLatin1String(which) == QLatin1String("nvidia"))
            run(QStringList{"--json", "change", "install", "nvidia-driver"} + cas);
        else
            run(QStringList{"--json", "edit", "/etc/ssh/sshd_config"} + cas);
    }
    window.refresh();
    waitUntilIdle(window);
}

// What "Copy for agent" copies on a page: the button is pressed and the clipboard read (empty when the
// page has no such button).
QString copiedFor(QWidget *page)
{
    for (QPushButton *button : page->findChildren<QPushButton *>()) {
        if (button->text() == QLatin1String("Copy for agent") && button->isVisibleTo(page)) {
            QApplication::clipboard()->clear();
            button->click();
            return QApplication::clipboard()->text();
        }
    }
    return {};
}



// ---------------------------------------------------------------- The next system and Getting back
//
// What a page says, for the checks: every label as written, every button as
// "[Words]" or "[Words](off)" when recab's verdict leaves it off.

QString wordsOn(QWidget *page)
{
    QStringList words;
    for (QWidget *child : page->findChildren<QWidget *>()) {
        if (!child->isVisibleTo(page))
            continue;
        if (auto *button = qobject_cast<QPushButton *>(child))
            words << QStringLiteral("[%1]%2").arg(button->text(), button->isEnabled() ? QString() : QStringLiteral("(off)"));
        else if (auto *label = qobject_cast<QLabel *>(child))
            words << label->text();
    }
    return words.join(QLatin1Char('\n'));
}

// Shows one of the three pages and says what it holds; Overview again after.
QString showWords(Window &window, int which)
{
    // As a click on the rail does.
    window.frame()->select(which == 0 ? QStringLiteral("overview") : which == 1 ? QStringLiteral("next")
                                                                                : QStringLiteral("back"));
    if (which == 0)
        window.showMain();
    else if (which == 1)
        window.showNext();
    else
        window.showBack();
    settle(200);
    switch (which) {
    case 0:
        return wordsOn(window.findChild<SystemPage *>());
    case 1:
        return wordsOn(window.findChild<NextPage *>());
    }
    return wordsOn(window.findChild<BackPage *>());
}

// The release page, as a person gets to it from Overview or the next system.
QString releaseWords(Window &window)
{
    window.showRelease();
    settle(200);
    return wordsOn(window.findChild<ReleasePage *>());
}

// The history page once recab has answered: its words, then its rows, one to
// a line with the columns joined by " | ".
QString historyWords(Window &window, QStringList *rows = nullptr)
{
    window.frame()->select(QStringLiteral("history"));
    window.showHistory();
    for (int waited = 0; window.busy() && waited < 1800; ++waited)
        settle(50);
    settle(200);
    auto *page = window.findChild<HistoryPage *>();
    if (rows) {
        rows->clear();
        if (auto *tree = page->findChild<QTreeWidget *>())
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                QStringList cells;
                for (int c = 0; c < tree->columnCount(); ++c)
                    cells << tree->topLevelItem(i)->text(c);
                *rows << cells.join(QStringLiteral(" | "));
            }
    }
    return wordsOn(page);
}

QString overviewWords(Window &window) { return showWords(window, 0); }
QString nextWords(Window &window) { return showWords(window, 1); }
QString backWords(Window &window) { return showWords(window, 2); }

using Save = std::function<void(QWidget &, const QString &)>;
using Expect = std::function<void(const QString &, bool)>;

void clickButton(QWidget *inside, const QString &words)
{
    for (QPushButton *button : inside->findChildren<QPushButton *>())
        if (button->text() == words && button->isVisible())
            button->click();
}

// ---------------------------------------------------------------- The Before page

// What of the system a Cancel must leave as it was.
QString systemMark(Window &window)
{
    const Status &s = window.status();
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(s.state, s.otherRole, s.runningSlot, s.preparationId)
        .arg(s.changes.size())
        .arg(s.update.hasMerge);
}

// Opens the Before page of one action, says what it holds and takes its
// picture. With `cancel` it then presses "Cancel" and checks that nothing was
// started and the system reads as it did; without, the page stays open for
// its button (`pressGo`).
void beforeCheck(Window &window, const Save &save, const Expect &expect, const QString &picture, const QString &what,
                 const std::function<void()> &open, const QStringList &words, const QString &go, bool cancel = true)
{
    const QString mark = systemMark(window);
    open();
    // The page is what the window shows once it has read the system: wait for that, not for a moment.
    BeforePage *page = window.beforePage();
    waitFor([&] { return page->isVisible() && page->pageText().contains("Nothing has started.") && !window.busy(); }, 10);
    settle(300);
    // The window may read the system again (coming back to it does) while the page stands; judge it when it does not.
    for (int waited = 0; window.busy() && waited < 1200; ++waited)
        settle(50);
    const QString text = page->pageText();
    expect(what + ": the Before page is shown and says nothing has started",
           page->isVisible() && text.contains("Nothing has started.") && !window.busy());
    expect(what + ": it has the sections What happens and What runs, Cancel and \"" + go + "\"",
           text.contains("What happens") && text.contains("What runs") && text.contains("Cancel") && text.contains(go));
    for (const QString &word : words)
        expect(what + ": it says \"" + word + "\"", text.contains(word));
    if (!picture.isEmpty())
        save(window, picture);
    if (!cancel)
        return;
    clickButton(page, "Cancel");
    waitFor([&] { return !window.busy(); }, 10);
    settle(300);
    expect(what + ": Cancel leaves everything as it was",
           !page->isVisible() && !window.busy() && systemMark(window) == mark);
}

// Presses the one named button of the page that is open, and waits.
void pressGo(Window &window, const QString &go)
{
    clickButton(window.beforePage(), go);
    waitUntilIdle(window);
}

// ---------------------------------------------------------------- A message or question the window raises

// What a modal message or question said while it was up.
struct Raised {
    bool seen = false;
    QString title, text, informative;
    QStringList buttons, items;
};

// Runs `trigger`, which makes the window raise a modal message (a failed step,
// a decision by hand) or question and wait for the person. While it waits the
// words are read, the picture is taken and the button named is pressed (the
// first one when none is named); a question gets `choose` as its answer.
Raised answering(const QString &folder, const QString &picture, const QString &button, const std::function<void()> &trigger,
                 const QString &choose = QString())
{
    Raised raised;
    QTimer timer;
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal || raised.seen)
            return;
        raised.seen = true;
        raised.title = modal->windowTitle();
        if (!picture.isEmpty())
            modal->grab().save(QDir(folder).filePath(picture + ".png"));
        if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            raised.text = box->text();
            raised.informative = box->informativeText();
            QAbstractButton *press = nullptr;
            for (QAbstractButton *one : box->buttons()) {
                raised.buttons << one->text();
                if (!press && (button.isEmpty() || one->text() == button))
                    press = one;
            }
            if (press)
                press->click();
            else
                box->close();
        } else if (auto *input = qobject_cast<QInputDialog *>(modal)) {
            raised.text = input->labelText();
            raised.items = input->comboBoxItems();
            if (!choose.isEmpty())
                input->setTextValue(choose);
            input->accept();
        } else {
            modal->close();
        }
    });
    timer.start();
    trigger();
    // A step that runs on its own (the restart) raises its message later: wait for it.
    waitFor([&] { return raised.seen; }, 30);
    return raised;
}

QString fileText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// Keeps the words a check judged, next to the pictures.
void keepWords(const QString &folder, const QString &name, const QString &words)
{
    QFile file(QDir(folder).filePath(name));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(words.toUtf8());
}

// A page that is taller than the window scrolls, and its last button can be brought into view: true when
// the page has a scroll area with something to scroll (unless `needsScroll` is false) and the button is wholly in view at the end of it.
bool scrollsToButton(QWidget *page, const QString &button, bool needsScroll = true)
{
    QScrollArea *scroller = page->findChild<QScrollArea *>();
    QPushButton *target = nullptr;
    for (QPushButton *each : page->findChildren<QPushButton *>())
        if (each->text() == button && each->isVisibleTo(page))
            target = each;
    if (!scroller || !target)
        return false;
    QScrollBar *bar = scroller->verticalScrollBar();
    const bool tallerThanTheWindow = bar->maximum() > 0;
    bar->setValue(bar->maximum());
    settle(100);
    const QPoint at = target->mapTo(scroller->viewport(), QPoint(0, 0));
    const bool seen = scroller->viewport()->rect().contains(QRect(at, target->size()));
    bar->setValue(0);
    settle(50);
    return (tallerThanTheWindow || !needsScroll) && seen;
}

// The titles of the boxes a page holds (the recovery list is one).
QStringList groupTitles(QWidget *page)
{
    QStringList titles;
    for (QGroupBox *box : page->findChildren<QGroupBox *>())
        if (box->isVisibleTo(page))
            titles << box->title();
    return titles;
}

// ---------------------------------------------------------------- Help and the first page
//
// The pages the frame owns after the app's own: Help for Recab, and the page a
// person sees the first time. The checks are `../kit/helpstory.*`; this says
// which page of Recab each target is.

void helpShots(Window &window, const QString &folder, const Save &save, const Expect &expect, const QString &prefix)
{
    Q_UNUSED(folder)
    helpstory::Plan plan;
    plan.app = Frame::Recab;
    plan.window = &window;
    plan.frame = window.frame();
    plan.firstEntry = QStringLiteral("overview");
    plan.idle = [&] { waitUntilIdle(window); };
    plan.pageShown = [&](const QString &key) {
        QWidget *page = key == QLatin1String("overview")  ? static_cast<QWidget *>(window.findChild<SystemPage *>())
                        : key == QLatin1String("next")    ? static_cast<QWidget *>(window.findChild<NextPage *>())
                        : key == QLatin1String("history") ? static_cast<QWidget *>(window.findChild<HistoryPage *>())
                        : key == QLatin1String("back")    ? static_cast<QWidget *>(window.findChild<BackPage *>())
                                                          : nullptr;
        return page && page->isVisible();
    };
    helpstory::run(plan, save, expect, prefix);
    window.frame()->choose(QStringLiteral("overview"));
    window.showMain();
    settle(100);
}

} // namespace

// The story needs the stand-in started with RECAB_STANDIN_FAIL_TRIALS=1, so
// the first restart into the next system fails and the second is kept.
int takeShots(Window &window, const QString &folder)
{
    QDir().mkpath(folder);
    QTextStream out(stdout);
    bool ok = true;
    auto save = [&](QWidget &widget, const QString &name) {
        settle(200);
        ok &= widget.grab().save(QDir(folder).filePath(name + ".png"));
    };
    auto expect = [&](const QString &what, bool holds) {
        if (!holds) {
            out << "not as expected: " << what << " (state " << window.status().state << ")\n";
            ok = false;
        }
    };
    window.show();
    // A picture run that is not told of its stand-ins refuses to start.
    helpstory::guardRefuses(expect, folder, "RECAB_GUI_RECAB");
    // Before recab has answered: what a person sees while the state is read.
    ok &= window.grab().save(QDir(folder).filePath("00-reading.png"));
    waitUntilIdle(window);

    expect("starts stable", window.status().state == "stable");
    save(window, "01-stable");

    // The two pages beside Overview, with no next system.
    {
        const QString overview = overviewWords(window);
        expect("Overview names the release to see, and sends to the two pages",
               overview.contains("What to do next") && overview.contains("A newer release is available")
                   && overview.contains("The rest is on:") && overview.contains("Getting back")
                   && !overview.contains("[Go back…]"));
        const QString next = nextWords(window);
        expect("the next system's page says there is none, and offers to install software",
               next.contains("There is no next system yet") && next.contains("[Install software…]") && window.frame()->badge("next") == 0
                   && window.frame()->selected() == "next");
        save(window, "p01-next-none");
        const QString back = backWords(window);
        expect("Getting back offers the way back, and says no restart is set",
               back.contains("[Go back…]") && back.contains("None is set.") && back.contains("[Cancel it](off)")
                   && back.contains("[Keep it](off)") && back.contains("Kept already.")
                   && back.contains("Pick the running system, the one that is kept.")
                   && !back.contains("Slot A") && !back.contains("recab commit(off)"));
        expect("the steps for a desktop that does not start say how to make the choice stay, without names",
               back.contains("recab commit") && back.contains("pick the system from before instead"));
        save(window, "p02-back");
        expect("the strip is not on Overview, nor on the next system's page", !window.frame()->stripShown());
        window.showMain();
        settle(100);
    }

    // The change is stopped half way, which is also how an interruption looks: the tool making it is ended.
    {
        qputenv("RECAB_STANDIN_STOP_AT", "change");
        const QString marker = QDir(qEnvironmentVariable("RECAB_STANDIN_STATE")).filePath("stopped-at-change");
        QFile::remove(marker);
        QProcess tool;
        tool.start(qEnvironmentVariable("RECAB_GUI_RECAB"), {"make-change", "nvidia"});
        waitFor([&] { return QFile::exists(marker); }, 30);
        tool.terminate();
        tool.waitForFinished(10000);
        qunsetenv("RECAB_STANDIN_STOP_AT");
        window.refresh();
        waitUntilIdle(window);
    }
    expect("a stopped change is reported as interrupted", window.status().state == "interrupted");
    save(window, "05-interrupted");
    {
        const QString next = nextWords(window);
        expect("the next system's page says it was interrupted, and offers to continue or discard",
               next.contains("Preparing the next system was interrupted") && next.contains("[Continue where it stopped]")
                   && next.contains("[Discard…]") && !next.contains("[Finish…]"));
        save(window, "p03-next-interrupted");
        const QString back = backWords(window);
        expect("Getting back says the system from before is gone while the next one holds its place",
               back.contains("There is none now: its place holds the next system.") && back.contains("[Go back…](off)"));
        window.showMain();
        settle(100);
    }

    beforeCheck(window, save, expect, "b02-continue", "continuing after the interruption", [&] { window.recover(); },
                {"Continue where it stopped?", "Nothing that was finished is done again.", "The running system is not touched",
                 "recab recover", "password"},
                "Continue");
    window.recover(false);
    waitUntilIdle(window);
    expect("recovering finishes the change", window.status().changes.size() == 1);

    makeChange(window, "ssh");
    expect("a second change joins the same next system", window.status().changes.size() == 2);
    save(window, "06-next-system");
    {
        const QStringList titles = window.status().changeTitles();
        const QString next = nextWords(window);
        expect("the badge on the rail is the number of changes",
               window.frame()->badge("next") == window.status().changes.size() && window.status().changes.size() == 2);
        expect("the next system's page says it holds two changes, open",
               next.contains("The next system holds 2 changes") && next.contains("It is open: not finished"));
        expect("it lists every change by its title and no more than recab says",
               next.contains(titles.value(0)) && next.contains(titles.value(1)) && !next.contains("Made by")
                   && !next.contains("Take out") && !next.contains("Edit a file") && !next.contains(" MiB"));
        expect("it offers what recab offers",
               next.contains("[Install software…]") && next.contains("[Finish…]")
                   && next.contains("[Reset to the running system…]") && next.contains("2. Choose when"));
        expect("it says what will differ, from the changes",
               next.contains("What will differ after the restart") && next.contains("Starts")
                   && next.contains("the next system, when you restart into it") && next.contains("Your files")
                   && next.contains("the same: home is shared by both systems")
                   && next.contains("comes back to the running system by itself")
                   && next.contains("See the two systems as blocks in Recdirstat"));
        save(window, "p04-next-open");
        expect("the strip is not on the next system's page", !window.frame()->stripShown());
        backWords(window);
        expect("the strip is on Getting back", window.frame()->stripShown());
        save(window, "p05-back-with-a-next-system");
        const QString overview = overviewWords(window);
        expect("Overview offers finishing as the one thing and not the rest of the next system's page",
               overview.contains("[Finish…]") && !overview.contains("[Reset") && !window.frame()->stripShown());
        save(window, "p06-overview-open");
        window.showMain();
        settle(100);
    }
    // The next system is open and holds two changes: each action that gives it
    // up or closes it says so first, and Cancel changes nothing.
    beforeCheck(window, save, expect, "b03-finish", "finishing the next system", [&] { window.finish(); },
                {"Finish the next system?", "The next system holds 2 changes", "checked and closed", "does not restart",
                 "recab finalize", "password"},
                "Finish");
    beforeCheck(window, save, expect, "b04-reset", "resetting the next system", [&] { window.reset(); },
                {"Reset the next system to the running one?", "Every change in the next system is given up.",
                 "exact copy of the running system", "A system from before does not come back.", "recab discard",
                 "recab prepare", "password"},
                "Reset");
    beforeCheck(window, save, expect, "b05-discard", "discarding the next system", [&] { window.discard(); },
                {"Discard the next system?", "Its changes are given up.", "its place was already reused",
                 "stays exactly as it is", "recab discard", "password"},
                "Discard");
    window.showMain();
    settle(100);
    helpShots(window, folder, save, expect, "h");

    {
        // The older stand-in knows a release and adds it at once.
        const QString overview = overviewWords(window);
        expect("Overview names the newer release and sends to the page about it",
               overview.contains("A newer release is available: LevitateOS") && overview.contains("[See it…]"));
        const QString release = releaseWords(window);
        expect("the release page names it and offers to add it, with what the stand-in says of it",
               release.contains("is available") && release.contains("[Add it to the next system…]")
                   && release.contains("What recab says about it") && release.contains("Size")
                   && release.contains("What happens to your changes") && !release.contains("[Check again]")
                   && !release.contains("downloaded"));
        save(window, "p12-release-old");
        window.showMain();
        settle(100);
    }

    beforeCheck(window, save, expect, "b06-add-release", "adding the release", [&] { window.addUpdate(); },
                {"Add LevitateOS", "merged into the next system", "disagree about a file", "does not restart",
                 "recab update apply", "password"},
                "Add it");
    window.addUpdate(false);
    waitUntilIdle(window);
    expect("the update clashes with the edited file", !window.status().clashes.isEmpty());
    save(window, "07-clash");
    {
        const QString next = nextWords(window);
        expect("the next system's page offers to settle the clash and not to finish",
               next.contains("The update clashes with one of your changes") && next.contains("[Settle the clash…]")
                   && !next.contains("[Finish…]") && next.contains("Release"));
        window.showMain();
        settle(100);
    }

    // Decided by hand first: the window asks about the file, with both versions as the stand-in says them.
    {
        window.showClash();
        settle(200);
        const Raised said = answering(folder, "07b-decide-by-hand", "Keep mine",
                                       [&] { clickButton(window.findChild<ClashPage *>(), "Decide by hand…"); });
        waitUntilIdle(window);
        expect("deciding by hand asks which file goes into the next system, with both versions in the stand-in's words",
               said.seen && said.title == "Decide by hand" && said.text == "Which /etc/ssh/sshd_config goes into the next system?"
                   && said.informative.contains("Mine: your edit: password logins turned off.")
                   && said.informative.contains("The update's: the update's edit: one new setting, and the included folder moved.")
                   && said.buttons.contains("Keep mine") && said.buttons.contains("Take the update's"));
        expect("keeping mine settles the clash and records whose version it is",
               window.status().clashes.isEmpty() && window.status().changeTitles().join("|").contains("your version"));
        save(window, "07c-decided-by-hand");
    }

    window.finish(false);
    waitUntilIdle(window);
    expect("finishing makes it ready", window.status().state == "ready");
    save(window, "09-ready");
    {
        const QString next = nextWords(window);
        expect("the next system's page says it is ready, and offers the restart now or later",
               next.contains("The next system is ready") && next.contains("[Restart now]")
                   && next.contains("[Start it at the next restart]") && !next.contains("[Finish…]"));
        save(window, "p07-next-ready");
        const QString back = backWords(window);
        expect("Getting back has nothing to cancel", back.contains("None is set.") && back.contains("[Cancel it](off)"));
        const QString overview = overviewWords(window);
        expect("Overview offers the restart as the one thing", overview.contains("[Restart into it]"));
        window.showMain();
        settle(100);
    }

    beforeCheck(window, save, expect, "b07-restart-now", "restarting into the next system", [&] { window.restart(); },
                {"Restart into the next system?", "recab sets the next system to start at the next restart",
                 "programs that are open are closed", "cannot stop it", "recab keeps the new system by itself",
                 "up to 15 minutes", "comes back to the running system by itself", "recab set-next", "reboot",
                 "password"},
                "Restart into it");
    beforeCheck(window, save, expect, "b08-restart-later", "setting the restart for later", [&] { window.restartLater(); },
                {"Start the next system at the next restart?", "does not restart now", "Any restart starts the next system",
                 "comes back to the running system by itself", "recab set-next", "password"},
                "Set it");
    window.restart(false);
    waitUntilIdle(window);
    expect("the first restart fails and falls back", window.status().state == "failed");
    save(window, "10-did-not-start");
    {
        const QString next = nextWords(window);
        expect("the next system's page says it did not start, and offers to try again or copy it for an agent",
               next.contains("The next system did not start, so the computer came back")
                   && next.contains("[Try the same again]") && next.contains("[Copy for agent]"));
        save(window, "p08-next-came-back");
        const QString back = backWords(window);
        expect("Getting back says the computer came back, with the time recab gave",
               back.contains("Kept already: the computer came back to it on") && back.contains("did not start properly")
                   && back.contains("recab says"));
        save(window, "p09-back-came-back");
        const QString overview = overviewWords(window);
        expect("Overview copies it for an agent and does not offer the reset",
               overview.contains("[Copy for agent]") && !overview.contains("[Reset"));
        const QString copied = copiedFor(window.findChild<SystemPage *>());
        expect("what is copied says what failed and what is known, in plain text",
               copied.startsWith("The issue:\n") && copied.contains("The facts:\n- What failed:"));
        window.showMain();
        settle(100);
    }

    {
        // The person's own tool fixes what failed in the next system.
        QProcess fix;
        fix.start(qEnvironmentVariable("RECAB_GUI_RECAB"), {"make-change", "fix-failed"});
        fix.waitForFinished(120000);
        window.refresh();
        waitUntilIdle(window);
    }
    expect("the fix opens the next system again", window.status().state == "preparing");
    window.finish(false);
    waitUntilIdle(window);
    window.restart(false);
    waitUntilIdle(window);
    expect("the second restart is kept", window.status().state == "stable" && window.status().trialOutcome == "kept");
    save(window, "11-on-the-new-system");
    {
        const QString back = backWords(window);
        expect("Getting back says the new system is kept, and when it started properly",
               back.contains("Kept already: it started properly on") && back.contains("startup ended with nothing failed")
                   && back.contains("[Go back…]") && !back.contains("[Go back…](off)"));
        save(window, "p10-back-kept");
        beforeCheck(window, save, expect, "b09-go-back", "going back", [&] { window.goBack(); },
                    {"Go back to ", "The computer restarts into ", "is kept beside it, so you can return", "recab rollback",
                     "reboot", "password"},
                    "Restart into it");
        const QString path = QDir(folder).filePath("p-steps-old.txt");
        QFile::remove(path);
        expect("the steps are written as a file", window.saveSteps(path));
        QFile file(path);
        const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        expect("the file names the steps, and no system by a name recab did not give",
               text.contains("1. Restart the computer") && text.contains("2. Pick the running system")
                   && text.contains("3. To make that choice stay, run \"recab commit\"") && !text.contains("Slot "));
        window.showMain();
        settle(100);
    }

    // Reading again by hand, as F5 does, leaves the page where it was.
    window.refresh();
    waitUntilIdle(window);
    expect("reading again keeps the state", window.status().state == "stable");

    // A change, then the reset: the next system is the running one again.
    makeChange(window, "ssh");
    expect("a change is there to be reset", window.status().changes.size() == 1);
    window.reset(false);
    waitUntilIdle(window);
    expect("the reset leaves a next system without changes",
           window.status().otherRole == "next" && window.status().changes.isEmpty()
               && window.status().state == "preparing");
    save(window, "12-reset");
    {
        const QString next = nextWords(window);
        expect("after the reset the next system holds nothing",
               next.contains("The next system holds no changes yet") && next.contains("Nothing yet")
                   && window.frame()->badge("next") == 0);
        save(window, "p11-next-reset");
        window.showMain();
        settle(100);
    }

    {
        // This stand-in has no `history`: the page says so and lists nothing.
        QStringList rows;
        const QString history = historyWords(window, &rows);
        expect("a recab with no history command says it keeps none",
               history.contains("This recab keeps no history it can show.") && rows.isEmpty()
                   && !history.contains("[Try again]") && window.frame()->selected() == "history");
        save(window, "p13-history-none");
        window.showMain();
        settle(100);
    }

    out << (ok ? "all flows ended where they should\n" : "some flows did not end where they should\n");
    return ok ? 0 : 1;
}

// The story the real recab offers today, against the stand-in that speaks its
// shape (`fixtures/recab-real`): two copies, a change by a tool, finishing,
// the restart, going back, and discarding. No update or clash:
// the real recab has none of those.
int takeRealShots(Window &window, const QString &folder)
{
    QDir().mkpath(folder);
    QTextStream out(stdout);
    bool ok = true;
    auto save = [&](QWidget &widget, const QString &name) {
        settle(200);
        ok &= widget.grab().save(QDir(folder).filePath(name + ".png"));
    };
    auto expect = [&](const QString &what, bool holds) {
        if (!holds) {
            out << "not as expected: " << what << " (state " << window.status().state << ", other "
                << window.status().otherRole << ")\n";
            ok = false;
        }
    };
    window.show();
    waitUntilIdle(window);
    expect("the status is read as the real recab's", window.status().real);
    if (!window.status().real) {
        // Nothing was read: show why, and go no further. The story below
        // changes the system, which must not happen on a system
        // that is not the stand-in's.
        save(window, "r0-not-read");
        out << "the system could not be read; stopped before anything was changed\n";
        return 1;
    }
    expect("it starts with two copies of one system",
           window.status().state == "stable" && window.status().otherRole == "copy");
    expect("the running system is named from os-release", window.status().name(window.status().runningRelease) == "LevitateOS 2026.11");
    save(window, "r1-two-copies");
    {
        const QString next = nextWords(window);
        expect("the next system's page says there is none, and that the other place holds a copy",
               next.contains("There is no next system yet") && next.contains("holds a copy of it"));
        const QString back = backWords(window);
        expect("Getting back says why there is nothing to go back to",
               back.contains("There is none: the other place holds a copy of the running system.")
                   && back.contains("[Go back…](off)") && back.contains("recab holds no record of one"));
        expect("the steps name the two systems as recab titles them, and the running one",
               back.contains("Pick \"LevitateOS (Slot A)\". That is the running system today, and it is kept.")
                   && back.contains("\"LevitateOS (Slot B)\" holds a copy of it.")
                   && back.contains("loader/entries/levitate-a.conf") && back.contains("loader/entries/levitate-b.conf"));
        save(window, "rp1-back-two-copies");
        const QString path = QDir(folder).filePath("rp-steps.txt");
        QFile::remove(path);
        expect("the steps are written as a file", window.saveSteps(path));
        QFile file(path);
        const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        expect("the file names the steps and the systems",
               text.contains("1. Restart the computer") && text.contains("2. Pick \"LevitateOS (Slot A)\"")
                   && text.contains("3. To make that choice stay, run \"recab commit\" as administrator")
                   && text.contains("\"LevitateOS (Slot B)\" holds a copy of it."));
        window.showMain();
        settle(100);
    }

    // No update source is configured: recab refuses to look, and the release page says so in recab's words.
    {
        qputenv("RECAB_STANDIN_UPDATE_SOURCE", "none");
        window.refresh();
        waitUntilIdle(window);
        const QString overview = overviewWords(window);
        expect("with no update source Overview still leads to the release page, where recab's refusal is said",
               overview.contains("Check for a new release"));
        const QString release = releaseWords(window);
        expect("the release page says recab cannot look, gives recab's own reason, keeps Check now off and a file possible",
               release.contains("No new release is known") && release.contains("it cannot look for one now")
                   && release.contains("It cannot be done now: No usable update source is configured")
                   && release.contains("[Check now](off)") && release.contains("[Choose a file…]")
                   && !release.contains("[Choose a file…](off)"));
        save(window, "r8a-release-no-source");
        qunsetenv("RECAB_STANDIN_UPDATE_SOURCE");
        window.refresh();
        waitUntilIdle(window);
        window.showMain();
        settle(100);
    }

    // A new release: recab knows of none until it is asked.
    {
        const QString overview = overviewWords(window);
        expect("Overview offers no release while none is known, only the way to ask",
               overview.contains("Check for a new release") && !overview.contains("[See it…]"));
        const QString release = releaseWords(window);
        expect("the release page says none is known, and offers to look or to take a file",
               release.contains("No new release is known") && release.contains("[Check now]")
                   && release.contains("[Choose a file…]") && !release.contains("What recab says about it"));
        save(window, "r8-release-unknown");

        qputenv("RECAB_STANDIN_CHECK_FAILS", "1");
        window.runReleaseOperation("update_check");
        waitUntilIdle(window);
        qunsetenv("RECAB_STANDIN_CHECK_FAILS");
        const QString failed = releaseWords(window);
        expect("a check that fails says so, in recab's words, and offers to try again",
               failed.contains("The release list could not be checked") && failed.contains("recab says: Recipe acquisition failed")
                   && failed.contains("[Try again]"));
        save(window, "r8b-release-check-failed");

        qputenv("RECAB_STANDIN_RELEASE", "none");
        window.runReleaseOperation("update_check");
        waitUntilIdle(window);
        qunsetenv("RECAB_STANDIN_RELEASE");
        const QString newest = releaseWords(window);
        expect("a check that finds nothing says this is the newest, and that recab looks only when asked",
               newest.contains("This is the newest release") && newest.contains("looks only when asked")
                   && newest.contains("[Check again]"));
        save(window, "r8c-release-newest");

        window.runReleaseOperation("update_check");
        waitUntilIdle(window);
        expect("a check that finds one makes it known", window.status().update.available()
                   && window.status().update.version == "2026.12" && window.status().update.channel == "stable");
        const QString found = releaseWords(window);
        expect("the release page names it, with only what recab says, and offers to download it",
               found.contains("LevitateOS 2026.12 is available") && found.contains("[Download it]")
                   && found.contains("Channel") && found.contains("stable") && found.contains("3.1 GiB, all its files")
                   && found.contains("no kernel, no package list") && found.contains("There is no next system yet; adding the release makes one.")
                   && found.contains("[Check again]") && found.contains("[Choose a file…]") && !found.contains("[Add it to the next system…]"));
        save(window, "r9-release-available");
        const QString notice = overviewWords(window);
        expect("Overview now names the release and sends to its page",
               notice.contains("A newer release is available: LevitateOS 2026.12") && notice.contains("[See it…]")
                   && !notice.contains("Check for a new release"));
        save(window, "r9b-overview-release-known");

        window.runReleaseOperation("update_download");
        waitUntilIdle(window);
        expect("the release is downloaded", window.status().update.downloaded);
        const QString got = releaseWords(window);
        expect("the release page says it is downloaded and verified, and offers to add it",
               got.contains("LevitateOS 2026.12 is downloaded") && got.contains("recab verified every file")
                   && got.contains("[Add it to the next system…]") && got.contains("verified, complete") && !got.contains("[Download it]"));
        save(window, "r9c-release-downloaded");
        window.showMain();
        settle(100);
    }

    makeChange(window, "ssh");
    expect("the change opens the next system",
           window.status().state == "preparing" && !window.status().preparationId.isEmpty()
               && window.status().changes.size() == 1);
    expect("the change is titled as recab names it, with nobody's label", !window.status().changeTitles().value(0).contains("by "));
    save(window, "r2-the-next-system-is-open");
    {
        const QString next = nextWords(window);
        expect("the next system's page shows who made the change, and when",
               next.contains("Made by") && next.contains("tool") && next.contains("When")
                   && next.contains("edited: /etc/ssh/sshd_config") && window.frame()->badge("next") == 1);
        window.showMain();
        settle(100);
    }
    helpShots(window, folder, save, expect, "rh");
    {
        // The reader the other two apps use for their strip, against the same recab.
        NextSystem next(qEnvironmentVariable("RECAB_GUI_RECAB"));
        bool answered = false;
        QObject::connect(&next, &NextSystem::known, [&] { answered = true; });
        next.read();
        waitFor([&] { return answered; });
        expect("the next-system reader finds the one change the window knows",
               answered && next.titles().size() == window.status().changes.size()
                   && next.titles() == window.status().changeTitles());
    }

    // A change made by the person, as Recipe would make it, with the stand-in's own command.
    {
        const QString recab = qEnvironmentVariable("RECAB_GUI_RECAB");
        QProcess person;
        person.start(recab, {"--json", "change", "install", "htop", "--preparation", window.status().preparationId,
                             "--revision", QString::number(window.status().preparationRevision), "--actor", "person"});
        person.waitForFinished(120000);
        expect("the stand-in took a change from the person",
               person.exitStatus() == QProcess::NormalExit && person.exitCode() == 0);
        window.refresh();
        waitUntilIdle(window);
        const QString next = nextWords(window);
        expect("the badge counts both changes", window.frame()->badge("next") == 2 && window.status().changes.size() == 2);
        expect("the page says who made each, and when: the tool, and you",
               next.contains("The next system holds 2 changes") && next.contains("tool") && next.contains("You")
                   && next.contains("htop, installed") && next.contains("added: htop") && next.contains("edited: /etc/ssh/sshd_config")
                   && !next.contains("Take out") && !next.contains(" MiB"));
        save(window, "rp2-next-open-with-who-and-when");
        const QString overview = overviewWords(window);
        expect("Overview offers finishing as the one thing, and sends to the two pages",
               overview.contains("[Finish…]") && !overview.contains("[Reset")
                   && overview.contains("The rest is on:"));
        save(window, "rp3-overview-open");
        window.showMain();
        settle(100);
    }

    // The release goes into this next system, which holds a file the release also changes.
    {
        beforeCheck(window, save, expect, "br05-add-release", "adding the release", [&] { window.addUpdate(); },
                    {"Add LevitateOS 2026.12 to the next system?", "merged into the next system", "disagree about a file",
                     "recab update apply", "password"},
                    "Add it");
        window.addUpdate(false);
        waitUntilIdle(window);
        const UpdateInfo &u = window.status().update;
        expect("recab stops with one clash", u.hasMerge && !u.mergeComplete && u.unresolved == 1
                   && window.status().clashes.size() == 1 && window.status().state == "preparing");
        const QString release = releaseWords(window);
        expect("the release page says it needs you, lists the clash as recab names it, and offers to settle or take it out",
               release.contains("Adding LevitateOS 2026.12 needs you: 1 thing to settle")
                   && release.contains("etc/ssh/sshd_config (changed on both sides): Not settled") && release.contains("[Settle…]")
                   && release.contains("[Take it out]") && !release.contains("[Choose a file…]")
                   && !release.contains("[Check again]"));
        save(window, "r10-release-clash");
        const QString overview = overviewWords(window);
        save(window, "r10b-overview-clash");
        expect("Overview offers settling the clash as the one thing",
               overview.contains("Adding LevitateOS 2026.12 needs you") && overview.contains("[Settle…]")
                   && !overview.contains("[Finish…]") && !overview.contains("[Reboot]"));
        const QString next = nextWords(window);
        expect("the next system's page offers to settle and not to finish",
               next.contains("[Settle the clash…]") && !next.contains("[Finish…]"));

        // Taken out again: recab keeps the changes and the release is only downloaded.
        beforeCheck(window, save, expect, "br06-take-out", "taking the release out", [&] { window.runReleaseOperation("update_cancel"); },
                    {"Take the release out of the next system?", "retains the changes prepared there", "recab update cancel",
                     "password"},
                    "Take it out");
        window.runReleaseOperation("update_cancel", false);
        waitUntilIdle(window);
        expect("taking it out leaves the release downloaded and the changes in place",
               !window.status().update.hasMerge && window.status().update.downloaded && window.status().changes.size() == 2
                   && window.status().clashes.isEmpty());
        expect("and the release page offers to add it again",
               releaseWords(window).contains("LevitateOS 2026.12 is downloaded"));

        // The merge stops on what no side can settle: the original release's baseline is not on record.
        const QString original = QDir(folder).filePath("original-release/release.json");
        qputenv("RECAB_STANDIN_MERGE", "baseline");
        window.addUpdate(false);
        waitUntilIdle(window);
        {
            const UpdateInfo &u = window.status().update;
            expect("a merge blocked on a missing baseline is stopped, with one open conflict of that kind",
                   u.stopped() && u.mergePhase == "blocked" && u.unresolved == 1 && u.conflicts.size() == 1
                       && u.conflicts.first().isBaseline() && window.status().state == "preparing");
            expect("recab offers to adopt a baseline, refuses finalizing with its reason, and has nothing to resolve or reproduce",
                   window.status().operation("adopt_baseline").ready && !window.status().operation("finalize").ready
                       && window.status().operation("finalize").reason
                              == "Resolve or recover the selected release before preparing another change"
                       && !window.status().operation("update_reproduce").ready
                       && window.status().operation("update_reproduce").reason == "The merge has no blocked package reproduction"
                       && window.status().operationObject("resolve:conflict1").isEmpty());
            const QString blocked = releaseWords(window);
            expect("the release page says what recab stopped at, in its words, and offers the baseline and not a side",
                   blocked.contains("Adding LevitateOS 2026.12 needs you: 1 thing to settle")
                       && blocked.contains("usr/lib/recab/release-baseline.json (the original release is not on record): Not settled")
                       && blocked.contains("recab says: original release baseline unavailable; adopt the verified original release, "
                                           "never capture the personalized system as O")
                       && blocked.contains("[Adopt the original release…]")
                       && blocked.contains("recab offers: Adopt the verified original release baseline.")
                       && blocked.contains("[Adopt it…]") && !blocked.contains("[Settle…]") && blocked.contains("[Take it out]"));
            save(window, "r20-release-blocked-baseline");
            const QString overview = overviewWords(window);
            expect("Overview says the same with the release's name and leads to the page",
                   overview.contains("Adding LevitateOS 2026.12 needs you") && overview.contains("[Settle…]")
                       && !overview.contains("The update clashes") && !overview.contains("[Finish…]"));
            const QString next = nextWords(window);
            expect("the next system's page says it, and offers to settle, not to finish",
                   next.contains("Adding LevitateOS 2026.12 needs you") && next.contains("[Settle the clash…]")
                       && !next.contains("[Finish…]"));
            window.showClash();
            settle(200);
            const QString clash = wordsOn(window.findChild<ClashPage *>());
            expect("the clash page says what recab says of it, offers the baseline with recab's label, and no way to copy it",
                   clash.contains("What recab stopped at")
                       && clash.contains("recab has no usable record of the original release this next system began from "
                                         "(usr/lib/recab/release-baseline.json)")
                       && clash.contains("What recab offers") && clash.contains("[Adopt it…]") && clash.contains("[Later]")
                       && !clash.contains("[Copy for agent]") && !clash.contains("(private)") && !clash.contains("was added"));
            save(window, "r20b-clash-baseline");
            window.showMain();
            settle(100);
        }
        beforeCheck(window, save, expect, "br16-adopt-baseline", "adopting the original release's baseline",
                    [&] { window.adoptBaseline(original); },
                    {"Adopt the original release as the baseline?", "has no baseline of the original release", original,
                     "exactly the release this system was installed from", "It does not add LevitateOS 2026.12",
                     "recab update adopt-baseline", "password"},
                    "Adopt it");
        window.adoptBaseline(original, false);
        waitUntilIdle(window);
        expect("with the baseline adopted there is no merge, and adding the release is offered again",
               !window.status().update.hasMerge && window.status().operation("update_apply").ready
                   && window.status().operationObject("adopt_baseline").isEmpty()
                   && releaseWords(window).contains("LevitateOS 2026.12 is downloaded"));
        save(window, "r20c-baseline-adopted");

        // The merge stops on a package no side can settle: it has to be reproduced through Recipe.
        qputenv("RECAB_STANDIN_MERGE", "rebuild");
        window.addUpdate(false);
        waitUntilIdle(window);
        {
            const UpdateInfo &u = window.status().update;
            expect("a merge that stops on a package has one open package_rebuild conflict, named by the package",
                   u.stopped() && u.unresolved == 1 && u.conflicts.size() == 1 && u.conflicts.first().isPackageRebuild()
                       && u.conflicts.first().path == "htop");
            expect("recab offers to reproduce it, and no resolve for it",
                   window.status().operation("update_reproduce").ready && window.status().operationObject("resolve:conflict1").isEmpty()
                       && !window.status().operation("update_recover").ready
                       && window.status().operation("update_recover").reason == "Resolve outstanding conflicts first");
            const QString rebuild = releaseWords(window);
            expect("the release page offers to reproduce the package, in recab's label, and says what recab found",
                   rebuild.contains("[Reproduce htop…]") && rebuild.contains("htop (software to reproduce through Recipe): Not settled")
                       && rebuild.contains("recab offers: Reproduce selected software for this release through Recipe.")
                       && rebuild.contains("[Reproduce…]") && !rebuild.contains("[Settle…]"));
            save(window, "r21-release-blocked-rebuild");
            window.showClash();
            settle(200);
            auto *clashPage = window.findChild<ClashPage *>();
            const QString clash = wordsOn(clashPage);
            const QString prompt = copiedFor(clashPage);
            expect("the clash page says what recab found of the package and offers to reproduce it; Decide by hand is off",
                   clash.contains("htop is software added to the next system through Recipe. recab found that LevitateOS 2026.12 "
                                  "changes system libraries or the kernel")
                       && clash.contains("[Reproduce…]") && clash.contains("[Decide by hand…](off)")
                       && clash.contains("[Copy for agent]") && !clash.contains("(private)"));
            expect("what is copied names the package, the release, and recab's own command with the open preparation",
                   prompt.contains("htop (software to reproduce through Recipe)")
                       && prompt.contains("recab update reproduce --preparation " + window.status().preparationId + " --revision "
                                          + QString::number(window.status().preparationRevision) + " htop [--recipe RECIPE]")
                       && prompt.contains("The release being added is LevitateOS 2026.12; the running system's release is LevitateOS 2026.11.")
                       && !prompt.contains("(private)") && !prompt.contains("was added"));
            expect("at 700 pixels high the clash page scrolls and the panel with its button can be reached",
                   window.height() == 700 && scrollsToButton(clashPage, "Copy for agent"));
            save(window, "r21b-clash-rebuild");
            QScrollArea *scrolled = clashPage->findChild<QScrollArea *>();
            if (scrolled)
                scrolled->verticalScrollBar()->setValue(scrolled->verticalScrollBar()->maximum());
            save(window, "r21b2-clash-rebuild-scrolled");
            if (scrolled)
                scrolled->verticalScrollBar()->setValue(0);
            window.showMain();
            settle(100);
        }
        beforeCheck(window, save, expect, "br17-reproduce", "reproducing the package", [&] { window.reproduce("htop"); },
                    {"Reproduce htop through Recipe?", "htop is software added to the next system through Recipe",
                     "using the recipe the next system recorded for it", "recab update reproduce", "password"},
                    "Reproduce it");
        window.reproduce("htop", false);
        waitUntilIdle(window);
        expect("once reproduced recab no longer stops at the package: no conflict is left and carrying on is offered",
               window.status().update.conflicts.isEmpty() && window.status().update.unresolved == 0
                   && window.status().operation("update_recover").ready && !window.status().operation("update_reproduce").ready);
        save(window, "r21c-package-reproduced");
        window.runReleaseOperation("update_cancel", false);
        waitUntilIdle(window);
        expect("taken out again, the release is only downloaded", !window.status().update.hasMerge && window.status().update.downloaded);

        // Every other kind of conflict, as recab names them: the page says what recab says of each, and copies the same for an agent.
        qputenv("RECAB_STANDIN_MERGE", "kinds");
        window.addUpdate(false);
        waitUntilIdle(window);
        {
            const UpdateInfo &u = window.status().update;
            expect("a merge can stop on six things of six kinds at once", u.stopped() && u.unresolved == 6 && u.conflicts.size() == 6);
            window.showClash();
            settle(200);
            auto *clashPage = window.findChild<ClashPage *>();
            const QString clash = wordsOn(clashPage);
            expect("the clash page names each as recab does, with the sentence for its kind and no word recab did not say",
                   clash.contains("Adding LevitateOS 2026.12 needs you")
                       && clash.contains("recab stopped at 6 things it cannot decide for you.")
                       && clash.contains("etc/ssh/sshd_config") && clash.contains("changed on both sides")
                       && clash.contains("The next system and LevitateOS 2026.12 both changed etc/ssh/sshd_config from what "
                                         "LevitateOS 2026.11 shipped, each differently: its content or its metadata (owner, "
                                         "permissions). recab does not say what either version holds.")
                       && clash.contains("etc/motd") && clash.contains("a different kind of thing on each side")
                       && clash.contains("etc/motd is a different kind of thing (file, directory or link) in the next system than "
                                         "in LevitateOS 2026.12, and both differ from what LevitateOS 2026.11 shipped. recab "
                                         "does not say which is which.")
                       && clash.contains("removed on one side, changed on the other")
                       && clash.contains("etc/issue was removed on one side and changed on the other: removed in the next system "
                                         "and changed by LevitateOS 2026.12, or the other way round. recab does not say which.")
                       && clash.contains("a directory and what is in it")
                       && clash.contains("etc/profile.d is a directory that one side replaced or removed while the other side "
                                         "kept or changed things inside it.")
                       && clash.contains("a file of software added through Recipe")
                       && clash.contains("usr/bin/nvidia-smi is a file of software added to the next system through Recipe, and "
                                         "LevitateOS 2026.12 has a different version of it. recab accepts only the next system's "
                                         "file")
                       && clash.contains("software to reproduce through Recipe") && clash.contains("[Reproduce…]")
                       && !clash.contains("(private)") && !clash.contains("was added to the next system"));
            const QString prompt = copiedFor(clashPage);
            const QString cas = " --preparation " + window.status().preparationId + " --revision "
                                + QString::number(window.status().preparationRevision);
            keepWords(folder, "r22-prompt.txt", prompt);
            expect("what is copied has each item with recab's sentence and the command recab offers for it",
                   prompt.contains("etc/ssh/sshd_config (changed on both sides): The next system and LevitateOS 2026.12 both "
                                   "changed")
                       && prompt.contains("`recab update resolve conflict1" + cas + " --choice local|release|edited [--file FILE]`")
                       && prompt.contains("`recab update resolve conflict4" + cas + " --choice local|release [--file FILE]`")
                       && prompt.contains("`recab update resolve conflict5" + cas + " --choice local [--file FILE]`")
                       && prompt.contains("`recab update reproduce" + cas + " htop [--recipe RECIPE]`")
                       && !prompt.contains("(private)") && !prompt.contains("was added"));
            expect("with six conflicts too the button can be reached (the page scrolls if it must)",
                   scrollsToButton(clashPage, "Copy for agent", false));
            save(window, "r22-clash-every-kind");
            // Decided by hand: the question names what each choice does, in the words of the page.
            const Raised asked = answering(folder, "r22b-decide-by-hand", QString(), [&] { clickButton(clashPage, "Decide by hand…"); },
                                           QStringLiteral("Keep the next system's version (local)"));
            waitUntilIdle(window);
            expect("deciding by hand asks about the first file recab lets a side be chosen for, with each choice in words",
                   asked.seen && asked.text == "Resolution"
                       && asked.items == QStringList({"Keep the next system's version (local)", "Take the release's version (release)",
                                                      "Use a file I give (edited)"}));
            expect("the choice was sent to recab and the clash page is shown again with five left",
                   window.status().update.unresolved == 5 && window.status().update.conflicts.first().resolution == "local"
                       && clashPage->isVisible()
                       && wordsOn(clashPage).contains("recab stopped at 5 things it cannot decide for you.")
                       && wordsOn(clashPage).contains("Settled: the next system's version stays"));
            save(window, "r22c-one-settled-by-hand");
            window.showMain();
            settle(100);
        }
        window.runReleaseOperation("update_cancel", false);
        waitUntilIdle(window);
        qunsetenv("RECAB_STANDIN_MERGE");
        expect("taken out again the release is downloaded and the changes are in place",
               !window.status().update.hasMerge && window.status().update.downloaded && window.status().changes.size() == 2);

        window.addUpdate(false);
        waitUntilIdle(window);
        expect("adding it again stops at the same clash", window.status().update.unresolved == 1);

        // Settled as the person's dialog would send it, with the stand-in's own command.
        QProcess settleIt;
        settleIt.start(qEnvironmentVariable("RECAB_GUI_RECAB"),
                       {"--json", "update", "resolve", "conflict1", "--choice", "local", "--preparation",
                        window.status().preparationId, "--revision", QString::number(window.status().preparationRevision),
                        "--actor", "person"});
        settleIt.waitForFinished(120000);
        expect("the stand-in took the choice", settleIt.exitStatus() == QProcess::NormalExit && settleIt.exitCode() == 0);
        window.refresh();
        waitUntilIdle(window);
        const QString settled = releaseWords(window);
        expect("with the clash settled the page says so and offers to carry on",
               settled.contains("Every clash is settled") && settled.contains("[Continue adding it]")
                   && settled.contains("etc/ssh/sshd_config (changed on both sides): Settled: the next system's version stays"));
        save(window, "r11-release-settled");
        beforeCheck(window, save, expect, "br07-carry-on", "carrying on adding the release", [&] { window.runReleaseOperation("update_recover"); },
                    {"Carry on adding the release?", "continues the merge", "recab update recover", "password"},
                    "Carry on");
        window.runReleaseOperation("update_recover", false);
        waitUntilIdle(window);
        expect("the merge is complete and the next system can be finished",
               window.status().update.mergeComplete && window.status().state == "preparing"
                   && window.status().can("finalize").ready);
        const QString added = releaseWords(window);
        expect("the release page says it is in the next system, and offers to finish",
               added.contains("LevitateOS 2026.12 is in the next system") && added.contains("[Finish and restart…]")
                   && added.contains("Nothing is in effect until you restart") && added.contains("Merge"));
        save(window, "r12-release-added");
        window.showMain();
        settle(100);
    }

    // A command started in a terminal is running: nothing can be started here.
    qputenv("RECAB_STANDIN_WORKING", "1");
    window.refresh();
    waitUntilIdle(window);
    expect("a command running elsewhere is reported as working", window.status().state == "working");
    save(window, "r2b-a-command-runs-elsewhere");
    {
        const QString overview = overviewWords(window);
        expect("Overview says it is not an interruption, offers nothing, and says so once, off",
               overview.contains("This is not an interruption: wait for it.") && overview.contains("Nothing can be started while this runs") && !overview.contains("[Finish…]")
                   && window.frame()->statusText().contains("every two seconds"));
        window.showMain();
        settle(100);
        const QString next = nextWords(window);
        expect("while a command runs elsewhere the next system's page offers nothing, and says so once",
               next.contains("The next system is being worked on")
                   && !next.contains("[Finish…]") && !next.contains("[Reset")
                   && next.count("Nothing can be started while that command runs.") == 1);
        save(window, "rp4-next-working");
        const QString back = backWords(window);
        expect("Getting back offers nothing then", back.contains("[Go back…](off)") && back.contains("[Cancel it](off)"));
        window.showMain();
        settle(100);
    }
    qunsetenv("RECAB_STANDIN_WORKING");
    window.refresh();
    waitUntilIdle(window);
    expect("when it has finished the next system is open again", window.status().state == "preparing");

    // A command that fails: its page stays up and says why, in recab's words, and nothing was changed.
    qputenv("RECAB_STANDIN_FAIL", "finalize");
    window.finish(false);
    waitUntilIdle(window);
    qunsetenv("RECAB_STANDIN_FAIL");
    {
        auto *work = window.findChild<WorkPage *>();
        const QString words = wordsOn(work);
        expect("a command that fails leaves its page up, says it stopped and why in recab's words, and offers Back",
               work->isVisible() && work->saysWhy() && words.contains("It stopped")
                   && words.contains("candidate or boot inputs changed since finalization began; reopen the preparation before "
                                     "changing its revision")
                   && words.contains("[Back]"));
        expect("and nothing was changed: the next system is still open with its two changes",
               window.status().state == "preparing" && window.status().changes.size() == 2);
        save(window, "r16-a-command-failed");
        clickButton(work, "Back");
        settle(200);
        expect("Back leaves for the page the person came from", !work->isVisible() && window.findChild<SystemPage *>()->isVisible());
        window.showMain();
        settle(100);
    }

    beforeCheck(window, save, expect, "br01-finish", "finishing the next system", [&] { window.finish(); },
                {"Finish the next system?", "The next system holds 2 changes", "checked and closed", "does not restart",
                 "recab finalize", "password"},
                "Finish");
    window.finish(false);
    waitUntilIdle(window);
    expect("finishing makes it ready", window.status().state == "ready");
    save(window, "r3-ready");
    {
        const QString next = nextWords(window);
        expect("the next system's page says it is ready, and offers the restart now or later",
               next.contains("The next system is ready") && next.contains("[Restart now]")
                   && next.contains("[Start it at the next restart]") && next.contains("tool")
                   && next.contains("You"));
        save(window, "rp5-next-ready");
        window.showMain();
        settle(100);
    }

    // The restart fails after the step before it was made: the window says the computer did not restart and what was made.
    {
        qputenv("RECAB_STANDIN_REBOOT_FAILS", "1");
        const Raised said = answering(folder, "r17-restart-failed-sentence", QString(), [&] { window.restart(false); });
        qunsetenv("RECAB_STANDIN_REBOOT_FAILS");
        waitUntilIdle(window);
        expect("a restart that fails says the computer did not restart, what was made, and what it said (" + said.text + ")",
               said.seen && said.text.contains("The computer did not restart.")
                   && said.text.contains("The step before it was made: recab set the next system to start at the next restart.")
                   && said.text.contains("The page shows where things stand now.")
                   && said.text.contains("It said: reboot: the restart was refused (stand-in)"));
        expect("the step before the restart stays made: the restart is set, and nothing restarted",
               window.status().state == "trial_pending" && window.status().runningSlot == "A");
        const QString next = nextWords(window);
        expect("the page shows where things stand: the restart is set",
               next.contains("The next system starts at the next restart") && next.contains("[Cancel the set restart]"));
        save(window, "r17-after-the-failed-restart");
        window.cancelRestart(false);
        waitUntilIdle(window);
        expect("taking the set restart back leaves the next system ready", window.status().state == "ready");
        window.showMain();
        settle(100);
    }

    // Set for the next restart without restarting, and taken back.
    beforeCheck(window, save, expect, "br02-restart-now", "restarting into the next system", [&] { window.restart(); },
                {"Restart into the next system?", "recab sets the next system to start at the next restart",
                 "programs that are open are closed", "up to 15 minutes", "comes back to the running system by itself",
                 "recab set-next", "reboot", "password"},
                "Restart into it");
    beforeCheck(window, save, expect, "br03-restart-later", "setting the restart for later", [&] { window.restartLater(); },
                {"Start the next system at the next restart?", "does not restart now", "Any restart starts the next system",
                 "recab set-next", "password"},
                "Set it", false);
    pressGo(window, "Set it"); // the button itself, as a person would press it
    expect("the page's button sets the restart and nothing restarts",
           window.status().state == "trial_pending" && window.status().runningSlot == "A");
    expect("the restart is set and nothing restarted",
           window.status().state == "trial_pending" && window.status().runningSlot == "A"
               && window.status().can("restart").ready);
    expect("while a restart is set recab offers to reconcile the trial, keeps commit, and refuses the rest with its reason",
           window.status().operation("trial_recover").ready && window.status().operation("commit").ready
               && !window.status().operation("finalize").ready && !window.status().operation("install").ready
               && window.status().operation("finalize").reason == "Reconcile the pending trial before changing either system");
    save(window, "r3b-starts-at-the-next-restart");
    {
        const QString next = nextWords(window);
        expect("the next system's page says the restart is set, and offers to restart or cancel it",
               next.contains("The next system starts at the next restart") && next.contains("[Restart now]")
                   && next.contains("[Cancel the set restart]") && next.contains("that restart is set"));
        save(window, "rp6-next-restart-set");
        const QString back = backWords(window);
        expect("Getting back offers to cancel the set restart",
               back.contains("A restart into the next system is set") && back.contains("[Cancel it]")
                   && !back.contains("[Cancel it](off)") && back.contains("[Go back…](off)"));
        save(window, "rp7-back-restart-set");
        window.showMain();
        settle(100);
    }
    beforeCheck(window, save, expect, "br04-cancel-restart", "cancelling the set restart", [&] { window.cancelRestart(); },
                {"Cancel the set restart?", "keeps the running system", "does not restart", "You can set the restart again",
                 "recab commit", "password"},
                "Cancel the restart");
    expect("after Cancel the restart is still set", window.status().state == "trial_pending");
    window.cancelRestart();
    settle(300);
    pressGo(window, "Cancel the restart");
    expect("cancelling leaves the next system ready", window.status().state == "ready");
    {
        const QString back = backWords(window);
        expect("after cancelling, Getting back says none is set", back.contains("None is set.") && back.contains("[Cancel it](off)"));
        save(window, "rp8-back-restart-cancelled");
        window.showMain();
        settle(100);
    }
    window.restartLater(false);
    waitUntilIdle(window);

    // Start-up has not been judged yet when the window opens after the restart.
    beforeCheck(window, save, expect, "br08-restart-set", "restarting with the restart set", [&] { window.restart(); },
                {"Restart now?", "A restart into the next system is set already", "only the restart is left",
                 "programs that are open are closed", "reboot", "password"},
                "Restart");
    qputenv("RECAB_STANDIN_KEEP", "hand");
    window.restart(false);
    waitUntilIdle(window);
    qunsetenv("RECAB_STANDIN_KEEP");
    expect("the restart lands on the other slot, on trial",
           window.status().state == "on_trial" && window.status().runningSlot == "B"
               && window.status().can("keep").ready);
    save(window, "r4-on-trial");
    {
        const QString overview = overviewWords(window);
        expect("the first view after the restart is the trial: what is judged, and the two things to do",
               overview.contains("You are trying the new system") && overview.contains("Start-up is being judged")
                   && overview.contains("up to 15 minutes") && overview.contains("[Go back now…]")
                   && overview.contains("[Keep it now]") && overview.contains("The new system, running on trial")
                   && overview.contains("with 2 changes") && overview.contains("htop, installed")
                   && overview.contains("Not kept yet") && overview.contains("Untouched: it is what a restart returns to")
                   && overview.contains("While a system is on trial")
                   && overview.contains("Reconcile the pending trial before changing either system")
                   && !overview.contains("What to do next"));
        expect("while on trial the other place is the system from before, and nothing is called next",
               window.status().otherRole == "previous" && window.frame()->badge("next") == 0 && !window.frame()->stripShown());
        save(window, "r4a-first-view-on-trial");
        window.showMain();
        settle(100);
    }
    {
        const QString next = nextWords(window);
        expect("the next system's page says this is the trial, and offers to keep it or go back",
               next.contains("You are on the next system, on trial") && next.contains("[Keep it]")
                   && next.contains("[Go back…]") && !next.contains("Everything in it"));
        save(window, "rp9-next-on-trial");
        const QString back = backWords(window);
        expect("Getting back offers keeping it, and names the system to pick if it does not start",
               back.contains("It started, and is not kept yet.") && back.contains("[Keep it]") && !back.contains("[Keep it](off)")
                   && back.contains("Pick \"LevitateOS (Slot A)\". That is the system from before. The running system, "
                                    "\"LevitateOS (Slot B)\", is on trial and is not kept."));
        save(window, "rp10-back-on-trial");
        window.showMain();
        settle(100);
    }
    beforeCheck(window, save, expect, "br09-go-back-on-trial", "going back while on trial", [&] { window.goBack(); },
                {"Go back to ", "The computer restarts into ", "left as it is, so its changes can be tried again",
                 "recab rollback", "reboot", "password"},
                "Restart into it");
    beforeCheck(window, save, expect, "br10-keep", "keeping the running system", [&] { window.keep(); },
                {"Keep the running system?", "becomes the system that every restart starts", "does not restart",
                 "recab commit", "password"},
                "Keep it");
    window.keep(false);
    waitUntilIdle(window);
    // A commit by hand leaves recab's last judged trial as it was: none here.
    expect("keeping it makes it the kept system",
           window.status().state == "stable" && window.status().runningSlot == "B"
               && window.status().trialOutcome.isEmpty() && window.status().otherRole == "previous");
    save(window, "r4b-on-the-new-system");
    {
        const QString back = backWords(window);
        expect("Getting back says it is kept already, and that recab holds no judged trial",
               back.contains("Kept already.") && back.contains("recab holds no record of one")
                   && back.contains("[Keep it](off)") && back.contains("[Go back…]") && !back.contains("[Go back…](off)"));
        save(window, "rp11-back-kept");
        window.showMain();
        settle(100);
    }

    beforeCheck(window, save, expect, "br11-go-back", "going back", [&] { window.goBack(); },
                {"Go back to ", "is kept beside it, so you can return", "recab rollback", "reboot", "password"},
                "Restart into it");
    window.goBack(false);
    waitUntilIdle(window);
    expect("going back lands on the first slot", window.status().runningSlot == "A");
    save(window, "r5-back");
    {
        const QString back = backWords(window);
        expect("after going back, the system kept is the one before, and the trial was of the other",
               back.contains("Kept already.") && back.contains("recab holds no record of one")
                   && back.contains("Pick \"LevitateOS (Slot A)\". That is the running system today"));
        save(window, "rp12-back-after-going-back");
        window.showMain();
        settle(100);
    }

    expect("after going back the system from before is there to be reused", window.status().otherRole == "previous");
    makeChange(window, "nvidia");
    expect("a second change opens a next system again",
           window.status().state == "preparing" && window.status().changes.size() == 1);
    const QString before = window.status().preparationId;
    beforeCheck(window, save, expect, "br13-reset", "resetting the next system", [&] { window.reset(); },
                {"Reset the next system to the running one?", "The next system holds one change",
                 "cannot be recovered afterwards", "recab discard", "recab prepare", "password"},
                "Reset");
    window.reset(false);
    waitUntilIdle(window);
    expect("the reset leaves a new, empty next system",
           window.status().state == "preparing" && window.status().changes.isEmpty()
               && !window.status().preparationId.isEmpty() && window.status().preparationId != before);
    save(window, "r6-reset");
    {
        const QString next = nextWords(window);
        expect("after the reset the next system holds nothing", next.contains("The next system holds no changes yet")
                                                                    && next.contains("Nothing yet") && window.frame()->badge("next") == 0);
        save(window, "rp13-next-reset");
        window.showMain();
        settle(100);
    }
    beforeCheck(window, save, expect, "br14-discard", "discarding the next system", [&] { window.discard(); },
                {"Discard the next system?", "The next system holds no changes yet.", "cannot be recovered afterwards",
                 "its place was already reused", "recab discard", "password"},
                "Discard");
    window.discard(false);
    waitUntilIdle(window);
    expect("discarding leaves no next system",
           window.status().state == "stable" && window.status().preparationId.isEmpty());
    save(window, "r7-discarded");

    // recab says the system needs recovery: the page says so in recab's words and lists every step with its verdict.
    {
        const QString reason = "initial A/B installation is incomplete; resume recab init on the mounted installation target";
        qputenv("RECAB_STANDIN_UNUSABLE", reason.toUtf8());
        window.refresh();
        waitUntilIdle(window);
        const QString overview = overviewWords(window);
        auto *page = window.findChild<SystemPage *>();
        expect("an unusable system is not called interrupted: the page says recab says it needs recovery, with recab's reason",
               overview.contains("recab says this system needs recovery") && overview.contains(reason)
                   && !overview.contains("Preparing the next system was interrupted")
                   && overview.contains("Nothing else is offered here while recab says this system needs recovery."));
        expect("the recovery list is shown, every step recab offers with its own verdict: all off but the restart of the computer",
               groupTitles(page).contains("Software, updates and recovery")
                   && overview.contains("[Prepare or continue the other system](off)")
                   && overview.contains("[Assess or reconcile trial recovery](off)") && overview.contains("[Reboot]")
                   && overview.contains("[Check for system updates](off)"));
        bool reasonShown = false;
        for (QPushButton *button : page->findChildren<QPushButton *>())
            if (button->text() == "Check for system updates")
                reasonShown = button->toolTip().contains(reason);
        expect("each refused step carries recab's reason", reasonShown);
        save(window, "r19-unusable");
        const QString next = nextWords(window);
        expect("the next system's page says the same and shows no step of its own",
               next.contains("recab says this system needs recovery") && next.contains(reason) && !next.contains("[Finish…]"));
        save(window, "r19b-unusable-next");
        qunsetenv("RECAB_STANDIN_UNUSABLE");
        window.refresh();
        waitUntilIdle(window);
        window.showMain();
        settle(100);
        expect("without it the system is stable again", window.status().state == "stable");
    }

    // A change cut off half way is left interrupted; recab offers its own operation to recover it.
    {
        qputenv("RECAB_STANDIN_STOP_AT", "change");
        const QString marker = QDir(qEnvironmentVariable("RECAB_STANDIN_STATE")).filePath("stopped-at-change");
        QFile::remove(marker);
        // The tool making the change is ended half way (the stand-in stops there and waits).
        const QString recab = qEnvironmentVariable("RECAB_GUI_RECAB");
        QProcess prepare;
        prepare.start(recab, {"--json", "prepare"});
        prepare.waitForFinished(120000);
        window.refresh();
        waitUntilIdle(window);
        QProcess tool;
        tool.start(recab, {"--json", "edit", "/etc/ssh/sshd_config", "--preparation", window.status().preparationId, "--revision",
                           QString::number(window.status().preparationRevision), "--actor", "tool"});
        waitFor([&] { return QFile::exists(marker); }, 30);
        tool.terminate();
        tool.waitForFinished(10000);
        qunsetenv("RECAB_STANDIN_STOP_AT");
        window.refresh();
        waitUntilIdle(window);
        save(window, "r18-change-cut-off");
        expect("a cut-off change is reported as interrupted, with recab's recover step ready and every new change refused",
               window.status().state == "interrupted" && !window.status().preparationId.isEmpty()
                   && window.status().changes.isEmpty() && window.status().operation("change_recover").ready
                   && !window.status().operation("install").ready && !window.status().operation("prepare").ready
                   && window.status().operation("prepare").reason == "the previous operation requires recovery before reopening");
        const QString overview = overviewWords(window);
        expect("Overview says it stopped and why, and the recovery list offers recab's own step, not the others",
               overview.contains("Preparing the next system was interrupted") && overview.contains("system publication must finish")
                   && groupTitles(window.findChild<SystemPage *>()).contains("Software, updates and recovery")
                   && overview.contains("[Recover the interrupted software/configuration change]")
                   && !overview.contains("[Recover the interrupted software/configuration change](off)")
                   && overview.contains("[Assess or reconcile trial recovery](off)")
                   && overview.contains("[Finalize the prepared system](off)"));
        save(window, "r18b-interrupted-recovery-list");
        auto press = [&] { clickButton(window.findChild<SystemPage *>(), "Recover the interrupted software/configuration change"); };
        beforeCheck(window, save, expect, "br15-recover-change", "recovering the cut-off change", press,
                    {"Recover the interrupted software/configuration change?", "This runs one operation of recab",
                     "recab change recover", "password"},
                    "Continue");
        press();
        waitFor([&] { return window.beforePage()->isVisible() && !window.busy(); }, 10);
        pressGo(window, "Continue");
        expect("recab's recover finishes the change: the next system is open again and holds it, by the person who asked",
               window.status().state == "preparing" && window.status().changes.size() == 1
                   && window.status().operation("change_recover").ready == false);
        save(window, "r18c-change-recovered");
        window.discard(false);
        waitUntilIdle(window);
        expect("discarding leaves no next system", window.status().state == "stable" && window.status().preparationId.isEmpty());
    }

    // A restart that fails, and the computer comes back; then the same again, kept by recab itself.
    makeChange(window, "ssh");
    window.finish(false);
    waitUntilIdle(window);
    qputenv("RECAB_STANDIN_TRIAL", "fail");
    window.restart(false);
    waitUntilIdle(window);
    qunsetenv("RECAB_STANDIN_TRIAL");
    expect("the restart failed and the computer came back", window.status().state == "failed"
               && window.status().runningSlot == "A" && window.status().trialOutcome == "failed");
    {
        const QString overview = overviewWords(window);
        expect("the first view after a failed start says the computer came back, with recab's detail, and the one thing to do",
               overview.contains("The next system did not start, so the computer came back")
                   && overview.contains("startup ended with failed units: sddm.service")
                   && overview.contains("[Copy for agent]") && overview.contains("[Try the same again]")
                   && !overview.contains("What to do next"));
        save(window, "r13-came-back");
        QStringList rows;
        const QString history = historyWords(window, &rows);
        expect("the history lists the restart that came back, and the change that went into it",
               rows.value(0).contains("Came back: it did not start properly") && rows.value(1).contains("A restart into the next system was set")
                   && rows.value(2).contains("The next system was finished")
                   && rows.value(3).contains("sshd_config, edited") && rows.value(3).contains("Went into a restart that came back"));
        expect("and the detail says what recab recorded", history.contains("recab recorded: startup ended with failed units: sddm.service"));
        save(window, "r13b-history-came-back");
        window.showMain();
        settle(100);
    }
    window.restart(false);
    waitUntilIdle(window);
    expect("the same again starts, and recab keeps it itself",
           window.status().state == "stable" && window.status().runningSlot == "B" && window.status().trialOutcome == "kept");
    {
        const QString overview = overviewWords(window);
        expect("the first view after a kept restart says so, and when, and that nothing is waiting",
               overview.contains("You are on the new system") && overview.contains("Kept: it started properly on")
                   && overview.contains("Nothing is waiting.") && !overview.contains("[Keep it now]"));
        save(window, "r14-kept-by-recab");
        window.showMain();
        settle(100);
    }

    // What happened, newest first, as recab's history lists it.
    {
        QStringList rows;
        const QString history = historyWords(window, &rows);
        expect("the history page is read from recab and lists what happened", history.contains("History")
                   && history.contains("[All]") && history.contains("[Restarts]") && !history.contains("[By the ")
                   && history.contains("[Save as a file…]") && rows.size() >= 20);
        auto row = [&rows](const QString &part) {
            for (const QString &one : rows)
                if (one.contains(part))
                    return one;
            return QString();
        };
        expect("the newest entry is the kept restart",
               rows.value(0).contains("Restart into the next system (Slot B)") && rows.value(0).contains("Kept: start-up finished with nothing failed"));
        expect("before it, the restart was set again, and the one before that came back, in recab's words",
               rows.value(1).contains("A restart into the next system was set (Slot B)")
                   && rows.value(2).contains("Restart into the next system (Slot B)")
                   && rows.value(2).contains("Came back: it did not start properly"));
        expect("a change by a tool is labelled as the caller gave it, and one by the person says so",
               row("edited").contains("tool") && row("htop, installed").contains("You"));
        expect("the change that went into the restart kept by hand says so",
               row("htop, installed").contains("Went into a restart that was kept"));
        expect("a change that went into restarts that ended kept says so, by the last one",
               row("sshd_config, edited").contains("Went into a restart that was kept"));
        expect("a release merge, a set restart, going back and giving up are listed",
               !row("A release was merged into the next system").isEmpty() && !row("A restart into the next system was set").isEmpty()
                   && !row("Going back to the system from before was set").isEmpty() && !row("The next system was given up").isEmpty()
                   && !row("The running system was kept").isEmpty() && !row("The set restart was cancelled").isEmpty());
        expect("an entry that names nobody does not claim one", !row("The next system was finished").contains("You"));
        save(window, "r15-history");
        // The filters.
        auto *page = window.findChild<HistoryPage *>();
        clickButton(page, QStringLiteral("Restarts"));
        settle(200);
        auto *tree = page->findChild<QTreeWidget *>();
        bool onlyRestarts = tree && tree->topLevelItemCount() > 0;
        for (int i = 0; tree && i < tree->topLevelItemCount(); ++i) {
            const QString what = tree->topLevelItem(i)->text(1);
            onlyRestarts &= what.contains("restart", Qt::CaseInsensitive) || what.contains("kept") || what.contains("Going back");
        }
        expect("the Restarts view lists only restarts, their keeping and going back", onlyRestarts);
        save(window, "r15b-history-restarts");
        clickButton(page, QStringLiteral("All"));
        settle(200);
        const QString path = QDir(folder).filePath("r-history.txt");
        QFile::remove(path);
        expect("the history is written as a file", window.saveHistory(path) && fileText(path).contains("Kept: start-up finished"));
        window.showMain();
        settle(100);
    }
    // A recab with a history it cannot read says why, with a way to try again.
    {
        qputenv("RECAB_STANDIN_NO_HISTORY", "1");
        const QString history = historyWords(window);
        qunsetenv("RECAB_STANDIN_NO_HISTORY");
        expect("a recab without the command says it keeps no history", history.contains("This recab keeps no history it can show."));
        window.showMain();
        settle(100);
    }

    // A restart that fails and leaves recab no preparation record for the failed system: it offers to replace it.
    {
        makeChange(window, "nvidia");
        window.finish(false);
        waitUntilIdle(window);
        qputenv("RECAB_STANDIN_TRIAL", "fail-no-record");
        window.restart(false);
        waitUntilIdle(window);
        qunsetenv("RECAB_STANDIN_TRIAL");
        expect("the restart failed, the computer came back, and recab keeps no preparation for the failed system",
               window.status().state == "failed" && window.status().preparationId.isEmpty() && window.status().otherRole == "next");
        const QJsonObject replace = window.status().operationObject("replace_failed");
        expect("recab offers to replace it, marked destructive, in its own label, and refuses to prepare with its reason",
               window.status().operation("replace_failed").ready && replace["destructive"].toBool()
                   && replace["label"].toString() == "Replace failed slot A with the running system"
                   && !window.status().operation("prepare").ready
                   && window.status().operation("prepare").reason.startsWith("The failed generation has no preparation record."));
        const QString overview = overviewWords(window);
        keepWords(folder, "r23-overview.txt", overview);
        expect("Overview offers it beside trying again, and says the next system starts nowhere meanwhile",
               overview.contains("The next system did not start, so the computer came back")
                   && overview.contains("[Replace it with the running system…]") && !overview.contains("[Replace it with the running system…](off)")
                   && overview.contains("[Try the same again](off)"));
        save(window, "r23-failed-no-record");
        const QString release = releaseWords(window);
        expect("the release page offers it as recab's step, with its destructive mark, and says adding a release is off",
               release.contains("The failed system") && release.contains("recab offers: Replace failed slot A with the running system. "
                                                                         "recab marks it destructive.")
                   && release.contains("[Replace it…]"));
        save(window, "r23b-failed-no-record-release");
        window.showMain();
        settle(100);
        beforeCheck(window, save, expect, "br18-replace-failed", "replacing the failed system",
                    [&] { window.replaceFailed(); },
                    {"Replace failed slot A with the running system?", "Replace failed slot A with the running system.",
                     "recab says: The failed generation has no preparation record. Retry its boot, or explicitly replace it; its "
                     "changes are kept until then.",
                     "recab marks it destructive.", "recab prepare", "password"},
                    "Replace it");
        window.replaceFailed(false);
        waitUntilIdle(window);
        expect("replacing it makes a new next system, as a copy of the running one, and the failed record no longer counts",
               window.status().state == "preparing" && window.status().changes.isEmpty() && !window.status().preparationId.isEmpty()
                   && window.status().operationObject("replace_failed").isEmpty());
        save(window, "r23c-failed-replaced");
        window.discard(false);
        waitUntilIdle(window);
    }

    out << (ok ? "all flows ended where they should\n" : "some flows did not end where they should\n");
    return ok ? 0 : 1;
}
