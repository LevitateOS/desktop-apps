# updater-gui

The desktop window "Updater", for **mutable installs only** (one root, no A/B). One page. It shows the
installed release, checks whether a newer one exists, installs it into the one root and asks for a
restart. Qt 6 Widgets, C++20, CMake; the shared widgets are in `../kit`. On an A/B system the Recab app does
this, and the Updater is not installed there (`docs/release-engineering.md` section 6). No agent inside:
only "Copy for agent" on a failure.

## What a person sees

- **The first screen**, after "Reading the installed release": "Installed release" (for example
  "LevitateOS 1.0"), "Updates" with "Check for updates", and until an install has finished through this
  app, "This system has one root: there is no second root to fall back to. The previous kernel stays as a
  second boot entry."
- **Check for updates** runs `recupdate check --json` (kit `release.*`): checking, up to date, "Release 1.1
  is available" with its date, download size and a link to what is new, or the failure with "Copy for
  agent". What the daily check wrote is read at start.
- **Install <version>** asks first (the Before page): the release is downloaded and checked before any file
  changes, it is installed into the one root, running programs keep old files until restarted, the
  one-root sentence above, the password, the command `recupdate apply`. Then `recupdate apply --json` runs
  through the root helper and the page (kit `install.*`) shows the steps and the progress live and the result
  at once. On success it offers "Restart now" (or "Later"); the first screen then keeps a "Restart to use
  release X" notice. On failure it shows the error and says some files may be new and some old and that the
  update can be finished.
- **Finish the update**: when `recupdate check --json` answers `"interrupted": {...}` (an earlier apply did not
  finish), the first screen starts with "An earlier update did not finish" and "Finish the update", which runs
  `recupdate apply --json` again; it resumes by itself.
- **Restart now** asks first, then runs `reboot` through the root helper (the same restart as the Recab
  app); a refusal shows its reason with "Copy for agent".
- F5, Ctrl+R, "Read again" and coming back to the window after twenty seconds read the installed release and
  the daily check's file again (never the network; only the button checks).

## What it asks of the system

| Command | When |
|---|---|
| `recupdate check --json` | Check for updates, as the person |
| `recupdate apply --json` | install or finish, through the root helper; JSON lines on standard output |
| `reboot` | Restart now, through the root helper |

Privileged: `pkexec recupdate apply --json` and `pkexec reboot`. `data/org.levitateos.updater.policy` has one
action, **`org.levitateos.updater.run`**, for `/usr/bin/recupdate`.

| Setting | What | Default |
|---|---|---|
| `RECUPDATE_BIN` | the program that is `recupdate` | `recupdate` |
| `RECUPDATE_STATE_DIR` | where `available.json` is | `/var/lib/recupdate` |
| `RECUPDATE_RELEASE_FILE` | the release the system runs | `/usr/lib/levitate/release.json` |
| `UPDATER_GUI_ROOT_HELPER` | what runs apply and the restart as root; empty: as the person | `pkexec` |
| `UPDATER_GUI_REBOOT` | the restart | `reboot` |

## Build

The Updater has no story mode; `-DLEVITATE_STORIES=OFF` (what the OS build passes to every app) is accepted and changes nothing here.

```sh
cmake -S updater -B build/updater -G Ninja
cmake --build build/updater
```

## Installed files (for mutable installs only)

| Path | Mode |
|---|---|
| `/usr/bin/updater-gui` | 0755 |
| `/usr/share/applications/org.levitateos.updater.desktop` | 0644 |
| `/usr/share/icons/hicolor/scalable/apps/org.levitateos.updater.svg` | 0644 |
| `/usr/share/polkit-1/actions/org.levitateos.updater.policy` | 0644 |

## The source

| File | What |
|---|---|
| `src/client.*` | the only place that runs `recupdate apply` and the restart |
| `src/pages.*` | the first screen and the restart problem |
| `src/window.*` | the window: reading, install, finish, restart, the Before page |
| `../kit/release.*`, `../kit/install.*` | the check and the live install page |
| `data/` | the menu entry and the polkit action |

## Not done

- No story (`--shots`) and no stand-in programs: the owner's rule was to write code only.
- Not run: the app was never started, nothing was shown or photographed; it compiled and nothing more.
  `recupdate`, `recab update` and `recupdate apply` did not exist when it was written: it follows
  `docs/release-engineering.md` section 10 only.
- That an install finished through this app (the one-root sentence on the first screen) is kept in QSettings, per
  person; an update made elsewhere does not count.
