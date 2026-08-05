## Why

`UndoRedo::StackImpl::has_undo_snapshot()` / `has_redo_snapshot()` — the functions `Plater::can_undo()` / `can_redo()` delegate to for every Undo/Redo affordance in the app (topbar buttons, Edit menu items, Ctrl+Z/Ctrl+Y) — are structurally wrong from the very first snapshot ever taken in a session. They were never noticed because, until `fix/topbar-undo-redo-state` (a separate, already-landed change), the topbar buttons were hardcoded to "enabled whenever the 3D Editor tab is active" and never actually queried these functions; the Edit menu items do query them via `wxUpdateUIEvent`, but nobody apparently paid close attention to their enabled state either. Now that the topbar honestly reflects `can_undo()`/`can_redo()`, both controls show Undo/Redo as permanently available even in a brand-new, untouched project, and even after undoing all the way back to the start — because the underlying query is broken, not because there's anything real left to undo/redo.

This is core, technology-agnostic code (`src/slic3r/Utils/UndoRedo.cpp`) used by every printer technology and every gizmo, so a fix here has wide blast radius and needs a deliberate design, not a quick patch — hence a dedicated change, separate from the topbar fix.

## What Changes

- **Root-cause the topmost-placeholder misclassification**: `StackImpl::take_snapshot()` pushes two entries per user action — the named snapshot, and a `"@@@ Topmost @@@"` placeholder representing "now" — and the placeholder reuses the caller's `snapshot_data` verbatim, so it inherits the same `snapshot_type` (e.g. `Action`, `ProjectSeparator`) as the action that was just taken. `has_undo_snapshot()`/`has_redo_snapshot()` classify "does this range contain a project-modifying entry" purely by `snapshot_type`, without excluding the topmost placeholder (`Snapshot::is_topmost()`) or the entry at the active position itself — so:
  - `has_undo_snapshot()` walks `[begin, active)` (**exclusive** of the active/topmost entry) and counts the very first real snapshot in history as always-undoable, even when the active position is already sitting right after it (nothing legitimate left to undo).
  - `has_redo_snapshot()` walks `[active, end)` (**inclusive** of the active/topmost entry itself) and counts the placeholder's own inherited type as a redo target, even though it represents "no action taken since here", not a real forward step.
  - Net effect: as soon as any snapshot has ever been taken, both functions report `true` forever, regardless of the actual undo/redo boundary.
- **Fix the classification** so `has_undo_snapshot()`/`has_redo_snapshot()` correctly answer "is there a real, distinct state to jump to in this direction", using `Snapshot::is_topmost()`/`is_topmost_captured()` (or an equivalent structural check) to exclude the placeholder/self-referential cases described above.
- **Audit sibling functions for the same bug pattern**: `has_undo_snapshot(size_t time_to_load)` and any other range-based classification in `UndoRedo.cpp` that walks snapshots relative to `m_active_snapshot_time` (see design.md for the full list) — determine whether each one has the same off-by-boundary issue and needs the same fix or is unaffected.
- **No behavior change to snapshot recording itself** — `take_snapshot()`'s two-entries-per-action structure, `is_topmost()`/`is_topmost_captured()` semantics, and existing serialized snapshot data are not being redesigned; this targets only how existing stack state is *read* for the undo/redo-availability query.

## Capabilities

### New Capabilities

- `undo-redo-availability-query`: `has_undo_snapshot()` / `has_redo_snapshot()` (and any sibling range-based availability queries in `UndoRedo::StackImpl` found to share the bug) correctly report whether a real, distinct snapshot exists to undo/redo to — excluding the topmost placeholder entry and the entry at the active position itself from being misclassified as a valid target purely because it inherited a project-modifying `snapshot_type` tag.

### Modified Capabilities

*(none — no existing spec in `openspec/specs/` currently documents undo/redo stack behavior; this introduces the first one)*

## Impact

- **Affected code**: `src/slic3r/Utils/UndoRedo.cpp` (`StackImpl::has_undo_snapshot()`, `has_redo_snapshot()`, and any sibling functions identified during the audit). Likely also `src/slic3r/Utils/UndoRedo.hpp` if a new helper predicate is added to `Snapshot`.
- **Blast radius**: every printer technology (FDM + SLA) and every gizmo — this is the single shared query both the topbar buttons and the Edit menu's Undo/Redo items already read from (`Plater::can_undo()`/`can_redo()`), so nothing app-specific needs to change to pick up the fix once this lands.
- **Not in scope**: `fix/topbar-undo-redo-state` (the topbar-sync fix that surfaced this bug) is a separate, already-committed change and is not touched here. `take_snapshot()`'s recording logic, `is_topmost()`/`is_topmost_captured()`'s existing semantics, and the on-disk/serialized snapshot format are not being redesigned — only the read-side availability query.
- **Compatibility**: needs explicit verification that the fix doesn't change behavior for in-flight/already-open sessions or any serialized undo/redo state carried across a save/load-project boundary (see design.md and tasks.md for the compatibility check).
- **Testing**: needs both Layer 1 (automated, `tests/libslic3r`) coverage of the corrected boundary logic and a Layer 2 manual matrix, following the pattern already established by this repo's other undo/redo changes (e.g. `fix-sla-undo-redo`, `resin-mode-scoped-undo-redo`).
