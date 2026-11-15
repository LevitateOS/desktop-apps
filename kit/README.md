# gui/kit

What `gui/recipe`, `gui/recab` and `gui/disk` share. Each app lists the kit
sources it uses in its own `CMakeLists.txt` (`../kit/frame.cpp` and so on) and
adds `../kit` to its include path. Colours come from the desktop's palette
only: no stylesheet.

| File | What |
|---|---|
| `look.*` | a stand-in look from a checkout (`--look`), and `settle` for the picture modes |
| `parts.*` | small widgets: text, `Card`, `Panel`, `Chip`, `ElidedLabel`, `ClickLabel`, `Mood` colours |
| `page.*` | `Page`, the base of every page (it rebuilds itself; says "loading") |
| `run.*` | `Run`, one command and its JSON events; `stop()` asks it to end |
| `work.*` | `WorkPage`, a long run with its steps and output, and a "Copy for agent" button for the output |
| `refresh.*` | `Refresher`: F5, Ctrl+R and coming back to the window |
| `frame.*` | the window frame (below) |
| `windowsize.*` | `rememberWindowSize`: a window opens at the size it had |
| `help.*` | the Help page, owned by the frame, and `helpText(app)`: the words of Help for each app (below) |
| `firstrun.*` | the page before the app's own the first time, owned by the frame (below) |
| `helpstory.*` | the checks of the stories about Help and the first page, the same in the three apps; only the picture modes use it |
| `nextsystem.*` | `NextSystem`: reads `recab status --json` for the strip |
| `before.*` | `BeforePage`: what will happen, shown before anything starts (below) |

## The frame

One widget that an app's window sets as its central widget. It is the rail on
the left (the app's name, then the app's own entries, then Help), the app's pages in the middle, a strip under them about the next
system, and a status line. Do not add entries that lead nowhere: only Help is shared, and the frame adds it under the app's own
entries.

```cpp
Frame(Frame::App app)                    // Frame::Recipe, Frame::Recab, Frame::Recdirstat
void setContent(QWidget *content);       // your page stack; shown whenever Help and the first page are not
void addEntry(key, title);               // the app's own rail entries, in order
void setBadge(key, count, warn = false); // 0 shows none; warn = amber (something waits for the person)
void select(key);                        // mark an entry and show the content (no signal)
void follow(key);                        // your pages moved on by themselves: mark that entry,
                                         //   but stay on Help when it is showing
void choose(key);                        // as a click would (marks, shows, emits chosen)
void setNextSystem(QStringList titles);  // the strip; empty hides it
void setStripAllowed(bool);              // keep the strip away from the page that says it itself
void setStatus(QString);                 // left of the status line: what was read and when
void addStatusWidget(QWidget *);         // beside it, e.g. "Read again"
void flash(QString);                     // said for a few seconds in place of the status; empty ends it at once
void startApp(App);                      // starts another app, for a labelled button or link; says so when it cannot
QStringList entryTitles() const;         // the rail's entries in order
HelpPage *help() const;
FirstRunPage *firstRun() const;
static QString helpKey();                // "help"
bool helpShown() const;
void showFirstRun();                     // the page before the app's own (below)
bool firstRunShown() const;
signals: chosen(key)  finishRequested()
```

- `chosen(key)` comes from the person's click or keyboard, for the app's entries. Switch your stack to the page of that key.
- Help (`Frame::helpKey()`) is the frame's own page: no `chosen`.
  Its links to the app's rail entries are chosen as
  a click on the rail would be, so an app's `chosen` handler already shows the
  page. Help draws a thing to do only when its page exists (a rail entry), so no
  link leads nowhere.
- `finishRequested()` is the strip's button, "Finish and restart…". Show the
  page where the next system is finished.
- The rail has no switcher: each window is one app. `startApp` starts another
  app's program detached for a labelled button or link only (Help's "Open …", Recab's
  "Finish and restart…" and Recdirstat's "Open Recab…"; `recipe-gui`, `recab-gui`, `disk-gui`; override with `LEVITATE_APP_RECIPE`,
  `LEVITATE_APP_SYSTEM`, `LEVITATE_APP_DISK`). If that fails the status line
  says so.
- The rail is reachable by Tab; Up and Down move between entries; Space or
  Return chooses. Every entry has an accessible name, with its badge.

The strip says "The next system holds N changes." with the changes in a few
words and the button. Recipe and Recdirstat do not hold the status, so they ask:

```cpp
auto *next = new NextSystem(recabProgram, this);   // runs `recab status --json`
connect(next, &NextSystem::known, this, [this, next] { frame_->setNextSystem(next->titles()); });
next->read();   // again after what changes it, and when Refresher asks
```

It reports nothing when there is no next system or the status cannot be read, and
nothing while the running system is on trial: recab still calls the other system
"next" then (the preparation that made the running system is kept until the trial
is kept), but it is the system from before, so there is nothing to finish and the
strip would say what is not so (Recab's own model reads it the same way).

### An app adopting the frame

```cpp
// window.cpp
frame_ = new Frame(Frame::Recdirstat);
frame_->setContent(stack_);                       // the pages the window already had
frame_->addEntry("map", tr("Mess"));
frame_->addEntry("compare", tr("Compare"));
frame_->select("map");
frame_->setStatus(tr("Read at %1").arg(time));
setCentralWidget(frame_);                         // and drop the QStatusBar

connect(frame_, &Frame::chosen, this, [this](const QString &key) {
    stack_->setCurrentWidget(pageFor(key));
});
connect(frame_, &Frame::finishRequested, this, [this] { /* open Overview of Recab, or show a note */ });
```

## Before

`BeforePage` is the page an app shows before it starts something that changes
the computer. It says nothing about recipes, disks or systems: the app puts the
words in. From the top: the title as a question, the line "Nothing has started.
This is what will happen if you go on.", then the sections that were filled
(left empty, a section is not shown), then the foot with "Cancel" and one named
button. It is a `Page`: add it to your stack and give it content when you need it.

```cpp
before_ = new BeforePage;                       // once, into the stack
BeforePage::Content c;
c.title = tr("Install Krita?");
c.whereTitle = tr("Where it goes");             // a choice between places: two or more are
c.choices = {{"pc", tr("For this computer"), tr("Into the next system."), tr("Asks for the password.")},
             {"me", tr("For me"), tr("Into your own folder."), tr("No password.")}};
c.chosen = "pc";                                // ...one is a statement, none is no section
c.happensTitle = tr("What happens");            // plain sentences, one line each
c.happens = {tr("Krita 5.3.1 is fetched and built.")};
c.tableTitle = tr("What it may use");           // two columns, then a line under them
c.table = {{tr("Network"), tr("download.kde.org")}, {tr("Memory"), tr("up to 4 GiB")}};
c.tableNote = tr("Anything else is refused.");
c.foot = tr("From the main source."); c.linkWords = tr("Read the recipe");   // link only with words
c.go = tr("Install");
before_->show(c);                               // draws it; call again to draw it anew
stack_->setCurrentWidget(before_);

connect(before_, &BeforePage::cancelled, ...);          // Cancel, and Escape
connect(before_, &BeforePage::confirmed, this, [](const QString &choice) { /* start it */ });
connect(before_, &BeforePage::choiceChanged, this, [](const QString &choice) { /* show(...) again if the words depend on it */ });
connect(before_, &BeforePage::linkClicked, ...);
```

- Picking a card draws the page again with the new choice at once and emits
  `choiceChanged(key)`; an app whose sentences depend on the choice answers with
  `show()` of new content (the scroll position stays).
- `confirmed(key)` carries the key picked (the only choice's key when there is
  one, empty when there are none).
- The sections scroll; Cancel and the button do not. The cards use the
  palette's highlight for the one picked; nothing else is coloured.
- For a check: `pageText()` (every label and button of the current page),
  `choiceNames()`, `chosen()`, `choose(key)`. A page drawn just now is shown
  once the event loop has run, so a check settles before clicking its button.
- The Recipe app is the first user (`gui/recipe/src/beforewords.*` has its
  words); Recab and Recdirstat list `before.cpp` in their `CMakeLists.txt` to use it.

## The size of a window

`rememberWindowSize(window, "recab", {1100, 700}, remember)` (`windowsize.*`)
gives a window the size it had when it was closed (QSettings, organisation
"LevitateOS", file `windows`, one key per app), 1100 by 700 the first time. Each
app's `main.cpp` calls it instead of `resize`. With `remember` false (the
picture modes) nothing is read or written.

## Help

`HelpPage` (`help.*`), the rail's last entry, one page for each app (the frame
knows which it is; the app adds nothing). `helpText(app)` holds every word of it,
so the first page can take Help's ideas. In order:

1. What the app is for in a few sentences, and what it never does by itself.
2. "The ideas you need": a short heading and two or three sentences each. Recab:
   the running system and the next system, finishing, the restart on trial,
   coming back. Recipe: recipes, the sandbox and what a recipe may use, the two
   places software goes. Recdirstat: mess, the two systems.
3. "How do I…": what the app can do, each row ending in "Go to PAGE", a link
   (underlined, in the palette's link colour) to the page where it is done.
4. "When something is wrong": Recab: a run stopped (its output has "Copy for
   agent"), the desktop does not start (a link to Getting back); Recipe: an
   install or update stopped; Recdirstat has none and the section is left out.
5. "The other two apps", one line each with "Open APP" (`Frame::startApp`), and
   "Show the first page again".

The page scrolls and its text column is at most 720 pixels wide. It has no
search. A sentence goes in only when a README or the code makes it true of the
app today, and it is read against the app's "Not done" list before it stays.
Things the design drew and nothing is behind are not on it: a menu bar and the
keys of its menus (the apps have none; the only shortcuts in the code are F5 and
Ctrl+R to read again, and Escape on the Before page), Ctrl+K "Go to
anything", F1, search in a page, opening an app at a place from the command line
(`--show ...`), and "About".

For a check: `HelpPage::pageText()`, `links()` and `press(words)`.

## The first time

`FirstRunPage` (`firstrun.*`): the first time a person opens an app, the frame
shows it in the content area in place of the app's own page, and the rail marks
nothing. It has: "Welcome to APP", what the app is for and never does (Help's),
"Worth knowing first" (the first sentence of the Help ideas that are marked
`forFirstRun`), and "Go on to APP" with a link "Read Help first".

- Whether it is wanted is kept per person and per app with `QSettings`
  (organisation "LevitateOS", file `firstrun`, one key per app), written when
  the page is first shown, so it comes once. In a picture run (`pictureMode()`)
  nothing is read or written and the page is never shown by itself; a story
  asks for it with `Frame::showFirstRun()`.
- The frame decides in its first `showEvent`, after the app has made its pages
  and started reading: the page is a widget of the frame's own stack, so the
  app's first read does not wait for it. Any
  rail entry, a link of the page or the button leaves it; the button returns to
  the page the app is on.
- Help has "Show the first page again" (`Frame::showFirstRun`). It does not change
  what was remembered.
- Never exercised: the remembering itself (the stories run in picture mode, where
  it is off), and the page on a real desktop.

## Checks

There are no unit tests. Each app's picture mode (`--shots`) is the check; the
frame and the strip are checked in `gui/recab/src/shots.cpp`. Help and the first
page are checked by `helpstory.*` in all three stories: the rail ends with Help;
the Help page of the app (picture `h-help`) with every idea, row and link of
`helpText`; every link goes where it says (the rail marks that entry and the
app's page is the one showing; `h-help-went` after two); "Open APP" asks the
frame to start the app; the first page (`h-firstrun`), "Read Help first", and
going on to the app (`h-firstrun-went-on`); a picture run wants no first page
and keeps no setting. The real-recab story of Recab uses the prefix `rh`.

**The guard.** A picture run starts the app's backend programs and a root helper,
and writes settings: run bare it would do that to the person's real computer.
`guardPictureMode` (`look.*`, called by each app's `main.cpp` before the window is
made, since the window starts reading at once) makes `--shots` refuse to start,
with a sentence that names what is missing and exit code 2, unless these are named
in the environment: the app's own programs (see its README), `HOME` (not the
person's own home folder), `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_DATA_HOME`,
`XDG_RUNTIME_DIR`, and the app's root helper (set and empty counts as named, and
runs the command directly). `startApp` does not start an app that `LEVITATE_APP_*`
does not name. Each app's story runs itself bare and checks that it refuses
(`helpstory::guardRefuses`).

**Waiting.** `waitFor` (`look.*`) is what the stories
wait with: for the thing itself, never a number of seconds; its limit is three times
what the thing normally takes and never less than a minute, because a busy machine
answers slowly.
