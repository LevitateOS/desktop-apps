#include "helpstory.h"

#include "firstrun.h"
#include "frame.h"
#include "help.h"
#include "look.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

namespace helpstory {

namespace {

QString appWord(Frame::App app)
{
    return app == Frame::Recipe ? QStringLiteral("Recipe") : app == Frame::Recab ? QStringLiteral("Recab")
                                                                                 : QStringLiteral("Recdirstat");
}

QString appVariable(Frame::App app)
{
    return app == Frame::Recipe ? QStringLiteral("LEVITATE_APP_RECIPE")
           : app == Frame::Recab ? QStringLiteral("LEVITATE_APP_SYSTEM")
                                   : QStringLiteral("LEVITATE_APP_DISK");
}

} // namespace

void run(const Plan &plan, const Save &save, const Expect &expect, const QString &prefix)
{
    Frame *frame = plan.frame;
    const Frame::App app = plan.app;
    const HelpText text = helpText(app);
    const QString name = text.name;

    // ---- The rail
    const QStringList titles = frame->entryTitles();
    expect(QStringLiteral("the rail ends with Help (%1)").arg(titles.join(QStringLiteral(", "))),
           !titles.isEmpty() && titles.last() == QStringLiteral("Help"));

    // ---- The page
    frame->choose(Frame::helpKey());
    plan.idle();
    expect(QStringLiteral("Help is showing and marked, and no other page of the frame is"),
           frame->selected() == Frame::helpKey() && !frame->firstRunShown());
    HelpPage *help = frame->help();
    const QString shown = help->pageText();
    for (const QString &words :
         {QStringLiteral("Help"), QStringLiteral("What %1 is for").arg(name), text.purpose, text.never,
          QStringLiteral("The ideas you need"), QStringLiteral("How do I…"),
          QStringLiteral("The other two apps"),
          QStringLiteral("Show the first page again")})
        expect(QStringLiteral("the Help page says: %1").arg(words.left(60)), shown.contains(words));
    for (const HelpIdea &idea : text.ideas)
        expect(QStringLiteral("the Help page has the idea \"%1\"").arg(idea.title),
               shown.contains(idea.title) && shown.contains(idea.first) && shown.contains(idea.more.left(40)));
    for (const HelpRow &row : text.how)
        expect(QStringLiteral("the Help page has \"%1\" with its link").arg(row.title),
               shown.contains(row.title) && shown.contains(row.words) && help->links().contains(QStringLiteral("Go to %1").arg(row.page)));
    if (!text.wrong.isEmpty())
        expect(QStringLiteral("the Help page has \"When something is wrong\""), shown.contains(QStringLiteral("When something is wrong")));
    for (const HelpRow &row : text.wrong)
        expect(QStringLiteral("the Help page has \"%1\"").arg(row.title), shown.contains(row.title) && shown.contains(row.words));
    expect(QStringLiteral("it offers to open the other two apps and not this one"),
           shown.contains(QStringLiteral("Open %1").arg(appWord(text.others[0].first)))
               && shown.contains(QStringLiteral("Open %1").arg(appWord(text.others[1].first)))
               && !shown.contains(QStringLiteral("Open %1").arg(name)));
    // No thing the app has not got: the words below are what the README lists as not done or not true.
    for (const char *untrue : {"Search this page", "Ctrl+K", "recipe-gui --show", "disk-gui ~/", "Go to anything", "cleared through Recipe"})
        expect(QStringLiteral("the page does not say: %1").arg(QLatin1String(untrue)), !shown.contains(QLatin1String(untrue)));
    plan.idle();
    save(*plan.window, prefix + "-help");
    // The page scrolls: a picture of each screen of it.
    for (int screen = 2; screen <= 8 && help->scrollDown(); ++screen)
        save(*plan.window, QStringLiteral("%1-help-%2").arg(prefix).arg(screen));

    // ---- Every link goes where it says
    int links = 0;
    QStringList words;
    for (const HelpRow &row : text.how + text.wrong)
        if (!row.target.isEmpty())
            words << QStringLiteral("Go to %1").arg(row.page);
    expect(QStringLiteral("the page has one link for each row that has a page (%1)").arg(words.join(QStringLiteral(", "))),
           help->links().size() == words.size());
    auto landed = [&](const QString &target) {
        return frame->selected() == target && !frame->helpShown() && plan.pageShown(target);
    };
    QList<QPair<QString, QString>> goes; // link words, target
    for (const HelpRow &row : text.how + text.wrong)
        if (!row.target.isEmpty())
            goes << qMakePair(QStringLiteral("Go to %1").arg(row.page), row.target);
    for (const auto &go : goes) {
        frame->choose(Frame::helpKey());
        plan.idle();
        const bool pressed = frame->help()->press(go.first);
        plan.idle();
        const bool there = landed(go.second);
        expect(QStringLiteral("\"%1\" goes to %2 (pressed %3, marked %4)").arg(go.first, go.second).arg(pressed).arg(frame->selected()),
               pressed && there);
        ++links;
        if (links == 2)
            save(*plan.window, prefix + "-help-went");
    }

    // ---- The other apps' buttons, and the first page again
    {
        Frame::App other = text.others[0].first;
        frame->choose(Frame::helpKey());
        frame->flash(QString());
        const bool pressed = frame->help()->press(QStringLiteral("Open %1").arg(appWord(other)));
        // Named in the environment it is started (a stand-in); not named, a picture run says it does not start it.
        const bool named = qEnvironmentVariableIsSet(appVariable(other).toLatin1().constData());
        expect(QStringLiteral("\"Open %1\" asks the frame to start it (%2)").arg(appWord(other), frame->statusText()),
               pressed && (named ? !frame->statusText().contains(QStringLiteral("could not be started"))
                                 : frame->statusText().contains(QStringLiteral("%1 is not started in a picture run").arg(appWord(other)))));
    }
    frame->choose(Frame::helpKey());
    expect(QStringLiteral("\"Show the first page again\" shows the first page"),
           frame->help()->press(QStringLiteral("Show the first page again")) && frame->firstRunShown());
    // Going on from the first page returns to the page the app is on: here, the one it opens on.
    frame->choose(plan.firstEntry);
    plan.idle();
    frame->flash(QString());

    // ---- The first page
    FirstRunPage *page = frame->firstRun();
    frame->showFirstRun();
    plan.idle();
    save(*plan.window, prefix + "-firstrun");
    const QString said = page->pageText();
    expect(QStringLiteral("the first page is showing and the rail marks nothing (%1)").arg(frame->selected()),
           frame->firstRunShown() && frame->selected() != Frame::helpKey());
    for (const QString &words : {QStringLiteral("Welcome to %1").arg(name), text.purpose, text.never,
                                 QStringLiteral("Worth knowing first"), QStringLiteral("Go on to %1").arg(name),
                                 QStringLiteral("Read Help first")})
        expect(QStringLiteral("the first page says: %1").arg(words.left(60)), said.contains(words));
    for (const HelpIdea &idea : text.ideas)
        expect(QStringLiteral("the first page %1 the idea \"%2\" (its first sentence only)")
                   .arg(idea.forFirstRun ? "has" : "leaves out").arg(idea.title),
               idea.forFirstRun ? said.contains(idea.title) && said.contains(idea.first) && !said.contains(idea.more.left(40))
                                : !said.contains(idea.title));

    // ---- Help from the first page, and going on to the app
    expect(QStringLiteral("\"Read Help first\" shows Help"),
           page->press(QStringLiteral("Read Help first")) && frame->selected() == Frame::helpKey());
    frame->showFirstRun();
    plan.idle();
    const bool went = page->press(QStringLiteral("Go on to %1").arg(name));
    plan.idle();
    expect(QStringLiteral("the one button goes on to the app's own page (%1)").arg(frame->selected()),
           went && !frame->firstRunShown() && frame->selected() == plan.firstEntry && plan.pageShown(plan.firstEntry));
    save(*plan.window, prefix + "-firstrun-went-on");

    // ---- Picture runs keep nothing about it
    QSettings kept(QStringLiteral("LevitateOS"), QStringLiteral("firstrun"));
    expect(QStringLiteral("a picture run neither wants the page nor wrote that it was seen (%1)")
               .arg(kept.allKeys().join(QStringLiteral(", "))),
           !FirstRunPage::wanted(app) && kept.allKeys().isEmpty());
}

void guardRefuses(const Expect &expect, const QString &folder, const QString &mustName)
{
    ::guardRefuses(expect, folder, mustName);
}

} // namespace helpstory
