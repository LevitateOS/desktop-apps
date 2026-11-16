// The pages of the window. Each one is told what to show and rebuilds itself;
// it asks for actions with signals and never runs `recipe` itself.
#pragma once

#include "client.h"
#include "model.h"
#include "page.h"

#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;
class Run;
class StepMark;

// The list of recipes under its own header, search and two boxes. Those stay
// while the list is drawn again, so typing is not interrupted.
class BrowsePage : public Page {
    Q_OBJECT
public:
    explicit BrowsePage(QWidget *parent = nullptr);
    // `origin`: only the recipes of this source; empty for all.
    void show(const Catalog &catalog, const QString &category, const QString &search, const QString &origin);
    void showProblem(const QString &problem);
    // Reading or failing: the header and the boxes are not shown, the words are.
    void showLoading(const QString &title, const QString &words);
    QLineEdit *searchBox() const { return search_; }
    QComboBox *fromBox() const { return from_; }
    QComboBox *categoryBox() const { return category_; }
    // The recipes the list shows now, in order, for a check.
    QStringList listed() const { return listed_; }
signals:
    void openRecipe(const QString &name);
    // Install asks where; update acts where the software is.
    void install(const QString &name);
    void update(const QString &name, Place place);
    void retry();
private:
    QWidget *top_;
    QLabel *title_, *subtitle_;
    QLineEdit *search_;
    QComboBox *from_, *category_;
    QStringList listed_;
};

class RecipePage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // `keep`: the state of the keep option on this page (the window remembers it for the page).
    void show(const Catalog &catalog, const QString &name, bool keep);
signals:
    void back();
    // From the page of software whose recipe is gone: back to what is installed.
    void backToInstalled();
    void install(const QString &name);
    void update(const QString &name, Place place);
    void remove(const QString &name, Place place);
    void open(const QString &name, Place place);
    // Open the recipe's file.
    void readRecipe(const QString &name);
    // "One step at a time": one step alone (download, build or install), where it is made.
    void runStep(const QString &name, Place place, const QString &step);
    // Deletes a program's downloads and build material, or one of them; the installed program stays.
    void cleanup(const QString &name, Place place, Cleanup what);
    // Removes only the installed program and keeps its downloads and build material.
    void removeProgramOnly(const QString &name, Place place);
    // The keep option ("Keep downloads and build after installing") was changed on this page.
    void keepChanged(const QString &name, bool keep);
    // "Check for updates": asks upstream for these programs' newest version (only when pressed).
    void checkUpstream(const QStringList &names);
private:
    // Where the software is, in each place that holds it.
    void whereItIs(QVBoxLayout *body, const Catalog &catalog, const QString &name);
    // What can be done to what is installed: remove (the receipt page). Nothing here needs a recipe.
    void actionRows(QVBoxLayout *body, const Catalog &catalog, const QString &name,
                    const QList<const InstalledItem *> &held);
    // Installed software whose recipe is gone: what its receipt says, and what still works.
    void showReceipt(const Catalog &catalog, const QString &name);
};

class RunPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // `system`: a system change (`recipe --system`, for everyone). The software is written into the
    // other root of an A/B computer, or into the running root of one with a single root, after the
    // recipe has run, and the root's caches are rebuilt after that: the recipe's own `done` is not the
    // end of it, the command's exit is.
    // `doing` and `done`: the title while one single step runs ("Downloading X") and when it ended
    // ("Downloaded X"); empty for an install or update. `step`: which single step it is (download,
    // build, install), so a failure says only what is true of that step.
    void begin(const Catalog &catalog, const QString &name, Run *run, Place place = Place::Computer, bool system = false,
               const QString &doing = QString(), const QString &done = QString(), const QString &step = QString());
    // What the command printed on its standard output when it ended (`recipe --system install`
    // says which root it wrote into); empty before.
    QJsonObject answer() const { return answer_; }
    // The run as the page shows it now, for a check.
    bool showsRunning() const;
    QString stateWords() const;
    // Everything the page shows now, whether or not the window has moved on to another page since:
    // its labels and buttons. For a check.
    QString pageText() const;
signals:
    void back();
    void retry(const QString &name);
    void succeeded(const QString &name);
    // The software is in the system, but rebuilding the system's caches failed: not a failed install.
    // The page stays, with what to do.
    void succeededWithoutCaches(const QString &name);
    void failed(const QString &name);
    void status(const QString &words);
private:
    void onEvent(const QJsonObject &event);
    // The end of the run, once: from its last event, or from the command's exit.
    void conclude(bool ok);
    void onFinished(bool ok);
    // The command failed only in its last step: the change is written and `effects.status` is "failed".
    void cachesFailed(const QJsonObject &answer);
    StepMark *mark(const QString &recipe, const QString &step);
    void addPlanRow(const QString &recipe, const QStringList &steps);

    Catalog catalog_;
    QString name_, updatingFrom_, doneWords_, doingWords_, singleStep_;
    Place place_ = Place::Computer;
    Run *run_ = nullptr;
    QVBoxLayout *column_ = nullptr;
    QGridLayout *plan_ = nullptr;
    QWidget *planBox_ = nullptr;
    QMap<QString, StepMark *> marks_;
    QLabel *title_ = nullptr, *state_ = nullptr, *outputTitle_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QPushButton *stop_ = nullptr;
    QLabel *stopNote_ = nullptr;
    int stepsTotal_ = 0, stepsStarted_ = 0;
    bool concluded_ = false, system_ = false;
    QJsonObject answer_;
    // The run's output, and where `recipe` said it keeps its log.
    QStringList lines_;
    QString log_;
};

// A short change that has no steps of its own: removing, adding or removing a
// source, fetching the lists.
// It shows what is being done, lets it be stopped, shows the command's own output,
// and when it did not finish says why and offers "Try again". The window drives it.
class ChangePage : public Page {
    Q_OBJECT
public:
    struct Words {
        // "Removing GIMP", what it does, and the line under the bar.
        QString title, subtitle, doing;
        // What stopping it means (also asked when the window is closed meanwhile).
        QString stopNote;
        // When it did not finish: the headline, and what is true then.
        QString failed, left;
        // Said in the status line when it worked.
        QString done;
    };
    using Page::Page;
    void begin(const Words &words, Run *run);
signals:
    void retry();
    void back();
private:
    void onFinished(bool ok);
    Words words_;
    Run *run_ = nullptr;
    QVBoxLayout *column_ = nullptr;
    QWidget *progress_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QLabel *outputTitle_ = nullptr;
    QPushButton *stop_ = nullptr;
    QLabel *stopNote_ = nullptr;
};

class InstalledPage : public Page {
    Q_OBJECT
public:
    using Page::Page;
    // `selectedPlace`: which place's row when a name is in two; -1 for the first.
    void show(const Catalog &catalog, const QString &selected, int selectedPlace = -1);
signals:
    void open(const QString &name, Place place);
    void remove(const QString &name, Place place);
    void update(const QString &name, Place place);
    void selected(const QString &name, Place place);
    // The page of software whose recipe is gone: what its receipt says.
    void openPage(const QString &name);
    // The row of the base system: the frame's page that says whether a newer release exists.
    void openRelease();
    // "Check for updates": asks upstream for every installed program's newest version.
    void checkUpstream();
};

class RemoveDialog : public QDialog {
public:
    // `takenOut`: what removing does where no store stands behind the software (system software).
    // `lastDesktop`: said before the button when this removes the last desktop of its root; else empty.
    RemoveDialog(const Recipe &recipe, const InstalledItem &item, const QString &takenOut, const QString &lastDesktop,
                 QWidget *parent = nullptr);
};
