#include "pages.h"

#include "beforewords.h"
#include "client.h"
#include "parts.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace {

QPushButton *primary(const QString &words)
{
    auto *button = new QPushButton(words);
    button->setDefault(true);
    return button;
}

// The top of a page about one recipe: its picture, name and one line, with
// the page's buttons on the right.
QHBoxLayout *recipeHeader(const Recipe &recipe, const QString &title, QLabel **titleOut = nullptr,
                          const QString &from = QString())
{
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    row->addWidget(new Tile(recipe.shown(), QString(), 60));
    auto *words = new QVBoxLayout;
    words->setSpacing(1);
    QLabel *name = heading(title);
    if (titleOut)
        *titleOut = name;
    words->addWidget(name);
    words->addWidget(text(recipe.description));
    QString line = recipe.category.isEmpty() ? QObject::tr("Version %1").arg(recipe.version)
                                             : QObject::tr("Version %1 · %2").arg(recipe.version, categoryTitle(recipe.category));
    if (!from.isEmpty())
        line += QObject::tr(" · from %1").arg(from);
    words->addWidget(dim(text(line)));
    row->addLayout(words);
    row->addStretch();
    return row;
}

QLabel *tinted(QLabel *label, Mood mood)
{
    // Inside a Panel (whose background is the view colour) a label's text is the Text role.
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, moodColour(palette, mood));
    palette.setColor(QPalette::Text, moodColour(palette, mood));
    label->setPalette(palette);
    return label;
}

// The flag shown before an install: proprietary software is chosen knowingly.
QString proprietaryWords(const Recipe &recipe)
{
    return QObject::tr("Proprietary software. Its licence: %1.").arg(recipe.license);
}

QString fetchSentence(const Recipe &recipe)
{
    if (recipe.sources.isEmpty())
        return recipe.needs.network.isEmpty()
                   ? QObject::tr("Nothing is fetched from the network.")
                   : QObject::tr("Fetches over the network, from the hosts listed beside this and no others.");
    QStringList parts;
    for (const Source &source : recipe.sources)
        parts << QObject::tr("from %1, %2").arg(source.host(), source.pin());
    return QObject::tr("Gets the source %1.").arg(parts.join(QObject::tr("; and ")));
}

QString buildSentence(const Recipe &recipe)
{
    if (!recipe.compiles())
        return QObject::tr("The recipe has no build step.");
    QString sentence = QObject::tr("Runs the recipe's build step on this computer, inside the sandbox. It sees none of your files.");
    if (!recipe.buildDeps.isEmpty())
        sentence += QObject::tr(" Builds first: %1.").arg(recipe.buildDeps.join(QObject::tr(", ")));
    return sentence;
}

// What it adds, as the end of a sentence.
QString addsWords(const Recipe &recipe)
{
    QStringList parts;
    if (!recipe.menu.isEmpty())
        parts << QObject::tr("the menu entry %1").arg(recipe.menu.join(QObject::tr(", ")));
    if (recipe.programs.size() == 1)
        parts << QObject::tr("the program %1").arg(recipe.programs.first());
    else if (recipe.programs.size() > 1)
        parts << QObject::tr("the programs %1").arg(recipe.programs.join(QObject::tr(", ")));
    return parts.join(QObject::tr(" and "));
}

QString installSentence(const Recipe &recipe)
{
    const QString adds = addsWords(recipe);
    QString sentence = adds.isEmpty() ? QObject::tr("Puts its files in place.") : QObject::tr("Adds %1.").arg(adds);
    if (recipe.afterInstall == QLatin1String("restart"))
        sentence += QObject::tr(" The computer must be restarted afterwards.");
    else if (recipe.afterInstall == QLatin1String("log-in-again"))
        sentence += QObject::tr(" You must log in again afterwards.");
    return sentence;
}

// A numeric address as `recipe` reports a refused connection: "203.0.113.9:443"
// or "[2001:db8::1]:443". It is an address, not a host name, and is worded so.
QString refusedAddressWords(const QString &endpoint)
{
    QString address = endpoint, port;
    if (endpoint.startsWith(QLatin1Char('['))) {
        const qsizetype close = endpoint.indexOf(QStringLiteral("]:"));
        if (close > 0) {
            address = endpoint.mid(1, close - 1);
            port = endpoint.mid(close + 2);
        }
    } else if (endpoint.count(QLatin1Char(':')) == 1) {
        address = endpoint.section(QLatin1Char(':'), 0, 0);
        port = endpoint.section(QLatin1Char(':'), 1);
    }
    return port.isEmpty() ? QObject::tr("the address %1").arg(address)
                          : QObject::tr("the address %1, port %2").arg(address, port);
}

// One numbered step: what the front end can say from enforced facts, then the
// author's own note, marked as such.
QHBoxLayout *stepRow(int number, const QString &title, const QString &sentence, const QString &note)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(new StepMark(number), 0, Qt::AlignTop);
    auto *words = new QVBoxLayout;
    words->setSpacing(1);
    words->addWidget(strong(title));
    words->addWidget(text(sentence, true));
    if (!note.isEmpty())
        words->addWidget(dim(text(QObject::tr("The author's note: %1").arg(note), true)));
    row->addLayout(words, 1);
    return row;
}

QTreeWidget *table(const QStringList &columns)
{
    auto *tree = new QTreeWidget;
    tree->setHeaderLabels(columns);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAllColumnsShowFocus(true);
    tree->setIndentation(0);
    tree->header()->setMinimumSectionSize(96);
    tree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < columns.size(); ++column)
        tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    return tree;
}

void clear(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *child = item->layout())
            clear(child);
        delete item;
    }
}

// What a system change reports on its standard output (tools/recipe system_route.rs): `packages` (what
// was written), `present` (already there), the settings files left as they were and the new versions
// written beside them, the accounts created, the state folders made, and how rebuilding the system's
// caches ended (`effects.status`). Only what is not empty is said.
QStringList stringsOf(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray())
        out << item.toString();
    return out;
}

// Recipe names these `kept_config`, `new_config_beside`, `accounts_created` and `var_seeded`; the
// plain names are read as well.
QJsonValue answerField(const QJsonObject &answer, std::initializer_list<const char *> names)
{
    for (const char *name : names)
        if (answer.contains(QLatin1String(name)))
            return answer[QLatin1String(name)];
    return QJsonValue();
}

QString rooted(const QString &path)
{
    return path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path;
}

QStringList systemResultLines(const QJsonObject &answer, const Catalog &catalog)
{
    QStringList lines;
    auto joined = [](const QStringList &names) { return names.join(QObject::tr(", ")); };
    const QStringList written = stringsOf(answer["packages"]);
    if (!written.isEmpty())
        lines << QObject::tr("Written into the system: %1.").arg(joined(catalog.shownNames(written)));
    const QStringList present = stringsOf(answer["present"]);
    if (!present.isEmpty())
        lines << QObject::tr("Already there, so not written again: %1.").arg(joined(catalog.shownNames(present)));
    QStringList kept;
    for (const QString &path : stringsOf(answerField(answer, {"kept_config", "kept"})))
        kept << rooted(path);
    if (!kept.isEmpty())
        lines << QObject::tr("Settings files that were changed on this computer were left as they are: %1.").arg(joined(kept));
    QStringList beside;
    for (QString path : stringsOf(answerField(answer, {"new_config_beside", "beside"}))) {
        if (!path.endsWith(QLatin1String(".recipe-new")))
            path += QStringLiteral(".recipe-new");
        beside << rooted(path);
    }
    if (!beside.isEmpty())
        lines << QObject::tr("The new versions of settings files were written beside them, ending in .recipe-new: %1.")
                     .arg(joined(beside));
    QStringList accounts;
    for (const QJsonValue &value : answerField(answer, {"accounts_created", "created"}).toArray()) {
        if (value.isString()) {
            accounts << value.toString();
            continue;
        }
        const QJsonObject o = value.toObject();
        accounts << (o["group"].toString().isEmpty() ? QObject::tr("%1 %2").arg(o["kind"].toString(), o["name"].toString())
                                                     : QObject::tr("the user %1 added to the group %2").arg(o["name"].toString(), o["group"].toString()));
    }
    if (!accounts.isEmpty())
        lines << QObject::tr("Accounts created: %1.").arg(joined(accounts));
    QStringList seeded;
    for (const QString &path : stringsOf(answerField(answer, {"var_seeded", "seeded"})))
        seeded << rooted(path);
    if (!seeded.isEmpty())
        lines << QObject::tr("State folders made: %1.").arg(joined(seeded));
    if (answer["effects"]["status"].toString() == QLatin1String("done"))
        lines << QObject::tr("The system's caches (menus, icons and fonts) were rebuilt.");
    return lines;
}

QString cachesFailedWords()
{
    return QObject::tr("The system's caches (menus, icons and fonts) could not be rebuilt, so the software may not show in menus or "
                       "have its icons yet. The next system change tries again.");
}

} // namespace

// ---------------------------------------------------------------- Browse

BrowsePage::BrowsePage(QWidget *parent)
    : Page(parent)
{
    top_ = new QWidget;
    auto *column = new QVBoxLayout(top_);
    column->setContentsMargins(28, 20, 28, 0);
    column->setSpacing(14);
    auto *words = new QVBoxLayout;
    words->setSpacing(1);
    title_ = heading(QString());
    subtitle_ = dim(text(QString()));
    words->addWidget(title_);
    words->addWidget(subtitle_);
    column->addLayout(words);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search by name or by what it does"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Search software"));
    row->addWidget(search_, 1);
    // Which source's recipes are shown: all of them, or one.
    from_ = new QComboBox;
    from_->setAccessibleName(tr("Recipes from"));
    row->addWidget(from_);
    category_ = new QComboBox;
    category_->setAccessibleName(tr("Kind of software"));
    row->addWidget(category_);
    column->addLayout(row);
    // Above the list, which `fresh` puts last; the header must not grow with the window.
    top_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, top_);
}

void BrowsePage::showLoading(const QString &title, const QString &words)
{
    top_->hide();
    listed_.clear();
    Page::showLoading(title, words);
}

void BrowsePage::show(const Catalog &catalog, const QString &category, const QString &search, const QString &origin)
{
    QVBoxLayout *column = fresh();
    // The header above has the page's top margin already: only the usual gap is left.
    column->setContentsMargins(28, 14, 28, 16);
    top_->show();
    listed_.clear();
    title_->setText(!search.isEmpty() ? tr("Search: %1").arg(search)
                    : category.isEmpty() ? tr("All software") : categoryTitle(category));
    const Origin *from = origin.isEmpty() ? nullptr : catalog.origin(origin);
    QString subtitle = tr("Each one is installed from a recipe you can read.");
    if (!search.isEmpty() && !category.isEmpty())
        subtitle = tr("Only in %1. %2").arg(categoryTitle(category), subtitle);
    if (from)
        subtitle = tr("Only what %1 has. %2").arg(from->shown(), subtitle);
    subtitle_->setText(subtitle);
    // More than one source: say on every line which one it is from.
    int sourcesWithRecipes = 0;
    for (const Origin &o : catalog.origins)
        sourcesWithRecipes += catalog.countFrom(o.name) > 0;

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::StyledPanel);
    auto *inner = new QWidget;
    inner->setBackgroundRole(QPalette::Base);
    inner->setAutoFillBackground(true);
    auto *grid = new QGridLayout(inner);
    grid->setContentsMargins(14, 2, 14, 2);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(0);
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(2, 150);
    grid->setColumnMinimumWidth(3, 70);
    grid->setColumnMinimumWidth(4, 100);

    int row = 0;
    for (const Recipe &recipe : catalog.recipes) {
        if (!recipe.listed() || (!category.isEmpty() && recipe.category != category))
            continue;
        if (!origin.isEmpty() && recipe.origin != origin)
            continue;
        if (!search.isEmpty() && !recipe.name.contains(search, Qt::CaseInsensitive)
            && !recipe.title.contains(search, Qt::CaseInsensitive)
            && !recipe.description.contains(search, Qt::CaseInsensitive))
            continue;
        listed_ << recipe.name;
        if (row)
            grid->addWidget(rule(), row++, 0, 1, 5);
        grid->setRowMinimumHeight(row, 58);
        const QString name = recipe.name;

        grid->addWidget(new Tile(recipe.shown(), QString(), 36), row, 0);
        auto *words = new QVBoxLayout;
        words->setSpacing(0);
        auto *link = new ClickLabel(recipe.shown());
        QFont bold = link->font();
        bold.setBold(true);
        link->setFont(bold);
        connect(link, &ClickLabel::clicked, this, [this, name] { emit openRecipe(name); });
        words->addStretch();
        words->addWidget(link, 0, Qt::AlignLeft);
        words->addWidget(dim(text(recipe.description)));
        // What a person should know before the button: proprietary, or not runnable here.
        const QString unavailable = catalog.unavailable(recipe.name);
        if (recipe.proprietary())
            words->addWidget(tinted(text(tr("Proprietary software")), Mood::Warn));
        if (!unavailable.isEmpty())
            words->addWidget(dim(text(catalog.unavailableLine(recipe.name)))); // the C library or ABOUT.systems: one line either way
        words->addStretch();
        grid->addLayout(words, row, 1);
        const Origin *its = catalog.origin(recipe.origin);
        grid->addWidget(dim(text(sourcesWithRecipes > 1 && its
                                     ? tr("%1\nfrom %2").arg(categoryTitle(recipe.category), its->shown())
                                     : categoryTitle(recipe.category))),
                        row, 2);
        grid->addWidget(dim(text(recipe.version)), row, 3);

        // Software can be in several places; the button is for what is next to do.
        const QList<const InstalledItem *> held = catalog.installedEverywhere(name);
        const UpdateItem *newer = catalog.update(name);
        bool onlyOther = !held.isEmpty();
        for (const InstalledItem *item : held)
            onlyOther = onlyOther && item->place == Place::Other;
        if (newer) {
            const Place where = newer->place;
            auto *button = new QPushButton(tr("Update"));
            connect(button, &QPushButton::clicked, this, [this, name, where] { emit update(name, where); });
            grid->addWidget(button, row, 4, Qt::AlignRight);
        } else if (held.isEmpty() && !catalog.inOtherRoot(name) && !unavailable.isEmpty()) {
            grid->addWidget(dim(text(tr("Not available"))), row, 4, Qt::AlignRight);
        } else if (held.isEmpty() && !catalog.inOtherRoot(name)) {
            auto *button = new QPushButton(tr("Install"));
            connect(button, &QPushButton::clicked, this, [this, name] { emit install(name); });
            grid->addWidget(button, row, 4, Qt::AlignRight);
        } else {
            grid->addWidget(dim(text(onlyOther || held.isEmpty()
                                         ? (catalog.otherRoot.isEmpty() ? tr("In the other root") : tr("In root %1").arg(catalog.otherRoot))
                                         : tr("Installed"))),
                            row, 4, Qt::AlignRight);
        }
        ++row;
    }
    if (!row)
        grid->addWidget(dim(text(search.isEmpty() ? tr("Nothing here.") : tr("No recipe matches “%1”.").arg(search))),
                        row++, 0, 1, 5, Qt::AlignCenter);
    grid->setRowStretch(row, 1);
    scroll->setWidget(inner);
    column->addWidget(scroll, 1);
}

void BrowsePage::showProblem(const QString &problem)
{
    QVBoxLayout *column = fresh();
    top_->hide();
    listed_.clear();
    column->addWidget(heading(tr("The recipes could not be read")));
    column->addWidget(text(problem, true));
    auto *again = new QPushButton(tr("Try again"));
    connect(again, &QPushButton::clicked, this, &BrowsePage::retry);
    column->addWidget(again, 0, Qt::AlignLeft);
    column->addStretch();
}

// ---------------------------------------------------------------- A recipe

namespace {

// A titled panel of rows: a name with its words under it, and a button or two
// on the right.
struct Rows {
    Panel *panel = new Panel;
    QGridLayout *grid = new QGridLayout;
    int row = 0;
    Rows()
    {
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(0, 1);
        panel->column()->addLayout(grid);
    }
    void add(const QString &title, const QString &words, QList<QPushButton *> buttons = {})
    {
        if (row)
            grid->addWidget(rule(), row++, 0, 1, 2);
        auto *column = new QVBoxLayout;
        column->setSpacing(1);
        column->addWidget(strong(title));
        if (!words.isEmpty())
            column->addWidget(dim(text(words, true)));
        grid->addLayout(column, row, 0);
        auto *right = new QHBoxLayout;
        right->setSpacing(6);
        for (QPushButton *button : buttons)
            right->addWidget(button);
        grid->addLayout(right, row, 1, Qt::AlignRight | Qt::AlignVCenter);
        ++row;
    }
};

} // namespace

void RecipePage::show(const Catalog &catalog, const QString &name)
{
    QVBoxLayout *column = fresh();
    const Recipe *found = catalog.recipe(name);
    if (!found) {
        // Installed software outlives its recipe: what its receipt says is shown, and what still works.
        if (!catalog.installedEverywhere(name).isEmpty()) {
            showReceipt(catalog, name);
            return;
        }
        auto *backLink = new ClickLabel(tr("‹ All software"));
        dim(backLink);
        connect(backLink, &ClickLabel::clicked, this, &RecipePage::back);
        column->addWidget(backLink, 0, Qt::AlignLeft);
        column->addWidget(heading(tr("No recipe named %1").arg(name)));
        column->addWidget(dim(text(tr("No source offers a recipe by that name, and nothing installed carries it. It may have "
                                      "been removed from its source, or the source may have gone."),
                                   true)));
        column->addStretch();
        return;
    }
    const Recipe recipe = *found;
    const QList<const InstalledItem *> held = catalog.installedEverywhere(name);
    const QList<Place> free = catalog.installPlaces(name);
    const bool several = held.size() > 1;
    // "for me", "for everyone": which place a button or a line is about.
    auto where = [&catalog](Place place) {
        if (!catalog.twoPlaces)
            return QString();
        return place == Place::Me ? tr("for me") : place == Place::Computer ? tr("for everyone") : tr("in the other root");
    };
    const QString unavailable = catalog.unavailable(name);

    auto *backLink = new ClickLabel(tr("‹ All software"));
    dim(backLink);
    connect(backLink, &ClickLabel::clicked, this, &RecipePage::back);
    column->addWidget(backLink, 0, Qt::AlignLeft);

    auto *inner = new QWidget;
    auto *body = new QVBoxLayout(inner);
    body->setContentsMargins(0, 0, 2, 0);
    body->setSpacing(14);

    // The top: what it is, and what can be done with it as a whole.
    const Origin *its = catalog.origin(recipe.origin);
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    header->addWidget(new Tile(recipe.shown(), QString(), 52), 0, Qt::AlignTop);
    auto *words = new QVBoxLayout;
    words->setSpacing(1);
    words->addWidget(heading(recipe.shown()));
    words->addWidget(text(recipe.description, true));
    QString line = tr("Version %1 in the recipe").arg(recipe.version);
    if (!recipe.category.isEmpty())
        line += tr(" · %1").arg(categoryTitle(recipe.category));
    if (its)
        line += tr(" · from %1").arg(its->shown());
    words->addWidget(dim(text(line, true)));
    // Said before anything can be installed: proprietary software is chosen knowingly, and a
    // recipe this system cannot run is not offered.
    if (recipe.proprietary())
        words->addWidget(tinted(text(proprietaryWords(recipe), true), Mood::Warn));
    if (!unavailable.isEmpty() && free.size() && held.isEmpty())
        words->addWidget(text(tr("Not available on this system. %1").arg(catalog.unavailableLine(name)), true));
    header->addLayout(words, 1);

    auto *actions = new QVBoxLayout;
    actions->setSpacing(6);
    auto *buttons = new QHBoxLayout;
    // What it does for each place that holds it; with two, the button says which.
    auto named = [&](const QString &text, Place place) {
        return several || (catalog.twoPlaces && place == Place::Other) ? tr("%1 %2").arg(text, where(place)) : text;
    };
    bool openOffered = false;
    for (const InstalledItem *item : held) {
        const Place place = item->place;
        // Software only the other root holds, or the booted root's list on an A/B system, has nothing
        // to open: the sentences under "Where it is" say why.
        if (!openOffered && item->inStore && place != Place::Other && !item->menu.isEmpty()) {
            openOffered = true;
            // The update is the page's main button when there is one to offer.
            bool offered = false;
            for (const InstalledItem *other : held) {
                const UpdateItem *update = catalog.update(name, other->place);
                offered = offered || (update && other->place != Place::Other);
            }
            auto *open = offered ? new QPushButton(tr("Open")) : primary(tr("Open"));
            connect(open, &QPushButton::clicked, this, [this, name, place] { emit this->open(name, place); });
            buttons->addWidget(open);
        }
    }
    bool anyUpdate = false;
    for (const InstalledItem *item : held) {
        const UpdateItem *update = catalog.update(name, item->place);
        if (!update || item->place == Place::Other)
            continue;
        anyUpdate = true;
        const Place place = item->place;
        auto *go = primary(named(tr("Update to %1").arg(update->to), place));
        connect(go, &QPushButton::clicked, this, [this, name, place] { emit this->update(name, place); });
        buttons->addWidget(go);
    }
    if (!free.isEmpty() && unavailable.isEmpty()) {
        // Installed in one place and free in the other: the page offers that other place.
        auto *go = held.isEmpty() ? primary(tr("Install")) : new QPushButton(tr("Install %1").arg(where(free.first())));
        connect(go, &QPushButton::clicked, this, [this, name] { emit install(name); });
        buttons->addWidget(go);
    }
    QString note = tr("You see what will happen before anything starts");
    if (!free.isEmpty() && !unavailable.isEmpty() && held.isEmpty())
        note = tr("Not available on this system");
    else if (!held.isEmpty() && !anyUpdate)
        note = tr("Installed");
    // Several buttons, each saying where, do not fit beside the name: they take a row of their own.
    if (buttons->count() > 2 || held.size() > 1) {
        body->addLayout(header);
        auto *row = new QHBoxLayout;
        row->addWidget(dim(text(note, true)), 1);
        row->addLayout(buttons);
        body->addLayout(row);
    } else {
        actions->addLayout(buttons);
        actions->addWidget(dim(text(note)), 0, Qt::AlignRight);
        header->addLayout(actions);
        body->addLayout(header);
    }
    if (!recipe.details.isEmpty())
        body->addWidget(text(recipe.details, true));

    whereItIs(body, catalog, name);

    actionRows(body, catalog, name, held);

    // What the recipe may use, and what comes with it.
    auto *pair = new QHBoxLayout;
    pair->setSpacing(14);

    auto *uses = new Panel;
    auto *usesTitle = new QVBoxLayout;
    usesTitle->setSpacing(0);
    usesTitle->addWidget(strong(tr("What it may use")));
    usesTitle->addWidget(dim(text(tr("What the recipe allows while it runs. The sandbox refuses anything else."))));
    uses->column()->addLayout(usesTitle);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(7);
    auto fact = [](QFormLayout *into, const QString &label, const QString &value) {
        into->addRow(dim(text(label)), text(value, true));
    };
    fact(form, tr("Network"), recipe.needs.network.isEmpty() ? tr("none")
                                                              : tr("%1. Nothing else.").arg(endpointsText(recipe.needs.network)));
    if (recipe.needs.networkMaxBytes)
        fact(form, tr("Download"), tr("up to %1").arg(bytesText(recipe.needs.networkMaxBytes)));
    if (recipe.needs.memoryMib)
        fact(form, tr("Memory"), tr("up to %1").arg(bytesText(recipe.needs.memoryMib * 1048576)));
    fact(form, tr("Disk"), recipe.needs.storageMaxBytes
                               ? tr("up to %1 while it builds").arg(bytesText(recipe.needs.storageMaxBytes))
                               : tr("the standard limit"));
    if (recipe.needs.timeoutSecs)
        fact(form, tr("Time"), counted(recipe.needs.timeoutSecs / 60, tr("up to %1 minute"), tr("up to %1 minutes")));
    fact(form, tr("Your files"), tr("none while it is built"));
    uses->column()->addLayout(form);
    uses->column()->addStretch();
    pair->addWidget(uses, 1);

    auto *comes = new Panel;
    comes->column()->addWidget(strong(tr("What comes with it")));
    auto *with = new QFormLayout;
    with->setHorizontalSpacing(16);
    with->setVerticalSpacing(7);
    const QString adds = addsWords(recipe);
    fact(with, tr("It adds"), adds.isEmpty() ? tr("its files only") : adds);
    if (recipe.afterInstall == QLatin1String("restart"))
        fact(with, tr("Afterwards"), tr("The computer must be restarted."));
    else if (recipe.afterInstall == QLatin1String("log-in-again"))
        fact(with, tr("Afterwards"), tr("You must log in again."));
    auto shownNames = [&catalog](const QStringList &names) {
        QStringList shown;
        for (const QString &each : names)
            shown << (catalog.recipe(each) ? catalog.recipe(each)->shown() : each);
        return shown.join(QObject::tr(", "));
    };
    if (!recipe.deps.isEmpty())
        fact(with, tr("Needs"), tr("%1, installed with it").arg(shownNames(recipe.deps)));
    QStringList buildOnly;
    for (const QString &each : recipe.buildDeps)
        if (!recipe.deps.contains(each))
            buildOnly << each;
    if (!buildOnly.isEmpty())
        fact(with, tr("To build it"), shownNames(buildOnly));
    if (!recipe.keeps.isEmpty())
        fact(with, tr("It keeps"), tr("its settings in %1. They stay when it is removed.").arg(recipe.keeps.join(tr(", "))));
    if (!recipe.license.isEmpty())
        fact(with, tr("Licence"), recipe.proprietary() ? tr("%1 (proprietary, not free software)").arg(recipe.license) : recipe.license);
    if (!recipe.homepage.isEmpty()) {
        auto *home = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                                    .arg(recipe.homepage.toHtmlEscaped(), QUrl(recipe.homepage).host().toHtmlEscaped()));
        home->setOpenExternalLinks(true);
        with->addRow(dim(text(tr("Home page"))), home);
    }
    if (recipe.details.isEmpty() && recipe.homepage.isEmpty() && recipe.license.isEmpty())
        with->addRow(dim(text(tr("About"))), dim(text(tr("This recipe says nothing about itself yet."), true)));
    comes->column()->addLayout(with);
    comes->column()->addStretch();
    pair->addWidget(comes, 1);
    body->addLayout(pair);

    // The recipe: where it is from, and what each step does.
    auto *recipePanel = new Panel;
    auto *recipeHead = new QHBoxLayout;
    recipeHead->setSpacing(12);
    recipeHead->addWidget(strong(tr("The recipe")));
    if (!recipe.path.isEmpty()) {
        auto *file = new QLabel(QFileInfo(recipe.path).fileName());
        file->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        recipeHead->addWidget(file);
    }
    if (its)
        recipeHead->addWidget(dim(text(tr("from %1").arg(its->shown()))));
    recipeHead->addStretch();
    // A recipe this system cannot run can still be read.
    if (!recipe.path.isEmpty() || !recipe.runsHere) {
        auto *read = new QPushButton(tr("Read it"));
        connect(read, &QPushButton::clicked, this, [this, name] { emit readRecipe(name); });
        recipeHead->addWidget(read);
    }
    recipePanel->column()->addLayout(recipeHead);
    // F2: the page shows state and two buttons; the steps (download, build, install, cleanup) with their disk use go behind "One step at a time".
    recipePanel->column()->addLayout(stepRow(1, tr("Fetch"), fetchSentence(recipe), recipe.notes.value("download")));
    recipePanel->column()->addLayout(stepRow(2, tr("Build"), buildSentence(recipe), recipe.notes.value("build")));
    recipePanel->column()->addLayout(stepRow(3, tr("Install"), installSentence(recipe), recipe.notes.value("install")));
    body->addWidget(recipePanel);

    body->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    column->addWidget(scroll, 1);
}

void RecipePage::whereItIs(QVBoxLayout *body, const Catalog &catalog, const QString &name)
{
    auto *rooms = new Panel;
    rooms->column()->addWidget(strong(tr("Where it is")));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int row = 0;
    for (Place place : {Place::Me, Place::Computer, Place::Other}) {
        const InstalledItem *item = catalog.installedItem(name, place);
        if (place == Place::Other && !item)
            continue;
        if (!catalog.twoPlaces && place != Place::Computer)
            continue;
        // Software of the system itself is never for one person: no "for me" row to say it is not there.
        if (place == Place::Me && !item && catalog.intoSystemOnly(name))
            continue;
        const QString label = catalog.ab && place == Place::Computer ? tr("For everyone, in %1").arg(catalog.bootedWords())
                              : catalog.twoPlaces ? placeTitle(place, catalog.otherRoot)
                                                  : tr("In this store");
        QString state;
        if (!item) {
            state = tr("Not installed");
        } else {
            QStringList parts{tr("Version %1").arg(item->version)};
            if (item->installedOn.isValid())
                parts << tr("installed on %1").arg(QLocale().toString(item->installedOn, "d MMMM yyyy"));
            if (item->sizeBytes)
                parts << bytesText(item->sizeBytes);
            if (place == Place::Other)
                parts << tr("active when %1 is started").arg(catalog.otherRoot.isEmpty() ? tr("that root") : tr("root %1").arg(catalog.otherRoot));
            state = parts.join(tr(" · "));
            QStringList sentences;
            if (item->leavesOther)
                sentences << tr("%1 no longer holds it.").arg(catalog.otherWords().left(1).toUpper() + catalog.otherWords().mid(1));
            if (place == Place::Other)
                sentences << tr("It cannot be opened until then.");
            else if (!item->inStore)
                sentences << catalog.cannotOpenWords();
            // What else installed in that root needs it.
            if (!item->inStore && !item->base) {
                const QStringList needed = catalog.neededBy(name, place == Place::Other ? Place::Other : Place::Computer);
                if (!needed.isEmpty())
                    sentences << tr("Needed by: %1.").arg(catalog.shownNames(needed).join(tr(", ")));
            }
            const UpdateItem *update = catalog.update(name, place);
            if (update && place != Place::Other)
                sentences << tr("%1 is ready.").arg(update->to);
            if (!sentences.isEmpty())
                state += tr(". %1").arg(sentences.join(QLatin1Char(' ')));
        }
        grid->addWidget(dim(text(label)), row, 0, Qt::AlignTop);
        QLabel *value = text(state, true);
        if (!item)
            dim(value);
        grid->addWidget(value, row, 1);
        ++row;
    }
    rooms->column()->addLayout(grid);
    body->addWidget(rooms);
}

void RecipePage::actionRows(QVBoxLayout *body, const Catalog &catalog, const QString &name,
                            const QList<const InstalledItem *> &held)
{
    const bool several = held.size() > 1;
    auto where = [&catalog](Place place) {
        if (!catalog.twoPlaces)
            return QString();
        return place == Place::Me ? tr("for me") : place == Place::Computer ? tr("for everyone") : tr("in the other root");
    };
    if (!held.isEmpty()) {
        Rows rows;
        for (const InstalledItem *item : held) {
            const Place place = item->place;
            const QString at = several ? tr(" (%1)").arg(where(place)) : QString();
            // The base is the system itself: it is replaced by a newer release, never removed.
            if (item->base)
                continue;
            // What else in the root the change goes into needs it: it stays while they are installed.
            const QStringList needed = item->inStore ? QStringList() : catalog.neededBy(name, catalog.systemTarget());
            if (!needed.isEmpty()) {
                rows.add(tr("Remove%1").arg(at),
                         tr("Needed by %1, so it cannot be removed while they are installed. Remove those first.")
                             .arg(catalog.shownNames(needed).join(tr(", "))));
                continue;
            }
            auto *remove = new QPushButton(tr("Remove…"));
            connect(remove, &QPushButton::clicked, this, [this, name, place] { emit this->remove(name, place); });
            const QString removed = !item->inStore ? catalog.takenOutWords(*item) : tr("No longer installed.");
            rows.add(tr("Remove%1").arg(at), removed, {remove});
        }
        body->addWidget(rows.panel);
    }
}

void RecipePage::showReceipt(const Catalog &catalog, const QString &name)
{
    QVBoxLayout *column = fresh();
    const QList<const InstalledItem *> held = catalog.installedEverywhere(name);
    const bool several = held.size() > 1;

    auto *backLink = new ClickLabel(tr("‹ Installed"));
    dim(backLink);
    connect(backLink, &ClickLabel::clicked, this, &RecipePage::backToInstalled);
    column->addWidget(backLink, 0, Qt::AlignLeft);

    auto *inner = new QWidget;
    auto *body = new QVBoxLayout(inner);
    body->setContentsMargins(0, 0, 2, 0);
    body->setSpacing(14);

    // The top: what the receipt calls it. There is no recipe to say more.
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    header->addWidget(new Tile(name, QString(), 52), 0, Qt::AlignTop);
    auto *words = new QVBoxLayout;
    words->setSpacing(1);
    words->addWidget(heading(name));
    words->addWidget(text(tr("Its recipe is gone: no recipe for it is in the collection or in the sources any more. "
                             "This page says what its receipt says."),
                          true));
    words->addWidget(dim(text(tr("Version %1").arg(held.first()->version))));
    header->addLayout(words, 1);
    auto *buttons = new QHBoxLayout;
    for (const InstalledItem *item : held) {
        if (item->inStore && item->place != Place::Other && !item->menu.isEmpty()) {
            const Place place = item->place;
            auto *open = primary(tr("Open"));
            connect(open, &QPushButton::clicked, this, [this, name, place] { emit this->open(name, place); });
            buttons->addWidget(open);
            break;
        }
    }
    header->addLayout(buttons);
    body->addLayout(header);

    whereItIs(body, catalog, name);

    // What the receipt holds, for each place that has one.
    for (const InstalledItem *item : held) {
        auto *panel = new Panel;
        QString title = tr("What its receipt says");
        if (several)
            title = tr("What its receipt says (%1)").arg(placeTitle(item->place, catalog.otherRoot).toLower());
        panel->column()->addWidget(strong(title));
        auto *form = new QFormLayout;
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(7);
        auto fact = [form](const QString &label, QLabel *value) { form->addRow(dim(text(label)), value); };
        fact(tr("Name"), text(item->name, true));
        fact(tr("Version"), text(item->version, true));
        if (item->installedOn.isValid())
            fact(tr("Installed on"), text(QLocale().toString(item->installedOn, "d MMMM yyyy"), true));
        if (item->sizeBytes)
            fact(tr("Size"), text(bytesText(item->sizeBytes), true));
        QString from;
        if (item->source.isEmpty()) {
            from = tr("The receipt names no source: Recipe records one only for a recipe fetched from a remote source.");
        } else {
            const Origin *origin = catalog.origin(item->source);
            from = origin ? tr("The source %1").arg(origin->shown()) : tr("The source %1, which is not among the sources any more").arg(item->source);
            if (!item->commit.isEmpty())
                from += tr(", at commit %1").arg(item->commit);
        }
        fact(tr("It came from"), text(from, true));
        if (!item->receiptRecipe.isEmpty()) {
            QLabel *file = text(item->receiptRecipe, true);
            file->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            form->addRow(dim(text(tr("Its recipe file"))), file);
            form->addRow(QString(), dim(text(QFileInfo::exists(item->receiptRecipe)
                                                 ? tr("The file is still there, but no source or recipe folder lists it.")
                                                 : tr("The file is not there any more."),
                                             true)));
        }
        // F2: `item->files` is always empty now; drop the receipt's file list from this page.
        QStringList shown = item->files.mid(0, 14);
        QString fileWords = shown.join(QLatin1Char('\n'));
        if (item->files.size() > shown.size())
            fileWords += tr("\n… and %1 more").arg(item->files.size() - shown.size());
        QLabel *files = text(item->files.isEmpty() ? tr("The receipt lists none.") : fileWords, true);
        if (!item->files.isEmpty())
            files->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        form->addRow(dim(text(counted(item->files.size(), tr("%1 file"), tr("%1 files")))), files);
        panel->column()->addLayout(form);
        body->addWidget(panel);
    }

    // Remove works from the receipt.
    actionRows(body, catalog, name, held);
    body->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    column->addWidget(scroll, 1);
}

// ---------------------------------------------------------------- A run

void RunPage::begin(const Catalog &catalog, const QString &name, Run *run, Place place, bool system)
{
    catalog_ = catalog;
    name_ = name;
    run_ = run;
    concluded_ = false;
    system_ = system;
    answer_ = QJsonObject();
    marks_.clear();
    lines_.clear();
    log_.clear();
    stepsTotal_ = stepsStarted_ = 0;
    const Recipe *found = catalog_.recipe(name);
    const Recipe recipe = found ? *found : Recipe{};
    const InstalledItem *installed = catalog_.installedItem(name, place);
    updatingFrom_ = installed ? installed->version : QString();
    place_ = place;

    column_ = fresh();
    QHBoxLayout *header = recipeHeader(
        recipe, updatingFrom_.isEmpty() ? tr("Installing %1").arg(recipe.shown()) : tr("Updating %1").arg(recipe.shown()),
        &title_);
    auto *actions = new QVBoxLayout;
    actions->setSpacing(6);
    stop_ = new QPushButton(tr("Stop"));
    connect(stop_, &QPushButton::clicked, this, [this, run] {
        stop_->setEnabled(false);
        run->stop();
    });
    actions->addWidget(stop_, 0, Qt::AlignRight);
    stopNote_ = dim(text(tr("Stopping keeps what is already fetched and built")));
    actions->addWidget(stopNote_);
    header->addLayout(actions);
    column_->addLayout(header);

    auto *planPanel = new Panel;
    planBox_ = planPanel;
    plan_ = new QGridLayout;
    plan_->setHorizontalSpacing(10);
    plan_->setVerticalSpacing(10);
    plan_->setColumnStretch(7, 1);
    planPanel->column()->addLayout(plan_);
    bar_ = new QProgressBar;
    bar_->setRange(0, 0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    planPanel->column()->addWidget(bar_);
    state_ = dim(text(tr("Starting")));
    planPanel->column()->addWidget(state_);
    column_->addWidget(planPanel);

    outputTitle_ = strong(tr("Output"));
    column_->addWidget(outputTitle_);
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    column_->addWidget(output_, 1);

    connect(run, &Run::event, this, [this, run](const QJsonObject &event) {
        if (run == run_)
            onEvent(event);
    });
    connect(run, &Run::output, this, [this, run](const QString &line) {
        if (run != run_)
            return;
        output_->appendPlainText(line);
        lines_ << line;
        if (lines_.size() > 400)
            lines_.removeFirst();
        // `recipe` says where it keeps the log and the retained record of a run, in a plain line:
        // "recipe: diagnostics: DIRECTORY" (tools/recipe, invocation_log.rs).
        static const QRegularExpression where(QStringLiteral(R"(diagnostics:\s+(/\S.*?)\s*$)"));
        const QRegularExpressionMatch found = where.match(line);
        if (found.hasMatch())
            log_ = found.captured(1);
    });
    // The command's exit ends the run when no last event did (or the run is written into the
    // other root afterwards). A command that lingers after its last event changes nothing.
    connect(run, &Run::finished, this, [this, run](bool ok) {
        if (run == run_)
            conclude(ok);
        run->deleteLater();
    });
    emit status(tr("Starting %1").arg(recipe.shown()));
}

void RunPage::addPlanRow(const QString &recipe, const QStringList &steps)
{
    const int row = plan_->rowCount();
    const Recipe *found = catalog_.recipe(recipe);
    // The effects recipe is the system's own: its steps rebuild the caches after the change.
    const QString effects = recipe == QLatin1String("effects") && !found ? tr("System caches") : recipe;
    QLabel *name = recipe == name_ ? strong(found ? found->shown() : recipe) : dim(text(effects));
    name->setMinimumWidth(110);
    plan_->addWidget(name, row, 0);
    int column = 1;
    for (int index = 0; index < steps.size(); ++index) {
        auto *stepMark = new StepMark(index + 1);
        marks_.insert(recipe + ':' + steps[index], stepMark);
        plan_->addWidget(stepMark, row, column++);
        QLabel *label = text(stepTitle(steps[index]));
        label->setMinimumWidth(70);
        plan_->addWidget(label, row, column++);
    }
    stepsTotal_ += steps.size();
}

StepMark *RunPage::mark(const QString &recipe, const QString &step)
{
    // A `recipe` that sends no plan still gets its rows, as its steps appear.
    if (!marks_.contains(recipe + ':' + step) && !marks_.contains(recipe + QStringLiteral(":download"))
        && !marks_.contains(recipe + QStringLiteral(":build")) && !marks_.contains(recipe + QStringLiteral(":install")))
        addPlanRow(recipe, {"download", "build", "install", "cleanup"});
    return marks_.value(recipe + ':' + step);
}

void RunPage::onEvent(const QJsonObject &event)
{
    const QString kind = event["event"].toString();
    const QString recipe = event["recipe"].toString();
    const Recipe *found = catalog_.recipe(recipe);
    const bool effects = !found && recipe == QLatin1String("effects");
    const QString shown = found ? found->shown() : effects ? tr("the system's caches") : recipe;

    if (kind == QLatin1String("plan")) {
        for (const QJsonValue &value : event["recipes"].toArray()) {
            QStringList steps;
            for (const QJsonValue &step : value.toObject()["steps"].toArray())
                steps << step.toString();
            addPlanRow(value.toObject()["name"].toString(), steps);
        }
        // One recipe's own plan, once its checks have run: the steps it does
        // not name are not needed this time.
        if (!recipe.isEmpty() && event["steps"].isArray()) {
            QStringList needed;
            for (const QJsonValue &step : event["steps"].toArray())
                needed << step.toString();
            for (const QString &step : {QStringLiteral("download"), QStringLiteral("build"), QStringLiteral("install"), QStringLiteral("cleanup")}) {
                StepMark *stepMark = needed.contains(step) ? nullptr : mark(recipe, step);
                if (stepMark)
                    stepMark->setState(StepMark::Skipped);
            }
        }
    } else if (kind == QLatin1String("step")) {
        const QString step = event["step"].toString();
        const QString outcome = event["status"].toString();
        if (step != QLatin1String("download") && step != QLatin1String("build") && step != QLatin1String("install")
            && step != QLatin1String("cleanup"))
            return;
        StepMark *stepMark = mark(recipe, step);
        if (!stepMark)
            return;
        if (outcome.contains("run")) {
            stepMark->setState(StepMark::Running);
            ++stepsStarted_;
            bar_->setRange(0, 0);
            const QString doing = step == QLatin1String("download") ? tr("Downloading %1").arg(shown)
                                  : step == QLatin1String("build") ? tr("Building %1").arg(shown)
                                  : step == QLatin1String("cleanup") ? tr("Cleaning up after %1").arg(shown)
                                                                      : tr("Installing %1").arg(shown);
            state_->setText(doing);
            emit status(tr("%1, step %2 of %3").arg(doing).arg(stepsStarted_).arg(qMax(stepsTotal_, stepsStarted_)));
        } else if (outcome.contains("skip") || outcome == QLatin1String("already") || outcome == QLatin1String("none")) {
            stepMark->setState(StepMark::Skipped);
        } else if (outcome.contains("fail")) {
            stepMark->setState(StepMark::Failed);
        } else if (outcome == QLatin1String("done")) {
            stepMark->setState(StepMark::Done);
        }
    } else if (kind == QLatin1String("fetch")) {
        const qint64 bytes = event["bytes"].toInteger(), total = event["total"].toInteger();
        if (total > 0) {
            bar_->setRange(0, int(total / 1024));
            bar_->setValue(int(bytes / 1024));
            state_->setText(tr("Fetching %1: %2 of %3").arg(shown, bytesText(bytes), bytesText(total)));
        } else {
            state_->setText(tr("Fetching %1: %2 so far").arg(shown, bytesText(bytes)));
        }
    } else if (kind == QLatin1String("progress")) {
        bar_->setRange(0, event["total"].toInt());
        bar_->setValue(event["done"].toInt());
        state_->setText(tr("Building %1: %2 of %3").arg(shown).arg(event["done"].toInt()).arg(event["total"].toInt()));
    } else if (kind == QLatin1String("say")) {
        state_->setText(event["text"].toString());
    } else if (kind == QLatin1String("done")) {
        // The recipe's last event. For a system change the writing into the root (and then the
        // rebuilding of its caches) is still to come: say so, and wait for the command to exit.
        // Otherwise this is the end, whenever the command exits.
        for (StepMark *stepMark : marks_)
            if (stepMark->state() == StepMark::Running)
                stepMark->setState(StepMark::Done);
        if (system_) {
            bar_->setRange(0, 0);
            if (effects) {
                state_->setText(tr("Rebuilding the system's caches…"));
                emit status(tr("Rebuilding the system's caches"));
            } else {
                const QString into = catalog_.ab ? catalog_.otherWords() : tr("the system");
                state_->setText(tr("%1 is ready. Writing it into %2…").arg(shown, into));
                emit status(tr("Writing %1 into %2").arg(shown, into));
            }
        } else {
            conclude(true);
        }
    } else if (kind == QLatin1String("failed")) {
        // `Run` has the reason and the step by now. A system change still says more (nothing was
        // written, or only the caches failed, which the effects recipe reports as its own `failed`),
        // so its exit is waited for.
        if (!system_)
            conclude(false);
    }
}

void RunPage::conclude(bool ok)
{
    if (concluded_)
        return;
    concluded_ = true;
    onFinished(ok);
}

bool RunPage::showsRunning() const
{
    return stop_ && stop_->isVisibleTo(this) && bar_ && bar_->isVisibleTo(this);
}

QString RunPage::stateWords() const
{
    return state_ ? state_->text() : QString();
}

QString RunPage::pageText() const
{
    QStringList words;
    for (const QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(this))
            words << label->text();
    for (const QAbstractButton *button : findChildren<QAbstractButton *>())
        if (button->isVisibleTo(this))
            words << QString(button->text()).remove(QLatin1Char('&'));
    return words.join(QLatin1Char('\n'));
}

void RunPage::onFinished(bool ok)
{
    emit status(QString());
    if (ok) {
        // Shown now, not when the lists have been read again: the run is over.
        const Recipe *found = catalog_.recipe(name_);
        const QString shown = found ? found->shown() : name_;
        answer_ = QJsonDocument::fromJson(run_->answer()).object();
        for (StepMark *stepMark : marks_)
            if (stepMark->state() == StepMark::Running)
                stepMark->setState(StepMark::Done);
        bar_->hide();
        stop_->hide();
        stopNote_->hide();
        title_->setText(updatingFrom_.isEmpty() ? tr("Installed %1").arg(shown) : tr("Updated %1").arg(shown));
        const bool nothingWritten = system_ && answer_["packages"].toArray().isEmpty();
        if (answer_.contains("other_root"))
            state_->setText(nothingWritten
                                ? tr("Nothing needed writing: %1 holds it already. Reading what is installed now…").arg(catalog_.otherWords())
                                : tr("Written into the other root (%1). It becomes active when root %1 is started. Reading what is installed now…")
                                      .arg(answer_["other_root"].toString()));
        else if (system_)
            state_->setText(nothingWritten
                                ? tr("Nothing needed writing: it was already in the system. Reading what is installed now…")
                                : tr("Written into the system. It is active at once; programs that are already running may need a "
                                     "restart. Reading what is installed now…"));
        else
            state_->setText(tr("Done. Reading what is installed now…"));
        // What the change did besides: only what is not empty.
        const QStringList lines = system_ ? systemResultLines(answer_, catalog_) : QStringList();
        if (!lines.isEmpty()) {
            auto *result = new Panel;
            result->column()->addWidget(strong(tr("What was done")));
            for (const QString &line : lines)
                result->column()->addWidget(text(line, true));
            column_->insertWidget(2, result);
        }
        emit succeeded(name_);
        return;
    }
    // Everything the command did is written and only the last step failed: the software is installed.
    if (system_) {
        const QJsonObject answer = QJsonDocument::fromJson(run_->answer()).object();
        if (answer["effects"]["status"].toString() == QLatin1String("failed")) {
            cachesFailed(answer);
            return;
        }
    }
    const QJsonObject failure = run_->failure();
    const bool stoppedByUser = !stop_->isEnabled() && failure.isEmpty();
    const Recipe *found = catalog_.recipe(name_);
    const QString shown = found ? found->shown() : name_;
    const QString version = found ? found->version : QString();
    const QString name = name_;
    bar_->hide();
    stop_->hide();
    stopNote_->hide();
    state_->setText(stoppedByUser ? tr("Stopped") : tr("Stopped at this step"));
    title_->setText(shown);

    const QString reason = failure["reason"].toString();

    // What happened, and what it left.
    QString what;
    if (stoppedByUser)
        what = tr("You stopped it");
    else if (updatingFrom_.isEmpty())
        what = tr("Installing %1 was stopped").arg(shown);
    else
        what = tr("The update to %1 was stopped").arg(version);
    const QString unchanged = updatingFrom_.isEmpty()
                                  ? tr("Nothing on this computer was changed.")
                                  : tr("%1 %2 is still installed and works as before.").arg(shown, updatingFrom_);
    auto *verdict = new Verdict;
    verdict->setFrameShape(QFrame::NoFrame);
    verdict->column()->addWidget(strong(what));
    verdict->column()->addWidget(
        text(stoppedByUser ? tr("What was already fetched and built is kept.") : unchanged, true));
    column_->insertWidget(1, verdict);

    auto *buttons = new QHBoxLayout;
    auto *again = new QPushButton(tr("Try again"));
    auto *leave = new QPushButton(tr("Back"));
    connect(again, &QPushButton::clicked, this, [this, name] { emit retry(name); });
    connect(leave, &QPushButton::clicked, this, &RunPage::back);
    if (stoppedByUser) {
        buttons->addWidget(again);
        buttons->addWidget(leave);
        buttons->addStretch();
        verdict->column()->addLayout(buttons);
        emit failed(name_);
        return;
    }

    // The page puts together the issue and the facts; "Copy for agent" puts them
    // on the clipboard with the whole output.
    const QJsonObject refused = failure["refused"].toObject();
    const QString step = failure["step"].toString();
    const QString doing = updatingFrom_.isEmpty() ? tr("Installing %1 %2").arg(shown, version)
                                                   : tr("Updating %1 from %2 to %3").arg(shown, updatingFrom_, version);
    QString issue;
    QStringList facts;
    if (!refused.isEmpty()) {
        const QString during = step == QLatin1String("download") ? tr("downloading")
                               : step == QLatin1String("build") ? tr("building") : tr("installing");
        QStringList declared;
        for (const QJsonValue &value : refused["declared"].toArray())
            declared << value.toString();
        issue = tr("%1 stopped while %2. The recipe tried to connect to %3, which is not on the recipe's list, so the "
                   "sandbox refused the connection.")
                    .arg(doing, during, refusedAddressWords(refused["endpoint"].toString()));
        facts << tr("The recipe's list allows: %1").arg(endpointsText(declared));
        // Recipe reports the address the sandbox saw, not the name it was reached by.
        facts << tr("Recipe reports the address the recipe tried to reach, not a host name.");
    } else {
        issue = step.isEmpty() ? tr("%1 stopped.").arg(doing)
                               : tr("%1 stopped in the %2 step.").arg(doing, stepTitle(step).toLower());
        if (!reason.isEmpty())
            facts << tr("Recipe's own message: %1").arg(reason);
    }
    if (found && !found->path.isEmpty())
        facts << tr("The recipe: %1").arg(found->path);
    if (!log_.isEmpty())
        facts << tr("Recipe keeps the log and the retained record of this run in %1").arg(log_);
    // The last lines of what the run printed: a bounded number, each cut short.
    // The whole output is in the log when `recipe` named one, and is copied in full.
    QString outputFact;
    if (!lines_.isEmpty()) {
        constexpr int most = 20, widest = 240;
        const QStringList last = lines_.mid(qMax<qsizetype>(0, lines_.size() - most));
        QStringList shownLines;
        for (const QString &each : last)
            shownLines << QStringLiteral("    ") + (each.size() > widest ? each.left(widest - 1) + QStringLiteral("…") : each);
        outputFact = tr("The last %1 of the run's output (%2 in all):\n%3")
                         .arg(counted(last.size(), tr("%1 line"), tr("%1 lines")))
                         .arg(lines_.size() >= 400 ? tr("more than 400 lines") : counted(lines_.size(), tr("%1 line"), tr("%1 lines")),
                              shownLines.join(QLatin1Char('\n')));
        facts << outputFact;
    }
    facts << unchanged;

    // The full text for "Copy for agent": the issue, the facts, then the whole output.
    QStringList copied = facts;
    copied.removeAll(outputFact);
    auto copyText = [this, issue, copied] {
        return tr("The issue:\n%1\n\nThe facts:\n- %2\n\nThe output:\n%3\n")
            .arg(issue, copied.join(QStringLiteral("\n- ")), output_->toPlainText());
    };

    auto *showOutput = new QPushButton(tr("Show the output"));
    showOutput->setCheckable(true);
    connect(showOutput, &QPushButton::toggled, this, [this](bool on) {
        outputTitle_->setVisible(on);
        output_->setVisible(on);
    });
    if (!updatingFrom_.isEmpty()) {
        // What stopped, and what Recipe recorded, are said.
        verdict->column()->addWidget(text(issue, true));
        for (const QString &fact : facts.mid(0, facts.size() - 1)) // the last is `unchanged`, said above
            if (fact != outputFact) // the output is one click away, under "Show the output"
                verdict->column()->addWidget(dim(text(fact, true)));
        buttons->addWidget(showOutput);
        buttons->addWidget(again);
        buttons->addWidget(leave);
        buttons->addStretch();
        buttons->addWidget(copyForAgentButton(copyText));
        verdict->column()->addLayout(buttons);
        outputTitle_->hide();
        output_->hide();
        column_->addStretch();
        emit failed(name_);
        return;
    }

    auto *account = new Panel;
    account->column()->addWidget(text(issue, true));
    for (const QString &fact : facts.mid(0, facts.size() - 1)) // the last is `unchanged`, said above
        if (fact != outputFact) // the output is one click away, under "Show the output"
            account->column()->addWidget(dim(text(fact, true)));

    buttons->addWidget(showOutput);
    buttons->addWidget(again);
    buttons->addWidget(leave);
    buttons->addStretch();
    buttons->addWidget(copyForAgentButton(copyText));
    // The facts can be long: they scroll, and the buttons stay in reach below them.
    auto *promptScroll = new QScrollArea;
    promptScroll->setWidgetResizable(true);
    promptScroll->setFrameShape(QFrame::NoFrame);
    promptScroll->setWidget(account);
    column_->insertWidget(2, promptScroll, 1);
    auto *buttonRow = new QWidget;
    buttonRow->setLayout(buttons);
    buttons->setContentsMargins(0, 0, 0, 0);
    column_->insertWidget(3, buttonRow);

    // The output stays one click away.
    outputTitle_->hide();
    output_->hide();
    column_->addStretch();
    emit failed(name_);
}

// The change is in the system; only rebuilding the caches (the effects recipe) failed. That is not a
// failed install: the page says so, gives Recipe's error and "Copy for agent", and the lists are read
// as after any install.
void RunPage::cachesFailed(const QJsonObject &answer)
{
    concluded_ = true;
    answer_ = answer;
    const Recipe *found = catalog_.recipe(name_);
    const QString shown = found ? found->shown() : name_;
    for (StepMark *stepMark : marks_)
        if (stepMark->state() == StepMark::Running)
            stepMark->setState(StepMark::Done);
    bar_->hide();
    stop_->hide();
    stopNote_->hide();
    title_->setText(updatingFrom_.isEmpty() ? tr("Installed %1").arg(shown) : tr("Updated %1").arg(shown));
    state_->setText(tr("%1 is in the system. Rebuilding the system's caches did not finish.").arg(shown));

    const QString error = answer["effects"]["error"].toString();
    const QStringList lines = systemResultLines(answer, catalog_);
    auto *verdict = new Verdict;
    verdict->setFrameShape(QFrame::NoFrame);
    verdict->column()->addWidget(strong(tr("%1 is installed, but one step after it did not finish").arg(shown)));
    verdict->column()->addWidget(text(cachesFailedWords(), true));
    if (!error.isEmpty())
        verdict->column()->addWidget(dim(text(tr("Recipe's own message: %1").arg(error), true)));
    for (const QString &line : lines)
        verdict->column()->addWidget(text(line, true));
    column_->insertWidget(1, verdict);

    const QString issue = tr("%1 was written into the system, but rebuilding the system's caches failed.").arg(shown);
    QStringList facts{tr("The software is installed; this is not a failed install."),
                      tr("The caches (menus, icons and fonts) are rebuilt after every system change; the next system change tries again.")};
    if (!error.isEmpty())
        facts << tr("Recipe's own message: %1").arg(error);
    facts << lines;
    auto copyText = [this, issue, facts] {
        return tr("The issue:\n%1\n\nThe facts:\n- %2\n\nThe output:\n%3\n")
            .arg(issue, facts.join(QStringLiteral("\n- ")), output_->toPlainText());
    };
    auto *showOutput = new QPushButton(tr("Show the output"));
    showOutput->setCheckable(true);
    connect(showOutput, &QPushButton::toggled, this, [this](bool on) {
        outputTitle_->setVisible(on);
        output_->setVisible(on);
    });
    auto *leave = new QPushButton(tr("Back"));
    connect(leave, &QPushButton::clicked, this, &RunPage::back);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(showOutput);
    buttons->addWidget(leave);
    buttons->addStretch();
    buttons->addWidget(copyForAgentButton(copyText));
    verdict->column()->addLayout(buttons);
    outputTitle_->hide();
    output_->hide();
    column_->addStretch();
    emit succeededWithoutCaches(name_);
}

// ---------------------------------------------------------------- A short change

void ChangePage::begin(const Words &words, Run *run)
{
    words_ = words;
    run_ = run;
    column_ = fresh();

    auto *header = new QHBoxLayout;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    top->addWidget(heading(words.title));
    if (!words.subtitle.isEmpty())
        top->addWidget(dim(text(words.subtitle, true)));
    header->addLayout(top, 1);
    auto *side = new QVBoxLayout;
    side->setSpacing(6);
    stop_ = new QPushButton(tr("Stop"));
    connect(stop_, &QPushButton::clicked, this, [this, run] {
        stop_->setEnabled(false);
        run->stop();
    });
    side->addWidget(stop_, 0, Qt::AlignRight);
    stopNote_ = dim(text(words.stopNote, true));
    stopNote_->setMaximumWidth(340);
    side->addWidget(stopNote_);
    header->addLayout(side);
    column_->addLayout(header);

    auto *panel = new Panel;
    progress_ = panel;
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    panel->column()->addWidget(bar);
    panel->column()->addWidget(dim(text(words.doing, true)));
    column_->addWidget(panel);

    outputTitle_ = strong(tr("Output"));
    column_->addWidget(outputTitle_);
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    column_->addWidget(output_, 1);

    connect(run, &Run::output, this, [this, run](const QString &line) {
        if (run == run_)
            output_->appendPlainText(line);
    });
    connect(run, &Run::finished, this, [this, run](bool ok) {
        if (run == run_)
            onFinished(ok);
    });
}

void ChangePage::onFinished(bool ok)
{
    if (ok)
        return; // the window leaves this page, and says in the status line that it worked
    const QJsonObject failure = run_->failure();
    const bool stoppedByUser = !stop_->isEnabled() && failure.isEmpty();
    progress_->hide();
    stop_->hide();
    stopNote_->hide();

    // A system change that was made, and only rebuilding the system's caches failed: the change
    // stands, and the page says so (the command's final answer carries `effects.status`).
    const QJsonObject answer = QJsonDocument::fromJson(run_->answer()).object();
    if (!stoppedByUser && answer["effects"]["status"].toString() == QLatin1String("failed")) {
        const QString error = answer["effects"]["error"].toString();
        auto *done = new Verdict;
        done->setFrameShape(QFrame::NoFrame);
        done->column()->addWidget(strong(tr("The change was made, but one step after it did not finish")));
        done->column()->addWidget(text(cachesFailedWords(), true));
        if (!error.isEmpty())
            done->column()->addWidget(dim(text(tr("Recipe's own message: %1").arg(error), true)));
        const QString issue = tr("%1, but rebuilding the system's caches failed.").arg(words_.title);
        const QStringList facts{tr("The change itself was made; this is not a failed change."),
                                tr("The next system change rebuilds the caches again."),
                                tr("Recipe's own message: %1").arg(error)};
        auto copyText = [this, issue, facts] {
            return tr("The issue:\n%1\n\nThe facts:\n- %2\n\nThe output:\n%3\n")
                .arg(issue, facts.join(QStringLiteral("\n- ")), output_->toPlainText());
        };
        auto *row = new QHBoxLayout;
        auto *leave = new QPushButton(tr("Back"));
        connect(leave, &QPushButton::clicked, this, &ChangePage::back);
        row->addWidget(leave);
        row->addStretch();
        row->addWidget(copyForAgentButton(copyText));
        done->column()->addLayout(row);
        column_->insertWidget(1, done);
        const bool printed = !output_->toPlainText().isEmpty();
        outputTitle_->setVisible(printed);
        output_->setVisible(printed);
        if (!printed)
            column_->addStretch();
        return;
    }

    // Recipe's own words first; when it gave none, the output below ends with them.
    const QString reason = failure["reason"].toString();
    auto *verdict = new Verdict;
    verdict->setFrameShape(QFrame::NoFrame);
    verdict->column()->addWidget(strong(stoppedByUser ? tr("You stopped it") : words_.failed));
    if (!stoppedByUser)
        verdict->column()->addWidget(text(!reason.isEmpty() ? reason
                                          : output_->toPlainText().isEmpty() ? tr("It gave no reason and printed nothing.")
                                                                             : tr("The output below ends with the reason."),
                                          true));
    verdict->column()->addWidget(text(words_.left, true));
    auto *buttons = new QHBoxLayout;
    auto *again = primary(tr("Try again"));
    auto *leave = new QPushButton(tr("Back"));
    connect(again, &QPushButton::clicked, this, &ChangePage::retry);
    connect(leave, &QPushButton::clicked, this, &ChangePage::back);
    buttons->addWidget(again);
    buttons->addWidget(leave);
    buttons->addStretch();
    verdict->column()->addLayout(buttons);
    column_->insertWidget(1, verdict);

    // What the command printed is shown as it is; nothing to show when it printed nothing.
    const bool printed = !output_->toPlainText().isEmpty();
    outputTitle_->setVisible(printed);
    output_->setVisible(printed);
    if (!printed)
        column_->addStretch();
}

// ---------------------------------------------------------------- Installed

namespace {

// Where a row is, in the words of a sentence about what the place means for
// this software, said only when there are two places to tell apart.
QString whereSentence(const InstalledItem &item, const Catalog &catalog)
{
    const QString other = catalog.otherWords();
    switch (item.place) {
    case Place::Me:
        return QObject::tr("It is in your own store: only you have it.");
    case Place::Computer:
        if (!item.inStore && !catalog.ab) // `recipe --system installed` on a computer with one root
            return QObject::tr("It is part of this computer's system, for everyone, and active now. ") + catalog.cannotOpenWords();
        if (!item.inStore) // `recipe --system installed` on an A/B system: the booted root
            return (item.leavesOther ? QObject::tr("It is in %1, for everyone, and active now. %2 no longer holds it.")
                                           .arg(catalog.bootedWords(), other.left(1).toUpper() + other.mid(1))
                                     : QObject::tr("It is in %1, for everyone, and active now.").arg(catalog.bootedWords()))
                   + QLatin1Char(' ') + catalog.cannotOpenWords();
        return QObject::tr("It is on this computer, for everyone.");
    case Place::Other:
        break;
    }
    return QObject::tr("Only %1 holds it. It becomes active when %2 is started, and cannot be opened until then.")
        .arg(other, catalog.otherRoot.isEmpty() ? QObject::tr("that root") : QObject::tr("root %1").arg(catalog.otherRoot));
}

// System changes that were in the other root, were wiped from it (`recab copy`), and could not be
// written again: each with Recipe's error and the button that copies it for the person's own agent.
void addNotDone(QVBoxLayout *into, const Catalog &catalog)
{
    for (const NotDone &each : catalog.notDone) {
        auto *panel = new Panel;
        panel->column()->addWidget(tinted(strong(QObject::tr("Not done yet: %1").arg(each.package)), Mood::Warn));
        panel->column()->addWidget(text(QObject::tr("%1 was written into %2 and is gone from it, because %2 was made a copy of %3. Writing it "
                                           "again did not work. Recipe tries again by itself the next time you change system software.")
                                            .arg(each.package, catalog.otherWords(), catalog.bootedWords()),
                                        true));
        panel->column()->addWidget(text(each.error, true));
        QString issue = QObject::tr("Recipe could not write %1 into %2 again after it was wiped from there.")
                            .arg(each.package, catalog.otherWords());
        QString copied = QObject::tr("The issue:\n%1\n\nThe facts:\n- Recipe's own message: %2\n- %3 is gone from %4 because it was made a "
                            "copy of %5.\n- Recipe tries again by itself on the next system action.\n")
                             .arg(issue, each.error, each.package, catalog.otherWords(), catalog.bootedWords());
        auto *row = new QHBoxLayout;
        row->addStretch();
        row->addWidget(copyForAgentButton([copied] { return copied; }));
        panel->column()->addLayout(row);
        into->addWidget(panel);
    }
}

// What a row says about a newer version.
QString newerCell(const UpdateItem *update)
{
    if (!update)
        return QString();
    return update->fromCollection ? QObject::tr("recipe has %1").arg(update->to) : QObject::tr("%1 is ready").arg(update->to);
}

} // namespace

void InstalledPage::show(const Catalog &catalog, const QString &selectedName, int selectedPlace)
{
    QVBoxLayout *column = fresh();
    // What Recipe says the stores take; summed from the list for a store it
    // could not measure.
    qint64 size = catalog.installedBytes(); // F2: one total from `storage` kinds, not `active_bytes`
    if (!size)
        for (const InstalledItem &item : catalog.installed)
            size += item.sizeBytes;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    top->addWidget(heading(tr("Installed")));
    QString summary;
    if (catalog.softwareCount() == 0 && !catalog.installed.isEmpty()) {
        summary = tr("Only the base system is installed. Software comes from recipes.");
    } else if (catalog.installed.isEmpty()) {
        summary = tr("Nothing is installed from recipes yet.");
    } else if (!catalog.twoPlaces) {
        summary = tr("%1, %2.").arg(counted(catalog.softwareCount(), tr("%1 program"), tr("%1 programs")), bytesText(size));
    } else {
        // Sizes only where a store measured them: the system's list has none. The base is the system, not a program.
        int me = 0, computer = 0, other = 0;
        for (const InstalledItem &item : catalog.installed) {
            if (item.base)
                continue;
            (item.place == Place::Me ? me : item.place == Place::Computer ? computer : other)++;
        }
        summary = tr("%1: %2 for me, %3 for everyone.")
                      .arg(counted(me + computer, tr("%1 program"), tr("%1 programs")),
                           QString::number(me), QString::number(computer));
        if (other)
            summary += tr(" %1 more only in %2.").arg(other).arg(catalog.otherWords());
    }
    top->addWidget(dim(text(summary, true)));
    if (!catalog.computerProblem.isEmpty())
        top->addWidget(dim(text(tr("This computer's own software could not be read: %1").arg(catalog.computerProblem), true)));
    column->addLayout(top);
    if (catalog.installed.isEmpty()) {
        addNotDone(column, catalog);
        column->addStretch();
        return;
    }

    bool anyDate = false, anySize = false;
    for (const InstalledItem &item : catalog.installed) {
        anyDate = anyDate || item.installedOn.isValid();
        anySize = anySize || item.sizeBytes;
    }
    auto shared = std::make_shared<Catalog>(catalog);
    auto trees = std::make_shared<QList<QTreeWidget *>>();

    // The groups: where the software is. Each says what its place means.
    auto *scrolled = new QWidget;
    auto *groups = new QVBoxLayout(scrolled);
    groups->setContentsMargins(0, 0, 2, 0);
    groups->setSpacing(8);
    QTreeWidgetItem *chosenLine = nullptr, *firstLine = nullptr;
    for (Place place : {Place::Me, Place::Computer, Place::Other}) {
        QList<const InstalledItem *> here;
        for (const InstalledItem &item : catalog.installed)
            if (item.place == place)
                here << &item;
        if (here.isEmpty())
            continue;
        if (catalog.twoPlaces) {
            const bool inStore = here.first()->inStore;
            const QString start = catalog.otherRoot.isEmpty() ? tr("that root") : tr("root %1").arg(catalog.otherRoot);
            QString title = place == Place::Me ? tr("For me · works at once, for you only")
                            : place == Place::Computer ? (inStore ? tr("For everyone · works at once")
                                                                  : catalog.ab ? tr("For everyone · in %1, active now").arg(catalog.bootedWords())
                                                                               : tr("For everyone · part of the system, active now"))
                                                       : tr("%1 · active when %2 is started").arg(placeTitle(Place::Other, catalog.otherRoot), start);
            int programs = 0;
            for (const InstalledItem *each : here)
                programs += !each->base;
            title += tr(" · %1").arg(counted(programs, tr("%1 program"), tr("%1 programs")));
            if (catalog.stores[int(place)].has && place != Place::Other)
                title += tr(", %1").arg(bytesText(catalog.stores[int(place)].installedBytes()) /* F2: sizes per kind */);
            groups->addWidget(strong(title));
        }
        QTreeWidget *tree = table({tr("Name"), tr("Version"), tr("Newer"), tr("Installed"), tr("Size")});
        for (const InstalledItem *item : here) {
            const Recipe *recipe = catalog.recipe(item->name);
            const UpdateItem *update = catalog.update(item->name, item->place);
            // The base is one row for the system itself; a library of a system group is a part that
            // comes with the software that needs it, not software of its own.
            QString shownName = recipe ? recipe->shown() : item->name;
            if (item->base) {
                shownName = catalog.baseWords(*item);
            } else if (recipe && recipe->isLibrary() && !item->inStore) {
                const QStringList with = catalog.neededBy(item->name, item->place == Place::Other ? Place::Other : Place::Computer);
                if (!with.isEmpty())
                    shownName = tr("%1 · comes with %2").arg(shownName, catalog.shownNames(with).join(tr(", ")));
            }
            auto *line = new QTreeWidgetItem(tree, {shownName, item->base ? QString() : item->version, newerCell(update),
                                                    QLocale().toString(item->installedOn, "d MMM yyyy"),
                                                    item->sizeBytes ? bytesText(item->sizeBytes) : QString()});
            line->setData(0, Qt::UserRole, item->name);
            line->setData(1, Qt::UserRole, int(item->place));
            line->setSizeHint(0, QSize(0, 32));
            if (!firstLine)
                firstLine = line;
            if (item->name == selectedName && (selectedPlace < 0 || int(item->place) == selectedPlace) && !chosenLine)
                chosenLine = line;
        }
        // A receipt written before Recipe recorded it has no date; recab's list has none of these.
        tree->setColumnHidden(3, !anyDate);
        tree->setColumnHidden(4, !anySize);
        tree->setMaximumHeight(tree->header()->sizeHint().height() + 6 + tree->sizeHintForRow(0) * int(here.size()));
        groups->addWidget(tree);
        trees->append(tree);
    }
    addNotDone(groups, catalog);
    groups->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(scrolled);
    column->addWidget(scroll, 1);

    auto *chosen = new Panel;
    auto *row = new QHBoxLayout;
    chosen->column()->addLayout(row);
    column->addWidget(chosen);

    auto fill = [this, row, shared](QTreeWidgetItem *line) {
        clear(row);
        if (!line)
            return;
        const QString name = line->data(0, Qt::UserRole).toString();
        const Place place = Place(line->data(1, Qt::UserRole).toInt());
        const InstalledItem *item = shared->installedItem(name, place);
        const Recipe *recipe = shared->recipe(name);
        const UpdateItem *update = shared->update(name, place);
        if (!item)
            return;
        auto *words = new QVBoxLayout;
        words->setSpacing(1);
        if (item->base) {
            // The system itself: replaced as a whole by a newer release, so no Update or Remove. The frame's release page says whether a newer release exists.
            words->addWidget(strong(shared->baseWords(*item)));
            words->addWidget(dim(text(tr("The files of the system release this computer runs. A newer release replaces it as a "
                                         "whole; it is not updated or removed like a program. \"Check for "
                                         "updates\" says whether there is one."),
                                      true)));
            row->addLayout(words, 1);
            auto *release = primary(tr("Check for updates"));
            connect(release, &QPushButton::clicked, this, &InstalledPage::openRelease);
            row->addWidget(release);
            emit selected(name, place);
            return;
        }
        words->addWidget(strong(tr("%1 %2").arg(recipe ? recipe->shown() : name, item->version)));
        QString about = recipe ? recipe->description + tr(".") : QString();
        if (!recipe) {
            // The recipe is gone from the collection and the sources: only the receipt speaks.
            about = tr("Its recipe is gone: no recipe for it is in the collection or the sources any more.");
            about += tr(" Its receipt lists %1.").arg(counted(item->files.size(), tr("%1 file"), tr("%1 files")));
            if (!item->source.isEmpty())
                about += tr(" It came from the source %1%2.")
                             .arg(item->source, shared->origin(item->source) ? QString() : tr(", which is not among the sources any more"));
        }
        if (shared->twoPlaces)
            about += (about.isEmpty() ? QString() : tr(" ")) + whereSentence(*item, *shared);
        // System software: what else installed in its root needs it, and so keeps it.
        QStringList needed;
        if (!item->inStore) {
            needed = shared->neededBy(name, place == Place::Other ? Place::Other : Place::Computer);
            if (recipe && recipe->isLibrary() && !needed.isEmpty())
                about += tr(" It is a shared part: it comes with %1, and is not software of its own.")
                             .arg(shared->shownNames(needed).join(tr(", ")));
            else if (!needed.isEmpty())
                about += tr(" Needed by: %1.").arg(shared->shownNames(needed).join(tr(", ")));
        }
        // Removing is held back while something needs it, in the root the change would go into.
        const QStringList blockers = item->inStore ? QStringList() : shared->neededBy(name, shared->systemTarget());
        if (!blockers.isEmpty())
            about += tr(" It cannot be removed while %1 %2 installed: remove %3 first.")
                         .arg(shared->shownNames(blockers).join(tr(", ")), blockers.size() == 1 ? tr("is") : tr("are"),
                              blockers.size() == 1 ? tr("it") : tr("them"));
        if (item->menu.isEmpty() && !item->programs.isEmpty())
            about += tr(" Run it in a terminal as %1.").arg(item->programs.join(tr(", ")));
        words->addWidget(dim(text(about, true)));
        row->addLayout(words, 1);
        if (!recipe) {
            auto *receipt = new QPushButton(tr("Its receipt"));
            connect(receipt, &QPushButton::clicked, this, [this, name] { emit openPage(name); });
            row->addWidget(receipt);
        }
        // A command-line tool has nothing to open; it is used from a terminal. Only software in a
        // store that is in effect has programs to start.
        if (!item->menu.isEmpty() && item->inStore && place != Place::Other) {
            auto *open = new QPushButton(tr("Open"));
            connect(open, &QPushButton::clicked, this, [this, name, place] { emit this->open(name, place); });
            row->addWidget(open);
        }
        if (blockers.isEmpty()) {
            auto *remove = new QPushButton(tr("Remove…"));
            connect(remove, &QPushButton::clicked, this, [this, name, place] { emit this->remove(name, place); });
            row->addWidget(remove);
        }
        if (update && place != Place::Other) {
            auto *newer = primary(tr("Update to %1").arg(update->to));
            connect(newer, &QPushButton::clicked, this, [this, name, place] { emit this->update(name, place); });
            row->addWidget(newer);
        }
        emit selected(name, place);
    };
    // One row is current in all the groups together.
    for (QTreeWidget *tree : *trees)
        connect(tree, &QTreeWidget::currentItemChanged, this, [tree, trees, fill](QTreeWidgetItem *line) {
            if (!line)
                return;
            for (QTreeWidget *other : *trees)
                if (other != tree) {
                    const QSignalBlocker quiet(other);
                    other->setCurrentItem(nullptr);
                }
            fill(line);
        });
    QTreeWidgetItem *start = chosenLine ? chosenLine : firstLine;
    if (start)
        start->treeWidget()->setCurrentItem(start);
}

// ---------------------------------------------------------------- Updates

void UpdatesPage::show(const Catalog &catalog, const QString &selectedName, int selectedPlace)
{
    QVBoxLayout *column = fresh();
    auto *header = new QHBoxLayout;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    const int counted_ = int(catalog.updates.size());
    top->addWidget(heading(counted_ ? counted(counted_, tr("%1 update"), tr("%1 updates")) : tr("Updates")));
    // What the updates ask for, in the three ways `recipe updates` can say it.
    int more = 0, same = 0, unknown = 0;
    for (const UpdateItem &item : catalog.updates) {
        (!item.changesKnown ? unknown : item.asksMore() ? more : same)++;
    }
    QStringList parts;
    if (same)
        parts << tr("%1 ask for nothing more than before").arg(same);
    if (more)
        parts << tr("%1 ask for more").arg(more);
    if (unknown)
        parts << tr("%1 cannot be compared").arg(unknown);
    QString summary;
    if (catalog.updates.isEmpty())
        summary = tr("Everything installed is at the newest version of its recipe.");
    else
        summary = tr("%1.").arg(parts.join(tr("; ")));
    qint64 fetched = 0;
    for (const Origin &origin : catalog.origins)
        fetched = qMax(fetched, origin.remote() ? origin.checked : 0);
    if (fetched > 0) {
        const QDateTime when = QDateTime::fromSecsSinceEpoch(fetched);
        const QString time = QLocale().toString(when.time(), QLocale::ShortFormat);
        summary += when.date() == QDate::currentDate() ? tr(" The sources' lists were last fetched today at %1.").arg(time)
                                                       : tr(" The sources' lists were last fetched on %1.").arg(QLocale().toString(when.date(), QLocale::ShortFormat));
    }
    top->addWidget(dim(text(summary, true)));
    header->addLayout(top, 1);
    auto *check = new QPushButton(tr("Check for updates"));
    check->setToolTip(tr("Fetches every source's list again"));
    connect(check, &QPushButton::clicked, this, &UpdatesPage::check);
    header->addWidget(check, 0, Qt::AlignBottom);
    column->addLayout(header);
    if (!catalog.updateProblems.isEmpty())
        column->addWidget(dim(text(tr("Not looked up: %1.").arg(catalog.updateProblems.join(tr("; "))), true)));
    if (catalog.updates.isEmpty()) {
        column->addStretch();
        return;
    }

    QTreeWidget *tree = table({tr("Name"), tr("Where"), tr("Version"), tr("What the newer recipe asks for")});
    auto shared = std::make_shared<Catalog>(catalog);
    for (const UpdateItem &item : catalog.updates) {
        const Recipe *recipe = catalog.recipe(item.name);
        const QString asks = !item.changesKnown ? tr("Not compared")
                             : item.asksMore() ? tr("More: %1").arg(item.asksMoreWords()) : tr("Nothing more");
        auto *line = new QTreeWidgetItem(tree, {recipe ? recipe->shown() : item.name, placeTitle(item.place, catalog.otherRoot),
                                                item.sameVersion ? tr("%1, another recipe").arg(item.from)
                                                                 : tr("%1 → %2").arg(item.from, item.to),
                                                asks});
        if (item.changesKnown && item.asksMore())
            line->setForeground(3, moodColour(tree->palette(), Mood::Warn));
        line->setData(0, Qt::UserRole, item.name);
        line->setData(1, Qt::UserRole, int(item.place));
        line->setSizeHint(0, QSize(0, 32));
        if (item.name == selectedName && (selectedPlace < 0 || int(item.place) == selectedPlace) && !tree->currentItem())
            tree->setCurrentItem(line);
    }
    if (!tree->currentItem())
        tree->setCurrentItem(tree->topLevelItem(0));
    tree->setColumnHidden(1, !catalog.twoPlaces);
    tree->setMaximumHeight(tree->header()->sizeHint().height() + 6
                           + tree->sizeHintForRow(0) * int(qMax<qsizetype>(catalog.updates.size(), 3)));
    column->addWidget(tree);

    auto *detail = new Panel;
    auto *inside = new QVBoxLayout;
    inside->setSpacing(10);
    detail->column()->addLayout(inside);
    column->addWidget(detail);
    column->addStretch();

    auto fill = [this, inside, shared](QTreeWidgetItem *line) {
        clear(inside);
        if (!line)
            return;
        const QString name = line->data(0, Qt::UserRole).toString();
        const Place place = Place(line->data(1, Qt::UserRole).toInt());
        const UpdateItem *item = shared->update(name, place);
        const Recipe *recipe = shared->recipe(name);
        if (!item)
            return;
        const QString shown = recipe ? recipe->shown() : name;
        auto buttons = [&] {
            auto *row = new QHBoxLayout;
            auto *go = new QPushButton(tr("Update %1").arg(shown));
            connect(go, &QPushButton::clicked, this, [this, name, place] { emit update(name, place); });
            row->addWidget(go);
            row->addStretch();
            return row;
        };
        auto *title = new QVBoxLayout;
        title->setSpacing(0);
        auto *form = new QFormLayout;
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(7);
        form->addRow(dim(text(tr("Version"))),
                     text(item->sameVersion ? tr("%1 still; its source publishes another recipe for it").arg(item->from)
                                            : tr("%1 → %2").arg(item->from, item->to)));
        if (!item->changesKnown) {
            title->addWidget(strong(tr("What the newer recipe of %1 may use").arg(shown)));
            title->addWidget(dim(text(item->fromCollection
                                          ? tr("What version %1 was allowed to use is not known here, so the two cannot be "
                                               "compared.").arg(item->from)
                                          : tr("Version %1 was installed before Recipe recorded what a recipe may use, so the "
                                               "two cannot be compared. From the next update on they can.")
                                                .arg(item->from), true)));
            inside->addLayout(title);
            if (recipe) {
                form->addRow(dim(text(tr("Network"))),
                             text(recipe->needs.network.isEmpty() ? tr("none") : endpointsText(recipe->needs.network), true));
                if (recipe->needs.networkMaxBytes)
                    form->addRow(dim(text(tr("Download"))), text(tr("up to %1").arg(bytesText(recipe->needs.networkMaxBytes))));
                if (recipe->needs.memoryMib)
                    form->addRow(dim(text(tr("Memory"))), text(tr("up to %1").arg(bytesText(recipe->needs.memoryMib * 1048576))));
            }
            inside->addLayout(form);
            inside->addLayout(buttons());
            emit selected(name, place);
            return;
        }
        title->addWidget(strong(item->asksMore() ? tr("%1 %2 asks for more than %3").arg(shown, item->to, item->from)
                                                 : tr("What changed in %1's recipe").arg(shown)));
        title->addWidget(dim(text(tr("Compared with the recipe version %1 was installed from.").arg(item->from))));
        inside->addLayout(title);
        // Only what differs is named: the installed version's limit beside the newer recipe's.
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(7);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(2, 1);
        QList<QLabel *> warn;
        grid->addWidget(dim(text(tr("%1, installed").arg(item->from))), 0, 1);
        grid->addWidget(dim(text(item->sameVersion ? tr("%1, the other recipe").arg(item->to) : item->to)), 0, 2);
        int at = 1;
        for (const Change &change : item->changes) {
            grid->addWidget(dim(text(change.title())), at, 0, Qt::AlignTop);
            const bool more = change.kind == QLatin1String("added") || change.kind == QLatin1String("raised");
            if (!change.isList()) {
                grid->addWidget(text(Change::amount(change.before)), at, 1, Qt::AlignTop);
                QLabel *after = more ? strong(Change::amount(change.after)) : text(Change::amount(change.after));
                grid->addWidget(after, at, 2, Qt::AlignTop);
                if (more)
                    warn << after;
            } else {
                // A list: what is new is in bold, what is gone is said.
                const bool network = change.field == QLatin1String("network");
                auto shownList = [network](const QStringList &names) {
                    QStringList each;
                    for (const QString &name : names)
                        each << (network ? endpointText(name) : name);
                    return each;
                };
                grid->addWidget(text(change.before == QLatin1String("none") ? tr("none") : shownList(change.stays() + change.removed()).join(tr(", ")), true),
                                at, 1, Qt::AlignTop);
                QStringList after;
                for (const QString &each : shownList(change.stays()))
                    after << each.toHtmlEscaped();
                for (const QString &each : shownList(change.added()))
                    after << QStringLiteral("<b>%1</b>").arg(each.toHtmlEscaped());
                QString words = after.isEmpty() ? tr("none") : after.join(tr(", "));
                if (!change.removed().isEmpty())
                    words += tr(" (no longer: %1)").arg(shownList(change.removed()).join(tr(", ")).toHtmlEscaped());
                auto *label = new QLabel(words);
                label->setTextFormat(Qt::RichText);
                label->setWordWrap(true);
                grid->addWidget(label, at, 2, Qt::AlignTop);
                if (!change.added().isEmpty())
                    warn << label;
            }
            ++at;
        }
        grid->addWidget(dim(text(item->changes.isEmpty() ? tr("What it may use") : tr("Everything else"))), at, 0);
        grid->addWidget(dim(text(item->changes.isEmpty() ? tr("the same as before: it asks for nothing new") : tr("as before"), true)), at, 1, 1, 2);
        inside->addLayout(grid);
        for (QLabel *label : warn)
            tinted(label, Mood::Warn);
        inside->addLayout(buttons());
        emit selected(name, place);
    };
    connect(tree, &QTreeWidget::currentItemChanged, this, [fill](QTreeWidgetItem *line) { fill(line); });
    fill(tree->currentItem());
}

// ---------------------------------------------------------------- Remove

RemoveDialog::RemoveDialog(const Recipe &recipe, const InstalledItem &item, const QString &takenOut, const QString &lastDesktop,
                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Remove %1").arg(recipe.shown()));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(22, 20, 22, 18);
    column->setSpacing(12);
    QLabel *question = strong(tr("Remove %1 %2?").arg(recipe.shown(), item.version));
    QFont larger = question->font();
    larger.setPointSizeF(larger.pointSizeF() * 1.2);
    question->setFont(larger);
    column->addWidget(question);
    QString words;
    if (!item.inStore) {
        // On an A/B system the change goes into the other root.
        words = takenOut;
    } else {
        // F2: `installed --json` lists no files any more; the menu entries are the recipe's (ABOUT.adds).
        words = recipe.menu.isEmpty()
                    ? tr("It is no longer installed.")
                    : tr("%1 leaves the applications menu and is no longer installed.").arg(recipe.shown());
    }
    words += recipe.keeps.isEmpty()
                 ? tr(" What it wrote in your home folder stays.")
                 : tr(" What it wrote in your home folder stays: %1.").arg(recipe.keeps.join(tr(", ")));
    QLabel *explained = text(words, true);
    explained->setMinimumWidth(420);
    column->addWidget(explained);
    // Said before the button: removing is still allowed.
    if (!lastDesktop.isEmpty()) {
        column->addWidget(tinted(text(lastDesktop, true), Mood::Warn));
    }
    column->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("Remove"), QDialogButtonBox::AcceptRole);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

