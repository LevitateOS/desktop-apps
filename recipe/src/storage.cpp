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

    auto *header = new QHBoxLayout;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    if (stores.isEmpty()) {
        top->addWidget(heading(tr("Recipe could not say what it keeps")));
        top->addWidget(dim(text(tr("`recipe storage` gave no answer for any place."), true)));
    } else {
        // F2: this page shows one total and one line per kind (`Store::storage.kinds`), each removable.
        top->addWidget(heading(tr("Installed software takes %1").arg(bytesText(catalog.installedBytes()))));
    }
    header->addLayout(top, 1);
    column->addLayout(header);

    auto *inner = new QWidget;
    auto *body = new QVBoxLayout(inner);
    body->setContentsMargins(0, 0, 2, 0);
    body->setSpacing(14);

    // Installed programs, by place: the numbers `recipe storage` gives for each store.
    auto *rooms = new Panel;
    rooms->column()->addWidget(strong(tr("What is installed takes")));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int row = 0;
    auto line = [&](const QString &place, const QString &words) {
        grid->addWidget(dim(text(place)), row, 0, Qt::AlignTop);
        grid->addWidget(text(words, true), row, 1);
        ++row;
    };
    for (Place place : {Place::Computer, Place::Me}) {
        const Store &store = catalog.stores[int(place)];
        if (store.has) {
            const QString where = !catalog.twoPlaces ? tr("in %1").arg(places.systemRoot.isEmpty() ? tr("the store recipe uses") : places.systemRoot)
                                  : place == Place::Me ? tr("in your own folder")
                                                       : tr("in %1").arg(places.systemRoot);
            line(catalog.twoPlaces ? placeTitle(place) : tr("This store"), tr("%1, %2").arg(bytesText(store.installedBytes()), where));
        } else if (place == Place::Computer && catalog.twoPlaces) {
            // An A/B system: Recipe answers with names and versions, no store stands behind them.
            line(placeTitle(place), catalog.ab ? tr("In %1. Recipe reports no size for it.").arg(catalog.bootedWords())
                                                : tr("Recipe reports no size for it."));
        }
    }
    int other = 0;
    for (const InstalledItem &item : catalog.installed)
        other += item.place == Place::Other;
    if (other)
        line(placeTitle(Place::Other, catalog.otherRoot),
             tr("%1 only there, active when it is started. Recipe reports no size for it.").arg(counted(other, tr("%1 program"), tr("%1 programs"))));
    rooms->column()->addLayout(grid);
    rooms->column()->addWidget(dim(text(tr("Only what `recipe storage` reports: what is installed. Recipe "
                                           "reports no sizes for downloads or builds, so none are shown."),
                                        true)));
    auto *list = new QPushButton(tr("Show what is installed"));
    connect(list, &QPushButton::clicked, this, &StoragePage::openInstalled);
    rooms->column()->addWidget(list, 0, Qt::AlignLeft);
    body->addWidget(rooms);

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
