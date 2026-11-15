# disk-gui

The third desktop app, titled "Recdirstat". It sits in the shared window frame
(`gui/kit/frame.*`, see its README): the rail with the app name, Recdirstat's entries
and Help; the pages; the status line.
It does these things, each its own rail entry (it opens on the first):

- **The disk**: how full the place for the person's files is, what could be
  freed and where it is, every partition of the disk, whose room it is.
- **Browse**: the folder being looked at, with the map beside it.
- **Mess**: where the mess is hiding, as a list to tick. "Copy for agent" puts
  the ticked places and the facts about them on the clipboard.
- **The two roots**: it compares the booted root with the other root (the A and B root
  partitions) and says how alike they are and what differs. **Only on a computer that
  has two roots**: recab can be run *and* the kernel command line has `recab.immutable`
  (an A/B install). On a single-root ("mutable") install there is no such entry, no page,
  no row about it in Help, and recab is never run.

The window only reads and shows; it deletes and moves nothing. Whoever clears
the mess does it outside this window; "Copy for agent" hands them the list. Qt 6 Widgets, C++20, CMake.
Shared widgets are in `gui/kit`.

## State

**The finding is real.** The scanner and the rules have run on a real home
folder (172 GiB read in about nine seconds, on a warm disk). The pages for The
disk, Browse and Mess were built against stand-ins for `lsblk` and
`recipe storage` (below) and never on a real desktop.

**recab compare has only run on sample trees.** It answers in the shape written down
under "What it asks of recab"; the two-root form, which mounts the other partition, has never
run on an A/B computer with this window. The stories use `fixtures/recab`, which speaks
`status` (schema 11) and `compare` without a disk.

## What a person sees
### The disk (the first page)

- **The heading** is true of what `statvfs` says for the disk the home folder is
  on: "The place for your files is nearly full" (under 5 GiB or a tenth free) or
  "has room", with how much is left of how much. Never counted from the list.
- **What can be freed**, rows with a size, a sentence and a button:
  *Known leftovers* (the Mess list: the same figure in the same number of
  places; the button, "Look at the list", goes to Mess); *Kept by Recipe* (`earlier_bytes` from
  `recipe storage --json`, asked as the person; the button starts the Recipe app
  through the frame's `startApp`; no row when Recipe cannot be asked or says
  zero). There is no row
  for logs and crash records: they cannot be measured without root.
- **Every partition of this disk**, from `lsblk --json --bytes` (`DISK_GUI_LSBLK`)
  for the disk that holds the home folder (found by its device, else by what is
  mounted at the home folder's filesystem). Which partition is the running
  root and which the other is comes from recab's status (`booted`, `other`) and from where
  the roots are mounted (`/`, and `other_mount`), or from the last comparison's `device`
  fields when it was made under the same booted root: "Root A — booted, locked", "Root B —
  other, unlocked". Used and free are `statvfs` at the
  mount point, so a partition that is not mounted (the other system's) says "not
  mounted, so not read"; when a comparison was kept, the other system's row adds
  what its files took then. Start-up files, swap and the home partition are named
  by what lsblk says (partition type, filesystem, mount point); anything else is
  listed by its label, never guessed.
- **Whose room it is**: a bar and a legend from the scan, in kinds the scanner
  really tells apart: files in the home folder, what programs keep (hidden
  folders of the home folder), known leftovers, other folders
  read. They add up to what the scan read. How many folders could not
  be read is said; their size is not known and is not guessed. Where lengths and
  room taken are counted alike, it also says how much of what the disk counts as
  used lies outside the folders read.
- "Look again" reads the scan, the partitions, Recipe's storage again.

### Browse

The folder being looked at: a path in which every part can be clicked, "Up", a
search that filters this folder; the folders as rows (size, share bar) beside
the map; the views "Folders", "Biggest files" (files of 1 MiB or more, which is
all the scanner records one by one) and "Not used for a year" (files of 1 MiB or
more not opened or changed for a year, from the later of last read and last
change, as the scanner records). The panel for the chosen place says what the
scan recorded (size, files, last used), what the rules say it is, and offers:
"Go in", "Open in Files" (`QDesktopServices`, or the program in `DISK_GUI_FILES`),
"Copy the path", "Add to the list" (put a place the rules did not name on the
list as "Picked by you"), "Take off the list", and "Never suggest this" (the
place leaves the list at once and is kept in `<config>/levitate/disk/never-suggest.json`;
the rules never name it or anything inside it at any later look; Mess lists these
with "Suggest it again"). A second look keeps the person where they were.

### Mess

The list in the mock-up's layout: kinds with their one-line meaning, each place
with its size, when it was last used (the latest read or change of anything
inside it) and the rules' verdict (`safe to remove`, `check first`, `left
alone`); a panel for the chosen place ("Show in Browse", "Open in Files", "Take
off the list", "Never suggest this"); "What you told it" (where it looked, what
is never suggested); "Tick only what is safe to remove"; and under it how many
places are ticked, how much that is and how much of it is safe to remove, with
**"Copy for agent"**: one click puts the ticked places on the clipboard as
plain text (path, size, last used, what it is, why it can go, the verdict), for
the person to paste where they choose. Nothing else happens. The map moved
to Browse; so did "Add to the list".

The rail entry "Mess" carries a badge: how many places of mess are on the list.

The status line says when the disk was looked at. "Read again" in the status
line, F5 and Ctrl+R look at the disk again on The disk, Browse and Mess, and ask
recab again on the two roots. Coming back to the window after a while asks recab
again; the disk is only looked at again when asked, since that takes a while and
drops what was ticked.

The fourth entry, only on a computer with two roots:

1. **What is there.** On opening, `recab status --json` is asked which root is booted
   (quick, no password): the page names both ("Root A", "Booted — locked"; "Root B",
   "Other — unlocked") and shows two empty frames. If a comparison was kept from
   before, it is shown instead, with when it was made; when another root is booted
   than at that comparison, it says so and offers to compare again. "Open Recab…"
   starts the Recab window, where the other root is copied and started.
2. **Compare.** "Compare" first shows the shared Before page: both roots are
   read from start to end, neither is changed, it takes a few minutes, whether the
   administrator's password is asked (it follows the root helper this window
   really uses) and the command that will run. Cancel compares nothing. Then both
   roots are read from start to end, which takes minutes, and with a root
   helper the administrator's password is asked; without one it runs as the person
   and the pages say so (the page before comparing says "no password is asked",
   the frame while it waits says "Waiting"). The page stays: each frame fills as
   its side is read, and Stop ends it.
3. **How alike they are.** A headline ("The two roots are identical", or
   "99.9% the same", by room taken), how many files differ, and which root is
   booted and which is the other.
4. **The two roots as blocks, side by side.** Two maps, the booted root left
   and the other right, drawn from one layout: every folder is a block in the
   same place on both sides, so two roots that are the same look the same.
   A block that holds files that differ is coloured, with the number of files
   on it: only on this side, different content, or same content with other
   settings. A block where only the other side has something more is outlined
   with dashes. A block that differs is drawn at least big enough to see,
   however little room the difference takes. A click on a block marks it on
   both sides and says whether it is the same in both.
5. **The list** shows every difference by folder. Files that differ by design
   (each root names its own partition) are counted apart.
6. **Copy for agent.** Under the list, one click puts the differences on the
   clipboard as plain text (the issue and the facts: which root is which,
   what is the same, the differences by folder, the biggest ones by name). A
   comparison that failed says why, with a "Copy for agent" button that copies
   the full error text, as does the page when the two roots could not be read.

## Help and the first page

Both are the frame's (`gui/kit/README.md`, "Help" and "The first time"); the window
adds nothing for them: every link of Help goes to a rail entry (The disk, Browse, Mess,
and The two roots where there is one). Help for Recdirstat has the idea of mess and what it
can do (see how full the disk is, find what takes the room, use "Copy for agent" on the mess, and on a
computer with two roots compare them). The first page comes once per person the first time Recdirstat opens
(never in a picture run). Both stories check them (`../kit/helpstory.*`; pictures `h-help*`,
`h-help-went`, `h-firstrun`).

## What counts as mess

The rules are in `src/mess.cpp`. A place smaller than 20 MiB is not named. A place the person said never to suggest, and anything inside it, is not named. Recipe's kept earlier versions (`package-generations`) are no longer a line here: they are the "Kept by Recipe" row on The disk, from Recipe's own number, so they are not counted twice.

| Kind | What | Verdict |
|---|---|---|
| Caches | each folder in `~/.cache`; any folder with a `CACHEDIR.TAG` | safe to remove |
| Build leftovers | `target` beside a `Cargo.toml`, and any tagged folder that looks like Rust output; a folder with a `CMakeCache.txt`; `node_modules` beside a `package.json` | safe to remove |
| Recipe's own | `~/.cache/recipe`, `~/.local/state/recipe`, `/var/lib/recipe/state` | left alone: listed, nothing to tick; they belong to Recipe, which has no command to clear them yet |
| The trash | `~/.local/share/Trash` | safe to remove |
| Old downloads | archives, images and packages in `~/Downloads` not opened for 90 days | check first |
| Large and untouched | a file of 256 MiB or more, not opened or changed for a year | check first |
| Picked by you | whatever the person added from the map | check first |

Build rules do not apply inside `~/.local/share`, `~/.config` and `~/.var`:
what a program keeps there for itself is not a leftover.

The scanner (`src/scan.cpp`) only reads. It does not follow links, does not
leave the filesystem it started on, counts a file with several names once, and
counts the room a file takes on disk, not its length.

## Running it from a checkout

```sh
cmake -S gui/disk -B .artifacts/disk-gui/build -G Ninja
cmake --build .artifacts/disk-gui/build
```

On a sample home folder, with the stand-ins:

```sh
A=$PWD/.artifacts/disk-gui; F=$PWD/gui/disk/fixtures
$F/make-sample $A/sample
DISK_GUI_ROOTS=$A/sample DISK_GUI_HOME=$A/sample DISK_GUI_APPARENT=1 \
  DISK_GUI_LSBLK=$F/lsblk DISK_GUI_RECIPE=$F/recipe DISK_GUI_RECAB=$F/recab \
  $A/build/disk-gui --look levitate
```

Window 1100 x 700 the first time, then the size it had when it closed
(`rememberWindowSize` in `gui/kit`; the picture modes always use 1100 x 700).

### The story

`--shots FOLDER` looks, saves pictures, checks expectations and compares the two
roots with the stand-in. It ends with "all as expected" or says "not as
expected: ..." for each thing that failed, and exits non-zero. It needs no
display, and no real recab, recipe, lsblk or administrator.
**`--shots` refuses to start (exit code 2, and a sentence naming what is missing)
unless every program it could run and every folder it could write is named in the
environment**: `DISK_GUI_RECAB`, `DISK_GUI_CMDLINE`, `DISK_GUI_RECIPE`, `DISK_GUI_FILES`,
`DISK_GUI_LSBLK`, `DISK_GUI_ROOTS`, `DISK_GUI_HOME`, `DISK_GUI_ROOT_HELPER` (empty is a
name), and `HOME` (not your own) and the XDG folders in scratch space (the command
below names them all). The story runs itself bare to check that.

Every Before page of the shared layout is checked too (comparing): what it says, that
Cancel leaves everything as it was (nothing compared), and that its button does it.
Pictures `1-disk`, `2a`..`2f` Browse, `3a-mess`, `3c-mess-told`, `3-picked`,
`5-systems`, `5a-before-compare`, `5b-comparing`, `6-compared`, `6b-disk-compared`,
`6c-stale`, `h-help*`, `h-firstrun`; on a single root `h-help-one-root` instead of the
ones about the two roots.

- the window is 1100 x 700, and **every picture** is taken at that size (no page
  may make the window wider or taller); the rail offers The disk, Browse, Mess,
  The two roots (only with two roots) and Help and nothing else; the window opens on
  The disk; the Mess badge equals the number of places listed, also after a second
  look; the status line says when the disk was looked at.
- **The disk**: the free space and total it names are what `statvfs` says for the
  sample's disk (within 256 MiB, because the disk is shared with other programs
  while the check runs), and the heading follows them; the Known leftovers row is
  the Mess list's figure in the same number of places; Kept by Recipe is what the
  `recipe` stand-in says, and its button starts the Recipe stand-in app; there is
  no row for logs; the partitions are the `lsblk` stand-in's (right disk, the
  other disk's partition left out, the booted root and the other root named
  from recab's status and the mounts, the home partition measured, the unmounted ones "not
  read", sizes); whose room it is adds up to exactly what the scan read; "Look
  again" reads again.
- **Browse**: starts in the home folder; going in and up; the path; search keeps
  what matches and says when nothing does; "Biggest files" starts with the
  biggest file and runs big to small; "Not used for a year" lists exactly the
  one file that old; "Copy the path" (the clipboard), "Open in Files" (a stand-in
  program is handed the folder); the panel's buttons follow whether the place is on the list;
  "Add to the list" and "Take off the list" move the badge; "Never suggest this"
  removes the place at once, writes it to the config file, and a second look
  does not bring it back; Mess says what you told it; "Suggest it again" puts it
  back and clears the file.
- **Mess**: heading, kinds' words, columns, the panel and every action, "Show in
  Browse" lands on the place, "Tick only what is safe to remove" leaves only
  places that are, and "Copy for agent" puts the ticked places on the clipboard.
- **The two roots**: which is booted and which is the other, before and after the
  comparison; the password sentence; the comparison itself, and "Copy for agent" copying
  the differences and starting nothing; "Open Recab…" starting the Recab stand-in; the
  partitions named "Root A — booted, locked" and "Root B — other, unlocked"; a kept
  comparison going out of date when the other root is booted; a status that fails.
- **Whether there are two roots at all**: `Window::hasTwoRoots` is checked for every
  combination (recab runs or not; the command line has `recab.immutable`, has
  `recab.immutable=value`, lacks it, has only words that contain it, cannot be read).
  A second run of the story with a command line without it (below) checks that there
  is no entry, no page, no row in Help, and that the stand-in recab was never run (not
  even on F5).

The story writes the "never suggest" list of the person (and puts it back at the
end), so run it with `HOME` and the `XDG_*` folders in scratch space.

```sh
A=$PWD/.artifacts/disk-gui; F=$PWD/gui/disk/fixtures
S=/tmp/disk-shots   # all state of the check lives here
mkdir -p $S/home; $F/make-sample $S/sample
printf '#!/bin/sh\ntouch "$DISK_STANDIN_SYSTEM_MARK"\n' > $S/system-app; chmod +x $S/system-app
printf '#!/bin/sh\ntouch "$DISK_STANDIN_RECIPE_MARK"\n' > $S/recipe-app; chmod +x $S/recipe-app
printf '#!/bin/sh\nprintf %%s "$1" > "$DISK_STANDIN_FILES_MARK"\n' > $S/files-app; chmod +x $S/files-app
mkdir -p $S/run; chmod 700 $S/run
export HOME=$S/home XDG_CONFIG_HOME=$S/home/config XDG_STATE_HOME=$S/home/state \
  XDG_CACHE_HOME=$S/home/cache XDG_DATA_HOME=$S/home/share XDG_RUNTIME_DIR=$S/run
QT_QPA_PLATFORM=offscreen DISK_GUI_ROOTS=$S/sample DISK_GUI_HOME=$S/sample DISK_GUI_APPARENT=1 \
  DISK_GUI_RECAB=$F/recab DISK_GUI_ROOT_HELPER= DISK_GUI_CMDLINE=$F/cmdline-ab \
  DISK_STANDIN_LOG=$S/recab-log DISK_STANDIN_SPEED=0 DISK_GUI_KEPT=$S/kept.json \
  LEVITATE_APP_SYSTEM=$S/system-app DISK_STANDIN_SYSTEM_MARK=$S/system-app-started \
  DISK_GUI_LSBLK=$F/lsblk DISK_STANDIN_LSBLK_MOUNT=$(df --output=target $S/sample | tail -1) \
  DISK_STANDIN_LSBLK_HOME_SIZE=$(df -B1 --output=size $S/sample | tail -1 | tr -d ' ') \
  DISK_GUI_RECIPE=$F/recipe LEVITATE_APP_RECIPE=$S/recipe-app DISK_STANDIN_RECIPE_MARK=$S/recipe-app-started \
  DISK_GUI_FILES=$S/files-app DISK_STANDIN_FILES_MARK=$S/files-app-asked \
  $A/build/disk-gui --look levitate --shots $S/pics
```

`$F/cmdline-ab` is a kernel command line with `recab.immutable` (two roots); a second run
with `DISK_GUI_CMDLINE=$F/cmdline-single` (no `recab.immutable`) is the single-root story.
Then `DISK_STANDIN_LOG` must not exist at the end: recab was not run.

Without the settings it reads the real home folder and compares nothing.

| Setting | What | Default |
|---|---|---|
| `DISK_GUI_ROOTS` | the folders to read, separated by `:` | the home folder, and `/var/lib/recipe` |
| `DISK_GUI_HOME` | which folder the home rules apply to | the home folder |
| `DISK_GUI_APPARENT` | `1`: count each file's length (the sample is made of empty-but-long files) | off |
| `DISK_GUI_RECAB` | the program asked for the roots' status and to compare them | `recab` |
| `DISK_GUI_CMDLINE` | the kernel command line that is searched for `recab.immutable` | `/proc/cmdline` |
| `DISK_GUI_ROOT_HELPER` | what runs it as root; empty: run it as the person | `pkexec` |
| `DISK_GUI_KEPT` | the file the last comparison is kept in | `comparison.json` in the person's cache |
| `DISK_GUI_LSBLK` | the program that lists the disk's partitions, asked `--json --bytes -o ...` | `lsblk` |
| `DISK_GUI_RECIPE` | the program asked `storage --json` for what Recipe keeps | `recipe` |
| `DISK_GUI_FILES` | a program started with a folder instead of the desktop's file manager (for checks) | the desktop's own |

The stand-ins, all in `fixtures/`:

- `lsblk` answers in lsblk's real JSON shape for two disks. The home folder's
  partition is mounted at `DISK_STANDIN_LSBLK_MOUNT` with
  `DISK_STANDIN_LSBLK_HOME_SIZE` bytes; nothing else is mounted, so no real disk
  of the computer it runs on is mistaken for one of the partitions.
  `DISK_STANDIN_LSBLK_FAIL=1` makes it fail.
- `recipe` answers `storage --json` (`DISK_STANDIN_EARLIER` bytes, default 6 GiB).
- `recab` reads no disk and speaks `status --json` (schema 11) and `compare --json
  --machine-events` only. `DISK_STANDIN_SCENARIO` picks what it says: `normal` (A booted),
  `booted-b`, `identical` (the two roots hold the same), `failing`. `DISK_STANDIN_LOG`
  names a file each run appends its arguments to.
- `cmdline-ab`, `cmdline-single`: kernel command lines with and without `recab.immutable`.

## What it asks of recab

`recab status --json` (schema 11), for which root is booted and which is the other:
`booted`, `other`, `other_mount`. The partitions come from where `/` and `other_mount` are
mounted, or from the last comparison's `device` fields. A comparison made while another root
was booted is shown as out of date.

And one command, run as root, that reads both roots and changes neither:

```
recab compare --json --machine-events
```

While it reads, it reports on its standard error with the events below. The
real recab sends `plan`, `step` and `done`, and no `progress`: the bar moves
without saying how far it is. A failure is recab's one JSON error object.
When done it writes one JSON object on its standard output:

| Field | What |
|---|---|
| `schema_version` | `1` |
| `running`, `other` | the booted root and the other root, each: `slot`, `device`, `files` (files and links, not folders), `bytes` (each hard-linked file once) |
| `same` | `files` and `bytes` that are the same in both |
| `folders` | every folder down to the depth recab chooses (three names below `/`), each with its whole size on both sides: `path`, `running_bytes`, `other_bytes`. The maps are drawn from this |
| `own` | paths that differ by design, because each system names its own partition; not counted as differences |
| `differences` | one per path: `path`, `change`, `running_bytes`, `other_bytes`, and `detail` for `metadata`. A folder that only one system has stands for all it holds |
| `more` | how many differences were left out of the list, if it was cut |

`change` is `only-running`, `only-other`, `content` (the content differs) or
`metadata` (same content; owner, mode, links or attributes differ, said in
`detail`). This is what `inventory` already reads for one system, taken for
both and set side by side.

## The source

| File | What |
|---|---|
| `src/scan.*` | reads a folder tree from the disk into memory |
| `src/mess.*` | the rules, and the finding for something picked by hand |
| `src/map.*` | the map of blocks, used for the mess and for each of the two roots |
| `src/disk.*` | The disk: measuring free space, reading lsblk's answer, the page, the split of whose room it is |
| `src/browse.*` | Browse |
| `src/pages.*` | the Mess list and its "Copy for agent" |
| `src/rules.*` | the places the person said never to suggest (their config folder) |
| `src/systems.*` | the comparison of the two roots: what recab answered, and its page |
| `src/window.*` | the frame, its four entries and the flows between them |
| `src/shots.*` | the picture mode and flow check |
| `fixtures/` | the stand-ins (lsblk, recipe, recab) and the sample home folder |
| `data/` | the menu entry |
| `../kit/` | the frame (rail, status line), shared widgets, the page base, the command runner |

## Not done

- Logs and crash records (the mock-up's third row), and any figure for what could
  not be read without the administrator's password, are not on The disk: neither
  can be measured as the person, so nothing is claimed. Only the number of
  folders the scan could not read is said.
- The kinds of file in Browse ("By kind of file") are not there: the scanner does
  not record file kinds.
- "Never suggest this" is a list of whole places; there is no rule language
  ("anything named *.qcow2 older than 90 days"), and no list of places not to
  look at.
- The partition table lists what `lsblk` lists below the disk. LVM, LUKS and
  btrfs layouts were not tried (the stand-in has plain partitions); an unlisted
  device shows by its label or filesystem. The lsblk columns `MOUNTPOINTS`
  (util-linux 2.37 or later) and a failing lsblk were not run against a real one.
- Choosing the other entry while a comparison reads is allowed (the page keeps
  filling).
- Checked only offscreen, with stand-ins; not on a real desktop, and the
  link buttons, the Recipe app and the Recab app were not started for real.
- A comparison of a real pair of roots: `recab compare` has only run on
  sample trees. It says no byte progress, so the bar cannot say how far it is.
- The kept comparison is one file in the person's cache
  (`DISK_GUI_KEPT`); nothing checks that it belongs to this computer's disks
  beyond which root was booted.
- Seeing what changed inside a file whose content differs; searching the list of
  differences.
- Recipe's own folders are left alone because Recipe has no command that clears
  them (`recipe cleanup NAME` is for one recipe's build files, `recipe storage
  --clear-earlier` for earlier versions). When it has one, Recdirstat can offer to clear them through
  Recipe. The real `recipe storage --json` was not run here: the shape (`earlier_bytes`,
  `earlier_generations`) was read from `tools/recipe` and `gui/recipe`.
- Mess inside the roots. The map reads only the home folder and
  `/var/lib/recipe`; the two roots are compared with each other, not searched
  for leftovers.
- In an image: `distro/builder/os-recipes/desktop-apps.rhai` builds and installs it with
  the other two, and the contract's `DESKTOP_APPS` lists it; no image with it has been built.
