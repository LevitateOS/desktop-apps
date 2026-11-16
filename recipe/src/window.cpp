#include "window.h"

#include "pages.h"
#include "sources.h"
#include "sourceview.h"
#include "storage.h"

#include "frame.h"
#include "parts.h"
#include "refresh.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QJsonObject>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

Window::Window(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Recipe"));

    stack_ = new QStackedWidget;
    browse_ = new BrowsePage;
    recipe_ = new RecipePage;
    runPage_ = new RunPage;
    installed_ = new InstalledPage;
    sources_ = new SourcesPage;
    storage_ = new StoragePage;
    change_ = new ChangePage;
    for (QWidget *page : {static_cast<QWidget *>(browse_), static_cast<QWidget *>(recipe_),
                          static_cast<QWidget *>(runPage_), static_cast<QWidget *>(installed_),
                          static_cast<QWidget *>(sources_), static_cast<QWidget *>(storage_),
                          static_cast<QWidget *>(change_)})
        stack_->addWidget(page);

    // The frame is the window: the rail, the pages in it, and the status line.
    frame_ = new Frame(Frame::Recipe);
    frame_->setContent(stack_);
    frame_->addEntry(QStringLiteral("browse"), tr("Browse"));
    frame_->addEntry(QStringLiteral("installed"), tr("Installed"));
    frame_->addEntry(QStringLiteral("sources"), tr("Sources"));
    frame_->addEntry(QStringLiteral("storage"), tr("Storage"));
    frame_->select(QStringLiteral("browse"));
    updateStatus();
    setCentralWidget(frame_);

    connect(frame_, &Frame::chosen, this, [this](const QString &key) {
        if (key == QLatin1String("browse"))
            showBrowse(category_);
        else if (key == QLatin1String("installed"))
            showInstalled(installedChoice_, installedPlace_);
        else if (key == QLatin1String("sources"))
            showSources();
        else if (key == QLatin1String("storage"))
            showStorage();
    });

    connect(browse_->searchBox(), &QLineEdit::textChanged, this, [this] { showBrowse(category_); });
    connect(browse_->categoryBox(), &QComboBox::activated, this,
            [this](int index) { showBrowse(browse_->categoryBox()->itemData(index).toString()); });
    connect(browse_->fromBox(), &QComboBox::activated, this,
            [this](int index) { chooseSource(browse_->fromBox()->itemData(index).toString()); });

    connect(browse_, &BrowsePage::openRecipe, this, &Window::showRecipe);
    connect(browse_, &BrowsePage::install, this, [this](const QString &name, Place place) { startInstall(name, place); });
    connect(browse_, &BrowsePage::update, this, &Window::updateFromRow);
    connect(browse_, &BrowsePage::retry, this, [this] { reload([this] { showBrowse(); }); });
    connect(sources_, &SourcesPage::refresh, this, &Window::fetchSources);
    connect(sources_, &SourcesPage::add, this, &Window::addSource);
    connect(sources_, &SourcesPage::viewFile, this, [this](const QString &path) { viewFile(path, QString()); });
    connect(sources_, &SourcesPage::remove, this, [this](const QString &name) { removeSource(name); });
    connect(sources_, &SourcesPage::browse, this, [this](const QString &name) {
        chooseSource(name);
        showBrowse();
    });
    connect(change_, &ChangePage::back, this, [this] { leaveChange(); });
    connect(change_, &ChangePage::retry, this, [this] {
        if (remake_)
            change(changeWords_, remake_);
    });
    // F5, and coming back to the window after a while: a terminal
    // may have installed something in the meantime.
    refresher_ = new Refresher(this, 20);
    connect(refresher_, &Refresher::asked, this, [this](bool) {
        frame_->refreshRelease(); // the page of "Check for updates", when it shows
        refresh();
    });
    auto *again = new QToolButton;
    again->setText(tr("Read again"));
    again->setToolTip(tr("Read the recipes and what is installed again (F5)"));
    again->setAutoRaise(true);
    connect(again, &QToolButton::clicked, this, &Window::refresh);
    frame_->addStatusWidget(again);
    connect(recipe_, &RecipePage::back, this, [this] { showBrowse(); });
    connect(recipe_, &RecipePage::install, this, [this](const QString &name, Place place) { startInstall(name, place); });
    connect(recipe_, &RecipePage::update, this, [this](const QString &name, Place place) { startInstall(name, place); });
    connect(recipe_, &RecipePage::remove, this, &Window::askRemove);
    connect(recipe_, &RecipePage::open, this, &Window::open);
    connect(recipe_, &RecipePage::readRecipe, this, &Window::viewSource);
    connect(recipe_, &RecipePage::runStep, this, &Window::runStep);
    connect(recipe_, &RecipePage::cleanup, this, &Window::cleanupNow);
    connect(recipe_, &RecipePage::removeProgramOnly, this, &Window::removeOnlyProgram);
    connect(recipe_, &RecipePage::keepChanged, this, [this](const QString &name, bool keep) { keepChoice_.insert(name, keep); });
    connect(recipe_, &RecipePage::checkUpstream, this, &Window::checkUpstream);
    connect(installed_, &InstalledPage::checkUpstream, this, [this] { checkUpstream({}); });
    connect(storage_, &StoragePage::cleanup, this, &Window::cleanupNow);
    connect(storage_, &StoragePage::cleanupAll, this, [this](Place place) { cleanupNow(QString(), place, Cleanup::Everything); });
    connect(storage_, &StoragePage::cleanupLogs, this, &Window::cleanupLogsNow);
    connect(storage_, &StoragePage::removeBuildImage, this, &Window::removeBuildImageNow);

    connect(runPage_, &RunPage::back, this, [this] { showBrowse(); });
    connect(runPage_, &RunPage::retry, this, [this](const QString &name) {
        if (const auto again = stepAgain_)
            again();
        else
            startInstall(name, runPlace_);
    });
    connect(runPage_, &RunPage::succeeded, this, [this](const QString &name) { runEnded(name, true); });
    // Installed, but the system's caches could not be rebuilt: the page stays to say so.
    connect(runPage_, &RunPage::succeededWithoutCaches, this, [this](const QString &name) { runEnded(name, true, true); });
    connect(runPage_, &RunPage::failed, this, [this](const QString &name) { runEnded(name, false); });
    connect(runPage_, &RunPage::status, this, [this](const QString &words) {
        buildingWords_ = words;
        updateStatus();
    });
    connect(installed_, &InstalledPage::open, this, &Window::open);
    connect(installed_, &InstalledPage::openPage, this, &Window::showRecipe);
    connect(recipe_, &RecipePage::backToInstalled, this, [this] { showInstalled(installedChoice_, installedPlace_); });
    // The base system's row: the frame's own "Check for updates" page.
    connect(installed_, &InstalledPage::openRelease, this, [this] { frame_->showRelease(); });
    connect(storage_, &StoragePage::openInstalled, this, [this] { showInstalled(installedChoice_, installedPlace_); });
    connect(installed_, &InstalledPage::remove, this, &Window::askRemove);
    connect(installed_, &InstalledPage::update, this, &Window::updateFromRow);
    connect(installed_, &InstalledPage::selected, this, [this](const QString &name, Place place) {
        installedChoice_ = name;
        installedPlace_ = int(place);
    });

    reload([this] { showBrowse(); });
}

void Window::updateStatus()
{
    frame_->setStatus(buildingWords_.isEmpty() ? readWords_ : tr("%1 · %2").arg(buildingWords_, readWords_));
}

void Window::clearFlash()
{
    frame_->flash(QString());
    updateStatus();
}

void Window::reload(std::function<void()> then)
{
    if (client_.loading()) {
        // What is being read began before whatever asked for this: read once more when it ends,
        // and then do what was asked, so a page that waits for it is not left waiting.
        reloadQueued_ = true;
        if (then)
            queuedThen_ = then;
        return;
    }
    if (!read_) {
        browse_->showLoading(tr("Reading the recipes"),
                             tr("Asking recipe what there is to install and what is installed already."));
        stack_->setCurrentWidget(browse_);
    }
    readWords_ = tr("Reading the recipes…");
    updateStatus();
    client_.load([this, then](bool ok, const Catalog &catalog, const QString &problem) {
        if (reloadQueued_) {
            reloadQueued_ = false;
            const auto queued = queuedThen_;
            queuedThen_ = nullptr;
            reload([then, queued] {
                if (then)
                    then();
                if (queued)
                    queued();
            });
            return;
        }
        if (!ok) {
            readWords_ = tr("The recipes could not be read");
            updateStatus();
            browse_->showProblem(problem);
            stack_->setCurrentWidget(browse_);
            return;
        }
        // "Newer upstream" is known only from the person's last "Check for updates": it stays across reads.
        Catalog read = catalog;
        read.upstream = catalog_.upstream;
        read.upstreamChecked = catalog_.upstreamChecked;
        catalog_ = read;
        read_ = true;
        refresher_->fresh();
        readWords_ = tr("%1, read at %2")
                         .arg(catalog_.originsWords(), QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
        if (catalog_.unreadable)
            readWords_ += tr(" · %1").arg(counted(catalog_.unreadable, tr("%1 recipe could not be read"),
                                                  tr("%1 recipes could not be read")));
        updateStatus();
        if (!origin_.isEmpty() && !catalog_.origin(origin_))
            origin_.clear(); // the chosen source is gone
        fillSources();
        fillCategories();
        fillRail();
        if (then)
            then();
    });
}

// Reading again leaves the person where they were: the page is drawn again
// from what was read, except a page that is being worked on.
void Window::refresh()
{
    if (busy())
        return;
    QWidget *shown = stack_->currentWidget();
    const bool drawn = shown != runPage_ && shown != change_;
    reload([this, drawn] {
        // A copy: drawing the page sets `again_` anew, and the one running must outlive that.
        if (const auto draw = again_; drawn && draw)
            draw();
    });
}

void Window::fillSources()
{
    QComboBox *from = browse_->fromBox();
    const QSignalBlocker quiet(from);
    from->clear();
    from->addItem(tr("From all sources"), QString());
    for (const Origin &origin : catalog_.origins)
        if (catalog_.countFrom(origin.name) > 0)
            from->addItem(tr("From %1").arg(origin.shown()), origin.name);
    from->setCurrentIndex(qMax(0, from->findData(origin_)));
    // One source: nothing to choose between.
    from->setVisible(from->count() > 2);
}

void Window::chooseSource(const QString &name)
{
    origin_ = name;
    QComboBox *from = browse_->fromBox();
    const QSignalBlocker quiet(from);
    from->setCurrentIndex(qMax(0, from->findData(origin_)));
    if (stack_->currentWidget() == browse_)
        showBrowse(category_);
}

void Window::fillCategories()
{
    QComboBox *box = browse_->categoryBox();
    const QSignalBlocker quiet(box);
    box->clear();
    box->addItem(tr("All kinds"), QString());
    for (const QString &category : catalog_.categories())
        box->addItem(categoryTitle(category), category);
    box->setCurrentIndex(qMax(0, box->findData(category_)));
}

void Window::fillRail()
{
    // Installed carries the number of programs with an "Update" mark.
    frame_->setBadge(QStringLiteral("installed"), int(catalog_.updates.size()));
}

void Window::showBrowse(const QString &category)
{
    category_ = category;
    again_ = [this, category] { showBrowse(category); };
    {
        QComboBox *box = browse_->categoryBox();
        const QSignalBlocker quiet(box);
        box->setCurrentIndex(qMax(0, box->findData(category)));
    }
    frame_->follow(QStringLiteral("browse"));
    browse_->show(catalog_, category, browse_->searchBox()->text().trimmed(), origin_);
    stack_->setCurrentWidget(browse_);
}

void Window::showRecipe(const QString &name)
{
    again_ = [this, name] { showRecipe(name); };
    const Recipe *recipe = catalog_.recipe(name);
    recipe_->show(catalog_, client_.places(), name, keepChoice_.value(name, recipe && recipe->keep));
    stack_->setCurrentWidget(recipe_);
}

void Window::showInstalled(const QString &selected, int place)
{
    again_ = [this] { showInstalled(installedChoice_, installedPlace_); };
    frame_->follow(QStringLiteral("installed"));
    installed_->show(catalog_, selected, place);
    stack_->setCurrentWidget(installed_);
}

// Update on a list row: at once, unless the newer recipe may use more than the installed one did (something
// added or raised). Then the program's page opens, so the change is seen beside its Update button.
void Window::updateFromRow(const QString &name, Place place)
{
    const UpdateItem *update = catalog_.update(name, place);
    bool more = false;
    if (update && update->changesKnown)
        for (const Change &change : update->changes)
            more = more || (change.isList() ? !change.added().isEmpty() : change.kind == QLatin1String("raised") || change.kind == QLatin1String("added"));
    if (!more) {
        startInstall(name, place);
        return;
    }
    frame_->flash(tr("%1 may use more than before. Read what changes, then press Update.").arg(shownName(name)));
    showRecipe(name);
}

// Install and Update start at once: the program's page says what will happen before the button.
void Window::startInstall(const QString &name, Place place)
{
    if (run_ || changeRun_) {
        frame_->flash(tr("One thing at a time: wait for the running one, or stop it."));
        stack_->setCurrentWidget(run_ ? static_cast<QWidget *>(runPage_) : change_);
        return;
    }
    const bool installed = catalog_.installedItem(name, place) != nullptr;
    // A recipe this system cannot run is not installable: nothing starts, and one line says why.
    if (!installed && !catalog_.unavailable(name).isEmpty()) {
        frame_->flash(tr("%1 is not available on this system. %2").arg(shownName(name), catalog_.unavailableLine(name)));
        return;
    }
    // `recipe` is given SOURCE:NAME, so two sources with one name cannot be confused.
    const QString named = catalog_.qualified(name);
    runPlace_ = place;
    runName_ = name;
    runUpdate_ = installed;
    runStepName_.clear();
    runDone_.clear();
    stepAgain_ = nullptr;
    run_ = installed ? client_.update(named, place, keepFor(name)) : client_.install(named, place, keepFor(name));
    // "For everyone" is a system change on both kinds of computer: written into the other root (A/B) or
    // the running root after the recipe has run, then the root's caches are rebuilt. The recipe's own
    // `done` is not the end of that run, the command's exit is.
    const bool system = !client_.places().one && place != Place::Me;
    runPage_->begin(catalog_, name, run_, place, system);
    stack_->setCurrentWidget(runPage_);
    run_->start();
}

Keep Window::keepFor(const QString &name) const
{
    const auto found = keepChoice_.constFind(name);
    if (found == keepChoice_.constEnd())
        return Keep::Default;
    return found.value() ? Keep::Keep : Keep::NoKeep;
}

// One single step of one program ("One step at a time"): it has the run page, with its own title.
void Window::runStep(const QString &name, Place place, const QString &step)
{
    if (run_ || changeRun_) {
        frame_->flash(tr("One thing at a time: wait for the running one, or stop it."));
        stack_->setCurrentWidget(run_ ? static_cast<QWidget *>(runPage_) : change_);
        return;
    }
    if (const QString why = catalog_.unavailable(name); !why.isEmpty()) {
        frame_->flash(tr("%1 is not available on this system. %2").arg(shownName(name), catalog_.unavailableLine(name)));
        return;
    }
    const QString named = catalog_.qualified(name);
    const QString shown = shownName(name);
    runPlace_ = place;
    runName_ = name;
    runUpdate_ = false;
    runStepName_ = step;
    stepAgain_ = [this, name, place, step] { runStep(name, place, step); };
    QString doing, done;
    if (step == QLatin1String("download")) {
        run_ = client_.download(named, place);
        doing = tr("Downloading %1").arg(shown);
        done = tr("Downloaded %1").arg(shown);
    } else if (step == QLatin1String("build")) {
        run_ = client_.build(named, place, keepFor(name));
        doing = tr("Building %1").arg(shown);
        done = tr("Built %1").arg(shown);
    } else {
        run_ = client_.install(named, place, keepFor(name));
        doing = tr("Installing %1").arg(shown);
        done = tr("Installed %1").arg(shown);
    }
    runDone_ = done;
    const bool system = step == QLatin1String("install") && !client_.places().one && place != Place::Me;
    runPage_->begin(catalog_, name, run_, place, system, doing, done, step);
    stack_->setCurrentWidget(runPage_);
    run_->start();
}

void Window::stopRun()
{
    if (run_)
        run_->stop();
}

void Window::runEnded(const QString &name, bool ok, bool stay)
{
    run_ = nullptr;
    if (closeAfterStop_) {
        // The window was closed while this ran, and the person said to stop it: it has stopped.
        closing_ = true;
        close();
        return;
    }
    if (ok) {
        // Said at once, before the lists are read again: what happened and where it is now.
        const QString shown = shownName(name);
        const QJsonObject answer = runPage_->answer();
        const QString other = answer["other_root"].toString();
        const bool system = !client_.places().one && runPlace_ != Place::Me;
        if (!runDone_.isEmpty())
            frame_->flash(runDone_);
        else if (stay)
            frame_->flash(tr("%1 is installed, but the system's caches could not be rebuilt. The page says more.").arg(shown));
        else if (!answer.isEmpty() && answer.contains("other_root"))
            frame_->flash(tr("%1 was written into the other root (%2). It becomes active when root %2 is started.").arg(shown, other));
        else if (system)
            frame_->flash(tr("%1 was written into the system. It is active at once; programs that are already running may need a restart.")
                              .arg(shown));
        else
            frame_->flash(runUpdate_ ? tr("Updated %1").arg(shown) : tr("Installed %1").arg(shown));
    }
    reload([this, name, ok, stay] {
        // On failure the run page stays, with what went wrong.
        if (ok && !stay)
            showRecipe(name);
    });
}

// ---------------------------------------------------------------- The recipe's file

// The file Recipe will really run for this entry is the one `recipe info` names (it looks the
// recipe up as `recipe install` does, and for a remote source fetches the copy to read), so
// that is what the viewer shows; the list's own `path` is not used for it.
void Window::viewSource(const QString &name)
{
    const Recipe *recipe = catalog_.recipe(name);
    if (!recipe)
        return;
    const QString title = recipe->shown();
    const Origin *origin = catalog_.origin(recipe->origin);
    QString from;
    if (origin) {
        from = origin->name == origin->shown() ? origin->name : tr("%1 (%2)").arg(origin->shown(), origin->name);
        if (!origin->where().isEmpty())
            from += tr(", %1").arg(origin->where());
    }
    client_.recipeFile(catalog_.qualified(name), [this, title, from](const QString &path, const QString &problem) {
        if (viewer_)
            viewer_->close();
        viewer_ = new SourceViewer(title, from, path, problem, this);
        viewer_->setAttribute(Qt::WA_DeleteOnClose);
        viewer_->setModal(false);
        viewer_->show();
    });
}

void Window::viewFile(const QString &path, const QString &from)
{
    if (viewer_)
        viewer_->close();
    viewer_ = new SourceViewer(QFileInfo(path).fileName(), from, path, QString(), this);
    viewer_->setAttribute(Qt::WA_DeleteOnClose);
    viewer_->setModal(false);
    viewer_->show();
}

// ---------------------------------------------------------------- Sources

void Window::showSources()
{
    again_ = [this] { showSources(); };
    frame_->follow(QStringLiteral("sources"));
    sources_->show(catalog_);
    stack_->setCurrentWidget(sources_);
}

void Window::fetchSources(const QString &name)
{
    ChangePage::Words words;
    words.title = name.isEmpty() ? tr("Fetching every source's list") : tr("Fetching the list of %1").arg(name);
    words.subtitle = tr("Recipe asks %1 for its list of recipes again, over the network. Nothing is installed.")
                         .arg(name.isEmpty() ? tr("each source") : tr("the source"));
    words.doing = tr("Asking Recipe to fetch again…");
    words.stopNote = tr("Stopping it leaves the lists Recipe already has. Fetch again whenever you like.");
    words.failed = name.isEmpty() ? tr("Fetching the lists did not finish") : tr("Fetching the list of %1 did not finish").arg(name);
    words.left = tr("The recipes are as they were when they were last read. Sources shows when each list was fetched.");
    words.done = name.isEmpty() ? tr("Fetched every source's list") : tr("Fetched the list of %1").arg(name);
    change(words, [this, name] { return client_.refresh(name); });
}

void Window::addSource(const QString &addressOrFolder)
{
    ChangePage::Words words;
    words.title = tr("Adding the source");
    words.subtitle = tr("%1 is added to the list of sources, and its list of recipes is read. Nothing is installed from it.")
                         .arg(addressOrFolder.trimmed());
    words.doing = tr("Asking Recipe to add the source…");
    words.stopNote = tr("Stopping it before it ends: Sources shows whether the source is listed. Add it again if it is not.");
    words.failed = tr("Adding the source did not finish");
    words.left = tr("Sources shows which sources are listed now.");
    words.done = tr("Added the source");
    QString where = addressOrFolder.trimmed();
    if (where.startsWith(QLatin1Char('~')))
        where = QDir::homePath() + where.mid(1);
    addedWhere_ = where;
    change(words, [this, addressOrFolder] { return client_.addSource(addressOrFolder); });
}

void Window::removeSource(const QString &name, bool askFirst)
{
    const Origin *origin = catalog_.origin(name);
    if (!origin)
        return;
    if (askFirst
        && QMessageBox::question(this, tr("Remove a source"),
                                 tr("Remove the source %1?\n\nIts recipes are no longer offered. What was installed "
                                    "from it stays installed.")
                                     .arg(origin->shown()),
                                 QMessageBox::Yes | QMessageBox::Cancel)
               != QMessageBox::Yes)
        return;
    ChangePage::Words words;
    words.title = tr("Removing the source %1").arg(origin->shown());
    words.subtitle = tr("Its recipes are no longer offered. What was installed from it stays installed.");
    words.doing = tr("Asking Recipe to remove the source…");
    words.stopNote = tr("Stopping it before it ends: Sources shows whether the source is still listed. Remove it again if it is.");
    words.failed = tr("Removing the source %1 did not finish").arg(origin->shown());
    words.left = tr("Sources shows which sources are listed now.");
    words.done = tr("Removed the source %1").arg(origin->shown());
    change(words, [this, name] { return client_.removeSource(name); });
}

void Window::showStorage()
{
    again_ = [this] { showStorage(); };
    frame_->follow(QStringLiteral("storage"));
    storage_->show(catalog_, client_.places());
    stack_->setCurrentWidget(storage_);
}

QString Window::shownName(const QString &name) const
{
    const Recipe *recipe = catalog_.recipe(name);
    return recipe ? recipe->shown() : name;
}

void Window::askRemove(const QString &name, Place place)
{
    const InstalledItem *item = catalog_.installedItem(name, place);
    if (!item)
        return;
    if (run_ || changeRun_) {
        frame_->flash(tr("One thing at a time: wait for the running one, or stop it."));
        return;
    }
    // The base is the system itself, and software that other installed software needs stays until
    // that is removed: the pages do not offer Remove then, and this says why if it is asked anyway.
    if (item->base) {
        frame_->flash(tr("The base system is not removed. A newer release replaces it: see Check for updates."));
        return;
    }
    if (!item->inStore) {
        const QStringList needed = catalog_.neededBy(name, catalog_.systemTarget());
        if (!needed.isEmpty()) {
            frame_->flash(tr("%1 is needed by %2, so it cannot be removed while they are installed.")
                              .arg(shownName(name), catalog_.shownNames(needed).join(tr(", "))));
            return;
        }
    }
    // `recipe remove` works from the receipt alone, so a program whose recipe is
    // gone (its source removed, the recipe deleted) can still be removed.
    Recipe gone;
    gone.name = name;
    const Recipe *recipe = catalog_.recipe(name);
    // Asked without waiting: the window stays alive under it, and a check can answer it.
    auto *dialog = new RemoveDialog(recipe ? *recipe : gone, *item, catalog_.takenOutWords(*item), lastDesktopWords(name, *item), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    removeBox_ = dialog;
    connect(dialog, &QDialog::accepted, this, [this, name, place] { removeNow(name, place); });
    dialog->open();
}

// Said in the question before the button, when removing takes out the last desktop of the root it goes into.
QString Window::lastDesktopWords(const QString &name, const InstalledItem &item) const
{
    if (item.inStore || !catalog_.lastDesktop(name))
        return QString();
    const QString root = catalog_.ab ? catalog_.otherWords() : tr("this computer");
    return tr("This is the last desktop in %1. After it is removed the computer starts to a text login. A desktop can be installed "
              "again with Recipe from there: recipe --system install NAME, with the name of the desktop.")
        .arg(root);
}

ChangePage::Words Window::removeWords(const QString &name, Place place) const
{
    const InstalledItem *item = catalog_.installedItem(name, place);
    const QString shown = shownName(name);
    const bool store = !item || item->inStore;
    ChangePage::Words words;
    words.title = tr("Removing %1").arg(shown);
    words.subtitle = !store ? catalog_.takenOutWords(*item)
                            : QString();
    words.doing = tr("Asking Recipe to remove %1…").arg(shown);
    words.stopNote = store || !catalog_.ab
                         ? tr("Stopping it before it ends leaves %1 installed. You can remove it again from Installed.").arg(shown)
                         : tr("Stopping it before it ends leaves %1 as it was. You can remove it again from Installed.").arg(catalog_.otherWords());
    words.failed = tr("Removing %1 did not finish").arg(shown);
    words.left = store && item ? tr("%1 %2 is still installed.").arg(shown, item->version)
                 : !catalog_.ab ? tr("Installed shows what the system holds now.")
                                : tr("%1 was not changed. Installed shows what each root holds.")
                                      .arg(catalog_.otherWords().left(1).toUpper() + catalog_.otherWords().mid(1));
    words.done = store ? tr("Removed %1").arg(shown)
                 : !catalog_.ab ? tr("Removed %1 from the system. It is gone at once; programs that are already running may need a restart.")
                                      .arg(shown)
                                : tr("Removed %1 from %2. It stays in %3 until the other root is started.")
                                      .arg(shown, catalog_.otherWords(), catalog_.bootedWords());
    return words;
}

void Window::removeOnlyProgram(const QString &name, Place place)
{
    ChangePage::Words words = removeWords(name, place);
    words.subtitle = tr("Only the program is removed. Its downloads and build material stay: Storage lists them.");
    change(words, [this, name, place] { return client_.remove(name, place, true); });
}

void Window::cleanupNow(const QString &name, Place place, Cleanup what)
{
    const QString shown = name.isEmpty() ? QString() : shownName(name);
    const QString part = what == Cleanup::Downloads ? tr("downloads") : what == Cleanup::Build ? tr("build material")
                                                                                              : tr("downloads and build material");
    ChangePage::Words words;
    if (name.isEmpty()) {
        words.title = tr("Cleaning up everything not installed");
        words.subtitle = tr("Deletes every program's downloads and build material. Installed programs stay.");
        words.done = tr("Deleted every program's downloads and build material");
    } else {
        words.title = tr("Deleting the %1 of %2").arg(part, shown);
        words.subtitle = tr("The installed program, if there is one, stays.");
        words.done = tr("Deleted the %1 of %2").arg(part, shown);
    }
    words.doing = tr("Asking Recipe to delete…");
    words.stopNote = tr("Stopping it leaves what is not deleted yet. Storage shows what is still there.");
    words.failed = tr("Deleting did not finish");
    words.left = tr("Storage shows what Recipe still holds.");
    change(words, [this, name, place, what] { return client_.cleanup(name.isEmpty() ? QString() : catalog_.qualified(name), place, what); });
}

void Window::cleanupLogsNow(Place place)
{
    ChangePage::Words words;
    words.title = tr("Deleting the logs");
    words.subtitle = tr("The diagnostic logs of earlier runs are deleted. Nothing installed is changed.");
    words.doing = tr("Asking Recipe to delete the logs…");
    words.stopNote = tr("Stopping it leaves the logs that are not deleted yet.");
    words.failed = tr("Deleting the logs did not finish");
    words.left = tr("Storage shows what Recipe still holds.");
    words.done = tr("Deleted the logs");
    change(words, [this, place] { return client_.cleanupLogs(place); });
}

void Window::removeBuildImageNow(Place place)
{
    ChangePage::Words words;
    words.title = tr("Deleting the build image");
    words.subtitle = tr("The cached compiler image is deleted. Recipe fetches it again when a recipe needs it.");
    words.doing = tr("Asking Recipe to delete the build image…");
    words.stopNote = tr("Stopping it leaves what is not deleted yet.");
    words.failed = tr("Deleting the build image did not finish");
    words.left = tr("Storage shows what Recipe still holds.");
    words.done = tr("Deleted the build image");
    change(words, [this, place] { return client_.removeBuildImage(place); });
}

// "Check for updates": only when asked. Upstream is asked once for each place that holds software.
void Window::checkUpstream(const QStringList &names)
{
    if (checking_ || client_.loading() || run_ || changeRun_) {
        frame_->flash(tr("One thing at a time: wait for the running one, or stop it."));
        return;
    }
    checking_ = true;
    frame_->flash(tr("Asking upstream for the newest versions…"));
    QList<Place> places;
    if (catalog_.twoPlaces)
        places << Place::Me;
    places << Place::Computer;
    QStringList qualified;
    for (const QString &name : names)
        qualified << catalog_.qualified(name);
    auto found = std::make_shared<QList<UpstreamItem>>();
    auto problems = std::make_shared<QStringList>();
    auto next = std::make_shared<std::function<void(int)>>();
    *next = [this, places, qualified, names, found, problems, next](int index) {
        if (index >= places.size()) {
            checking_ = false;
            if (found->isEmpty() && !problems->isEmpty()) {
                frame_->flash(tr("Upstream could not be asked: %1").arg(problems->first()));
                return;
            }
            QList<UpstreamItem> merged = names.isEmpty() ? QList<UpstreamItem>() : catalog_.upstream;
            for (const UpstreamItem &item : *found) {
                merged.removeIf([&item](const UpstreamItem &old) { return old.name == item.name; });
                merged << item;
            }
            catalog_.upstream = merged;
            catalog_.upstreamChecked = true;
            int newer = 0;
            for (const UpstreamItem &item : *found)
                newer += item.newer;
            frame_->flash(newer ? tr("Upstream has a newer version for %1.").arg(counted(newer, tr("%1 program"), tr("%1 programs")))
                                : tr("Upstream has nothing newer than the recipes install."));
            QWidget *shown = stack_->currentWidget();
            if (const auto draw = again_; draw && shown != runPage_ && shown != change_)
                draw();
            return;
        }
        client_.checkUpstream(qualified, places[index], [found, problems, next, index](bool ok, const QList<UpstreamItem> &list, const QString &problem) {
            if (ok)
                *found += list;
            else
                *problems << problem;
            (*next)(index + 1);
        });
    };
    (*next)(0);
}

void Window::removeNow(const QString &name, Place place)
{
    change(removeWords(name, place), [this, name, place] { return client_.remove(name, place); });
}

// A short change has its own page: what it does, Stop, what the command printed, and when it did
// not finish, why. The person may go elsewhere meanwhile; the status line says what runs.
void Window::change(const ChangePage::Words &words, std::function<Run *()> make)
{
    if (run_ || changeRun_) {
        frame_->flash(tr("One thing at a time: wait for the running one, or stop it."));
        stack_->setCurrentWidget(run_ ? static_cast<QWidget *>(runPage_) : change_);
        return;
    }
    remake_ = make;
    changeWords_ = words;
    changeRun_ = make();
    Run *run = changeRun_;
    change_->begin(words, run);
    stack_->setCurrentWidget(change_);
    buildingWords_ = words.title;
    frame_->flash(QString()); // what ran before is said; the status line now says what runs
    updateStatus();
    // The page hears the end first (it shows why), then the window moves on.
    connect(run, &Run::finished, this, [this, run](bool ok) {
        if (run != changeRun_)
            return;
        changeEnded(ok);
        run->deleteLater();
    });
    run->start();
}

void Window::changeEnded(bool ok)
{
    changeRun_ = nullptr;
    buildingWords_.clear();
    updateStatus();
    if (closeAfterStop_) {
        closing_ = true;
        close();
        return;
    }
    const bool onThePage = stack_->currentWidget() == change_;
    if (!ok)
        addedWhere_.clear();
    if (ok) {
        frame_->flash(changeWords_.done);
        // Back where the person was, drawn from what was read after the change.
        reload([this] {
            // A source just added: the Sources page shows that Recipe reads it now.
            if (!addedWhere_.isEmpty()) {
                const QString where = addedWhere_;
                addedWhere_.clear();
                for (const Origin &origin : catalog_.origins)
                    if (QDir::cleanPath(origin.where()) == QDir::cleanPath(where)) {
                        showSources();
                        frame_->flash(tr("Recipe now reads recipes from %1 (%2).")
                                          .arg(origin.shown(), counted(catalog_.countFrom(origin.name), tr("%1 recipe"), tr("%1 recipes"))));
                        return;
                    }
            }
            if (stack_->currentWidget() == change_ || stack_->currentWidget() == nullptr) {
                if (const auto draw = again_)
                    draw();
                else
                    showBrowse(category_);
            } else if (const auto draw = again_; draw && stack_->currentWidget() != runPage_) {
                draw();
            }
        });
        return;
    }
    // It did not finish: the page stays with what went wrong, and the read still happens so that
    // the other pages are true. Someone elsewhere is told in the status line.
    if (!onThePage)
        frame_->flash(tr("%1. Its page is in the Recipe window.").arg(changeWords_.failed));
    reload(nullptr);
}

// "Back" on a change that did not finish.
void Window::leaveChange()
{
    reload([this] {
        if (const auto draw = again_)
            draw();
        else
            showBrowse(category_);
    });
}

// ---------------------------------------------------------------- Opening a program

namespace {

// A value of the "Desktop Entry" group of a menu entry's file.
QString desktopValue(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    bool inEntry = false;
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (inEntry && line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1).trimmed();
    }
    return {};
}

// The command a menu entry's Exec line says, without its field codes (%U, %f ...).
QStringList execWords(const QString &exec)
{
    QStringList words;
    static const QRegularExpression field(QStringLiteral("%[a-zA-Z]"));
    for (QString word : QProcess::splitCommand(exec)) {
        if (word.size() == 2 && field.match(word).hasMatch() && word.at(1) != QLatin1Char('%'))
            continue;
        word.replace(QStringLiteral("%%"), QStringLiteral("\x01"));
        word.remove(field);
        word.replace(QLatin1Char('\x01'), QLatin1Char('%'));
        words << word;
    }
    return words;
}

} // namespace

// Starts the program from the profile of the store it is installed in: that profile's `bin` may
// not be on PATH (the person's own store usually is not, and this computer's is another). The
// program a menu entry of the package names comes first, as the applications menu would start it;
// else the first program the package put in `bin`.
void Window::open(const QString &name, Place place)
{
    const InstalledItem *item = catalog_.installedItem(name, place);
    if (!item)
        return;
    const QString shown = shownName(name);
    if (place == Place::Other) {
        frame_->flash(tr("%1 is only in %2, so it cannot be opened until %3 is started.")
                          .arg(shown, catalog_.otherWords(), catalog_.otherRoot.isEmpty() ? tr("that root") : tr("root %1").arg(catalog_.otherRoot)));
        return;
    }
    if (!item->inStore) {
        frame_->flash(tr("%1 cannot be opened from here: the list of the system's software names it and its version, "
                         "not its programs.")
                          .arg(shown));
        return;
    }
    if (item->profile.isEmpty()) {
        frame_->flash(tr("%1 could not be started: Recipe did not say where it is installed.").arg(shown));
        return;
    }
    const QString bin = item->profile + QStringLiteral("/bin");
    QStringList command;
    QString terminalCommand, missing;
    for (const QString &entry : item->menu) {
        const QString file = item->profile + QStringLiteral("/share/applications/") + entry + QStringLiteral(".desktop");
        const QStringList words = execWords(desktopValue(file, QStringLiteral("Exec")));
        if (words.isEmpty()) {
            if (missing.isEmpty())
                missing = tr("the menu entry %1 in %2").arg(entry, item->profile + QStringLiteral("/share/applications"));
            continue;
        }
        QString program = words.first();
        if (!program.contains(QLatin1Char('/'))) {
            program = QStandardPaths::findExecutable(program, {bin});
            if (program.isEmpty())
                program = QStandardPaths::findExecutable(words.first());
        }
        if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
            if (missing.isEmpty())
                missing = tr("%1, which the menu entry %2 starts, in %3").arg(words.first(), entry, bin);
            continue;
        }
        if (desktopValue(file, QStringLiteral("Terminal")).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            terminalCommand = program;
            continue;
        }
        command = QStringList{program} + words.mid(1);
        break;
    }
    if (command.isEmpty()) {
        if (!terminalCommand.isEmpty()) {
            frame_->flash(tr("%1 runs in a terminal: start it there as %2.").arg(shown, terminalCommand));
            return;
        }
        for (const QString &program : item->programs) {
            const QString path = bin + QLatin1Char('/') + program;
            if (QFileInfo(path).isExecutable()) {
                command = {path};
                break;
            }
        }
        if (command.isEmpty()) {
            if (missing.isEmpty() && !item->programs.isEmpty())
                missing = tr("%1 in %2").arg(item->programs.first(), bin);
            frame_->flash(missing.isEmpty() ? tr("%1 could not be started: it put no program in %2.").arg(shown, bin)
                                            : tr("%1 could not be started: %2 is not there.").arg(shown, missing));
            return;
        }
    }
    QProcess process;
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(QDir::homePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), bin + QLatin1Char(':') + environment.value(QStringLiteral("PATH")));
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached())
        frame_->flash(tr("%1 could not be started: %2 would not run (%3).").arg(shown, command.first(), process.errorString()));
    else
        frame_->flash(tr("Started %1").arg(shown));
}

// ---------------------------------------------------------------- Closing the window

Run *Window::runningRun() const
{
    if (changeRun_ && changeRun_->running())
        return changeRun_;
    if (run_ && run_->running())
        return run_;
    return nullptr;
}

QString Window::closeWords() const
{
    if (Run *run = runningRun(); run && run == changeRun_)
        return tr("%1 is still running.\n\n%2").arg(changeWords_.title, changeWords_.stopNote);
    const QString shown = shownName(runName_);
    const InstalledItem *item = catalog_.installedItem(runName_, runPlace_);
    const bool single = runStepName_ == QLatin1String("download") || runStepName_ == QLatin1String("build");
    // A download or a build installs nothing, but leaves what it fetched or made.
    const QString unchanged = single                 ? tr("Nothing is installed by it.")
                              : runUpdate_ && item ? tr("%1 %2 stays installed and works as before.").arg(shown, item->version)
                                                   : tr("Nothing on this computer is changed.");
    const QString running = runStepName_ == QLatin1String("download") ? tr("Downloading %1").arg(shown)
                            : runStepName_ == QLatin1String("build")  ? tr("Building %1").arg(shown)
                            : runUpdate_                              ? tr("Updating %1").arg(shown)
                                                                      : tr("Installing %1").arg(shown);
    QString words = tr("%1 is still running.\n\nIf you close the window now it is stopped. What is already fetched and built is kept. "
                       "%2 You can start it again from %3's page whenever you like.")
                        .arg(running, unchanged, shown);
    return words;
}

void Window::closeEvent(QCloseEvent *event)
{
    if (closing_ || !runningRun()) {
        QMainWindow::closeEvent(event);
        return;
    }
    // Closing would end the run without a word: ask first, with what is true.
    event->ignore();
    askClose();
}

void Window::askClose()
{
    if (closeBox_) {
        closeBox_->raise();
        return;
    }
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Question);
    box->setWindowTitle(tr("Close while it runs?"));
    const QString words = closeWords();
    box->setText(words.section(QStringLiteral("\n\n"), 0, 0));
    box->setInformativeText(words.section(QStringLiteral("\n\n"), 1));
    QPushButton *stop = box->addButton(tr("Stop it and close"), QMessageBox::AcceptRole);
    QPushButton *keep = box->addButton(tr("Keep the window open"), QMessageBox::RejectRole);
    box->setDefaultButton(keep);
    box->setEscapeButton(keep);
    closeBox_ = box;
    connect(stop, &QPushButton::clicked, this, [this] { stopAndClose(); });
    box->open();
}

// The person said to stop it: the run is asked to end, and the window closes when it has.
void Window::stopAndClose()
{
    Run *run = runningRun();
    if (!run) {
        closing_ = true;
        close();
        return;
    }
    closeAfterStop_ = true;
    frame_->flash(tr("Stopping it, then closing…"));
    run->stop();
    // A run that does not answer is ended with the window.
    QTimer::singleShot(8000, this, [this] {
        if (closeAfterStop_) {
            closing_ = true;
            close();
        }
    });
}
