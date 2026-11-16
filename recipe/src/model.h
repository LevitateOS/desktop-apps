// What the front end knows about recipes: plain data read from `recipe`'s JSON.
// No package rules live here; Recipe stays the only thing that knows them.
#pragma once

#include "words.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

struct Source {
    QString name, url, git, commit, sha256, checksumFrom;
    QString host() const;
    // How the source is pinned, as the end of a sentence.
    QString pin() const;
};

struct Needs {
    QStringList network;
    qint64 memoryMib = 0, storageMaxBytes = 0, timeoutSecs = 0, networkMaxBytes = 0;
};

// Where recipes come from: a repository whose index was fetched, or a folder
// on this computer. Recipe calls both a source.
struct Origin {
    QString name, title, kind, address, path, commit, problem;
    qint64 checked = 0; // when a remote source's index was last fetched, in seconds
    bool remote() const { return kind == QLatin1String("remote"); }
    // Its title; the two folders Recipe always reads have none and are named here.
    QString shown() const;
    // The address or the folder.
    QString where() const { return remote() ? address : path; }
    // "fetched today at 09:12", or why it could not be.
    QString state() const;
};

// Where a program stands on this system (`recipe state`, section 6.2 of the recipe format): nothing
// of it here (or only part of its downloads), all its downloads, a kept result not installed, or
// installed (any version).
enum class State { None, Downloaded, Built, Installed };

State stateFromText(const QString &text);
// One word for a person: "not here", "downloaded", "built", "installed".
QString stateTitle(State state);

// What a program holds on disk beside its installed files, as `recipe installed --json` and
// `recipe state --json` give it (`on_disk`).
struct OnDisk {
    qint64 downloads = 0, build = 0, unfinished = 0;
    qint64 total() const { return downloads + build + unfinished; }
    static OnDisk fromJson(const QJsonObject &object);
};

// How a recipe says where its newest upstream version is (`UPSTREAM`): a GitHub project, or its own
// `latest()` function. Neither: nowhere.
struct UpstreamDecl {
    QString github;
    bool latestFunction = false;
    bool declared() const { return latestFunction || !github.isEmpty(); }
};

struct Recipe {
    QString origin; // the name of the source it is from
    QString name, version, description;
    // From ABOUT: the author's words.
    QString kind, category, title, details, homepage, license, afterInstall;
    QStringList programs, menu, keeps;
    QMap<QString, QString> notes;
    // Enforced.
    QList<Source> sources;
    Needs needs;
    QStringList deps, buildDeps;
    // The steps it has (`recipe list --json` `steps`: from download, build, install, cleanup), and the
    // functions it defines (build, configure, verify, latest).
    QStringList steps, functions;
    UpstreamDecl upstream;
    // On this system, and the keep option (section 6.4).
    State state = State::None;
    QString installedVersion;
    bool keep = false;
    // ABOUT.libc: the C libraries its build is linked against (empty: any), and
    // whether the package brings its own C library and loader.
    QStringList libc;
    bool libcCarried = false;
    // From `recipe list --json`: whether this system can run it, and if not, the reason Recipe gives.
    bool runsHere = true;
    QString notRunnableReason;
    QString path;

    QString shown() const { return title.isEmpty() ? name : title; }
    bool hasStep(const QString &step) const { return steps.contains(step); }
    // It compiles something: it shows a build step (a recipe without a `build` function shows none).
    bool compiles() const { return hasStep(QStringLiteral("build")); }
    bool listed() const { return kind != QLatin1String("library"); }
    // ABOUT.kind "system": a group of the system's own software (the desktop, the printing stack).
    bool isSystem() const { return kind == QLatin1String("system"); }
    bool isLibrary() const { return kind == QLatin1String("library"); }
    // The licence is `proprietary …` (the recipe format requires SPDX or `proprietary`).
    bool proprietary() const { return license.trimmed().startsWith(QLatin1String("proprietary"), Qt::CaseInsensitive); }
    static Recipe fromJson(const QJsonObject &object);
};

// Where software is. `Me` is the person's own store, in effect at once for
// them only. `Computer` is the system's, for everyone: its store when the
// system has one root, the booted root's software when it has two (A/B).
// `Other` exists only on an A/B system: software written into the other root,
// which becomes active when that root is booted. The one store a check names
// (RECIPE_GUI_PACKAGE_ROOT) is `Computer`, the only place there is.
enum class Place { Me = 0, Computer = 1, Other = 2 };

struct InstalledItem {
    QString name, version;
    QDate installedOn;
    qint64 sizeBytes = 0;
    // What it adds: the programs (in the profile's `bin`) and menu entries, taken from its recipe's
    // ABOUT.adds (`installed --json` lists no files). Empty when its recipe is gone.
    QStringList programs, menu;
    // Not in `installed --json` any more: always empty. F2: the page that lists a receipt's files
    // drops that list.
    QStringList files;
    // What the receipt says besides: the source its recipe was fetched from and the commit (both
    // only when recorded), and the recipe file it was installed from.
    QString source, commit, receiptRecipe;
    // The keep option, and what the program holds beside its installed files.
    bool keep = false;
    OnDisk onDisk;
    // The profile of the store it is in, as `installed --json` names it.
    // Empty where no store stands behind it.
    QString profile;
    Place place = Place::Computer;
    // A store stands behind it: storage applies.
    // False for what `recipe --system installed` answers on an A/B system,
    // which says only name and version.
    bool inStore = true;
    // On an A/B system: the other root no longer holds it (it is in the booted root).
    bool leavesOther = false;
    // Only what `recipe --system installed --json` says of a package: the names it needs, and how many
    // desktop sessions it provides (`dependencies`, `sessions`).
    QStringList dependencies;
    int sessions = 0;
    // The reserved entry `base` (`"release": true`): the system's base tree, the files of the system
    // release it runs. It is one row for the system itself, and is not changed like a package.
    bool base = false;
    qint64 serial = 0;
};

// One field of what a recipe may use (NEEDS) that the newer recipe changes,
// as `recipe updates` names it.
struct Change {
    QString field, before, after, kind; // kind: added, removed, raised, lowered, changed
    // The field in a person's words.
    QString title() const;
    // For a list (the network, what it reads and writes): what is new, what
    // is gone and what stays.
    QStringList added() const, removed() const, stays() const;
    bool isList() const;
    // An amount in a person's words: "8388608 bytes" as "8 MiB".
    static QString amount(const QString &value);
};

// "Update": the recipe as last fetched names another version, or is another file for the same
// version. Known without asking upstream (`installed --json` `update`, `updates --json` `updates`).
// Never mixed with `UpstreamItem`, "Newer upstream".
struct UpdateItem {
    QString name, from, to;
    Place place = Place::Computer;
    // Made from the collection's version, not from `recipe`'s own mark (which
    // cannot look at an A/B system's software).
    bool fromCollection = false;
    QList<Change> changes;
    // False when `recipe` has not said what changed in what the recipe may use: `updates --json` is
    // not read yet for it, or nothing was recorded when the installed version was installed.
    bool changesKnown = true;
    // The version is the same and the source publishes another recipe file.
    bool sameVersion = false;
    bool asksMore() const;
    // What it asks for beyond what the installed version had, in a few words:
    // "network, memory". Empty when nothing is more.
    QString asksMoreWords() const;
};

// "Newer upstream": what `recipe updates --upstream --json` found out when the person asked. Only
// `newer` is a mark; `error` says why upstream could not be asked for this one.
struct UpstreamItem {
    QString name, installed, recipeVersion, latest, how, error;
    bool newer = false;
};

// One line of `recipe history`: what was done to a store. `from` and `to` are
// versions; any field that does not apply is empty.
struct HistoryEntry {
    qint64 at = 0; // seconds since 1970
    QString what;  // install, update, remove
    QString package, from, to, source;
};

// One kind of what Recipe holds on disk (`recipe storage --json` `kinds`): installed, downloaded,
// built, kept, unfinished, build_image, logs.
struct StorageKind {
    QString kind;
    qint64 bytes = 0;
    bool removable = false;
    // The command that removes it, as Recipe says it ("recipe cleanup NAME --downloads"); empty for none.
    QString remove;
    // Per program; empty for build_image and logs.
    QList<QPair<QString, qint64>> programs;
    // The folder, for build_image and logs.
    QString path;
};

// What one store holds, as `recipe storage` says.
struct StorageInfo {
    qint64 totalBytes = 0;
    QList<StorageKind> kinds;
    const StorageKind *kind(const QString &name) const;
    qint64 bytesOf(const QString &name) const;
};

// What one store holds, as `recipe storage` says, and what was done to it, as
// `recipe history` says (newest first).
struct Store {
    bool has = false;
    StorageInfo storage;
    qint64 installedBytes() const { return storage.bytesOf(QStringLiteral("installed")); }
    QList<HistoryEntry> history;
    // `recipe history` answered (an empty history is an answer).
    bool historyKnown = false;
};

// What `recipe state NAME --json` says (section 8.5).
struct StateInfo {
    QString name, version, installedVersion;
    State state = State::None;
    bool keep = false;
    QStringList steps;
    OnDisk onDisk;
    static StateInfo fromJson(const QJsonObject &object);
};

// One step of a finished run, and the program it ran for.
struct RunStep {
    QString step, status; // status: done, already, none
};

struct RunDependency {
    QString name;
    State state = State::None;
    QList<RunStep> steps;
};

// A finished run (section 8.6): what `install`, `update`, `download`, `build`, `remove` and
// `cleanup` print at the end.
struct RunResult {
    QString operation, name, version;
    State state = State::None;
    QList<RunStep> steps;
    bool kept = false;
    qint64 freedBytes = 0;
    // The profile and its `bin`, when a package was activated.
    QString profile, bin;
    QList<RunDependency> dependencies;
    static RunResult fromJson(const QJsonObject &object);
};

// A recipe `recipe list` could not read, and why.
struct RecipeProblem {
    QString path, problem;
};

// A system change that is not in the other root, and why writing it again failed.
struct NotDone {
    QString package, error;
};

struct Catalog {
    QList<Origin> origins;
    int unreadable = 0; // recipes in the collection that `recipe` could not read
    QList<RecipeProblem> unreadableRecipes;
    QList<Recipe> recipes;
    QList<InstalledItem> installed;
    QList<UpdateItem> updates;
    // Installed software `recipe updates` could not look up, each as "name: why".
    QStringList updateProblems;
    // "Newer upstream", only after the person asked (`recipe updates --upstream`); empty before.
    // The window keeps it across reads of the catalog.
    bool upstreamChecked = false;
    QList<UpstreamItem> upstream;
    // What each store holds, by `Place`.
    Store stores[2];
    // More than one place holds software (or can): rows say where they are.
    bool twoPlaces = false;
    // The computer's own software could not be read; why. Empty otherwise.
    QString computerProblem;
    // An A/B system: two roots, the booted one locked, the other unlocked. Their
    // names (A or B) as `recipe --system installed` says them. When `ab` is
    // false none of this applies and no A/B word is shown.
    bool ab = false;
    QString bootedRoot, otherRoot, otherMount;
    // System changes that were in the other root, were wiped from it (`recab
    // copy`), and whose writing again failed.
    QList<NotDone> notDone;
    // This system's C library as `recipe list --json` says it ("glibc", "musl", "unknown").
    QString hostLibc;
    // The system's name for its base row (os-release NAME); empty when it says none.
    QString systemName;

    const Recipe *recipe(const QString &name) const;
    const Origin *origin(const QString &name) const;
    // The name `recipe` is given: SOURCE:NAME, so two sources with one name cannot be confused.
    QString qualified(const QString &name) const;
    // Where the recipes are from, in a few words.
    QString originsWords() const;
    int countFrom(const QString &origin) const;
    // The first place that holds it (Me before Computer before Other), or the given one.
    const InstalledItem *installedItem(const QString &name) const;
    const InstalledItem *installedItem(const QString &name, Place place) const;
    // Summed over the stores there are: what is installed.
    qint64 installedBytes() const;
    // The upstream answer for one program, newer or not; null before it was asked.
    const UpstreamItem *upstreamOf(const QString &name) const;
    // Only when upstream has a newer version than the recipe names.
    const UpstreamItem *newerUpstream(const QString &name) const;
    const UpdateItem *update(const QString &name) const;
    const UpdateItem *update(const QString &name, Place place) const;
    // The other root holds it (and the booted root does not hold that version).
    bool inOtherRoot(const QString &name) const;
    // "the other root (B)", or "the other root" when its name is not known.
    QString otherWords() const;
    // "the running root (A)": the booted root, which is locked.
    QString bootedWords() const;
    // What taking this item out does where no store stands behind it, as a sentence: on an A/B
    // system only the other root is changed; on one root the change is in effect at once.
    QString takenOutWords(const InstalledItem &item) const;
    // Why this system cannot run the recipe, in Recipe's words (`not_runnable_reason`); empty when it can.
    QString unavailable(const QString &name) const;
    // Why this window cannot open software the system route lists (it says names and versions, not programs).
    QString cannotOpenWords() const;
    // The same as one line for a list or a page: without the command that installs it anyway.
    QString unavailableLine(const QString &name) const;
    // A recipe that is installed only into the system: ABOUT.kind "system", or a library that a
    // system group needs (directly or through other libraries). No choice of place is offered for it.
    bool intoSystemOnly(const QString &name) const;
    // What a root holds, as rows: the running root's (`Place::Computer`) or the other root's
    // (`Place::Other`, A/B only: what only it holds, and what both hold).
    QList<const InstalledItem *> rootItems(Place root) const;
    // The root a system change is written into: the other root on an A/B computer, else the computer's.
    Place systemTarget() const { return ab ? Place::Other : Place::Computer; }
    // The names (as the person reads them) of the packages of that root that need this one.
    QStringList neededBy(const QString &name, Place root) const;
    // Removing it from the target root takes out the last package of that root that provides a desktop session.
    bool lastDesktop(const QString &name) const;
    // "Needed by: A, B": the shown names of neededBy.
    QStringList shownNames(const QStringList &names) const;
    // The system's name for the base row: "LevitateOS 1.1, the base system".
    QString baseWords(const InstalledItem &item) const;
    // Rows that are not software of their own: the base, and shared parts of the system.
    int softwareCount() const;
    // The places it could still be installed in, the computer's first.
    QList<Place> installPlaces(const QString &name) const;
    // Every place that holds it.
    QList<const InstalledItem *> installedEverywhere(const QString &name) const;
    QStringList categories() const;
};

QString categoryTitle(const QString &id);
// The words for a step: "Download", "Build", "Install", "Clean up".
QString stepTitle(const QString &step);


// "For me", "For everyone", "In the other root (B)".
QString placeTitle(Place place, const QString &otherRoot = QString());
