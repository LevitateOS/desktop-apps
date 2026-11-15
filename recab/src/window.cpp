#include "window.h"

#include "pages.h"
#include "refresh.h"

#include <QLocale>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTime>
#include <QToolButton>

Window::Window(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Recab"));

    stack_ = new QStackedWidget;
    roots_ = new RootsPage;
    output_ = new OutputPage;
    before_ = new BeforePage;
    for (QWidget *page : {static_cast<QWidget *>(roots_), static_cast<QWidget *>(output_), static_cast<QWidget *>(before_)})
        stack_->addWidget(page);
    setCentralWidget(stack_);

    connect(roots_, &RootsPage::copyOther, this, [this] { copyOther(); });
    connect(roots_, &RootsPage::startOther, this, &Window::startOther);
    connect(roots_, &RootsPage::doNotStartOther, this, &Window::doNotStartOther);
    connect(roots_, &RootsPage::restartNow, this, [this] { restart(); });
    connect(roots_, &RootsPage::readAgain, this, [this] { reload(); });
    connect(roots_, &RootsPage::readAsAdministrator, this, [this] {
        asRoot_ = true;
        reload();
    });
    connect(output_, &OutputPage::back, this, &Window::showRoots);

    // The Before page: Cancel leaves everything as it was; the button does it.
    connect(before_, &BeforePage::cancelled, this, [this] {
        go_ = nullptr;
        showRoots();
    });
    connect(before_, &BeforePage::confirmed, this, [this] {
        const auto go = std::move(go_);
        go_ = nullptr;
        if (go)
            go();
    });

    // F5, and coming back to the window after a while: a terminal may have
    // changed the roots in the meantime.
    refresher_ = new Refresher(this, 20);
    connect(refresher_, &Refresher::asked, this, [this](bool byHand) {
        if (byHand || showingRoots())
            refresh();
    });
    auto *again = new QToolButton;
    again->setText(tr("Read again"));
    again->setToolTip(tr("Read the two roots again (F5)"));
    again->setAutoRaise(true);
    connect(again, &QToolButton::clicked, this, &Window::refresh);
    statusBar()->addPermanentWidget(again);

    reload();
}

bool Window::showingRoots() const { return stack_->currentWidget() == roots_; }
bool Window::showingOutput() const { return stack_->currentWidget() == output_; }
bool Window::showingBefore() const { return stack_->currentWidget() == before_; }
QString Window::statusLine() const { return statusBar()->currentMessage(); }

void Window::refresh()
{
    // The sentences of the Before page and of a run's output are true of what
    // was read when they were shown.
    if (busy() || !showingRoots())
        return;
    note_.clear();
    reload();
}

void Window::reload(std::function<void()> then)
{
    if (reading_)
        return;
    reading_ = client_.read(asRoot_);
    if (!read_) {
        roots_->showLoading(tr("Reading the two roots"), tr("Asking recab which root is booted and which is the other."));
        stack_->setCurrentWidget(roots_);
    }
    statusBar()->showMessage(tr("Reading the two roots…"));
    connect(reading_, &Run::finished, this, [this, then](bool ok) {
        Run *run = reading_;
        reading_ = nullptr;
        run->deleteLater();
        QString problem;
        Status status;
        bool permission = false;
        if (!client_.understand(run, ok, &status, &problem, &permission)) {
            statusBar()->showMessage(tr("The two roots could not be read"));
            roots_->showProblem(problem, permission && !asRoot_ && client_.asksPassword());
            asRoot_ = false; // a refused password is not asked for again unasked
            return;
        }
        status_ = status;
        read_ = true;
        refresher_->fresh();
        const QString at = tr("Read at %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
        statusBar()->showMessage(note_.isEmpty() ? at : tr("%1 · %2").arg(note_, at));
        // A run's output or a Before page being read is left as it is; the roots page is drawn from the status.
        if (showingRoots())
            roots_->show(status_);
        if (then)
            then();
    });
    reading_->start();
}

void Window::showRoots()
{
    roots_->show(status_);
    stack_->setCurrentWidget(roots_);
}

void Window::askBefore(const BeforePage::Content &content, std::function<void()> go)
{
    go_ = std::move(go);
    before_->show(content);
    stack_->setCurrentWidget(before_);
}

// Runs one command with its output shown as it comes; the roots are read
// again when it has ended, and `then` is told how it ended.
void Window::work(Run *run, const QString &title, const QString &subtitle,
                  std::function<void(bool, const QString &)> then)
{
    run_ = run;
    output_->begin(title, subtitle);
    stack_->setCurrentWidget(output_);
    connect(run, &Run::output, output_, &OutputPage::add);
    connect(run, &Run::finished, this, [this, run, then](bool ok) {
        run_ = nullptr;
        const QString said = ok ? QString::fromUtf8(run->answer()).trimmed() : run->problem();
        run->deleteLater();
        then(ok, said);
        reload();
    });
    run->start();
}

void Window::copyOther(bool askFirst)
{
    if (busy() || !read_)
        return;
    auto go = [this] {
        const QString other = status_.other, booted = status_.booted;
        work(client_.copy(), tr("Making root %1 a copy of root %2").arg(other, booted),
             tr("Root %1 is being overwritten. Root %2 is not changed.").arg(other, booted),
             [this, other](bool ok, const QString &said) {
                 if (ok)
                     output_->succeeded(said.isEmpty() ? tr("Root %1 is now a copy of the booted root.").arg(other) : said);
                 else
                     output_->failed(tr("The copy stopped"),
                                     said.isEmpty() ? tr("recab stopped without saying why.") : said);
             });
    };
    if (!askFirst) {
        go();
        return;
    }
    BeforePage::Content c;
    c.title = tr("Make root %1 a copy of root %2?").arg(status_.other, status_.booted);
    c.happensTitle = tr("What happens");
    c.happens << tr("Root %1 is overwritten with a fresh copy of root %2. Everything on root %1 is lost, including changes "
                    "made there, such as software Recipe installed into it.")
                     .arg(status_.other, status_.booted);
    c.happens << tr("Root %1 cannot be started until the copy has finished.").arg(status_.other);
    c.happens << tr("Root %1 is not changed.").arg(status_.booted);
    c.happens << (client_.asksPassword() ? tr("You are asked for the administrator password.")
                                         : tr("It runs as you, so no password is asked."));
    c.tableTitle = tr("The command that runs");
    c.table = {{tr("Command"), client_.commandWords({"copy"})}};
    c.go = tr("Overwrite root %1").arg(status_.other);
    askBefore(c, go);
}

void Window::startOther()
{
    if (busy() || !read_)
        return;
    const QString other = status_.other;
    work(client_.bootOther(false), tr("Starting root %1 at the next boot").arg(other), QString(),
         [this, other](bool ok, const QString &said) {
             if (ok) {
                 showRoots();
                 note_ = tr("Root %1 starts at the next boot, once.").arg(other);
             } else {
                 output_->failed(tr("It stopped"), said.isEmpty() ? tr("recab stopped without saying why.") : said);
             }
         });
}

void Window::doNotStartOther()
{
    if (busy() || !read_)
        return;
    work(client_.bootOther(true), tr("Not starting the other root at the next boot"), QString(),
         [this](bool ok, const QString &said) {
             if (ok) {
                 showRoots();
                 note_ = tr("The other root is not picked for the next boot.");
             } else {
                 output_->failed(tr("It stopped"), said.isEmpty() ? tr("recab stopped without saying why.") : said);
             }
         });
}

void Window::restart(bool askFirst)
{
    if (busy() || !read_)
        return;
    auto go = [this] {
        work(client_.reboot(), tr("Restarting"), tr("The computer restarts now."),
             [this](bool ok, const QString &said) {
                 if (ok) {
                     showRoots();
                     note_ = tr("The computer is restarting…");
                 } else {
                     output_->failed(tr("The computer did not restart"),
                                     said.isEmpty() ? tr("The restart stopped without saying why.") : said);
                 }
             });
    };
    if (!askFirst) {
        go();
        return;
    }
    BeforePage::Content c;
    c.title = tr("Restart now?");
    c.happensTitle = tr("What happens");
    c.happens << tr("The computer restarts at once. Anything unsaved is lost.");
    c.happens << (status_.startsNext().isEmpty() ? tr("The boot loader's own choice starts.")
                                                 : status_.nextOnce.isEmpty()
                                                       ? tr("Root %1, the boot default, starts.").arg(status_.startsNext())
                                                       : tr("Root %1, picked for this one boot, starts.").arg(status_.startsNext()));
    c.happens << (client_.asksPassword() ? tr("You are asked for the administrator password.")
                                         : tr("It runs as you, so no password is asked."));
    c.go = tr("Restart now");
    askBefore(c, go);
}
