# recipe-gui

The graphical front end for Recipe on the desktop images: Plasma on LevitateOS,
LXQt on AcornOS. One window to find software, see what its recipe will do and
may use, read the recipe, install, update, go back a version and remove.

It holds no package rules. It runs `recipe`, reads its JSON and shows it. It has
no agent inside: the only bridge to one is a "Copy for agent" button on failure
views, which puts the text on the clipboard.

Qt 6 Widgets, C++20, CMake. It uses no stylesheet and no colours of its own
beyond the red for "this went wrong", so it takes each desktop's own style.
The rules it follows are the settled design in the superproject's
`docs/product-handoff.md` (sections 2.3 to 2.8).

## The window

The window is the shared frame (`../kit/frame.*`, see `../kit/README.md`): the
rail on the left, the pages in the middle, a status line below. The rail is
Browse, Installed, Updates (the number of updates is its badge), Sources,
Storage, and Help, which the frame owns. The window opens at 1100 x 700 the
first time, then at the size it had.

- **Browse**: the search and the boxes for the source and the kind of software,
  then the recipes. A recipe's page has what it is, its version and source,
  where it is installed, what can be done to it, what it may use and what comes
  with it, the licence, and the recipe itself.
- **Installed**: what is installed, grouped by place (below), with Open, Update,
  Remove and the rest on each row.
- **Updates**: what `recipe updates` says would change, with what the newer
  recipe may use that the installed one may not. "Check for updates" fetches the
  sources' lists again. "Update all" updates every update that is not kept at a
  version, each after its own Before step.
- **Sources**: where recipes come from, and how to use recipes of one's own.
- **Storage**: what installed software and its earlier versions take.
- **Help**: the frame's page, the same shape in all three apps.

The status line says where the recipes are from and when they were read.
"Read again" sits beside it.

## Where software goes

There are two places, and the window says which in words on the Before page,
on every row of Installed and Updates and on a recipe's page.

| Place | What it means | What the window runs |
|---|---|---|
| For me | The person's own store (`~/.local/share/recipe`). Active at once, for them only. It is the default choice. | `recipe install NAME`, as the person, no root helper |
| For everyone, one root | A computer without `recab.immutable` on its kernel command line has one root. The change goes into root's store (`/var/lib/recipe`) and is active at once. | the root helper, then `recipe install NAME --package-root /var/lib/recipe` |
| For everyone, A/B | On an A/B computer the booted root is locked. The change is written into the unlocked other root and is active when that root is booted. The booted root is never written. | the root helper, then `recipe --system install NAME` |

Whether the computer is A/B is read from the kernel command line
(`recab.immutable`, `RECIPE_GUI_CMDLINE`). The window never runs `recab`:
`recipe` finds the other root itself.

On an A/B computer Installed shows three groups: "For me", "For everyone · in
the running root (A), active now" and "In the other root (B) · active when root
B is started" (the letters are the real ones). They come from `recipe --system
installed --json`, which answers `booted`, `other` and `not_done`. A `not_done`
entry is a system change that `recab copy` wiped from the other root and that
Recipe could not write again. The window shows Recipe's error and "Copy for
agent"; Recipe tries again by itself on the next system action. After an install
into the other root the window says "NAME was written into the other root (B).
It becomes active when root B is started." A plain `{packages, profile}` answer
is read as one store, with no A/B words.

For software in the running root of an A/B computer the window shows only what
Recipe answers: no size, no date, no earlier versions, no programs and so no
Open, no Go back, no keeping at a version. It offers Update when the collection
has another version, and Remove.

## Before anything happens

An install or an update does not start at once. The window shows the shared
Before page (`../kit/before.*`, the words in `src/beforewords.*`):

- **Where it goes**: the places that do not hold the software yet. One explicit
  store, or a place that is already taken, leaves nothing to choose.
- **What happens**: which version of what; that it is fetched from the hosts of
  the recipe's declared network endpoints and built or put in place inside the
  sandbox; which other recipes come with it and which are only needed to build
  it; for an update, from which version to which. Proprietary software says so
  here, with its licence, before the button: the person chooses it knowingly.
- **What the recipe may use**: network, the most it may download, memory, disk
  while it builds, time, and "The sandbox refuses anything else." For an update,
  what `recipe updates` reported as changed.
- **Foot**: which source the recipe is from, and "Read the recipe".

"Install" or "Update" starts the run. "Cancel" and Escape go back and nothing
has run. The page does not give a download size or disk use: `recipe` reports
neither before it has run.

A recipe whose `ABOUT.libc` this system's C library cannot run is not offered:
one line says why. `recipe list --json` carries `host_libc` once and `runs_here`
and `not_runnable_reason` for each recipe; the window reads them and decides
nothing.

## The run

A run is the command's `--machine-events` stream, shown as steps, a progress bar,
the output and a Stop. It ends on its last event: `done` or `failed` shows the
result at once (no Stop, no bar), whenever the command exits and before the lists
are read again. On an A/B computer the `done` event comes before the other root
is written, so there the page says "Writing it into the other root (B)…" until
the command exits.

A stopped install shows the issue in one sentence and the facts: what Recipe
recorded (the refused address and the recipe's allowed list, Recipe's own
message, the recipe's path, what is still installed) and the last 20 lines of the
output, with where the run's log is (`recipe: diagnostics: DIRECTORY`).
"Try again" repeats it, "Show the output" reveals the whole output and "Copy for
agent" puts the issue, the facts and the output on the clipboard. A refused
connection names the numeric address Recipe reports, never a host name.

Short changes (remove, go back, keep at a version, add or remove a source, fetch
the lists, clear earlier versions) have a page of their own with a Stop, the
command's output, and on failure "Try again" and "Back". Only one change runs at
a time: a second one is told "One thing at a time". Closing the window while a
run is going asks first, says what stops and what is kept, and defaults to
keeping the window open.

## A program's page and Installed

- **Open** starts the program from the profile of the place it is in: the `Exec`
  line of `PROFILE/share/applications/NAME.desktop` (field codes dropped), else
  the first `bin/` file the receipt lists, with `PROFILE/bin` first on its
  `PATH`. A menu entry with `Terminal=true` is not started: the status line says
  to run it in a terminal. `DBusActivatable`, `TryExec` and `Path` are ignored.
- **Read it** (and "Read the recipe" on the Before page) opens a read-only viewer
  of the recipe's file (`src/sourceview.*`), with Rhai highlighting and the
  source it is from. The file is the one `recipe info NAME` names; for a remote
  source `recipe info` fetches the copy to read. It cannot be changed here.
- **Keep at this version** and **Let it update again** (`recipe hold`, `unhold`)
  mark a program "kept at this version". A kept program is listed by `recipe
  updates` but not counted in the badge, and "Update all" leaves it alone.
- **Go back** (`recipe go-back`) returns to the newest earlier version.
- **A program with no recipe** (the source is gone) is not a dead end: Installed
  says where it came from, and "Its receipt" opens a page of what the receipt
  holds and what can be done from it (keep at the version, go back, remove).

## Storage

One section of numbers per store the computer has (`recipe storage --json`): what
the earlier versions take and in how many generations (Recipe's own count of
the earlier versions of the user profile that `go-back` returns to), what is
installed takes, and what was done to the stores (`recipe history --json`, as
sentences with the time). "Clear earlier versions…" asks first, then runs
`recipe storage --clear-earlier`. Only numbers `recipe` reports are shown: it has
no build-cache or download sizes, and the page says so. An A/B computer's running
root has no store behind it, so it has no row of numbers.

## Sources and your own recipes

The Sources page shows each source (`recipe list --json`: `name`, `title`, `kind`,
`address` or `path`, `commit`, `checked`, `problem`) and its recipes, and says on
every recipe where it is from.

| Action | Command |
|---|---|
| Fetch again, Fetch all again, Check for updates | `recipe refresh [NAME]`, as the person |
| Add a source (an address, or a folder) | `recipe source add ADDRESS` or `recipe source add --path FOLDER`, as root |
| Remove | `recipe source remove NAME`, as root |

A recipe of a source is named `SOURCE:NAME`. The page also teaches, in plain
words, how to use recipes of one's own with one's own agent: point the agent at
the recipes folder Recipe will actually read, make a folder or repository for
the new recipes, then add that folder here ("Add my recipe folder…", or
`recipe source add --path ~/my-recipes`). The flow ends with the page saying
"Recipe now reads recipes from NAME (N recipes)".

## Reading

The catalog is read without holding the window up. The first time the page says
"Reading the recipes". It is read again after every change, on "Read again", on
F5 or Ctrl+R, and when the person comes back to the window after 20 seconds or
more. That fetches nothing from the network.

A read is `list`, then for each place that has a store `installed`, `updates`,
`storage` and `history`, and `--system installed --json` for the A/B computer's
root. When `updates` fails the catalog is not shown and the page says why; when
`storage` or `history` fails only the sizes or the history are missing.

## What it asks of `recipe`

Reads (JSON on stdout, `"schema_version": 1`):

| Command | The window reads |
|---|---|
| `recipe list --json` | `host_libc`; per recipe `name`, `version`, `description`, `about`, `needs`, `deps`, `build_deps`, `hooks`, `source`, `runs_here`, `not_runnable_reason`; `sources`; `problems` for recipes that cannot be read |
| `recipe installed --json` | per package `name`, `version`, `recipe`, `files`, `installed_on`, `size_bytes`, `kept`, `held`, `source`, `commit`; and `profile` |
| `recipe updates --json` | `updates` (`name`, `source`, `from`, `to`, `reason`, `changes`, `held`) and `problems` |
| `recipe storage --json` | `active_bytes`, `earlier_generations`, `earlier_bytes` |
| `recipe history --json` | `history`, each with `at`, `what`, `package`, `from`, `to`, `source` |
| `recipe --system installed --json` | on an A/B computer: `booted`, `other`, `not_done` |
| `recipe info NAME` | the line `Recipe: PATH`, to find the recipe's file |

Changes, through the root helper unless they are for the person's own store,
with `--machine-events`: `install`, `update`, `remove`, `go-back`, `hold`,
`unhold`, with `--system` first on an A/B computer for the three that change
system software; `storage --clear-earlier --json`; `refresh`; `source add`,
`source remove`. `--recipes-path` is added when `RECIPE_GUI_RECIPES_PATH` is set.

The events the run page reads, one JSON object per line on stderr; any other line
is shown as output: `plan` (recipes and their steps), `step` and `recipe-hook`
(`recipe`, `hook`, `status`, `msg`), `fetch` (`bytes`, `total`), `progress`
(`done`, `total`), `say`, `output`, and the last one, `done` or `failed`
(`recipe`, `hook`, `reason`, and `refused` with `endpoint` and `declared` for a
refused connection). Without a `plan` the window adds three steps for each recipe
as its first step arrives.

The password prompt (`data/org.levitateos.recipe.policy`) says what the action
covers: install, update, remove, keeping at a version, going back, adding or
removing a source and clearing earlier versions. Fetching the lists runs as the
person and asks for nothing.

## Build and run from a checkout

From this repository's root:

```sh
cmake -S recipe -B build/recipe -G Ninja
cmake --build build/recipe

RECIPE_GUI_RECIPE=$PWD/recipe/fixtures/recipe RECIPE_GUI_ROOT_HELPER= \
  build/recipe/recipe-gui --look levitate
```

Needs the Qt 6 base and SVG development files (`qt6-qtbase-devel`,
`qt6-qtsvg-devel` on Fedora). This app alone needs only Qt 6.4.

| Setting | What | Default |
|---|---|---|
| `RECIPE_GUI_RECIPE` | the `recipe` to run | `recipe` on `PATH` |
| `RECIPE_GUI_ROOT_HELPER` | what changes are run through; empty runs them directly | `pkexec` |
| `RECIPE_GUI_RECIPES_PATH` | the collection to list and install from | Recipe's own choice |
| `RECIPE_GUI_PACKAGE_ROOT` | when set, even to nothing: one explicit store and nothing else, so no choice of place | unset: the two places |
| `RECIPE_GUI_SYSTEM_ROOT` | the computer's store when it is not A/B | `/var/lib/recipe` |
| `RECIPE_GUI_CMDLINE` | the file read for `recab.immutable` | `/proc/cmdline` |
| `LEVITATE_APP_RECIPE`, `LEVITATE_APP_SYSTEM`, `LEVITATE_APP_DISK` | the programs Help and its buttons start | the installed ones |

`--look levitate|acorn` shows a stand-in look for a desktop that is neither.

### The stand-in and the story

`fixtures/recipe` is a stand-in for `recipe` that answers in the real shapes
from `fixtures/catalog.json` and a state folder (`RECIPE_STANDIN_STATE`). It
downloads and installs nothing. Its settings are listed at the top of the file:
`RECIPE_STANDIN_AB=0|1` (an A/B computer or not), `RECIPE_STANDIN_NOT_DONE`,
`RECIPE_STANDIN_HOST_LIBC`, `RECIPE_STANDIN_SPEED`, `RECIPE_STANDIN_STOP_AT`,
`RECIPE_STANDIN_FAIL`, `RECIPE_STANDIN_LOG` and others. `fixtures/as-root` stands
in for `pkexec` with `sudo`; `fixtures/helper-log` notes what the helper is given.

`--shots FOLDER` walks the pages and flows against the stand-in, saves a picture
at each point, checks what the window says and ends with
`all flows ended where they should` or names what failed. With
`QT_QPA_PLATFORM=offscreen` it needs no display. It refuses to start (exit 2,
with a sentence naming what is missing) unless every program it could run and
every folder it could write is named in the environment, and it runs itself bare
to check that:

```sh
F=$PWD/recipe/fixtures; S=/tmp/recipe-shots   # all state of the check lives here
rm -rf $S; mkdir -p $S/home $S/store $S/run; chmod 700 $S/run
QT_QPA_PLATFORM=offscreen HOME=$S/home XDG_CONFIG_HOME=$S/home/config XDG_STATE_HOME=$S/home/state \
  XDG_DATA_HOME=$S/home/data XDG_RUNTIME_DIR=$S/run \
  RECIPE_GUI_RECIPE=$F/recipe RECIPE_GUI_ROOT_HELPER= \
  RECIPE_GUI_PACKAGE_ROOT=$S/store RECIPE_STANDIN_STATE=$S/state RECIPE_STANDIN_SPEED=0 \
  build/recipe/recipe-gui --look levitate --shots $S/shots
```

The state folders must be new. The story needs `pgrep`. Its last part opens
windows of its own on a scratch command line, one for an A/B computer and one
for a computer that is not, and checks the exact arguments of every change and
that "for me" never reaches the root helper.

`--shots FOLDER --try NAME` runs against the real `recipe` instead: it shows the
recipe, installs it into a test store, pictures each point and removes it again
(`--keep` leaves it installed, `--read-only` installs nothing). It needs
`RECIPE_GUI_RECIPE` pointed at a built `recipe`, the Ginux launcher and image as
`tools/recipe`'s README describes, and `RECIPE_GUI_ROOT_HELPER=fixtures/as-root`.

## The source

| File | What |
|---|---|
| `src/model.*` | the data: recipes, what is installed, updates, as read from JSON |
| `src/client.*` | the only code that runs `recipe`; a `Run` reports one change as events |
| `src/beforewords.*` | the words of the Before page, from what `recipe` reported |
| `src/pages.*` | Browse, a program's page, a run, Installed, Updates, the remove question |
| `src/sources.*` | the Sources page |
| `src/storage.*` | the Storage page |
| `src/sourceview.*` | the read-only recipe viewer |
| `src/window.*` | the frame, its rail and status, and the flows between pages |
| `src/shots.*` | the picture mode and the story |
| `../kit/` | the shared frame, Before page, widgets, command runner (see its README) |
| `fixtures/` | the stand-in `recipe`, its catalog, `as-root`, `helper-log` |
| `data/` | the menu entry and the polkit action |

## Check for updates

At the right end of the status line there is a "Check for updates" button (kit `frame.*`, `release.*`,
`releasepage.*`). It opens a page of the frame's own, like Help, that runs `recupdate check --json`
as the person and says: checking, up to date (with the installed release), a newer release (version,
date, download size, a link to what is new), or the failure with "Copy for agent". What the daily
check wrote (`/var/lib/recupdate/available.json`) is read at start, so a release it found shows
without the network. It installs nothing: when a release is available it says where it is installed
from and has a button that starts that app, "Open Recab" on an A/B system (`/usr/bin/recab` exists)
and "Open Updater" on a mutable one. F5 and coming back to the window read the file again.

| Setting | What | Default |
|---|---|---|
| `RECUPDATE_BIN` | the program that is `recupdate` | `recupdate` |
| `RECUPDATE_STATE_DIR` | where `available.json` is | `/var/lib/recupdate` |
| `RECUPDATE_RELEASE_FILE` | the release the system runs | `/usr/lib/levitate/release.json` |
| `LEVITATE_AB` | `1`: an A/B system, `0`: a mutable one; unset: whether `/usr/bin/recab` exists | unset |
| `LEVITATE_APP_SYSTEM`, `LEVITATE_APP_UPDATER` | the programs "Open Recab" and "Open Updater" start | `recab-gui`, `updater-gui` |

## Not done

- Everything here ran offscreen against the stand-in. The real `recipe` was only
  read from (list, installed, updates, storage, history) on a hand-made store.
  Installing, going back, keeping at a version, clearing, system installs into a
  real other root and a real refused connection have not been run through the
  window. Neither has the window run on a real desktop, with `pkexec` or a real
  polkit prompt.
- Not shown, because `recipe` carries nothing for it: "Copy to your own recipes",
  "Needed by", sort and licence boxes, search of Installed, sizes of downloads and
  builds, a source's "added by you on", and which build is running as the reason
  something cannot be cleared.
- "Update all" updates everything that is not kept, each after its own Before
  step; it does not limit itself to updates that ask for nothing new.
- Remove asks in its own small window, not on the Before page. Go back and Clear
  earlier versions act without a Before page (Clear asks in a box first).
- Recipes show a letter until the catalog carries `NAME.svg`. No translation
  exists, though every string goes through `tr()`. No notice appears when updates
  appear.
- Building it for each distribution against that distribution's Qt, and shipping
  it in the desktop images, are not finished: no image carrying it has been built
  and run.
- None of "Check for updates" was run: no app was started, no story was run and no screenshot was taken; it
  compiled and nothing more. `recupdate` (`check --json`, `fetch`, `apply --json`), `recab update` and
  `recupdate apply` did not exist when it was written: it follows `docs/release-engineering.md` section 10 only.
