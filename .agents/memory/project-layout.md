---
name: Project layout
description: Where the GD Ghost Bot Mod source files live in the repo.
---

The `GD-Ghost-Bot-Mod` directory was deleted and the source moved to the repository root. The main file is now `src/main.cpp`, not `GD-Ghost-Bot-Mod/src/main.cpp`.

**Why:** The repo was reorganized so it matches a standard Geode mod layout at the root level.

**How to apply:** Always operate on `src/main.cpp` and the root-level `mod.json` / `CMakeLists.txt`. Do not recreate the `GD-Ghost-Bot-Mod` subdirectory unless the user explicitly asks to move things back.