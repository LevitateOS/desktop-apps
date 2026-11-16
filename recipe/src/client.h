// The only place that runs `recipe`. Reads go through as the desktop user;
// changes go through the root helper (pkexec), which shows the desktop's own
// password prompt.
#pragma once

#include "model.h"
#include "run.h"

#include <QObject>
#include <QProcess>

#include <functional>
#include <memory>

// The places software can go on this computer, as the client knows them.
struct Places {
    // One explicit store (RECIPE_GUI_PACKAGE_ROOT): the only place, no choice.
    bool one = false;
    // The kernel command line says `recab.immutable`: an A/B system. "For everyone"
    // changes are written into the other root and become active when it is booted. Without it
    // they are written into the running root and are active at once. Both are `recipe --system`.
    bool ab = false;
    // The explicit store (RECIPE_GUI_PACKAGE_ROOT), else the store whose sources are read.
    QString systemRoot;
    // The computer's changes go through a root helper that asks for the password.
    bool password = false;
};

class RecipeClient : public QObject {
    Q_OBJECT
public:
    explicit RecipeClient(QObject *parent = nullptr);

    // Read again every time: a check swaps the command line file between passes.
    Places places() const;

    // Reads the catalog from `list`, `installed`, `updates` and `storage`, one after the
    // other, without holding the window up. `done` gets the catalog, or the
    // problem when it could not be read.
    using Loaded = std::function<void(bool ok, const Catalog &catalog, const QString &problem)>;
    void load(Loaded done);
    bool loading() const { return loading_ > 0; }

    // Each change names the place it is made in (see model.h, `Place`).
    Run *install(const QString &name, Place place);
    Run *update(const QString &name, Place place);
    Run *remove(const QString &name, Place place);

    // The file `recipe info` names as this recipe's: the one Recipe runs for that entry (for a
    // remote source, the copy it fetches to read). `then` gets the path, or why there is none.
    void recipeFile(const QString &qualifiedName, std::function<void(const QString &path, const QString &problem)> then);

    // Sources. Fetching a source's list again is a person's own business;
    // adding and removing one changes the system's list and needs root.
    Run *refresh(const QString &source = QString());
    Run *addSource(const QString &addressOrFolder);
    Run *removeSource(const QString &name);

private:
    using Answered = std::function<void(const QJsonObject &answer, const QString &problem)>;
    void query(const QStringList &arguments, Answered then);
    // `place`: the person's own store runs without the root helper.
    Run *change(const QStringList &arguments, Place place);
    // What goes before the command (`--system` for everyone) and
    // after it (the explicit store, if one was named), for a place.
    QStringList lead(Place place) const;
    QStringList at(Place place) const;
    QStringList collection() const;
    // The store whose sources are listed and fetched: the computer's.
    QStringList store() const;
    bool ab() const;
    QString program_, rootHelper_, recipesPath_, packageRoot_, systemRoot_;
    bool explicit_ = false;
    int loading_ = 0;
};
