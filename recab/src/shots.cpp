#include "shots.h"

#include "look.h"
#include "pages.h"
#include "window.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString scenarioFile()
{
    return qEnvironmentVariable("RECAB_STANDIN_STATE");
}

// What the stand-in recab holds now: the truth the window is checked against.
QJsonObject standIn()
{
    QFile file(scenarioFile());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QPushButton *buttonNamed(QWidget *inside, const QString &words)
{
    for (QPushButton *button : inside->findChildren<QPushButton *>())
        if (button->text() == words && button->isVisibleTo(inside))
            return button;
    return nullptr;
}

} // namespace

int takeShots(Window &window, const QString &folder)
{
    QDir().mkpath(folder);
    QTextStream out(stdout);
    bool ok = true;
    QString everything; // every word the window said, for the check of words it must never use
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
               window.width() == 760 && window.height() == 560);
        ok &= window.grab().save(QDir(folder).filePath(name + ".png"));
    };
    auto idle = [&] {
        settle(100);
        waitFor([&] { return !window.busy(); }, 20);
        settle(250);
    };
    // The words on the page that shows now.
    auto words = [&]() -> QString {
        const QString said = window.showingRoots()    ? window.rootsPage()->pageText()
                             : window.showingOutput() ? window.outputPage()->pageText()
                                                      : window.beforePage()->pageText();
        everything += said + QLatin1Char('\n');
        return said;
    };
    // Starts the stand-in over in `scenario`, and reads it.
    auto scene = [&](const QString &scenario) {
        qputenv("RECAB_STANDIN_SCENARIO", scenario.toLocal8Bit());
        QFile::remove(scenarioFile());
        if (!window.showingRoots())
            window.beforePage()->cancelled(); // leaves a page that is in the way
        window.refresh();
        idle();
    };
    const bool helper = !qEnvironmentVariable("RECAB_GUI_ROOT_HELPER").isEmpty();
    const QString copyWords = QStringLiteral("Make the other root a copy of the booted root");
    const QString startWords = QStringLiteral("Start the other root at the next boot");
    const QString stopWords = QStringLiteral("Do not start the other root at the next boot");
    const QString restartWords = QStringLiteral("Restart now");

    window.show();
    // A picture run that is not told of its stand-ins refuses to start.
    guardRefuses(expect, folder, "RECAB_GUI_RECAB");
    expect(QStringLiteral("the window is 760 x 560"), window.width() == 760 && window.height() == 560);

    // ---- Loading: the window says it is reading before recab has answered.
    expect(QStringLiteral("while recab is asked, the page says so (%1)").arg(words().left(80)),
           window.busy() && words().contains(QStringLiteral("Reading the two roots")));
    save("r0-loading");
    idle();

    // ---- Normal: A booted and locked, B unlocked, mounted, startable.
    QString said = words();
    out << "normal:\n" << said << "\n";
    expect(QStringLiteral("the window opened on the two roots, read as A booted and B the other"),
           window.showingRoots() && window.status().booted == "A" && window.status().other == "B");
    for (const QString &phrase : {QStringLiteral("The two roots"), QStringLiteral("Root A"), QStringLiteral("Booted — locked"),
                                  QStringLiteral("Root B"), QStringLiteral("Other — unlocked"),
                                  QStringLiteral("Mounted at /run/recab/other."), QStringLiteral("Starts at the next boot.")})
        expect(QStringLiteral("normal says \"%1\"").arg(phrase), said.contains(phrase));
    expect(QStringLiteral("normal offers the three actions, exactly"),
           buttonNamed(window.rootsPage(), copyWords) && buttonNamed(window.rootsPage(), startWords)
               && buttonNamed(window.rootsPage(), restartWords) && !buttonNamed(window.rootsPage(), stopWords));
    expect(QStringLiteral("normal: all three are enabled and no reason is given"),
           window.rootsPage()->copyButton()->isEnabled() && window.rootsPage()->startButton()->isEnabled()
               && window.rootsPage()->restartButton()->isEnabled() && window.rootsPage()->startReason().isEmpty());
    expect(QStringLiteral("normal: it does not say the boot default is unknown"),
           !said.contains(QStringLiteral("Neither root is the boot default")) && !said.contains(QStringLiteral("not mounted"))
               && !said.contains(QStringLiteral("cannot be started")));
    expect(QStringLiteral("the status line says when the roots were read (%1)").arg(window.statusLine()),
           window.statusLine().startsWith(QStringLiteral("Read at ")));
    save("r1-normal");

    // ---- Start the other root, once.
    buttonNamed(window.rootsPage(), startWords)->click();
    idle();
    said = words();
    expect(QStringLiteral("the click ran `boot-other`: recab says B is picked once (%1)").arg(standIn()["next_boot_once"].toString()),
           standIn()["next_boot_once"].toString() == "B");
    expect(QStringLiteral("then the page says B starts at the next boot, once, and offers to take it back"),
           window.showingRoots() && said.contains(QStringLiteral("Starts at the next boot, once.")) && buttonNamed(window.rootsPage(), stopWords)
               && !buttonNamed(window.rootsPage(), startWords) && window.status().nextOnce == "B");
    expect(QStringLiteral("and the status line says it (%1)").arg(window.statusLine()),
           window.statusLine().contains(QStringLiteral("Root B starts at the next boot, once.")));
    save("r2-once-set");
    // ---- Take it back.
    buttonNamed(window.rootsPage(), stopWords)->click();
    idle();
    said = words();
    expect(QStringLiteral("the click ran `boot-other --cancel`: no one-time pick is left (%1)")
               .arg(QString::fromUtf8(QJsonDocument(standIn()).toJson(QJsonDocument::Compact))),
           standIn()["next_boot_once"].isNull());
    expect(QStringLiteral("then A starts at the next boot again and the button is back"),
           said.contains(QStringLiteral("Starts at the next boot.")) && !said.contains(QStringLiteral("once."))
               && buttonNamed(window.rootsPage(), startWords) && !buttonNamed(window.rootsPage(), stopWords));

    // ---- A one-time pick that was already there.
    scene("once");
    said = words();
    expect(QStringLiteral("a pick made elsewhere shows: B starts once, and the button takes it back"),
           window.status().nextOnce == "B" && said.contains(QStringLiteral("Starts at the next boot, once."))
               && said.contains(QStringLiteral("Starts at the next boot.")) == false && buttonNamed(window.rootsPage(), stopWords));
    save("r3-once");

    // ---- B booted: the letters follow the roots, not the other way round.
    scene("booted-b");
    said = words();
    expect(QStringLiteral("B booted: the window says B is booted and A the other"),
           window.status().booted == "B" && window.status().other == "A" && said.contains(QStringLiteral("Root B"))
               && said.contains(QStringLiteral("Root A")) && said.contains(QStringLiteral("Starts at the next boot.")));
    save("r4-booted-b");

    // ---- The other root is not mounted.
    scene("not-mounted");
    said = words();
    expect(QStringLiteral("not mounted: the other root says so, and can still be started"),
           said.contains(QStringLiteral("Not mounted.")) && !said.contains(QStringLiteral("Mounted at"))
               && window.rootsPage()->startButton()->isEnabled());
    save("r5-not-mounted");

    // ---- No boot default.
    scene("no-default");
    expect(QStringLiteral("no boot default: the page says neither root is the default"),
           words().contains(QStringLiteral("Neither root is the boot default.")));
    save("r6-no-default");

    // ---- A copy that did not finish: the other root cannot be started.
    scene("not-bootable");
    said = words();
    expect(QStringLiteral("not startable: the other root says why, the start button is disabled with a one-line reason"),
           said.contains(QStringLiteral("It cannot be started: a copy into it did not finish."))
               && !window.rootsPage()->startButton()->isEnabled()
               && window.rootsPage()->startReason().contains(QStringLiteral("cannot be started"))
               && !window.rootsPage()->startReason().contains(QLatin1Char('\n')));
    expect(QStringLiteral("not startable: copying and restarting stay possible"),
           window.rootsPage()->copyButton()->isEnabled() && window.rootsPage()->restartButton()->isEnabled());
    save("r7-not-bootable");

    // ---- Make the other root a copy: first it asks.
    qputenv("RECAB_STANDIN_SPEED", "1");
    window.rootsPage()->copyButton()->click();
    settle(300);
    said = words();
    expect(QStringLiteral("the copy asks first, on the Before page, and says it overwrites the other root"),
           window.showingBefore() && said.contains(QStringLiteral("Nothing has started.")) && said.contains(QStringLiteral("Make root B a copy of root A?"))
               && said.contains(QStringLiteral("Root B is overwritten with a fresh copy of root A"))
               && said.contains(QStringLiteral("Everything on root B is lost")) && said.contains(QStringLiteral("Root A is not changed."))
               && said.contains(QStringLiteral("recab copy")) && !window.busy());
    expect(QStringLiteral("it says the truth about the password (%1)").arg(helper ? "asked" : "none asked"),
           helper ? said.contains(QStringLiteral("You are asked for the administrator password."))
                  : said.contains(QStringLiteral("no password is asked")));
    save("r8-before-copy");
    buttonNamed(window.beforePage(), QStringLiteral("Cancel"))->click();
    settle(200);
    expect(QStringLiteral("Cancel copies nothing: the roots show, nothing runs, the other root is still not startable"),
           window.showingRoots() && !window.busy() && standIn()["other_bootable"].toBool() == false);
    // ---- Then it does, with its progress shown.
    window.rootsPage()->copyButton()->click();
    settle(300);
    QPushButton *go = buttonNamed(window.beforePage(), QStringLiteral("Overwrite root B"));
    expect(QStringLiteral("the Before page has the button \"Overwrite root B\""), go != nullptr);
    if (go)
        go->click();
    settle(700);
    said = words();
    expect(QStringLiteral("while it runs, recab's own words are the progress (%1)").arg(said.left(60)),
           window.showingOutput() && window.busy() && said.contains(QStringLiteral("Making root B a copy of root A"))
               && said.contains(QStringLiteral("recab: ")));
    save("r9-copying");
    idle();
    qputenv("RECAB_STANDIN_SPEED", "0");
    said = words();
    expect(QStringLiteral("when it ends the page says it is done, with recab's result (%1)").arg(said.left(120)),
           window.outputPage()->finished() && said.contains(QStringLiteral("It is done"))
               && said.contains(QStringLiteral("Root B is now a copy of root A")) && said.contains(QStringLiteral("Copy for agent"))
               && said.contains(QStringLiteral("Back")));
    expect(QStringLiteral("the copy happened: B can be started now (%1)").arg(standIn()["other_bootable"].toBool()),
           standIn()["other_bootable"].toBool() && window.status().otherBootable);
    QApplication::clipboard()->clear();
    buttonNamed(window.outputPage(), QStringLiteral("Copy for agent"))->click();
    expect(QStringLiteral("\"Copy for agent\" copies the whole output and starts nothing"),
           QApplication::clipboard()->text().contains(QStringLiteral("creating root B's filesystem anew"))
               && QApplication::clipboard()->text().contains(QStringLiteral("writing root B's boot files")) && !window.busy());
    save("r10-copied");
    buttonNamed(window.outputPage(), QStringLiteral("Back"))->click();
    settle(200);
    said = words();
    expect(QStringLiteral("Back shows the roots as they are now: startable, no reason"),
           window.showingRoots() && !said.contains(QStringLiteral("cannot be started")) && window.rootsPage()->startButton()->isEnabled()
               && window.rootsPage()->startReason().isEmpty());

    // ---- A copy that fails.
    scene("copy-failing");
    window.copyOther(false);
    idle();
    said = words();
    expect(QStringLiteral("a failing copy: the page says it stopped and why (%1)").arg(said.left(160)),
           window.showingOutput() && window.outputPage()->finished() && said.contains(QStringLiteral("The copy stopped"))
               && said.contains(QStringLiteral("No space left on device")) && !said.contains(QStringLiteral("It is done")));
    QApplication::clipboard()->clear();
    buttonNamed(window.outputPage(), QStringLiteral("Copy for agent"))->click();
    expect(QStringLiteral("\"Copy for agent\" copies the reason and the output"),
           QApplication::clipboard()->text().contains(QStringLiteral("No space left on device"))
               && QApplication::clipboard()->text().contains(QStringLiteral("copying root A into root B")));
    save("r11-copy-failed");
    buttonNamed(window.outputPage(), QStringLiteral("Back"))->click();
    idle();
    said = words();
    expect(QStringLiteral("after the failed copy B cannot be started, and the page says so"),
           window.showingRoots() && !window.rootsPage()->startButton()->isEnabled()
               && said.contains(QStringLiteral("a copy into it did not finish")));
    save("r12-after-failed-copy");

    // ---- Restart now: it asks first.
    scene("normal");
    const QString mark = qEnvironmentVariable("RECAB_STANDIN_REBOOT_MARK");
    QFile::remove(mark);
    window.rootsPage()->restartButton()->click();
    settle(300);
    said = words();
    expect(QStringLiteral("restart asks first and says which root starts"),
           window.showingBefore() && said.contains(QStringLiteral("Restart now?")) && said.contains(QStringLiteral("Root A, the boot default, starts."))
               && !QFileInfo::exists(mark));
    save("r13-before-restart");
    buttonNamed(window.beforePage(), QStringLiteral("Cancel"))->click();
    settle(200);
    expect(QStringLiteral("Cancel restarts nothing"), window.showingRoots() && !QFileInfo::exists(mark));
    window.rootsPage()->restartButton()->click();
    settle(300);
    buttonNamed(window.beforePage(), restartWords)->click();
    idle();
    expect(QStringLiteral("the restart was asked for (the stand-in made its mark)"), QFileInfo::exists(mark));
    expect(QStringLiteral("and the status line says so (%1)").arg(window.statusLine()),
           window.showingRoots() && window.statusLine().contains(QStringLiteral("restarting")));
    // With a one-time pick, the restart says it is the one that starts.
    scene("once");
    window.restart(true);
    settle(300);
    expect(QStringLiteral("with a one-time pick the restart says root B starts, picked for this one boot"),
           words().contains(QStringLiteral("Root B, picked for this one boot, starts.")));
    buttonNamed(window.beforePage(), QStringLiteral("Cancel"))->click();
    settle(200);
    // A restart that is refused says why.
    qputenv("RECAB_STANDIN_REBOOT_FAIL", "1");
    window.restart(false);
    idle();
    said = words();
    expect(QStringLiteral("a refused restart: the page says the computer did not restart, with the reason (%1)").arg(said.left(120)),
           window.showingOutput() && said.contains(QStringLiteral("The computer did not restart"))
               && said.contains(QStringLiteral("Operation not permitted")));
    save("r14-restart-failed");
    qunsetenv("RECAB_STANDIN_REBOOT_FAIL");
    buttonNamed(window.outputPage(), QStringLiteral("Back"))->click();
    idle();

    // ---- Reading needs the administrator.
    scene("root-only");
    said = words();
    expect(QStringLiteral("when only the administrator can read, the page says the roots could not be read, with Try again and Copy for agent (%1)")
               .arg(said.left(120)),
           window.showingRoots() && said.contains(QStringLiteral("The two roots could not be read")) && said.contains(QStringLiteral("Try again"))
               && said.contains(QStringLiteral("Copy for agent")));
    expect(QStringLiteral("it offers \"Read it as administrator\" exactly when there is a root helper (%1)").arg(helper ? "yes" : "no"),
           helper ? said.contains(QStringLiteral("Read it as administrator")) : !said.contains(QStringLiteral("Read it as administrator")));
    save("r15-read-refused");
    if (helper) {
        buttonNamed(window.rootsPage(), QStringLiteral("Read it as administrator"))->click();
        idle();
        expect(QStringLiteral("read as administrator, the roots show"),
               window.showingRoots() && words().contains(QStringLiteral("Booted — locked")) && window.status().booted == "A");
        save("r16-read-as-administrator");
    }

    // ---- F5 reads again what changed outside the window.
    scene("normal");
    {
        QJsonObject changed = standIn();
        changed["next_boot_once"] = QStringLiteral("B");
        QFile file(scenarioFile());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(changed).toJson(QJsonDocument::Compact));
    }
    expect(QStringLiteral("before F5 the window still shows the old state"), window.status().nextOnce.isEmpty());
    QAction *f5 = nullptr;
    for (QAction *action : window.actions())
        for (const QKeySequence &key : action->shortcuts())
            if (key == QKeySequence(Qt::Key_F5))
                f5 = action;
    expect(QStringLiteral("F5 is bound"), f5 != nullptr);
    if (f5)
        f5->trigger();
    idle();
    expect(QStringLiteral("F5 reads again: the pick made elsewhere shows"),
           window.status().nextOnce == "B" && buttonNamed(window.rootsPage(), stopWords) != nullptr);

    // ---- Words that are not part of this window's concepts, anywhere it said anything.
    static const QRegularExpression never(QStringLiteral("trial|seal|candidate|commit|rollback|generation|adoption|prepar|finali[sz]e|next system|known-good"),
                                          QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch bad = never.match(everything);
    expect(QStringLiteral("no word of the old lifecycle was said (%1)").arg(bad.hasMatch() ? bad.captured() : QString()), !bad.hasMatch());
    QString noCopyForAgent = everything;
    noCopyForAgent.remove(QStringLiteral("Copy for agent"));
    expect(QStringLiteral("nothing agent-like but \"Copy for agent\""), !noCopyForAgent.contains(QStringLiteral("agent"), Qt::CaseInsensitive));

    out << (ok ? "all as expected\n" : "some things were not as expected\n");
    return ok ? 0 : 1;
}
