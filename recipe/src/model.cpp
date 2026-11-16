#include "model.h"

#include "words.h"

#include <QDateTime>
#include <QMap>
#include <QLocale>

#include <QJsonArray>
#include <QLocale>
#include <QUrl>

#include <functional>

static QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray())
        out << item.toString();
    return out;
}

QString Source::host() const
{
    return QUrl(git.isEmpty() ? url : git).host();
}

QString Source::pin() const
{
    if (!commit.isEmpty())
        return QObject::tr("pinned to commit %1").arg(commit.left(10));
    if (checksumFrom == QLatin1String("publisher"))
        return QObject::tr("checked against the checksum the project publishes");
    if (checksumFrom == QLatin1String("signature"))
        return QObject::tr("checked against the project's signature");
    return QObject::tr("checked against a checksum the recipe's author took at first download");
}

State stateFromText(const QString &text)
{
    if (text == QLatin1String("installed"))
        return State::Installed;
    if (text == QLatin1String("built"))
        return State::Built;
    if (text == QLatin1String("downloaded"))
        return State::Downloaded;
    return State::None;
}

QString stateTitle(State state)
{
    switch (state) {
    case State::Installed:
        return QObject::tr("installed");
    case State::Built:
        return QObject::tr("built");
    case State::Downloaded:
        return QObject::tr("downloaded");
    case State::None:
        break;
    }
    return QObject::tr("not here");
}

OnDisk OnDisk::fromJson(const QJsonObject &object)
{
    return OnDisk{object["downloads"].toInteger(), object["build"].toInteger(), object["unfinished"].toInteger()};
}

const StorageKind *StorageInfo::kind(const QString &name) const
{
    for (const StorageKind &k : kinds)
        if (k.kind == name)
            return &k;
    return nullptr;
}

qint64 StorageInfo::bytesOf(const QString &name) const
{
    const StorageKind *found = kind(name);
    return found ? found->bytes : 0;
}

StateInfo StateInfo::fromJson(const QJsonObject &object)
{
    StateInfo info;
    info.name = object["name"].toString();
    info.version = object["version"].toString();
    info.installedVersion = object["installed_version"].toString();
    info.state = stateFromText(object["state"].toString());
    info.keep = object["keep"].toBool();
    info.steps = strings(object["steps"]);
    info.onDisk = OnDisk::fromJson(object["on_disk"].toObject());
    return info;
}

static QList<RunStep> runSteps(const QJsonValue &value)
{
    QList<RunStep> out;
    for (const QJsonValue &item : value.toArray())
        out << RunStep{item.toObject()["step"].toString(), item.toObject()["status"].toString()};
    return out;
}

RunResult RunResult::fromJson(const QJsonObject &object)
{
    RunResult result;
    result.operation = object["operation"].toString();
    result.name = object["name"].toString();
    result.version = object["version"].toString();
    result.state = stateFromText(object["state"].toString());
    result.steps = runSteps(object["steps"]);
    result.kept = object["kept"].toBool();
    result.freedBytes = object["freed_bytes"].toInteger();
    result.profile = object["activation"].toObject()["profile"].toString();
    result.bin = object["activation"].toObject()["bin"].toString();
    for (const QJsonValue &value : object["dependencies"].toArray()) {
        const QJsonObject o = value.toObject();
        result.dependencies << RunDependency{o["name"].toString(), stateFromText(o["state"].toString()), runSteps(o["steps"])};
    }
    return result;
}

Recipe Recipe::fromJson(const QJsonObject &object)
{
    Recipe r;
    r.name = object["name"].toString();
    r.version = object["version"].toString();
    r.description = object["description"].toString();
    r.path = object["path"].toString();

    const QJsonObject about = object["about"].toObject();
    r.kind = about["kind"].toString();
    r.category = about["category"].toString();
    r.title = about["title"].toString();
    r.details = about["details"].toString();
    r.homepage = about["homepage"].toString();
    r.license = about["license"].toString();
    r.afterInstall = about["after_install"].toString();
    r.programs = strings(about["adds"].toObject()["programs"]);
    r.menu = strings(about["adds"].toObject()["menu"]);
    r.keeps = strings(about["keeps"]);
    const QJsonObject notes = about["steps"].toObject();
    for (auto it = notes.begin(); it != notes.end(); ++it)
        r.notes.insert(it.key(), it.value().toString());

    const QJsonObject sources = object["sources"].toObject();
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        Source source{it.key(), s["url"].toString(), s["git"].toString(), s["commit"].toString(),
                      s["sha256"].toString(), s["checksum_from"].toString()};
        source.url.replace("{version}", r.version).replace("{name}", r.name);
        r.sources << source;
    }

    const QJsonObject needs = object["needs"].toObject();
    r.needs.network = strings(needs["network"]);
    r.needs.memoryMib = needs["memory_mib"].toInteger();
    r.needs.storageMaxBytes = needs["storage_max_bytes"].toInteger();
    r.needs.timeoutSecs = needs["timeout_secs"].toInteger();
    r.needs.networkMaxBytes = needs["network_max_bytes"].toInteger();

    r.origin = object["source"].toString();
    r.deps = strings(object["deps"]);
    r.buildDeps = strings(object["build_deps"]);
    r.steps = strings(object["steps"]);
    r.functions = strings(object["functions"]);
    const QJsonObject upstream = object["upstream"].toObject();
    r.upstream.github = upstream["github"].toString();
    r.upstream.latestFunction = upstream["latest"].toBool();
    r.state = stateFromText(object["state"].toString());
    r.installedVersion = object["installed_version"].toString();
    r.keep = object["keep"].toBool();
    r.libc = strings(about["libc"]);
    r.libcCarried = about["libc_carried"].toBool();
    r.runsHere = object["runs_here"].toBool(true);
    r.notRunnableReason = object["not_runnable_reason"].toString();
    return r;
}

QString Change::title() const
{
    static const QMap<QString, QString> titles{
        {"network", QObject::tr("Network")},          {"network_max_bytes", QObject::tr("Download")},
        {"inputs", QObject::tr("Reads")},             {"outputs", QObject::tr("Writes")},
        {"memory_mib", QObject::tr("Memory")},        {"storage_max_objects", QObject::tr("Files while it runs")},
        {"storage_max_bytes", QObject::tr("Disk while it runs")}, {"timeout_secs", QObject::tr("Time")}};
    return titles.value(field, field);
}

bool Change::isList() const
{
    return field == QLatin1String("network") || field == QLatin1String("inputs") || field == QLatin1String("outputs");
}

static QStringList listOf(const QString &value)
{
    return value == QLatin1String("none") ? QStringList() : value.split(QStringLiteral(", "), Qt::SkipEmptyParts);
}

QStringList Change::added() const
{
    QStringList found;
    const QStringList was = listOf(before);
    for (const QString &name : listOf(after))
        if (!was.contains(name))
            found << name;
    return found;
}

QStringList Change::removed() const
{
    QStringList found;
    const QStringList is = listOf(after);
    for (const QString &name : listOf(before))
        if (!is.contains(name))
            found << name;
    return found;
}

QStringList Change::stays() const
{
    QStringList found;
    const QStringList is = listOf(after);
    for (const QString &name : listOf(before))
        if (is.contains(name))
            found << name;
    return found;
}

QString Change::amount(const QString &value)
{
    const qsizetype space = value.indexOf(QLatin1Char(' '));
    bool isNumber = false;
    const qint64 number = value.left(space).toLongLong(&isNumber);
    if (space < 0 || !isNumber)
        return value;
    const QString unit = value.mid(space + 1);
    if (unit == QLatin1String("bytes"))
        return bytesText(number);
    if (unit == QLatin1String("MiB"))
        return bytesText(number * 1048576);
    if (unit == QLatin1String("s"))
        return number >= 120 ? counted(int(number / 60), QObject::tr("%1 minute"), QObject::tr("%1 minutes"))
                             : counted(int(number), QObject::tr("%1 second"), QObject::tr("%1 seconds"));
    if (unit == QLatin1String("objects"))
        return counted(int(number), QObject::tr("%1 file"), QObject::tr("%1 files"));
    return value;
}

bool UpdateItem::asksMore() const
{
    for (const Change &change : changes)
        if (change.kind == QLatin1String("added") || change.kind == QLatin1String("raised"))
            return true;
    return false;
}

QString UpdateItem::asksMoreWords() const
{
    QStringList fields;
    for (const Change &change : changes)
        if (change.kind == QLatin1String("added") || change.kind == QLatin1String("raised"))
            fields << change.title().toLower();
    return fields.join(QObject::tr(", "));
}

const Recipe *Catalog::recipe(const QString &name) const
{
    for (const Recipe &r : recipes)
        if (r.name == name)
            return &r;
    return nullptr;
}

const Origin *Catalog::origin(const QString &name) const
{
    for (const Origin &o : origins)
        if (o.name == name)
            return &o;
    return nullptr;
}

QString Catalog::qualified(const QString &name) const
{
    const Recipe *found = recipe(name);
    return found && !found->origin.isEmpty() ? found->origin + QLatin1Char(':') + name : name;
}

int Catalog::countFrom(const QString &origin) const
{
    int count = 0;
    for (const Recipe &r : recipes)
        count += r.listed() && r.origin == origin;
    return count;
}

QString Catalog::originsWords() const
{
    QStringList named;
    for (const Origin &o : origins)
        if (countFrom(o.name) > 0)
            named << o.shown();
    if (named.isEmpty())
        return QObject::tr("No source has recipes");
    if (named.size() == 1)
        return QObject::tr("Recipes from %1").arg(named.first());
    return QObject::tr("Recipes from %1 sources").arg(named.size());
}

QString Origin::shown() const
{
    if (!title.isEmpty())
        return title;
    if (name == QLatin1String("local"))
        return QObject::tr("your own recipes");
    if (name == QLatin1String("image"))
        return QObject::tr("the recipes shipped with the system");
    return name;
}

QString Origin::state() const
{
    if (!problem.isEmpty())
        return problem;
    if (!remote())
        return QObject::tr("A folder on this computer, read as it is");
    if (checked <= 0)
        return QObject::tr("Its list was never fetched");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(checked);
    const QString time = QLocale().toString(when.time(), QLocale::ShortFormat);
    if (when.date() == QDate::currentDate())
        return QObject::tr("Its list was fetched today at %1").arg(time);
    return QObject::tr("Its list was fetched on %1").arg(QLocale().toString(when.date(), QLocale::ShortFormat));
}

const InstalledItem *Catalog::installedItem(const QString &name) const
{
    for (Place place : {Place::Me, Place::Computer, Place::Other})
        if (const InstalledItem *found = installedItem(name, place))
            return found;
    return nullptr;
}

const InstalledItem *Catalog::installedItem(const QString &name, Place place) const
{
    for (const InstalledItem &i : installed)
        if (i.name == name && i.place == place)
            return &i;
    return nullptr;
}

QList<const InstalledItem *> Catalog::installedEverywhere(const QString &name) const
{
    QList<const InstalledItem *> found;
    for (Place place : {Place::Me, Place::Computer, Place::Other})
        if (const InstalledItem *item = installedItem(name, place))
            found << item;
    return found;
}

qint64 Catalog::installedBytes() const
{
    qint64 bytes = 0;
    for (const Store &store : stores)
        bytes += store.installedBytes();
    return bytes;
}

const UpstreamItem *Catalog::upstreamOf(const QString &name) const
{
    for (const UpstreamItem &u : upstream)
        if (u.name == name)
            return &u;
    return nullptr;
}

const UpstreamItem *Catalog::newerUpstream(const QString &name) const
{
    const UpstreamItem *found = upstreamOf(name);
    return found && found->newer ? found : nullptr;
}

const UpdateItem *Catalog::update(const QString &name) const
{
    for (const UpdateItem &u : updates)
        if (u.name == name)
            return &u;
    return nullptr;
}

const UpdateItem *Catalog::update(const QString &name, Place place) const
{
    for (const UpdateItem &u : updates)
        if (u.name == name && u.place == place)
            return &u;
    return nullptr;
}

bool Catalog::inOtherRoot(const QString &name) const
{
    return installedItem(name, Place::Other) != nullptr;
}

QString Catalog::otherWords() const
{
    return otherRoot.isEmpty() ? QObject::tr("the other root") : QObject::tr("the other root (%1)").arg(otherRoot);
}

QString Catalog::bootedWords() const
{
    return bootedRoot.isEmpty() ? QObject::tr("the running root") : QObject::tr("the running root (%1)").arg(bootedRoot);
}

QString Catalog::takenOutWords(const InstalledItem &item) const
{
    if (!ab)
        return QObject::tr("It is taken out of this computer's system, for everyone, at once. Programs that are already running may "
                           "need a restart.");
    if (item.place == Place::Other)
        return QObject::tr("It is taken out of %1, which is the only root that holds it.").arg(otherWords());
    return QObject::tr("It is taken out of %1. %2 keeps it until %3 is started.")
        .arg(otherWords(), bootedWords().left(1).toUpper() + bootedWords().mid(1),
             otherRoot.isEmpty() ? QObject::tr("the other root") : otherRoot);
}

QString Catalog::unavailable(const QString &name) const
{
    const Recipe *found = recipe(name);
    return found && !found->runsHere ? found->notRunnableReason : QString();
}

QString Catalog::cannotOpenWords() const
{
    return ab ? QObject::tr("This window cannot open it: the list of the running root names the software and its version, not its programs.")
              : QObject::tr("This window cannot open it: the list of the system's software names it and its version, not its programs.");
}

QString Catalog::unavailableLine(const QString &name) const
{
    // Recipe ends the reason (whether the cause is the C library or ABOUT.systems) with the command
    // that installs it anyway; a window that does not offer that leaves it out.
    QString why = unavailable(name);
    const qsizetype anyway = why.indexOf(QLatin1String(" To install it anyway:"));
    if (anyway >= 0)
        why.truncate(anyway);
    return why.section(QLatin1Char('\n'), 0, 0).trimmed();
}

// A system group's libraries are part of the system too: reached through `deps`, from a recipe of
// kind system.
bool Catalog::intoSystemOnly(const QString &name) const
{
    const Recipe *found = recipe(name);
    if (!found)
        return false;
    if (found->isSystem())
        return true;
    if (!found->isLibrary())
        return false;
    QStringList seen;
    std::function<bool(const Recipe &)> needed = [&](const Recipe &group) {
        if (seen.contains(group.name))
            return false;
        seen << group.name;
        for (const QString &dep : group.deps) {
            if (dep == name)
                return true;
            const Recipe *next = recipe(dep);
            if (next && (next->isSystem() || next->isLibrary()) && needed(*next))
                return true;
        }
        return false;
    };
    for (const Recipe &each : recipes)
        if (each.isSystem() && needed(each))
            return true;
    return false;
}

QList<const InstalledItem *> Catalog::rootItems(Place root) const
{
    QList<const InstalledItem *> found;
    if (root == Place::Computer) {
        for (const InstalledItem &item : installed)
            if (item.place == Place::Computer && !item.inStore)
                found << &item;
        return found;
    }
    if (root != Place::Other)
        return found;
    for (const InstalledItem &item : installed)
        if (item.place == Place::Other)
            found << &item;
    // What both roots hold is listed once, in the running root's row.
    for (const InstalledItem &item : installed)
        if (item.place == Place::Computer && !item.inStore && !item.leavesOther && !installedItem(item.name, Place::Other))
            found << &item;
    return found;
}

QStringList Catalog::neededBy(const QString &name, Place root) const
{
    QStringList found;
    for (const InstalledItem *item : rootItems(root))
        if (item->name != name && !item->base && item->dependencies.contains(name))
            found << item->name;
    found.sort();
    return found;
}

bool Catalog::lastDesktop(const QString &name) const
{
    const QList<const InstalledItem *> root = rootItems(systemTarget());
    bool provides = false;
    for (const InstalledItem *item : root)
        if (item->name == name)
            provides = item->sessions > 0;
    if (!provides)
        return false;
    for (const InstalledItem *item : root)
        if (item->name != name && item->sessions > 0)
            return false;
    return true;
}

QStringList Catalog::shownNames(const QStringList &names) const
{
    QStringList shown;
    for (const QString &name : names)
        shown << (recipe(name) ? recipe(name)->shown() : name);
    return shown;
}

QString Catalog::baseWords(const InstalledItem &item) const
{
    const QString who = systemName.isEmpty() ? QObject::tr("This system") : systemName;
    return QObject::tr("%1 %2, the base system").arg(who, item.version);
}

int Catalog::softwareCount() const
{
    int count = 0;
    for (const InstalledItem &item : installed)
        count += !item.base;
    return count;
}

QList<Place> Catalog::installPlaces(const QString &name) const
{
    QList<Place> free;
    // "For me" comes first: it is the default. The other root counts as held: installing
    // again would repeat what is already written there.
    // Software of the system itself (a desktop, the printing stack, a library one needs) is never "for me".
    if (twoPlaces && !installedItem(name, Place::Me) && !intoSystemOnly(name))
        free << Place::Me;
    if (!installedItem(name, Place::Computer) && !installedItem(name, Place::Other))
        free << Place::Computer;
    return free;
}

QString placeTitle(Place place, const QString &otherRoot)
{
    switch (place) {
    case Place::Me:
        return QObject::tr("For me");
    case Place::Computer:
        return QObject::tr("For everyone");
    case Place::Other:
        break;
    }
    return otherRoot.isEmpty() ? QObject::tr("In the other root") : QObject::tr("In the other root (%1)").arg(otherRoot);
}

static const QList<QPair<QString, const char *>> &categoryTable()
{
    static const QList<QPair<QString, const char *>> table = {
        {"office", QT_TRANSLATE_NOOP("QObject", "Office")},
        {"mail", QT_TRANSLATE_NOOP("QObject", "Mail")},
        {"chat", QT_TRANSLATE_NOOP("QObject", "Chat")},
        {"web", QT_TRANSLATE_NOOP("QObject", "Web")},
        {"media", QT_TRANSLATE_NOOP("QObject", "Media")},
        {"graphics", QT_TRANSLATE_NOOP("QObject", "Graphics")},
        {"development", QT_TRANSLATE_NOOP("QObject", "Developer tools")},
        {"games", QT_TRANSLATE_NOOP("QObject", "Games")},
        {"utilities", QT_TRANSLATE_NOOP("QObject", "Utilities")},
        {"drivers-codecs", QT_TRANSLATE_NOOP("QObject", "Drivers and codecs")},
    };
    return table;
}

QStringList Catalog::categories() const
{
    QStringList used;
    for (const auto &entry : categoryTable())
        for (const Recipe &r : recipes)
            if (r.listed() && r.category == entry.first) {
                used << entry.first;
                break;
            }
    return used;
}

QString categoryTitle(const QString &id)
{
    for (const auto &entry : categoryTable())
        if (entry.first == id)
            return QObject::tr(entry.second);
    return id;
}

QString stepTitle(const QString &step)
{
    if (step == QLatin1String("download"))
        return QObject::tr("Download");
    if (step == QLatin1String("build"))
        return QObject::tr("Build");
    if (step == QLatin1String("install"))
        return QObject::tr("Install");
    if (step == QLatin1String("cleanup"))
        return QObject::tr("Clean up");
    return step;
}
