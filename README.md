# LevitateOS Desktop Apps

This repository contains the desktop applications and shared components used by LevitateOS. The apps provide graphical interfaces for software management, the two roots, and disk inspection.

## Applications

| Directory | Program | Purpose |
|---|---|---|
| [`recipe/`](recipe/) | `recipe-gui` | Browse software and recipes, inspect what software may use, and install, update, or remove software. |
| [`recab/`](recab/) | `recab-gui` | Show the two roots, A and B (the booted root is locked, the other is unlocked); make the other root a copy of the booted root, start it at the next boot, restart. |
| [`disk/`](disk/) | `disk-gui` | Inspect disk usage, browse files and known leftovers, and, on an A/B computer, compare the two roots. |

The three apps share widgets and window components in [`kit/`](kit/). Each app has its own README with further details, configuration, and checks.

## Build

### Requirements

- CMake 3.21 or later
- A C++20 compiler
- Qt 6.5 or later: Widgets and Svg (`recipe-gui` alone needs only Qt 6.4, so it builds on Debian 12 and Ubuntu 24.04)
- Ninja is optional but used in the example below

Build all applications from the repository root:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

To install, optionally choose an installation prefix:

```sh
cmake --install build --prefix /usr/local
```

Each application can also be configured and built from its own directory. For example:

```sh
cmake -S recipe -B build/recipe -G Ninja
cmake --build build/recipe
```

Refer to the corresponding application README for runtime setup and safe use of its fixture-based checks. In particular, the `--shots` modes can run programs and write state; use the documented stand-ins and scratch directories rather than an ordinary user environment.

## Integration

The apps are plain front ends over their system tools (`recipe`, `recab` and the disk tools). Failure and output views have a "Copy for agent" button, which only puts the text on the clipboard. They can be launched with fixture programs for development; each app README documents the relevant environment variables. Production use may also require the underlying LevitateOS tools and desktop authorization support for privileged operations.

## Project status

The READMEs describe which flows are exercised with stand-ins and which integrations have not yet been verified on a real desktop or system. See each application’s **Not done** section for current limitations and validation status.
