// The window every app of the three shares: a rail on the left (the app's
// name, then the app's own entries, then Help), the app's pages in the
// middle, a strip under them about the next system, and a status line. An
// app's window uses it as its central widget and keeps its own pages in one
// widget it hands over with `setContent`. See README.md for an example.
#pragma once

#include <QMap>
#include <QStringList>
#include <QWidget>

class ElidedLabel;
class FirstRunPage;
class HelpPage;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class RailButton;
class Strip;

class Frame : public QWidget {
    Q_OBJECT
public:
    enum App { Recipe, Recab, Recdirstat };

    // `app` is the app this window is: its name heads the rail.
    explicit Frame(App app, QWidget *parent = nullptr);

    // ---- The middle. The widget (usually a QStackedWidget of the app's pages)
    // is shown whenever Help and the first page are not.
    void setContent(QWidget *content);

    // ---- The rail. Entries are the app's own: they come under the app name
    // and before Help, in the order added. Only Help is shared.
    void addEntry(const QString &key, const QString &title);
    // The number on an entry (0 shows none); `warn` makes it amber, for
    // something that waits for the person.
    void setBadge(const QString &key, int count, bool warn = false);
    // The person's choice, made in code: marks the entry and shows the
    // content. Does not emit `chosen`.
    void select(const QString &key);
    // The app's page changed by itself (a flow moved on): marks the entry of
    // that page, and leaves Help showing when it is showing. Does not emit
    // `chosen`.
    void follow(const QString &key);
    // As a click on the entry would: shows it, marks it and emits `chosen`.
    void choose(const QString &key);
    // The number and colour on an entry now.
    int badge(const QString &key) const;
    bool badgeWarns(const QString &key) const;
    // The key of the marked entry (`helpKey()` while Help shows).
    QString selected() const { return selected_; }
    // Help is the frame's own: the rail's last entry. One page for each app (the
    // frame knows which it is). Its links go to the app's rail entries through
    // `choose`.
    bool helpShown() const;
    static QString helpKey() { return QStringLiteral("help"); }

    // ---- The first time. The page before the app's own, shown once per person
    // and per app (QSettings, never in a picture run, where a story asks for it
    // with `showFirstRun`). The rail marks nothing while it shows. Its button
    // goes on to the app's own page; choosing anything in the rail leaves it too.
    void showFirstRun();
    bool firstRunShown() const;
    FirstRunPage *firstRun() const { return firstRun_; }
    HelpPage *help() const { return help_; }

    // ---- The strip: "The next system holds N changes." with a button that
    // finishes it. The titles are the changes' own ("htop, installed"); an
    // empty list hides the strip.
    void setNextSystem(const QStringList &titles);
    // The app may keep the strip away from the page that already says it.
    void setStripAllowed(bool allowed);
    bool stripShown() const;
    QString stripText() const;

    // ---- The status line: the app's own words.
    void setStatus(const QString &words);
    // Something shown beside the words ("Read again"), in the order added.
    void addStatusWidget(QWidget *widget);
    // Said in the status line for a few seconds, in place of the app's words.
    // An empty text ends a flash at once.
    void flash(const QString &words);
    QString statusText() const;

    // Starts an app's program detached, as a labelled button for it does
    // (LEVITATE_APP_* name the programs); the status line says when it cannot be started.
    void startApp(App app);
    // The titles of the rail's entries in order, the app's own and then Help.
    QStringList entryTitles() const;

signals:
    // The person chose an app entry.
    void chosen(const QString &key);
    // The strip's button: finish the next system. The app decides where that goes.
    void finishRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void mark(const QString &key);
    void step(RailButton *from, int by);
    // The shared pages the frame shows itself, and how a key finds its place in the stack.
    int indexOf(const QString &key) const;
    void helpGo(const QString &target);
    void leaveShared();

    App app_;
    QString selected_, followed_;
    QStackedWidget *stack_;
    HelpPage *help_;
    FirstRunPage *firstRun_;
    bool firstRunChecked_ = false;
    QMap<QString, RailButton *> entries_;
    QList<RailButton *> order_;
    QWidget *appEntries_;
    Strip *strip_;
    ElidedLabel *status_;
    QHBoxLayout *statusExtras_;
    QTimer *flashTimer_;
    QString statusWords_, flashWords_;
    bool stripAllowed_ = true;
};
