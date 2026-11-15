// The mess list.
#pragma once

#include "disk.h"
#include "mess.h"
#include "page.h"
#include "scan.h"

#include <QList>

#include <memory>

class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

// What looks like mess, as a list to tick, and what is known about the place
// chosen. The map is on the Browse page; this page is the list.
class MessPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    void showScanning(const QStringList &roots);
    void showCount(qint64 files, qint64 bytes);
    // `space` is the room on the disk the home folder is on, measured.
    void show(std::shared_ptr<Node> tree, const QList<Finding> &found, const QStringList &roots, const Space &space,
              const QString &home, const QStringList &never, qint64 now);
    // The list changed (a place was added, taken off or never suggested again).
    void setFound(const QList<Finding> &found, const QStringList &never);
    // The findings that are ticked.
    QList<Finding> chosen() const;
    // The ticked places and the facts about them, as "Copy for agent" copies them.
    QString copyText() const;
    // Chooses a place of the list, if it is on it.
    void pick(Node *node);
    // Unticks what the rules say to check first.
    void tickOnlySure();
    Node *pointed() const { return picked_; }

signals:
    void rescan();
    void openFolder(const QString &path);
    void showInBrowse(Node *node);
    void takeOffRequested(Node *node);
    void neverRequested(Node *node);
    void suggestAgain(const QString &path);

private:
    void fillList();
    void updateFoot();
    void updatePanel();
    void updateTold();

    std::shared_ptr<Node> tree_;
    QList<Finding> found_;
    QStringList roots_, never_;
    QString home_;
    qint64 now_ = 0;
    Space space_;
    Node *picked_ = nullptr;
    QLabel *progress_ = nullptr, *head_ = nullptr, *foot_ = nullptr, *what_ = nullptr, *why_ = nullptr;
    QPushButton *show_ = nullptr, *open_ = nullptr, *takeOff_ = nullptr, *never_button_ = nullptr, *copy_ = nullptr;
    QWidget *panel_ = nullptr;
    QVBoxLayout *told_ = nullptr;
    QTreeWidget *list_ = nullptr;
};
