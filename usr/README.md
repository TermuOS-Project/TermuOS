# TermuOS userspace (`usr/`)

Tree for userspace libraries, binaries, and data.

| Path    | Purpose                                      |
|---------|----------------------------------------------|
| `bin/`  | Installed executables (`.tsys`, etc.)        |
| `lib/`  | Userspace libs (or docs pointing at `tsys/lib`) |
| `share/`| Icons, fonts, assets                         |
| `src/`  | Optional git submodules (e.g. Luna)          |

**Luna** (desktop/compositor) lives in a **separate repository**.
This tree only receives the built `luna.tsys` (or a submodule under `src/luna`).

Small tools today still build from `tsys/`; they may move under `usr/` later.

Kernel does not embed the Luna UI long-term; it exposes fb/input via syscalls.