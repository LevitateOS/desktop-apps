#include "window.h"

#include "browse.h"
#include "disk.h"
#include "frame.h"
#include "nextsystem.h"
#include "pages.h"
#include "refresh.h"
#include "rules.h"
#include "run.h"
#include "words.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStorageInfo>
#include <algorithm>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

Window::Window(QWidget *parent)
    : QMainWindow(parent)
    , home_(qEnvironmentVariable("DISK_GUI_HOME", QDir::homePath()))
    , recab_(qEnvironmentVariable("DISK_GUI_RECAB", "recab"))
    , rootHelper_(qEnvironmentVariable("DISK_GUI_ROOT_HELPER", "pkexec"))
    , osRelease_(qEnvironmentVariable("DISK_GUI_OS_RELEASE", "/etc/os-release"))
    , kept_(qEnvironmentVariable("DISK_GUI_KEPT",
                                 QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/comparison.json"))
    , lsblk_(qEnvironmentVariable("DISK_GUI_LSBLK", "lsblk"))
    , recipe_(qEnvironmentVariable("DISK_GUI_RECIPE", "recipe"))
    , files_(qEnvironmentVariable("DISK_GUI_FILES"))
    , apparent_(qEnvironmentVariable("DISK_GUI_APPARENT") == QLatin1String("1"))
{
    setWindowTitle(tr("Recdirstat"));
    // What is the person's to clear: their home, and what Recipe keeps for the system.
    roots_ = qEnvironmentVariable("DISK_GUI_ROOTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (roots_.isEmpty()) {
        roots_ << home_;
        if (QFileInfo(QStringLiteral("/var/lib/recipe")).isReadable())
            roots_ << QStringLiteral("/var/lib/recipe");
    }
    never_ = readNever();

    stack_ = new QStackedWidget;
    disk_ = new DiskPage;
    browse_ = new BrowsePage;
    mess_ = new MessPage;
    systems_ = new SystemsPage;
    systems_->setPasswordAsked(!rootHelper_.isEmpty());
    before_ = new BeforePage;
    for (QWidget *page : std::initializer_list<QWidget *>{disk_, browse_, mess_, systems_, before_})
        stack_->addWidget(page);

    // The frame is the window: the rail, the pages, the strip, the status line.
    // Only the four things to look at have entries.
    frame_ = new Frame(Frame::Recdirstat);
    frame_->setContent(stack_);
    frame_->addEntry(QStringLiteral("disk"), tr("The disk"));
    frame_->addEntry(QStringLiteral("browse"), tr("Browse"));
    frame_->addEntry(QStringLiteral("mess"), tr("Mess"));
    frame_->addEntry(QStringLiteral("systems"), tr("The two systems"));
    frame_->select(QStringLiteral("disk"));
    frame_->setStatus(tr("Looking at the disk…"));
    setCentralWidget(frame_);
    next_ = new NextSystem(recab_, this);
    connect(next_, &NextSystem::known, this, [this] { frame_->setNextSystem(next_->titles()); });

    tick_ = new QTimer(this);
    tick_->setInterval(200);
    connect(tick_, &QTimer::timeout, this, [this] {
        if (!scanner_)
            return;
        scanFiles_ = scanner_->files();
        scanBytes_ = scanner_->bytes();
        mess_->showCount(scanFiles_, scanBytes_);
    });

    connect(frame_, &Frame::chosen, this, [this](const QString &key) {
        if (key == QLatin1String("disk"))
            go(disk_);
        else if (key == QLatin1String("browse"))
            go(browse_);
        else if (key == QLatin1String("mess"))
            go(mess_);
        else
            showSystems();
    });
    connect(frame_, &Frame::finishRequested, this, &Window::openSystem);
    connect(mess_, &MessPage::openFolder, this, &Window::openFolder);
    connect(browse_, &BrowsePage::openFolder, this, &Window::openFolder);
    connect(browse_, &BrowsePage::said, this, [this](const QString &words) { frame_->flash(words); });
    connect(systems_, &SystemsPage::stop, this, &Window::stopComparing);
    connect(systems_, &SystemsPage::readAgain, this, &Window::readState);
    connect(systems_, &SystemsPage::openSystem, this, &Window::openSystem);

    // The list of mess is one list, changed here and shown on three pages.
    connect(browse_, &BrowsePage::addRequested, this, &Window::addToList);
    connect(browse_, &BrowsePage::takeOffRequested, this, &Window::takeOffList);
    connect(browse_, &BrowsePage::neverRequested, this, [this](Node *node) { neverSuggest(node->path()); });
    connect(browse_, &BrowsePage::showMess, this, [this] { frame_->choose(QStringLiteral("mess")); });
    connect(mess_, &MessPage::takeOffRequested, this, &Window::takeOffList);
    connect(mess_, &MessPage::neverRequested, this, [this](Node *node) { neverSuggest(node->path()); });
    connect(mess_, &MessPage::suggestAgain, this, &Window::suggestAgain);
    connect(mess_, &MessPage::showInBrowse, this, [this](Node *node) {
        browse_->goTo(node->parent);
        browse_->pick(node);
        frame_->choose(QStringLiteral("browse"));
    });

    connect(disk_, &DiskPage::lookAgain, this, &Window::lookAgain);
    connect(disk_, &DiskPage::showMess, this, [this] { frame_->choose(QStringLiteral("mess")); });
    connect(disk_, &DiskPage::showBrowse, this, [this] { frame_->choose(QStringLiteral("browse")); });
    connect(disk_, &DiskPage::showSystems, this, [this] { frame_->choose(QStringLiteral("systems")); });
    connect(disk_, &DiskPage::openRecipe, this, [this] { frame_->startApp(Frame::Recipe); });

    // F5 reads again what the tab shows. Coming back to the window reads the
    // state of the two systems again, which is quick; the disk is only looked
    // at again when asked, since that takes a while and drops what was ticked.
    refresher_ = new Refresher(this, 20);
    connect(refresher_, &Refresher::asked, this, [this](bool byHand) {
        if (byHand)
            refresh();
        else if (!busy())
            readState();
        next_->read();
    });
    auto *again = new QToolButton;
    again->setText(tr("Read again"));
    again->setToolTip(tr("On the disk, Browse and Mess: look at the disk again. On the two systems: ask recab again. (F5)"));
    again->setAutoRaise(true);
    connect(again, &QToolButton::clicked, this, &Window::refresh);
    frame_->addStatusWidget(again);
    connect(mess_, &MessPage::rescan, this, &Window::lookAgain);
    connect(systems_, &SystemsPage::compare, this, &Window::askCompare);
    // Cancel on a Before page leaves everything as it was and shows the page the person came from.
    connect(before_, &BeforePage::cancelled, this, [this] {
        beforeGo_ = nullptr;
        if (beforeBack_)
            go(beforeBack_);
    });
    connect(before_, &BeforePage::confirmed, this, [this] {
        const auto doIt = std::move(beforeGo_);
        beforeGo_ = nullptr;
        if (doIt)
            doIt();
    });

    recall();
    systems_->showLoading(tr("Reading the two systems"), tr("Asking recab which system is running and what the other one is."));
    browse_->showLoading(tr("Looking at the disk"), tr("Reading the folders. Nothing is changed."));
    go(disk_);
    scan();
    readState();
    readLayout();
    readEarlier();
    next_->read();
}

Window::~Window()
{
    // Threads that read the disk must end before the window does.
    if (scanner_) {
        scanner_->requestInterruption();
        scanner_->wait();
    }
}

bool Window::busy() const
{
    return scanner_ != nullptr || run_ != nullptr || reading_ != nullptr || layoutRun_ != nullptr
           || recipeRun_ != nullptr;
}

void Window::refresh()
{
    // The sentences of a Before page are true of what was read when it was
    // shown; reading again under it would make them say what is no longer so.
    if (busy() || stack_->currentWidget() == before_)
        return;
    if (stack_->currentWidget() == systems_)
        readState();
    else
        lookAgain();
}

void Window::lookAgain()
{
    if (scanner_ || run_)
        return;
    scan();
    readLayout();
    readEarlier();
}

// "Open in Files": the desktop's file manager. DISK_GUI_FILES names a stand-in
// for a check, so that no real program is started from a story.
void Window::openFolder(const QString &path)
{
    const QFileInfo info(path);
    const QString folder = info.isDir() ? path : info.absolutePath();
    if (!files_.isEmpty()) {
        if (!QProcess::startDetached(files_, {folder}))
            frame_->flash(tr("%1 could not be opened").arg(folder));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        frame_->flash(tr("%1 could not be opened").arg(folder));
}

// The strip's button and "Open Recab…" are one thing: start the Recab app,
// as the rail does.
void Window::openSystem()
{
    frame_->startApp(Frame::Recab);
}

void Window::readStripFrom(const QString &recab)
{
    delete next_;
    next_ = new NextSystem(recab, this);
    connect(next_, &NextSystem::known, this, [this] { frame_->setNextSystem(next_->titles()); });
    next_->read();
}

bool Window::showingDisk() const { return stack_->currentWidget() == disk_; }
bool Window::showingBrowse() const { return stack_->currentWidget() == browse_; }
bool Window::showingMess() const { return stack_->currentWidget() == mess_; }
bool Window::showingSystems() const { return stack_->currentWidget() == systems_; }
bool Window::showingBefore() const { return stack_->currentWidget() == before_; }

QString Window::entryOf(QWidget *page) const
{
    if (page == disk_)
        return QStringLiteral("disk");
    if (page == browse_)
        return QStringLiteral("browse");
    if (page == mess_)
        return QStringLiteral("mess");
    if (page == systems_)
        return QStringLiteral("systems");
    if (page == before_)
        return beforeEntry_;
    return QStringLiteral("disk");
}

// Shows a page and marks the entry it belongs to.
void Window::go(QWidget *page)
{
    stack_->setCurrentWidget(page);
    frame_->follow(entryOf(page));
}

QStringList Window::zones() const
{
    QStringList all{home_};
    for (const QString &root : roots_)
        if (!all.contains(root))
            all << root;
    return all;
}

// ---------------------------------------------------------------- The scan, the mess, Browse

void Window::scan()
{
    if (scanner_)
        return;
    scanFiles_ = scanBytes_ = 0;
    scanner_ = new Scanner(roots_, apparent_, this);
    connect(scanner_, &QThread::finished, this, &Window::scanned);
    mess_->showScanning(roots_);
    browse_->showLoading(tr("Looking at the disk"), tr("Reading %1. Nothing is changed.").arg(roots_.join(tr(", "))));
    frame_->setStatus(tr("Looking at the disk…"));
    tick_->start();
    scanner_->start();
    drawDisk();
}

void Window::scanned()
{
    tick_->stop();
    tree_ = scanner_->tree();
    unreadable_ = int(scanner_->unreadable().size());
    scanner_->deleteLater();
    scanner_ = nullptr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    found_ = findMess(tree_.get(), home_, now, never_);
    mess_->show(tree_, found_, roots_, spaceAt(home_), home_, never_, now);
    browse_->show(tree_, found_, home_, now);
    frame_->setBadge(QStringLiteral("mess"), int(found_.size()));
    frame_->setStatus(tr("Looked at the disk at %1")
                          .arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
    drawDisk();
}

QString Window::pickBiggestUnnamed()
{
    // Among the folders directly inside each root: the biggest that holds no finding.
    Node *best = nullptr;
    if (!tree_)
        return {};
    for (const auto &root : tree_->children)
        for (const auto &child : root->children) {
            if (!child->dir || child->kind >= 0)
                continue;
            bool holds = false;
            for (const Finding &finding : found_)
                for (Node *up = finding.node; up && !holds; up = up->parent)
                    holds = up == child.get();
            if (!holds && (!best || child->bytes > best->bytes))
                best = child.get();
        }
    if (!best)
        return {};
    addToList(best);
    return best->path();
}

void Window::listChanged()
{
    std::sort(found_.begin(), found_.end(), [](const Finding &a, const Finding &b) { return a.bytes > b.bytes; });
    frame_->setBadge(QStringLiteral("mess"), int(found_.size()));
    mess_->setFound(found_, never_);
    browse_->setFound(found_);
    drawDisk();
}

void Window::addToList(Node *node)
{
    if (!node || findingAround(found_, node))
        return;
    found_ << pickedByHand(node, home_, found_);
    listChanged();
}

void Window::takeOffList(Node *node)
{
    for (int index = 0; index < found_.size(); ++index)
        if (found_[index].node == node) {
            const bool picked = found_[index].kind == kindPicked;
            found_.removeAt(index);
            node->kind = -1;
            listChanged();
            frame_->flash(picked ? tr("Taken off the list.")
                                 : tr("Taken off the list. It is found again when you look again, unless you say never to suggest it."));
            return;
        }
}

void Window::neverSuggest(const QString &path)
{
    if (path.isEmpty())
        return;
    if (!never_.contains(path))
        never_ << path;
    if (!writeNever(never_))
        frame_->flash(tr("The list of places never to suggest could not be saved in %1").arg(neverFile()));
    // What is at or inside that place leaves the list now; the rules keep it away at every later look.
    for (int index = found_.size() - 1; index >= 0; --index)
        if (found_[index].path == path || found_[index].path.startsWith(path + QLatin1Char('/'))) {
            found_[index].node->kind = -1;
            found_.removeAt(index);
        }
    listChanged();
}

void Window::suggestAgain(const QString &path)
{
    never_.removeAll(path);
    if (!writeNever(never_))
        frame_->flash(tr("The list of places never to suggest could not be saved in %1").arg(neverFile()));
    // Whether it is mess is for the rules to say again.
    lookAgain();
}

// ---------------------------------------------------------------- The disk

DiskInfo Window::diskInfo() const
{
    DiskInfo info;
    info.home = spaceAt(home_);
    const QStorageInfo storage(home_);
    info.homeDevice = QString::fromLocal8Bit(storage.device());
    info.homeMount = storage.rootPath();
    info.scanning = scanner_ != nullptr;
    info.scanFiles = scanFiles_;
    info.scanBytes = scanBytes_;
    info.scannedBytes = tree_ ? tree_->bytes : 0;
    info.unreadable = unreadable_;
    info.apparent = apparent_;
    info.found = found_;
    if (tree_ && !scanner_)
        info.slices = composition(tree_.get(), found_, home_);
    info.earlierBytes = earlierBytes_;
    info.earlierGenerations = earlierGenerations_;
    info.layoutRead = layoutRead_;
    info.layout = layout_;
    info.runningDevice = state_.runningDevice;
    info.otherDevice = state_.otherDevice;
    info.otherRole = state_.otherRole;
    if (compared_) {
        info.compared = true;
        info.otherFilesBytes = comparison_.other.bytes;
        info.otherDeviceCompared = comparison_.other.device;
        info.comparedWhen = comparedWhen_;
    }
    return info;
}

void Window::drawDisk()
{
    disk_->show(diskInfo());
}

void Window::readLayout()
{
    if (layoutRun_)
        return;
    layoutRun_ = new Run(lsblk_,
                         {"--json", "--bytes", "-o", "NAME,PATH,TYPE,SIZE,FSTYPE,LABEL,PARTLABEL,PARTTYPE,MOUNTPOINTS"}, this);
    connect(layoutRun_, &Run::finished, this, [this](bool ok) {
        Run *run = layoutRun_;
        layoutRun_ = nullptr;
        run->deleteLater();
        const QStorageInfo storage(home_);
        if (ok) {
            layout_ = layoutFromLsblk(QJsonDocument::fromJson(run->answer()).object(), QString::fromLocal8Bit(storage.device()),
                                      storage.rootPath());
        } else {
            layout_ = DiskLayout();
            layout_.problem = tr("`%1` could not be run: %2").arg(lsblk_, run->problem());
        }
        layoutRead_ = true;
        drawDisk();
    });
    layoutRun_->start();
}

void Window::readEarlier()
{
    if (recipeRun_)
        return;
    recipeRun_ = new Run(recipe_, {"storage", "--json"}, this);
    connect(recipeRun_, &Run::finished, this, [this](bool ok) {
        Run *run = recipeRun_;
        recipeRun_ = nullptr;
        run->deleteLater();
        earlierBytes_ = -1;
        earlierGenerations_ = 0;
        if (ok) {
            const QJsonObject answer = QJsonDocument::fromJson(run->answer()).object();
            if (answer.contains("earlier_bytes")) {
                earlierBytes_ = answer["earlier_bytes"].toInteger();
                earlierGenerations_ = answer["earlier_generations"].toInteger();
            }
        }
        drawDisk();
    });
    recipeRun_->start();
}

// ---------------------------------------------------------------- The two systems

void Window::showSystems()
{
    go(systems_);
}

void Window::readState()
{
    if (reading_ || job_ == Job::Comparing)
        return;
    reading_ = new Run(recab_, {"status", "--json"}, this);
    connect(reading_, &Run::finished, this, [this](bool ok) {
        Run *run = reading_;
        reading_ = nullptr;
        run->deleteLater();
        if (ok) {
            state_ = SystemsState::fromJson(QJsonDocument::fromJson(run->answer()).object(), osRelease_);
        } else {
            state_ = SystemsState();
            state_.problem = tr("`%1 status --json` failed: %2").arg(recab_, run->problem());
        }
        stateRead_ = true;
        refresher_->fresh();
        drawDisk(); // which partitions are the two systems comes from here
        // A page that is being worked on is left alone.
        if (job_ == Job::None)
            drawSystems();
    });
    reading_->start();
}

// The comparison is out of date when either system has changed since.
bool Window::stale() const
{
    return compared_ && state_.known && !comparedKey_.isEmpty() && state_.key != comparedKey_;
}

// The systems page as things stand: the kept comparison when there is one,
// else what recab says is there.
void Window::drawSystems(const QString &why)
{
    if (compared_ && why.isEmpty())
        systems_->show(comparison_, comparedWhen_, stale());
    else
        systems_->showState(state_, why);
}

void Window::openBefore(const QString &entry, QWidget *back, const BeforePage::Content &content, std::function<void()> go)
{
    beforeEntry_ = entry;
    beforeBack_ = back;
    beforeGo_ = std::move(go);
    before_->show(content);
    this->go(before_);
}

// What comparing does, said before it starts. The sentences follow how this
// window will really run it: whether it asks for the password depends on the
// helper it was started with.
void Window::askCompare()
{
    if (run_ || reading_)
        return;
    const QStringList arguments{"compare", "--json", "--machine-events"};
    const QStringList command = rootHelper_.isEmpty() ? QStringList{recab_} + arguments : QStringList{rootHelper_, recab_} + arguments;
    BeforePage::Content content;
    content.title = state_.known ? tr("Compare %1 with %2?").arg(state_.runningName(), state_.otherName()) : tr("Compare the two systems?");
    content.happensTitle = tr("What happens");
    content.happens << tr("Both systems are read from start to end: every file of the running system and of the other one.");
    content.happens << tr("Neither system is changed. Only what was found is kept, so this window can show it again next time.");
    content.happens << tr("It takes a few minutes. You can stop it while it reads.");
    content.happens << (rootHelper_.isEmpty()
                            ? tr("It runs as you, so no password is asked.")
                            : tr("The other system can only be read by the administrator, so you are asked for the administrator password."));
    content.tableTitle = tr("The command that runs");
    content.table = {{tr("Command"), command.join(QLatin1Char(' '))}};
    content.go = tr("Compare");
    openBefore(QStringLiteral("systems"), systems_, content, [this] { compare(); });
}

void Window::compare()
{
    if (run_ || reading_)
        return;
    // recab reads both systems; the other one can only be read as root.
    const QStringList arguments{"compare", "--json", "--machine-events"};
    run_ = rootHelper_.isEmpty() ? new Run(recab_, arguments, this)
                                 : new Run(rootHelper_, QStringList{recab_} + arguments, this);
    job_ = Job::Comparing;
    stopped_ = false;
    // It stays on the tab: the two sides fill as they are read.
    systems_->showComparing(state_);
    go(systems_);
    connect(run_, &Run::event, systems_, &SystemsPage::progress);
    connect(run_, &Run::finished, this, &Window::compared);
    run_->start();
}

void Window::stopComparing()
{
    if (run_ && job_ == Job::Comparing) {
        stopped_ = true;
        run_->stop();
    }
}

void Window::compared(bool ok)
{
    Run *run = run_;
    run_ = nullptr;
    job_ = Job::None;
    run->deleteLater();
    QString why;
    if (stopped_) {
        // Nothing to say: the person stopped it.
    } else if (!ok) {
        why = run->problem().isEmpty() ? tr("recab stopped without saying why.") : run->problem();
    } else {
        const QJsonObject answer = QJsonDocument::fromJson(run->answer()).object();
        if (answer.isEmpty())
            why = tr("recab finished without an answer this window can read.");
        const Comparison comparison = why.isEmpty() ? Comparison::fromJson(answer, &why) : Comparison();
        if (why.isEmpty()) {
            comparison_ = comparison;
            compared_ = true;
            comparedWhen_ = QDateTime::currentDateTime();
            comparedKey_ = state_.known ? state_.key : QString();
            comparedAnswer_ = answer;
            keep();
        }
    }
    drawSystems(why);
    drawDisk();
}

// The last comparison is kept, so the window opens with it next time.
void Window::keep()
{
    QDir().mkpath(QFileInfo(kept_).absolutePath());
    QFile file(kept_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject{{"when", comparedWhen_.toSecsSinceEpoch()},
                                         {"state", comparedKey_},
                                         {"answer", comparedAnswer_}})
                   .toJson(QJsonDocument::Compact));
}

void Window::recall()
{
    QFile file(kept_);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject kept = QJsonDocument::fromJson(file.readAll()).object();
    QString error;
    const Comparison comparison = Comparison::fromJson(kept["answer"].toObject(), &error);
    if (!error.isEmpty())
        return;
    comparison_ = comparison;
    compared_ = true;
    comparedAnswer_ = kept["answer"].toObject();
    comparedWhen_ = QDateTime::fromSecsSinceEpoch(kept["when"].toInteger());
    comparedKey_ = kept["state"].toString();
}
