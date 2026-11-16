// What the installed software takes, and what was
// done to the stores: `recipe storage --json` and `recipe history --json`,
// per place where a store stands behind the software.
#pragma once

#include "client.h"
#include "model.h"
#include "page.h"

class StoragePage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    void show(const Catalog &catalog, const Places &places);
    // The sentences of the history as shown, newest first, for a check.
    QStringList historyLines() const { return lines_; }
signals:
    void openInstalled();
    // Deletes one program's downloads or build material (`cleanup NAME --downloads|--build`, or both).
    void cleanup(const QString &name, Place place, Cleanup what);
    // `recipe cleanup`: every program's downloads and build material.
    void cleanupAll(Place place);
    void cleanupLogs(Place place);
    void removeBuildImage(Place place);
private:
    QStringList lines_;
};

// One history entry as a plain sentence ("Updated Element from 1.12.3 to 1.12.4").
QString historySentence(const Catalog &catalog, const HistoryEntry &entry);
