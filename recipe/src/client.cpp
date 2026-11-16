#include "client.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QRegularExpression>

RecipeClient::RecipeClient(QObject *parent)
    : QObject(parent)
    , program_(qEnvironmentVariable("RECIPE_GUI_RECIPE", "recipe"))
    , rootHelper_(qEnvironmentVariable("RECIPE_GUI_ROOT_HELPER", "pkexec"))
    , recipesPath_(qEnvironmentVariable("RECIPE_GUI_RECIPES_PATH"))
    , packageRoot_(qEnvironmentVariable("RECIPE_GUI_PACKAGE_ROOT"))
    , systemRoot_(qEnvironmentVariable("RECIPE_GUI_SYSTEM_ROOT", "/var/lib/recipe"))
    // Set, even to nothing, means one explicit store and no choice of place.
    , explicit_(qEnvironmentVariableIsSet("RECIPE_GUI_PACKAGE_ROOT"))
{
}

// The kernel command line says whether this is an A/B computer (`recab.immutable`): `recipe`
// asks the same question to choose between the other root and the running one. The window asks it
// only to say where things go. The file is read again each time so that a check can swap it.
bool RecipeClient::ab() const
{
    QFile file(qEnvironmentVariable("RECIPE_GUI_CMDLINE", "/proc/cmdline"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(file.readAll()).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)
        .contains(QStringLiteral("recab.immutable"));
}

Places RecipeClient::places() const
{
    Places found;
    found.one = explicit_;
    found.ab = !explicit_ && ab();
    found.systemRoot = explicit_ ? packageRoot_ : systemRoot_;
    found.password = !rootHelper_.isEmpty();
    return found;
}

// The collection to list and install from: `recipe`'s own choice unless told.
QStringList RecipeClient::collection() const
{
    return recipesPath_.isEmpty() ? QStringList() : QStringList{"--recipes-path", recipesPath_};
}

// The computer's store, whose sources are read and fetched; or the one the
// window was told to use.
QStringList RecipeClient::store() const
{
    const QString root = explicit_ ? packageRoot_ : systemRoot_;
    return root.isEmpty() ? QStringList() : QStringList{"--package-root", root};
}

// Software "for me" is in the person's own store: no --package-root, which is how `recipe` tells
// it from the computer's. Everything for everyone is a system change on both kinds of computer:
// `--system` writes it into the other root of an A/B computer, and into the running root of one
// with a single root. Only the one explicit store a check names (RECIPE_GUI_PACKAGE_ROOT) is
// changed without it.
QStringList RecipeClient::lead(Place place) const
{
    return !explicit_ && place != Place::Me ? QStringList{"--system"} : QStringList();
}

QStringList RecipeClient::at(Place place) const
{
    Q_UNUSED(place);
    return explicit_ ? store() : QStringList();
}

// One question to `recipe`, answered later: the JSON it printed, or why not.
void RecipeClient::query(const QStringList &arguments, Answered then)
{
    auto *run = new Run(program_, arguments, this);
    connect(run, &Run::finished, this, [this, run, arguments, then](bool ok) {
        run->deleteLater();
        const QString asked = tr("`%1 %2`").arg(program_, arguments.join(' '));
        if (!ok) {
            then({}, tr("%1 failed: %2").arg(asked, run->problem()));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(run->answer()).object();
        // `installed` carries no version of its own yet; an answer that names one must name ours.
        if (object.contains("schema_version") && object["schema_version"].toInt() != 1) {
            then({}, tr("%1 answered in a form this program does not know (schema %2)")
                         .arg(asked)
                         .arg(object["schema_version"].toInt()));
            return;
        }
        if (object.isEmpty()) {
            then({}, tr("%1 gave no answer this program can read").arg(asked));
            return;
        }
        then(object, QString());
    });
    run->start();
}

static void readList(const QJsonObject &list, Catalog &fresh)
{
    fresh.hostLibc = list["host_libc"].toString();
    for (const QJsonValue &value : list["sources"].toArray()) {
        const QJsonObject o = value.toObject();
        Origin origin;
        origin.name = o["name"].toString();
        origin.title = o["title"].toString();
        origin.kind = o["kind"].toString();
        origin.address = o["address"].toString();
        origin.path = o["path"].toString();
        origin.commit = o["commit"].toString();
        origin.problem = o["problem"].toString();
        origin.checked = o["checked"].toInteger();
        fresh.origins << origin;
    }
    fresh.unreadable = list["problems"].toArray().size();
    for (const QJsonValue &value : list["problems"].toArray())
        fresh.unreadableRecipes << RecipeProblem{value.toObject()["path"].toString(), value.toObject()["problem"].toString()};
    for (const QJsonValue &value : list["recipes"].toArray()) {
        const Recipe recipe = Recipe::fromJson(value.toObject());
        // Two sources can hold one name; the first is the one Recipe uses.
        if (!fresh.recipe(recipe.name))
            fresh.recipes << recipe;
    }
}

static void readInstalled(const QJsonObject &installed, Catalog &fresh, Place place)
{
    const QString profile = installed["profile"].toString();
    for (const QJsonValue &value : installed["packages"].toArray()) {
        const QJsonObject o = value.toObject();
        InstalledItem item;
        item.place = place;
        item.name = o["name"].toString();
        item.version = o["version"].toString();
        if (o.contains("installed_on"))
            item.installedOn = QDateTime::fromSecsSinceEpoch(o["installed_on"].toInteger()).date();
        item.sizeBytes = o["size_bytes"].toInteger();
        item.profile = profile;
        item.source = o["source"].toString();
        item.commit = o["commit"].toString();
        item.receiptRecipe = o["recipe"].toString();
        // What it put on the system, from the receipt's own list of files.
        for (const QJsonValue &file : o["files"].toArray()) {
            const QString path = file.toString();
            item.files << path;
            if (path.startsWith("bin/"))
                item.programs << path.mid(4);
            else if (path.startsWith("share/applications/") && path.endsWith(".desktop"))
                item.menu << path.mid(19).chopped(8);
            // Until the receipt carries a size, measure what the profile holds.
            if (!o.contains("size_bytes") && !profile.isEmpty())
                item.sizeBytes += QFileInfo(profile + '/' + path).size();
        }
        fresh.installed << item;
    }
}

// The system's name for the row of its base: os-release NAME (RECIPE_GUI_OS_RELEASE names another file).
static QString osName()
{
    QFile file(qEnvironmentVariable("RECIPE_GUI_OS_RELEASE", "/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("NAME=")))
            return line.mid(5).remove(QLatin1Char('"')).remove(QLatin1Char('\'')).trimmed();
    }
    return QString();
}

// A package of `recipe --system installed --json`: `{name, version, recipe, recipe_digest,
// dependencies, sessions}`, and for the base `{name: "base", version, serial, release: true, ...}`
// (tools/recipe system_route.rs `summary`). Recipe's own reserved entries are not software.
static bool readRootPackage(const QJsonObject &o, InstalledItem &item)
{
    const QString name = o["name"].toString();
    if (name == QLatin1String("system-effects") || name == QLatin1String("previous-kernel"))
        return false;
    item.name = name;
    item.version = o["version"].toString();
    item.inStore = false;
    item.base = o["release"].toBool();
    item.serial = o["serial"].toInteger();
    item.sessions = o["sessions"].toInt();
    item.receiptRecipe = o["recipe"].toString();
    for (const QJsonValue &dependency : o["dependencies"].toArray())
        item.dependencies << dependency.toString();
    return true;
}

// What `recipe --system installed --json` answers. On an A/B computer:
// `{booted: {root, packages}, other: {root, mount, packages}, not_done: [{package, error}]}`.
// On one with a single root: `{root, packages}` (the running root). Either way each package is a name
// and a version, what it needs and how many desktop sessions it provides: no sizes, no programs.
static void readSystem(const QJsonObject &answer, Catalog &fresh)
{
    fresh.systemName = osName();
    QMap<QString, InstalledItem> booted, other;
    auto readAll = [](const QJsonArray &packages, QMap<QString, InstalledItem> &into) {
        for (const QJsonValue &value : packages) {
            InstalledItem item;
            if (readRootPackage(value.toObject(), item))
                into.insert(item.name, item);
        }
    };
    if (!answer["booted"].isObject()) {
        readAll(answer["packages"].toArray(), booted);
        for (InstalledItem item : booted) {
            item.place = Place::Computer;
            fresh.installed << item;
        }
        return;
    }
    fresh.ab = true;
    fresh.bootedRoot = answer["booted"]["root"].toString();
    fresh.otherRoot = answer["other"]["root"].toString();
    fresh.otherMount = answer["other"]["mount"].toString();
    readAll(answer["booted"]["packages"].toArray(), booted);
    readAll(answer["other"]["packages"].toArray(), other);
    for (InstalledItem item : booted) {
        item.place = Place::Computer;
        item.leavesOther = !other.contains(item.name);
        fresh.installed << item;
    }
    // What only the other root holds: new, or at another version.
    for (InstalledItem item : other) {
        if (booted.contains(item.name) && booted[item.name].version == item.version)
            continue;
        item.place = Place::Other;
        fresh.installed << item;
    }
    for (const QJsonValue &value : answer["not_done"].toArray())
        fresh.notDone << NotDone{value.toObject()["package"].toString(), value.toObject()["error"].toString()};
}

static void readUpdates(const QJsonObject &updates, Catalog &fresh, Place place)
{
    for (const QJsonValue &value : updates["updates"].toArray()) {
        const QJsonObject o = value.toObject();
        UpdateItem item{o["name"].toString(), o["from"].toString(), o["to"].toString(), place, false, {}};
        // Null: nothing was recorded when the installed version was installed.
        item.changesKnown = o["changes"].isArray();
        item.sameVersion = o["reason"] == QLatin1String("recipe");
        for (const QJsonValue &change : o["changes"].toArray()) {
            const QJsonObject c = change.toObject();
            item.changes << Change{c["field"].toString(), c["before"].toString(), c["after"].toString(),
                                   c["kind"].toString()};
        }
        fresh.updates << item;
    }
    for (const QJsonValue &value : updates["problems"].toArray())
        fresh.updateProblems << QStringLiteral("%1: %2").arg(value.toObject()["name"].toString(),
                                                             value.toObject()["problem"].toString());
}

static void readStorage(const QJsonObject &storage, Catalog &fresh, Place place)
{
    Store &store = fresh.stores[int(place)];
    store.has = true;
    store.activeBytes = storage["active_bytes"].toInteger();
}

// What `recipe history --json` answers: `{"schema_version": 1, "history": [...]}`,
// each entry `at`, `what`, `package`, `from`, `to`, `source`, newest first
// (tools/recipe src/packages/history.rs `HistoryEntry`).
static void readHistory(const QJsonObject &answer, Catalog &fresh, Place place)
{
    Store &store = fresh.stores[int(place)];
    store.historyKnown = true;
    for (const QJsonValue &value : answer["history"].toArray()) {
        const QJsonObject o = value.toObject();
        store.history << HistoryEntry{o["at"].toInteger(), o["what"].toString(), o["package"].toString(),
                                      o["from"].toString(), o["to"].toString(), o["source"].toString()};
    }
}

namespace {

// One read: the arguments, and what to do with the answer. A read that is
// `soft` may fail without the catalog failing: its problem is kept instead.
struct Step {
    QStringList arguments;
    std::function<void(const QJsonObject &)> read;
    std::function<void(const QString &)> failed; // set: the catalog goes on
};

} // namespace

void RecipeClient::load(Loaded done)
{
    ++loading_;
    auto fresh = std::make_shared<Catalog>();
    auto finish = [this, done, fresh](bool ok, const QString &problem) {
        --loading_;
        if (ok) {
            // `recipe updates` cannot look at the computer's system software (the route that
            // lists it says only names and versions); the collection's own version says what is
            // newer. Never for a recipe of the system's release (source "system"): those change
            // only with a system release, and never for one Recipe says cannot be installed here.
            for (const InstalledItem &item : fresh->installed) {
                const Recipe *recipe = fresh->recipe(item.name);
                // Not when the other root already holds the recipe's version.
                const InstalledItem *other = fresh->installedItem(item.name, Place::Other);
                const bool taken = other && recipe && other->version == recipe->version;
                const bool own = recipe && recipe->runsHere && recipe->origin != QLatin1String("system");
                if (item.place == Place::Computer && !item.inStore && own && recipe->version != item.version && !taken) {
                    UpdateItem newer{item.name, item.version, recipe->version, Place::Computer, true, {}};
                    newer.changesKnown = false;
                    fresh->updates << newer;
                }
            }
            for (const Store &store : fresh->stores) {
                fresh->activeBytes += store.activeBytes;
            }
        }
        done(ok, *fresh, problem);
    };

    // What is read, in order. The collection first; then each place's
    // software, updates and storage.
    auto steps = std::make_shared<QList<Step>>();
    const bool one = explicit_;
    fresh->twoPlaces = !one;
    steps->append({QStringList{"list", "--json"} + collection() + store(), [fresh](const QJsonObject &list) { readList(list, *fresh); }, nullptr});
    auto store_ = [&](Place place) {
        steps->append({QStringList{"installed", "--json"} + at(place), [fresh, place](const QJsonObject &a) { readInstalled(a, *fresh, place); }, nullptr});
        steps->append({QStringList{"updates", "--json"} + collection() + at(place), [fresh, place](const QJsonObject &a) { readUpdates(a, *fresh, place); }, nullptr});
        // What the store takes is said beside the list; the list does not wait on it failing.
        steps->append({QStringList{"storage", "--json"} + at(place), [fresh, place](const QJsonObject &a) { readStorage(a, *fresh, place); },
                       [](const QString &) {}});
        // What was done to the store; like storage, its absence only leaves the history out.
        steps->append({QStringList{"history", "--json"} + at(place), [fresh, place](const QJsonObject &a) { readHistory(a, *fresh, place); },
                       [](const QString &) {}});
    };
    if (one) {
        store_(Place::Computer);
    } else {
        // The computer's own software is read the same way on both kinds of computer.
        store_(Place::Me);
        steps->append({QStringList{"--system", "installed", "--json"}, [fresh](const QJsonObject &a) { readSystem(a, *fresh); },
                       [fresh](const QString &why) { fresh->computerProblem = why; }});
    }

    auto next = std::make_shared<std::function<void(int)>>();
    *next = [this, steps, next, finish, fresh](int index) {
        if (index >= steps->size())
            return finish(true, QString());
        const Step &step = steps->at(index);
        query(step.arguments, [steps, next, finish, index](const QJsonObject &answer, const QString &problem) {
            const Step &step = steps->at(index);
            if (!problem.isEmpty()) {
                if (!step.failed)
                    return finish(false, problem);
                step.failed(problem);
            } else {
                step.read(answer);
            }
            (*next)(index + 1);
        });
    };
    (*next)(0);
}

void RecipeClient::recipeFile(const QString &qualifiedName,
                              std::function<void(const QString &path, const QString &problem)> then)
{
    auto *run = new Run(program_, QStringList{"info", qualifiedName} + collection() + store(), this);
    connect(run, &Run::finished, this, [run, then](bool ok) {
        static const QRegularExpression colour(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
        static const QRegularExpression says(QStringLiteral("^Recipe:\\s+(\\S.*?)\\s*$"), QRegularExpression::MultilineOption);
        const QString text = QString::fromUtf8(run->answer()).remove(colour);
        const QRegularExpressionMatch found = says.match(text);
        const QString problem = run->problem();
        run->deleteLater();
        if (ok && found.hasMatch())
            then(found.captured(1), QString());
        else
            then(QString(), ok ? QObject::tr("`recipe info` did not say which file it is.")
                               : QObject::tr("`recipe info` failed: %1").arg(problem));
    });
    run->start();
}

Run *RecipeClient::change(const QStringList &arguments, Place place)
{
    // The person's own store needs no helper: it is theirs.
    if (rootHelper_.isEmpty() || (place == Place::Me && !explicit_))
        return new Run(program_, arguments, this);
    return new Run(rootHelper_, QStringList{program_} + arguments, this);
}

Run *RecipeClient::install(const QString &name, Place place)
{
    return change(lead(place) + QStringList{"install", name, "--machine-events"} + collection() + at(place), place);
}

Run *RecipeClient::update(const QString &name, Place place)
{
    return change(lead(place) + QStringList{"update", name, "--machine-events"} + collection() + at(place), place);
}

Run *RecipeClient::remove(const QString &name, Place place)
{
    return change(lead(place) + QStringList{"remove", name, "--machine-events"} + collection() + at(place), place);
}

Run *RecipeClient::refresh(const QString &source)
{
    QStringList arguments{"refresh"};
    if (!source.isEmpty())
        arguments << source;
    return new Run(program_, arguments + store(), this);
}

Run *RecipeClient::addSource(const QString &addressOrFolder)
{
    QString where = addressOrFolder.trimmed();
    if (where.startsWith(QLatin1Char('~')))
        where = QDir::homePath() + where.mid(1);
    const bool folder = where.startsWith(QLatin1Char('/'));
    return change((folder ? QStringList{"source", "add", "--path", where} : QStringList{"source", "add", where}) + store(),
                  Place::Computer);
}

Run *RecipeClient::removeSource(const QString &name)
{
    return change(QStringList{"source", "remove", name} + store(), Place::Computer);
}
