#include "window.h"

#include "pages.h"
#include "refresh.h"
#include "words.h"

#include <QLocale>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTime>
#include <QToolButton>

Window::Window(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Updater"));
    // Whether an install finished through this app before: kept per person.
    installedBefore_ = QSettings(QStringLiteral("LevitateOS"), QStringLiteral("updater")).value("installed").toBool();

    updates_ = new ReleaseCheck(this);
    stack_ = new QStackedWidget;
    main_ = new MainPage;
    install_ = new InstallPage;
    before_ = new BeforePage;
    for (QWidget *page : {static_cast<QWidget *>(main_), static_cast<QWidget *>(install_), static_cast<QWidget *>(before_)})
        stack_->addWidget(page);
    setCentralWidget(stack_);

    connect(main_, &MainPage::install, this, [this] { install(); });
    connect(main_, &MainPage::finish, this, [this] { finishInterrupted(); });
    connect(main_, &MainPage::restartNow, this, [this] { restart(); });
    connect(main_, &MainPage::back, this, &Window::showMain);
    connect(install_, &InstallPage::back, this, &Window::showMain);
    connect(install_, &InstallPage::restartNow, this, [this] { restart(); });
    connect(install_, &InstallPage::ended, this, [this](bool ok, bool restartNeeded, const QString &version) {
        installing_ = false;
        if (ok) {
            installedBefore_ = true;
            QSettings(QStringLiteral("LevitateOS"), QStringLiteral("updater")).setValue("installed", true);
            if (restartNeeded)
                restartFor_ = version;
        }
        // The answer of a check made before the install is not true any more.
        updates_->readFile();
    });
    connect(before_, &BeforePage::cancelled, this, [this] {
        go_ = nullptr;
        showMain();
    });
    connect(before_, &BeforePage::confirmed, this, [this] {
        const auto go = std::move(go_);
        go_ = nullptr;
        if (go)
            go();
    });

    // What the page shows is drawn from the answer, whenever the answer changes.
    connect(updates_, &ReleaseCheck::changed, this, [this] {
        if (read_ && stack_->currentWidget() == main_)
            main_->show(updates_, restartFor_, installedBefore_);
    });
    connect(updates_, &ReleaseCheck::fileRead, this, [this] {
        read_ = true;
        statusBar()->showMessage(tr("Read at %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
        if (stack_->currentWidget() == main_)
            main_->show(updates_, restartFor_, installedBefore_);
    });

    // F5, and coming back to the window after a while.
    refresher_ = new Refresher(this, 20);
    connect(refresher_, &Refresher::asked, this, [this](bool) { refresh(); });
    auto *again = new QToolButton;
    again->setText(tr("Read again"));
    again->setToolTip(tr("Read the installed release and the last automatic check again (F5)"));
    again->setAutoRaise(true);
    connect(again, &QToolButton::clicked, this, &Window::refresh);
    statusBar()->addPermanentWidget(again);

    main_->showLoading(tr("Reading the installed release"), tr("Looking at the release this system runs and at the last check for updates."));
    stack_->setCurrentWidget(main_);
    statusBar()->showMessage(tr("Reading…"));
    updates_->readFile();
}

void Window::refresh()
{
    // The words of a Before page and of a run are true of what was read when they were shown.
    if (installing_ || stack_->currentWidget() != main_)
        return;
    statusBar()->showMessage(tr("Reading…"));
    updates_->readFile();
    refresher_->fresh();
}

void Window::showMain()
{
    main_->show(updates_, restartFor_, installedBefore_);
    stack_->setCurrentWidget(main_);
}

void Window::ask(const BeforePage::Content &content, std::function<void()> go)
{
    go_ = std::move(go);
    before_->show(content);
    stack_->setCurrentWidget(before_);
}

BeforePage::Content Window::installContent(const QString &title, const QString &go, const QString &first) const
{
    BeforePage::Content c;
    c.title = title;
    c.happensTitle = tr("What happens");
    c.happens << first;
    c.happens << tr("The release is installed into this system's one root. Programs that are running keep their old files "
                    "until they are restarted, so you are asked to restart at the end.");
    c.happens << tr("This system has one root, so there is no second root to fall back to. The previous kernel stays as a second boot entry.");
    c.happens << (client_.asksPassword() ? tr("You are asked for the administrator password.")
                                         : tr("It runs as you, so no password is asked."));
    c.tableTitle = tr("The command that runs");
    c.table = {{tr("Command"), client_.commandWords({"apply"})}};
    c.go = go;
    return c;
}

void Window::install(bool askFirst)
{
    const ReleaseState &release = updates_->state();
    if (installing_ || release.kind != ReleaseState::Available)
        return;
    const QString version = release.version;
    const QString first = release.downloadBytes > 0
                              ? tr("Release %1 is downloaded (%2) and checked before any file changes.").arg(version, bytesText(release.downloadBytes))
                              : tr("Release %1 is downloaded and checked before any file changes.").arg(version);
    if (!askFirst) {
        applyNow(version, false);
        return;
    }
    ask(installContent(tr("Install release %1?").arg(version), tr("Install %1").arg(version), first),
        [this, version] { applyNow(version, false); });
}

void Window::finishInterrupted(bool askFirst)
{
    const ReleaseState &release = updates_->state();
    if (installing_ || !release.interrupted)
        return;
    const QString version = release.interruptedVersion;
    if (!askFirst) {
        applyNow(version, true);
        return;
    }
    ask(installContent(tr("Finish installing release %1?").arg(version), tr("Finish the update"),
                       tr("The install of release %1 stopped part of the way. It carries on from where it stopped.").arg(version)),
        [this, version] { applyNow(version, true); });
}

void Window::applyNow(const QString &version, bool resuming)
{
    InstallWords words;
    words.title = resuming ? tr("Finishing release %1").arg(version) : tr("Installing release %1").arg(version);
    words.subtitle = tr("This system's one root is being updated.");
    words.doneTitle = tr("Release %1 is installed").arg(version);
    words.doneText = tr("Restart the computer to use it. The previous kernel stays as a second boot entry.");
    words.restartButton = tr("Restart now");
    words.failedTitle = tr("The update stopped");
    words.failedNote = tr("Some files of this system may be new and some old. Run the update again to finish it: "
                          "it carries on from where it stopped. The first screen offers it.");
    installing_ = true;
    install_->follow(client_.apply(), words);
    stack_->setCurrentWidget(install_);
}

void Window::restart(bool askFirst)
{
    if (installing_)
        return;
    auto go = [this] {
        Run *run = client_.reboot();
        connect(run, &Run::finished, this, [this, run](bool ok) {
            const QString said = run->problem();
            run->deleteLater();
            if (ok) {
                statusBar()->showMessage(tr("The computer is restarting…"));
                showMain();
            } else {
                main_->showProblem(tr("The computer did not restart"),
                                   said.isEmpty() ? tr("The restart stopped without saying why.") : said);
                stack_->setCurrentWidget(main_);
            }
        });
        run->start();
    };
    if (!askFirst) {
        go();
        return;
    }
    BeforePage::Content c;
    c.title = tr("Restart now?");
    c.happensTitle = tr("What happens");
    c.happens << tr("The computer restarts at once. Anything unsaved is lost.");
    c.happens << tr("It starts with the new kernel; the previous kernel is the second boot entry.");
    c.happens << (client_.asksPassword() ? tr("You are asked for the administrator password.")
                                         : tr("It runs as you, so no password is asked."));
    c.go = tr("Restart now");
    ask(c, go);
}
