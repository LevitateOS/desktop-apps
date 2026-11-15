// The one window. Four things to look at: the disk, Browse, the mess, the two
// systems.
#pragma once

#include "before.h"
#include "disk.h"
#include "mess.h"
#include "pages.h"
#include "scan.h"
#include "systems.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMainWindow>
#include <QMap>
#include <QPointer>

#include <functional>
#include <memory>

class BrowsePage;
class DiskPage;
class Frame;
class MessPage;
class NextSystem;
class QStackedWidget;
class QTimer;
class Refresher;
class Run;

class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QWidget *parent = nullptr);
    ~Window() override;

    void scan();
    // Adds the biggest folder the rules did not name to the list, as a person
    // would by clicking it. Says which, or nothing when there is none.
    QString pickBiggestUnnamed();
    // The list of mess, changed by the person: the places Browse and Mess offer.
    void addToList(Node *node);
    void takeOffList(Node *node);
    void neverSuggest(const QString &path);
    void suggestAgain(const QString &path);
    // Everything again: the scan, the partitions, Recipe's storage.
    void lookAgain();

    // The page that says what will happen before it does.
    BeforePage *beforePage() const { return before_; }
    SystemsPage *systemsPage() const { return systems_; }
    bool showingBefore() const;

    // The two systems: the page and the comparison.
    void showSystems();
    // Asks recab what the two systems are; quick, and changes nothing.
    void readState();
    void compare();
    // Say first what comparing does (the Before page), or do it at once.
    void askCompare();
    void stopComparing();
    // What F5 does: on the mess, look again; on the systems, read their state again.
    void refresh();

    bool busy() const;
    bool comparing() const { return job_ == Job::Comparing; }
    const SystemsState &state() const { return state_; }
    bool stale() const;
    const QList<Finding> &found() const { return found_; }
    qint64 scannedBytes() const { return tree_ ? tree_->bytes : 0; }
    // Null until the two systems were compared.
    const Comparison *comparison() const { return compared_ ? &comparison_ : nullptr; }
    const std::shared_ptr<Node> &tree() const { return tree_; }
    const DiskLayout &layout() const { return layout_; }
    const QStringList &never() const { return never_; }
    // The room on the disk the home folder is on, measured now.
    Space space() const { return spaceAt(home_); }
    QString home() const { return home_; }
    DiskPage *diskPage() const { return disk_; }
    BrowsePage *browsePage() const { return browse_; }
    MessPage *messPage() const { return mess_; }
    Frame *frame() const { return frame_; }
    // Which page shows, for a check.
    bool showingDisk() const;
    bool showingBrowse() const;
    bool showingMess() const;
    bool showingSystems() const;
    // The strip is read with the recab program the window has. For a check
    // that must feed it a recab of another shape (the comparison stand-in
    // answers `status` in an older shape), this reads it from another program.
    void readStripFrom(const QString &recab);

private:
    enum class Job { None, Comparing };
    void scanned();
    void go(QWidget *page);
    void compared(bool ok);
    // Shows the Before page: its button runs `go`, Cancel shows `back`, and
    // `entry` is the rail entry it belongs to.
    void openBefore(const QString &entry, QWidget *back, const BeforePage::Content &content, std::function<void()> go);
    void drawSystems(const QString &why = QString());
    void drawDisk();
    void readLayout();
    void readEarlier();
    void listChanged();
    DiskInfo diskInfo() const;
    QStringList zones() const;
    void keep();
    void recall();
    void openFolder(const QString &path);
    // The Recab app: from the strip's button and from "Open Recab…".
    void openSystem();
    // The rail entry the page belongs to.
    QString entryOf(QWidget *page) const;

    QStringList roots_;
    QString home_, recab_, rootHelper_, osRelease_, kept_, lsblk_, recipe_, files_;
    bool apparent_;

    Scanner *scanner_ = nullptr;
    Run *run_ = nullptr, *reading_ = nullptr;
    Job job_ = Job::None;
    Run *layoutRun_ = nullptr, *recipeRun_ = nullptr;
    bool layoutRead_ = false;
    DiskLayout layout_;
    QStringList never_;
    qint64 earlierBytes_ = -1, earlierGenerations_ = 0;
    int unreadable_ = 0;
    qint64 scanFiles_ = 0, scanBytes_ = 0;
    // The Before page: what its button does, where Cancel and the rail entry go.
    std::function<void()> beforeGo_;
    QWidget *beforeBack_ = nullptr;
    QString beforeEntry_;
    SystemsState state_;
    bool stateRead_ = false, stopped_ = false;
    QDateTime comparedWhen_;
    QString comparedKey_; // recab's state of the two systems when they were compared
    QJsonObject comparedAnswer_;
    Refresher *refresher_;
    std::shared_ptr<Node> tree_;
    QList<Finding> found_;
    Comparison comparison_;
    bool compared_ = false;

    Frame *frame_;
    NextSystem *next_;
    QStackedWidget *stack_;
    DiskPage *disk_;
    BrowsePage *browse_;
    BeforePage *before_;
    MessPage *mess_;
    SystemsPage *systems_;
    QTimer *tick_;
};
