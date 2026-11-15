// The page about where recipes come from: the list of sources.
#pragma once

#include "model.h"
#include "page.h"

// The sources: what each is, when its list was fetched, and what can be done.
class SourcesPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    void show(const Catalog &catalog);
    // The words of the teaching panel, for a check.
    static QString ownRecipesWords(const Catalog &catalog);
signals:
    // Fetch one source's list again, or all when the name is empty.
    void refresh(const QString &name);
    void add(const QString &addressOrFolder);
    void remove(const QString &name);
    void browse(const QString &name);
    // Read a recipe file Recipe could not read, in the viewer.
    void viewFile(const QString &path);
};
