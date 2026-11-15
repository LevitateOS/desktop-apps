// The two roots side by side, each as a map of blocks: how alike the
// booted root and the other one are, and what differs. recab reads both
// and compares them; this only draws what recab answered.
#pragma once

#include "page.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QStringList>

#include <memory>

class DiskMap;
class QLabel;
class QTreeWidget;
struct Blocks; // both systems as one tree of blocks, so both maps share a layout
struct Node;

struct Side {
    QString slot, device;
    qint64 files = 0, bytes = 0;
};

struct Difference {
    enum Change { OnlyRunning, OnlyOther, Content, Metadata };
    QString path;
    Change change = Content;
    qint64 runningBytes = 0, otherBytes = 0;
    QString detail; // for Metadata: what differs, in recab's words
};

// A folder's whole size in each root.
struct FolderSize {
    QString path;
    qint64 runningBytes = 0, otherBytes = 0;
};

struct Comparison {
    Side running, other;
    qint64 sameFiles = 0, sameBytes = 0;
    QList<Difference> differences;
    QList<FolderSize> folders; // what the maps are drawn from
    QStringList own;  // files that differ by design: each root names its own partition
    qint64 more = 0;  // differences recab left out of the list

    // Reads recab's answer. An empty `error` means it was understood.
    static Comparison fromJson(const QJsonObject &object, QString *error);
    bool identical() const { return differences.isEmpty() && more == 0; }
    qint64 count() const { return differences.size() + more; }
    // The room taken by what differs: for each difference, the bigger side.
    qint64 differingBytes() const;
    // How much of the two roots is the same, by room taken, 0 to 1.
    double alike() const;
    // One sentence on which root is booted and which is the other.
    QString rootsWords() const;
    // What the differences are, as plain text for "Copy for agent".
    QString issue() const;
    QStringList facts() const;
    QString copyText() const;
};

// What `recab status --json` says about the two roots: quick to ask, and
// enough to say which is which before anything is compared.
struct SystemsState {
    bool known = false;
    QString problem; // why it is not known
    QString booted, other, otherMount; // "A" or "B"; where the other is mounted, if it is
    // Changes whenever the roots swap: a comparison made under another key is out of date.
    QString key;

    // Reads `recab status --json` (schema 11).
    static SystemsState fromJson(const QJsonObject &object);
};

class SideFrame;

class SystemsPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // Whether comparing runs through a root helper, which asks for the administrator's
    // password; without one it runs as the person and nothing is asked.
    void setPasswordAsked(bool asked) { passwordAsked_ = asked; }
    // What is there, before anything was compared. `why` is why the last
    // comparison failed, if one did.
    void showState(const SystemsState &state, const QString &why = QString());
    // Both systems are being read: each side fills as it is read.
    void showComparing(const SystemsState &state);
    // An event of the running comparison (plan, step, progress).
    void progress(const QJsonObject &event);
    // `when` is when the systems were compared; `stale` says they have
    // changed since.
    void show(const Comparison &comparison, const QDateTime &when, bool stale);

signals:
    void compare();
    void stop();
    void readAgain();
    void openSystem();

private:
    bool passwordAsked_ = true;
    // Points at one block: on both maps, in the line under them, in the list.
    void point(Node *node, bool inList = true);

    void forget();

    std::shared_ptr<Blocks> blocks_;
    SideFrame *reading_[2] = {nullptr, nullptr};
    QLabel *step_ = nullptr;
    DiskMap *here_ = nullptr, *there_ = nullptr;
    QLabel *pointed_ = nullptr;
    QTreeWidget *list_ = nullptr;
};
