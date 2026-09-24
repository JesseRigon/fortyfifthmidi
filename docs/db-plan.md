# Persistent storage: plan

Status: **proposal, nothing implemented.**

## What this is for

Two things the host cannot give us:

1. **Saved progressions** that outlive a session and are shared across every
   host. A progression written in Ardour should be loadable in Reaper.
2. **User preferences** that should be the same in every new instance — the
   default key, glide mode, keyboard bindings, pedal action.

Everything else already works and should stay as it is.

## What we must NOT move into a database

DPF state is how a host saves a plugin instance inside a project. That is the
right mechanism and it stays:

- The progression **currently in the grid** — it belongs to the project, not
  to the user. Reopening a project must restore what that project had, not
  whatever was last saved globally.
- Per-instance settings: octave, current key, glide mode, the grid.

The database is for a **library** the user draws from, plus the **defaults** a
new instance starts at. If the two ever disagree, the project wins.

## Where the file goes

Not beside the plugin binary — VST3 and CLAP folders are frequently read-only,
and on Windows they sit under `Program Files`.

| OS      | Path                                                  |
|---------|-------------------------------------------------------|
| Windows | `%LOCALAPPDATA%\FortyFifthMidi\fortyfifth.db`         |
| Linux   | `$XDG_DATA_HOME/fortyfifthmidi/fortyfifth.db`, falling back to `~/.local/share/...` |
| macOS   | `~/Library/Application Support/FortyFifthMidi/fortyfifth.db` |

Created on first write, not at startup. A plugin that creates files merely by
being scanned is a plugin that annoys people.

## Schema

```sql
PRAGMA user_version = 1;          -- migration marker

CREATE TABLE setting (
    key    TEXT PRIMARY KEY,
    value  TEXT NOT NULL
);

CREATE TABLE progression (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    grid        TEXT NOT NULL,     -- the same string pushProgression() builds
    grid_beats  INTEGER NOT NULL,  -- 16, 32 or 64
    created_at  INTEGER NOT NULL,  -- unix seconds
    updated_at  INTEGER NOT NULL
);
```

`grid` deliberately reuses the existing wire format
(`"4|0.0.0,5.2.0;2|..."`). It already round-trips through
`pushProgression()` and `parseProgression()`, both of which are tested, so
saving is one call and loading is one call. A second serialisation would be a
second thing to keep in step.

`user_version` is how a future schema change is detected without a separate
migrations table.

## Threading

**The audio thread never touches SQLite.** No file I/O, no allocation, no
locks on the audio thread — that rule does not bend for convenience.

All database work happens on the UI thread, in response to a click. Loading a
progression decodes into the UI's `fProg`, then goes to the DSP by the
existing `setState("progression", ...)` path, which is already safe.

This means no new concurrency problems: the database sits entirely on the side
of the code that already runs on the UI thread.

## How SQLite gets built

Amalgamation (`sqlite3.c` + `sqlite3.h`) vendored into `src/sqlite/`, compiled
into the plugin. One file, public domain, no package dependency, and it
cross-compiles to MinGW the same way the rest does.

Built with:

```
SQLITE_OMIT_LOAD_EXTENSION
SQLITE_THREADSAFE=1
SQLITE_DQS=0
SQLITE_DEFAULT_MEMSTATUS=0
```

Adds roughly 700 KB to each binary. Worth it for not requiring the user to
install anything.

Alternative considered and rejected: a JSON or INI file. It would be smaller,
but concurrent instances would clobber each other's writes — two plugin
instances in one project is normal, and SQLite's locking handles that whereas
a naive file rewrite does not.

## UI

On the PROGRESSIONS screen, the left-hand LOAD column gains:

- **SAVE** — names the current grid and stores it. Saving over an existing
  name asks first.
- A **user list** under the built-in presets, visually separated, each row
  loading on click.
- **Rename** and **delete** per row, reusing the existing right-click-free
  pattern — a small button, like the section rows already have.

Built-in presets stay in the binary. They are not user data and should not be
deletable.

## Settings that become user defaults

A "save as default" action on the Setup screen writes the current keyboard
bindings, pedal action, key, glide mode and bass-note choice to `setting`. New
instances read those in the constructor, before the host applies any project
state — so a project still overrides them, which is the correct precedence.

## Phasing

1. **Vendor SQLite, open/create the database, `PRAGMA user_version`.** Nothing
   user-visible. Verifiable by the file appearing where it should and nowhere
   else.
2. **Save and load progressions**, with the UI above. This is the part that was
   actually asked for.
3. **User defaults** for bindings and preferences.
4. *(Later, if wanted)* History — a table of recently played progressions, or
   an undo log. Deliberately not in scope now; the schema above does not
   prevent it.

## Open questions for you

- **Sharing.** Should a saved progression be exportable as a file, so it can
  be sent to someone else? That changes nothing structurally but adds an
  import/export path worth knowing about before I build the UI.
- **Naming.** Auto-name from the degrees (`I-V-vi-IV`) with the option to
  rename, or always prompt?
- **Scope of "user settings".** I listed bindings, pedal, key, glide and bass
  note. Anything you would add or leave out?
