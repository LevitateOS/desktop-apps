# recab-gui

The desktop window for recab, titled "Recab". One window, one page. It shows
the two roots of an A/B computer and has three actions. Qt 6 Widgets, C++20,
CMake; the shared widgets are in `../kit`. The rules it follows are the settled
design in `docs/product-handoff.md` (sections 2.2 and 2.8): the booted root is
locked, the other root is unlocked and is where changes are made, and the
product GUI has no agent inside, only "Copy for agent" on failure and output
views.

## What a person sees

**The two roots**, side by side, booted root first. Each card has the root's
letter ("Root A"), then "Booted — locked" or "Other — unlocked". The other root
also says where it is mounted ("Mounted at /run/recab/other.") or "Not mounted.",
and "It cannot be started: a copy into it did not finish." when `recab status`
says `other_bootable` is false. The root that starts at the next boot (the
one-time pick if there is one, else the boot default) says "Starts at the next
boot." or "Starts at the next boot, once."; when the boot default names neither
root the page says "Neither root is the boot default."

**Three buttons**, each a plain button:

- **Make the other root a copy of the booted root.** It asks first (the shared
  Before page): the other root is overwritten and everything on it is lost,
  including software Recipe installed into it; it cannot be started until the
  copy has finished; the booted root is not changed; whether the administrator
  password is asked; the command, `recab copy`. "Overwrite root B" runs it, and
  the page shows recab's own words as they come, then "It is done" with recab's
  result, or "The copy stopped" with the reason. Both views have "Copy for agent"
  (the reason, then the whole output) and "Back".
- **Start the other root at the next boot** (`recab boot-other`), which becomes
  **Do not start the other root at the next boot** (`recab boot-other --cancel`)
  while a one-time pick is set. When the other root cannot be started the button
  is disabled, with a one-line reason under it. Taking the pick back stays enabled.
- **Restart now.** It asks first, says which root starts, then runs `reboot`
  through the root helper. A refused restart shows its reason with "Copy for agent".

**Reading.** The roots are read without holding the window up: the page says
"Reading the two roots" with a moving bar first. F5, Ctrl+R, "Read again" in the
status bar and coming back to the window after twenty seconds read them again. If
`recab status` is refused for lack of rights, the page says so and, when there is a
root helper, offers "Read it as administrator". Any read that fails says why and
has "Copy for agent" and "Try again". The window opens at the size it had.

## What it asks of recab

| Command | When |
|---|---|
| `recab status --json` | to read (schema 11: `booted`, `other`, `other_mount`, `other_bootable`, `boot_default`, `next_boot_once`); as the person, and through the root helper after "Read it as administrator" |
| `recab copy` | the copy, through the root helper. Not `--json`: without it recab says what it is doing on standard error, which is the progress |
| `recab boot-other [--cancel] --json` | the pick, through the root helper |
| `reboot` | Restart now, through the root helper |

The root helper is `pkexec` (`RECAB_GUI_ROOT_HELPER`; empty runs the command as
the person). `data/org.levitateos.recab.policy` has one action, for `/usr/bin/recab`:
polkit names a program, not its arguments, so it covers the three recab commands.

## Running it from a checkout

From this repository's root:

```sh
cmake -S recab -B build/recab -G Ninja
cmake --build build/recab
S=/tmp/recab-shots; mkdir -p $S/home $S/run; chmod 700 $S/run; F=$PWD/recab/fixtures
RECAB_STANDIN_STATE=$S/state.json RECAB_GUI_RECAB=$F/recab RECAB_GUI_REBOOT=$F/reboot \
  RECAB_GUI_ROOT_HELPER= RECAB_STANDIN_REBOOT_MARK=$S/rebooted \
  build/recab/recab-gui --look levitate
```

| Setting | What | Default |
|---|---|---|
| `RECAB_GUI_RECAB` | the program that is `recab` | `recab` |
| `RECAB_GUI_ROOT_HELPER` | what runs recab and the restart as root; empty: as the person | `pkexec` |
| `RECAB_GUI_REBOOT` | the restart | `reboot` |

### The stand-ins (`fixtures/`)

- `recab` speaks exactly `status --json`, `copy` and `boot-other [--cancel] --json`,
  in recab's JSON (schema 11) and words, and reads no disk. Its state is the one file
  `RECAB_STANDIN_STATE`, made from `RECAB_STANDIN_SCENARIO` the first time, so a copy
  or a pick changes what `status` says next. Scenarios: `normal`, `booted-b`, `once`
  (one-time pick set), `not-mounted`, `not-bootable` (a copy did not finish),
  `no-default`, `copy-failing`, `root-only` (`status` is refused without the helper).
  `RECAB_STANDIN_SPEED=0` removes the waiting.
- `root-helper` stands in for `pkexec` and says it has the administrator's rights.
- `reboot` makes the file `RECAB_STANDIN_REBOOT_MARK`; `RECAB_STANDIN_REBOOT_FAIL=1`
  makes it fail.

### The story

`recab-gui --shots FOLDER` walks every state and every action against the stand-in,
saves a picture at each point, checks what the window says and ends with "all as
expected" or "not as expected: ..." for each thing that failed (exit 1). It needs no
display and no real recab. It refuses to start (exit 2) unless the stand-ins, the
state file, the mark and scratch `HOME` and `XDG_*` folders are named in the
environment; it runs itself bare to check that. Run it with and without the helper
(`RECAB_GUI_ROOT_HELPER=$F/root-helper`, or empty): the sentences about the password
and the "Read it as administrator" button follow it.

It checks: the loading page; normal; the roots when B is booted; a one-time pick
set, made and taken back; the other root not mounted; no boot default; the other
root not startable (button disabled, reason one line); the copy (asks first and says it
overwrites, Cancel copies nothing, progress while it runs, the result, "Copy for agent",
Back); a copy that fails; restart (asks first, Cancel, the restart asked for, a refused
restart); a read refused for lack of rights; F5 reading what changed elsewhere; and that
no word of the old lifecycle and nothing agent-like but "Copy for agent" was ever said.

## The source

| File | What |
|---|---|
| `src/status.*` | `recab status --json` as a struct |
| `src/client.*` | the only place that runs `recab` and the restart |
| `src/pages.*` | the roots page and the output page |
| `src/window.*` | the window: reading, the three actions, the Before page |
| `src/shots.*` | the story |
| `fixtures/` | the stand-ins |
| `data/` | the menu entry and the polkit action |

## Not done

- Not run on a real A/B computer, with real recab, polkit or a real restart; checked
  offscreen against stand-ins only.
- Coming back to the window after twenty seconds (the other way to read again) is not
  exercised by the story.
- The progress of a copy is recab's own lines, with no figure for how far it is.
