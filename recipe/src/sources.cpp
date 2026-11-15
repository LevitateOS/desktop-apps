#include "sources.h"

#include "parts.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>

// ---------------------------------------------------------------- The sources

// Plain words, with Recipe's own `recipe source` commands. "The recipes root" is a folder Recipe
// reads: the folder of a source. The ones it reads now are named, so that an agent can be pointed at one.
QString SourcesPage::ownRecipesWords(const Catalog &catalog)
{
    QStringList folders;
    for (const Origin &origin : catalog.origins)
        if (!origin.remote() && !origin.path.isEmpty())
            folders << origin.path;
    const QString reads = folders.isEmpty() ? tr("Recipe reads no folder on this computer yet.")
                                            : tr("The folders of recipes Recipe reads now: %1.").arg(folders.join(tr(", ")));
    return tr("An agent that writes or fixes recipes must be pointed at the recipes root: the folder whose recipes Recipe really "
              "reads. %1\n\n"
              "You can make a folder or a repository of your own and put your recipes in it, one .rhai file for each. Then tell "
              "Recipe to read it: press \"Add my recipe folder…\" here, or run `recipe source add --path ~/my-recipes` "
              "(for a repository on the network, `recipe source add https://…`). `recipe source list` shows what Recipe reads, "
              "and `recipe source remove NAME` stops reading one.\n\n"
              "It ends here: when you have added the folder, it is listed below with the number of recipes it holds, and Browse shows "
              "them under it. Nothing is installed until you ask.")
        .arg(reads);
}

void SourcesPage::show(const Catalog &catalog)
{
    QVBoxLayout *column = fresh();
    auto *header = new QHBoxLayout;
    auto *top = new QVBoxLayout;
    top->setSpacing(1);
    top->addWidget(heading(tr("Sources")));
    top->addWidget(dim(text(tr("Where recipes come from: repositories whose list is fetched, and folders on this "
                               "computer. Nothing is installed from a source until you ask."),
                            true)));
    header->addLayout(top, 1);
    auto *all = new QPushButton(tr("Fetch all again"));
    connect(all, &QPushButton::clicked, this, [this] { emit refresh(QString()); });
    header->addWidget(all, 0, Qt::AlignBottom);
    auto *more = new QPushButton(tr("Add a source…"));
    connect(more, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString where = QInputDialog::getText(
            this, tr("Add a source"),
            tr("The address of a repository of recipes, or a folder on this computer:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !where.trimmed().isEmpty())
            emit add(where.trimmed());
    });
    header->addWidget(more, 0, Qt::AlignBottom);
    column->addLayout(header);

    // Teaching: how to use recipes of one's own, with one's own agent.
    auto *teach = new Panel;
    teach->column()->addWidget(strong(tr("Use your own recipes, and your own agent")));
    teach->column()->addWidget(text(ownRecipesWords(catalog), true));
    auto *teachRow = new QHBoxLayout;
    teachRow->addStretch();
    auto *folder = new QPushButton(tr("Add my recipe folder…"));
    connect(folder, &QPushButton::clicked, this, [this] {
        const QString where = QFileDialog::getExistingDirectory(this, tr("The folder that holds your recipes"), QDir::homePath());
        if (!where.isEmpty())
            emit add(where);
    });
    teachRow->addWidget(folder);
    teach->column()->addLayout(teachRow);
    column->addWidget(teach);

    auto *inner = new QWidget;
    auto *list = new QVBoxLayout(inner);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(10);
    for (const Origin &origin : catalog.origins) {
        const QString name = origin.name;
        auto *panel = new Panel;
        auto *row = new QHBoxLayout;
        auto *words = new QVBoxLayout;
        words->setSpacing(2);
        QString title = origin.shown();
        title[0] = title[0].toUpper();
        words->addWidget(strong(title == origin.name ? title : tr("%1 (%2)").arg(title, origin.name)));
        // A folder's path is a path: shown as one.
        QLabel *where = text(origin.where(), true);
        if (!origin.remote())
            where->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        dim(where);
        words->addWidget(where);
        const int recipes = catalog.countFrom(origin.name);
        QLabel *state = text(tr("%1. %2.").arg(counted(recipes, tr("%1 recipe"), tr("%1 recipes")), origin.state()), true);
        if (origin.problem.isEmpty())
            dim(state);
        words->addWidget(state);
        row->addLayout(words, 1);
        if (recipes) {
            auto *look = new QPushButton(tr("Show its recipes"));
            connect(look, &QPushButton::clicked, this, [this, name] { emit browse(name); });
            row->addWidget(look, 0, Qt::AlignTop);
        }
        if (!origin.remote() && !origin.path.isEmpty()) {
            auto *folder = new QPushButton(tr("Open the folder"));
            const QString path = origin.path;
            connect(folder, &QPushButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
            row->addWidget(folder, 0, Qt::AlignTop);
        }
        if (origin.remote()) {
            auto *again = new QPushButton(tr("Fetch again"));
            connect(again, &QPushButton::clicked, this, [this, name] { emit refresh(name); });
            row->addWidget(again, 0, Qt::AlignTop);
        }
        // The two folders Recipe always reads, and the system release's own recipes, are not
        // sources that can be removed.
        if (name != QLatin1String("local") && name != QLatin1String("image") && origin.kind != QLatin1String("release")) {
            auto *drop = new QPushButton(tr("Remove…"));
            connect(drop, &QPushButton::clicked, this, [this, name] { emit remove(name); });
            row->addWidget(drop, 0, Qt::AlignTop);
        }
        panel->column()->addLayout(row);
        list->addWidget(panel);
    }
    if (catalog.origins.isEmpty())
        list->addWidget(dim(text(tr("recipe named no source."))));
    // Recipes `recipe list` could not read: named with its own reason, and not offered.
    if (!catalog.unreadableRecipes.isEmpty()) {
        auto *problems = new Panel;
        auto *title = new QVBoxLayout;
        title->setSpacing(0);
        title->addWidget(strong(counted(catalog.unreadableRecipes.size(), tr("%1 recipe could not be read"),
                                        tr("%1 recipes could not be read"))));
        title->addWidget(dim(text(tr("They are not offered until they can be."))));
        problems->column()->addLayout(title);
        for (const RecipeProblem &problem : catalog.unreadableRecipes) {
            auto *row = new QHBoxLayout;
            row->setSpacing(14);
            auto *file = new QLabel(QFileInfo(problem.path).fileName());
            file->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            file->setMinimumWidth(130);
            row->addWidget(file, 0, Qt::AlignTop);
            row->addWidget(text(problem.problem, true), 1);
            auto *read = new QPushButton(tr("Read the file"));
            const QString path = problem.path;
            connect(read, &QPushButton::clicked, this, [this, path] { emit viewFile(path); });
            row->addWidget(read, 0, Qt::AlignTop);
            problems->column()->addLayout(row);
        }
        list->addWidget(problems);
    }
    list->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    column->addWidget(scroll, 1);
}
