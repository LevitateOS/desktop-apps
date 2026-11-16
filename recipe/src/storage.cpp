#include "storage.h"

#include "parts.h"
#include "words.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace {

QString shownName(const Catalog &catalog, const QString &name)
{
    const Recipe *recipe = catalog.recipe(name);
    return recipe ? recipe->shown() : name;
}

QString whenWords(qint64 seconds)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(seconds);
    const QString time = QLocale().toString(when.time(), QLocale::ShortFormat);
    if (when.date() == QDate::currentDate())
        return QObject::tr("today at %1").arg(time);
    return QObject::tr("%1 at %2").arg(QLocale().toString(when.date(), QLocale::ShortFormat), time);
}

QString placeWords(Place place)
{
    return place == Place::Me ? QObject::tr("for me") : QObject::tr("for everyone");
}

} // namespace

QString historySentence(const Catalog &catalog, const HistoryEntry &entry)
{
    const QString what = entry.what;
    const QString name = shownName(catalog, entry.package);
    if (what == QLatin1String("install")) {
        QString words = QObject::tr("Installed %1 %2").arg(name, entry.to);
        // `from` is set when it was installed over an installed version.
        if (!entry.from.isEmpty() && entry.from != entry.to)
            words = QObject::tr("Installed %1 %2, over %3").arg(name, entry.to, entry.from);
        return entry.source.isEmpty() ? words : QObject::tr("%1, from %2").arg(words, entry.source);
    }
    if (what == QLatin1String("update")) {
        const QString words = entry.from.isEmpty() || entry.from == entry.to
                                  ? QObject::tr("Updated %1 to %2").arg(name, entry.to)
                                  : QObject::tr("Updated %1 from %2 to %3").arg(name, entry.from, entry.to);
        return entry.source.isEmpty() ? words : QObject::tr("%1, from %2").arg(words, entry.source);
    }
    if (what == QLatin1String("remove"))
        return QObject::tr("Removed %1 %2").arg(name, entry.from);
    // A kind of change this window does not know: say it as `recipe` did.
    return entry.package.isEmpty() ? entry.what : QObject::tr("%1 %2").arg(entry.what, entry.package);
}

void StoragePage::show(const Catalog &catalog, const Places &places)
{
    QVBoxLayout *column = fresh();
    lines_.clear();

    // The places a store stands behind, in the order the window names them.
    QList<Place> stores;
    for (Place place : {Place::Me, Place::Computer})
        if (catalog.stores[int(place)].has)
            stores << place;

    Q_UNUSED(places);
    qint64 total = 0;
    for (Place place : stores) {
        const StorageInfo &info = catalog.stores[int(place)].storage;
        qint64 summed = 0;
        for (const StorageKind &kind : info.kinds)
            summed += kind.bytes;
        total += info.totalBytes ? info.totalBytes : summed;
    }

    auto *header = new QHBoxLayout;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    if (stores.isEmpty()) {
        top->addWidget(heading(tr("Recipe could not say what it keeps")));
        top->addWidget(dim(text(tr("`recipe storage` gave no answer for any place."), true)));
    } else {
        top->addWidget(heading(tr("Recipe holds %1 on this computer").arg(bytesText(total))));
        top->addWidget(dim(text(tr("One line for each kind of what it keeps, with a way to remove it."), true)));
    }
    header->addLayout(top, 1);
    column->addLayout(header);

    auto *inner = new QWidget;
    auto *body = new QVBoxLayout(inner);
    body->setContentsMargins(0, 0, 2, 0);
    body->setSpacing(14);

    // One panel for each store: a line for each kind `recipe storage` names.
    auto addStore = [&](Place place) {
        const StorageInfo &info = catalog.stores[int(place)].storage;
        auto *panel = new Panel;
        auto *head = new QHBoxLayout;
        QString title = catalog.twoPlaces ? placeTitle(place) : tr("What Recipe keeps");
        head->addWidget(strong(title));
        head->addStretch();
        qint64 notInstalled = 0;
        for (const char *kind : {"downloaded", "built", "kept", "unfinished"})
            notInstalled += info.bytesOf(QLatin1String(kind));
        auto *all = new QPushButton(tr("Clean up everything not installed"));
        all->setToolTip(tr("Deletes every program's downloads and build material. Installed programs stay."));
        all->setEnabled(notInstalled > 0);
        connect(all, &QPushButton::clicked, this, [this, place] { emit cleanupAll(place); });
        head->addWidget(all);
        panel->column()->addLayout(head);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(6);
        grid->setColumnStretch(0, 1);
        panel->column()->addLayout(grid);
        int row = 0;
        for (const StorageKind &kind : info.kinds) {
            QString name, words;
            std::optional<Cleanup> programs; // removes one program's part of this kind
            if (kind.kind == QLatin1String("installed")) {
                name = tr("Installed programs");
                words = tr("The files of what is installed. A program is removed from Installed.");
            } else if (kind.kind == QLatin1String("downloaded")) {
                name = tr("Downloaded");
                words = tr("Downloads of programs that are not installed.");
                programs = Cleanup::Downloads;
            } else if (kind.kind == QLatin1String("built")) {
                name = tr("Built");
                words = tr("Build material and results of programs that are not installed.");
                programs = Cleanup::Build;
            } else if (kind.kind == QLatin1String("kept")) {
                name = tr("Kept by choice");
                words = tr("Downloads and build material of installed programs that have the keep option.");
                programs = Cleanup::Everything;
            } else if (kind.kind == QLatin1String("unfinished")) {
                name = tr("Unfinished runs");
                words = tr("What a run that was interrupted left behind.");
                programs = Cleanup::Build;
            } else if (kind.kind == QLatin1String("build_image")) {
                name = tr("Build image");
                words = tr("The cached compiler image. It is fetched again when a recipe needs it.");
            } else if (kind.kind == QLatin1String("logs")) {
                name = tr("Logs");
                words = tr("Diagnostic logs of runs.");
            } else {
                name = kind.kind;
            }
            if (!kind.path.isEmpty())
                words += tr(" In %1.").arg(kind.path);
            if (row)
                grid->addWidget(rule(), row++, 0, 1, 3);
            auto *label = new QVBoxLayout;
            label->setSpacing(1);
            label->addWidget(strong(name));
            if (!words.isEmpty())
                label->addWidget(dim(text(words, true)));
            grid->addLayout(label, row, 0);
            grid->addWidget(text(bytesText(kind.bytes)), row, 1, Qt::AlignRight | Qt::AlignTop);
            QPushButton *remove = nullptr;
            if (kind.kind == QLatin1String("installed")) {
                remove = new QPushButton(tr("Show what is installed"));
                connect(remove, &QPushButton::clicked, this, &StoragePage::openInstalled);
            } else if (kind.kind == QLatin1String("build_image")) {
                remove = new QPushButton(tr("Remove"));
                remove->setEnabled(kind.removable && kind.bytes > 0);
                if (!kind.removable)
                    remove->setToolTip(tr("This image was installed with the system"));
                connect(remove, &QPushButton::clicked, this, [this, place] { emit removeBuildImage(place); });
            } else if (kind.kind == QLatin1String("logs")) {
                remove = new QPushButton(tr("Remove"));
                remove->setEnabled(kind.removable && kind.bytes > 0);
                connect(remove, &QPushButton::clicked, this, [this, place] { emit cleanupLogs(place); });
            }
            if (remove)
                grid->addWidget(remove, row, 2, Qt::AlignRight | Qt::AlignTop);
            ++row;
            if (programs) {
                const Cleanup what = *programs;
                for (const auto &program : kind.programs) {
                    const QString programName = program.first;
                    grid->addWidget(dim(text(tr("    %1").arg(shownName(catalog, programName)))), row, 0);
                    grid->addWidget(dim(text(bytesText(program.second))), row, 1, Qt::AlignRight);
                    auto *each = new QPushButton(tr("Remove"));
                    each->setEnabled(kind.removable);
                    connect(each, &QPushButton::clicked, this, [this, programName, place, what] { emit cleanup(programName, place, what); });
                    grid->addWidget(each, row, 2, Qt::AlignRight);
                    ++row;
                }
            }
        }
        body->addWidget(panel);
    };
    for (Place place : stores)
        addStore(place);
    if (stores.isEmpty()) {
        // Nothing to list; the heading says why.
    }
    int other = 0;
    for (const InstalledItem &item : catalog.installed)
        other += item.place == Place::Other;
    if (other || (!stores.isEmpty() && !catalog.stores[int(Place::Computer)].has && catalog.twoPlaces)) {
        auto *rooms = new Panel;
        rooms->column()->addWidget(strong(tr("Software of the system")));
        rooms->column()->addWidget(dim(text(other ? tr("%1 only in %2, active when it is started. Recipe reports no size for the system's own software.")
                                                          .arg(counted(other, tr("%1 program"), tr("%1 programs")), catalog.otherWords())
                                                  : tr("Recipe reports no size for the system's own software."),
                                              true)));
        body->addWidget(rooms);
    }

    // What was done, newest first across the stores.
    struct Line {
        HistoryEntry entry;
        Place place;
    };
    QList<Line> done;
    bool known = false;
    for (Place place : stores) {
        known = known || catalog.stores[int(place)].historyKnown;
        for (const HistoryEntry &entry : catalog.stores[int(place)].history)
            done << Line{entry, place};
    }
    std::stable_sort(done.begin(), done.end(), [](const Line &a, const Line &b) { return a.entry.at > b.entry.at; });
    auto *history = new Panel;
    auto *historyTitle = new QVBoxLayout;
    historyTitle->setSpacing(0);
    historyTitle->addWidget(strong(tr("What was done")));
    historyTitle->addWidget(dim(text(tr("Newest first. Recipe keeps the newest entries of this record."))));
    history->column()->addLayout(historyTitle);
    if (done.isEmpty()) {
        history->column()->addWidget(dim(text(known ? tr("Nothing has been done to the installed software yet")
                                                    : tr("Recipe could not say what was done."), true)));
    } else {
        auto *entries = new QGridLayout;
        entries->setHorizontalSpacing(16);
        entries->setVerticalSpacing(6);
        entries->setColumnStretch(0, 1);
        int at = 0;
        for (const Line &each : done) {
            const QString sentence = historySentence(catalog, each.entry);
            lines_ << sentence;
            entries->addWidget(text(sentence, true), at, 0);
            entries->addWidget(dim(text(catalog.twoPlaces ? tr("%1 · %2").arg(whenWords(each.entry.at), placeWords(each.place))
                                                          : whenWords(each.entry.at))),
                               at, 1);
            ++at;
        }
        history->column()->addLayout(entries);
    }
    body->addWidget(history);
    body->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    column->addWidget(scroll, 1);
}
