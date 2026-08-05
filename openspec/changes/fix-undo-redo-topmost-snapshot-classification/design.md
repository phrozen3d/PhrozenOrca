## Context

`UndoRedo::StackImpl` (`src/slic3r/Utils/UndoRedo.cpp`) keeps a flat, time-ordered vector of `Snapshot` entries (`m_snapshots`) plus a cursor `m_active_snapshot_time`. Every user action that should be undoable calls `StackImpl::take_snapshot(name, ..., snapshot_data)`, which pushes **two** entries, not one:

```cpp
m_snapshots.emplace_back(snapshot_name, m_current_time, model.id().id, snapshot_data);   // the real, named action
m_active_snapshot_time = ++m_current_time;
m_snapshots.emplace_back(topmost_snapshot_name, m_active_snapshot_time, 0, snapshot_data); // "@@@ Topmost @@@" placeholder, same snapshot_data
```

`topmost_snapshot_name` is the literal string `"@@@ Topmost @@@"`; `Snapshot::is_topmost()` checks `name == topmost_snapshot_name`, and `is_topmost_captured()` checks `model_id > 0` (the placeholder starts uncaptured at `model_id == 0`, and only gets "captured" — reserialized with real content — lazily, the first time `undo()` actually needs to jump away from it; see `undo()` below). Critically, the placeholder's `snapshot_data` — including `snapshot_type` — is copied verbatim from whatever action was just taken. It is not a neutral/empty marker; it looks, type-wise, exactly like the action that preceded it.

Two independent pieces of code answer "is there something to undo/redo", and they disagree:

**1. The real jump resolution, inside `undo()`/`redo()` themselves** (used when `Plater::priv::undo()`/`redo()` call with `time_to_load == SIZE_MAX`, i.e. "just go one step"):

```cpp
// StackImpl::undo(), SIZE_MAX branch
auto it_current = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
if (-- it_current == m_snapshots.begin())
    return false;   // nothing to undo
time_to_load = it_current->timestamp;
```
```cpp
// StackImpl::redo(), SIZE_MAX branch
auto it_current = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
if (++ it_current == m_snapshots.end())
    return false;   // nothing to redo
time_to_load = it_current->timestamp;
```

This is a plain adjacency check relative to `m_active_snapshot_time`'s position, and it is correct: it treats the very first stack entry (`begin()`) as the un-undoable origin, and correctly excludes the active entry itself from being its own redo target.

**2. The availability query, `has_undo_snapshot()` / `has_redo_snapshot()`** — what `Plater::can_undo()`/`can_redo()` actually call to decide whether to show Undo/Redo as available:

```cpp
bool StackImpl::has_undo_snapshot() const {
    auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
    for (auto it2 = m_snapshots.begin(); it2 != it; ++it2)
        if (snapshot_modifies_project(*it2)) return true;   // scans [begin, active), exclusive
    return false;
}
bool StackImpl::has_redo_snapshot() const {
    auto it = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), Snapshot(m_active_snapshot_time));
    for (; it != m_snapshots.end(); ++it)
        if (snapshot_modifies_project(*it)) return true;    // scans [active, end), INCLUSIVE of active itself
    return false;
}
```

Unlike `undo()`/`redo()`, these do not consult `is_topmost()` at all, and `has_redo_snapshot()`'s range includes the active entry itself. Combined with the placeholder inheriting a project-modifying `snapshot_type`, the result is:

- Right after the very first `take_snapshot()` call in a session (e.g. the automatic `"New Project"` snapshot taken at startup), the stack is `[ {"New Project", t=0, ProjectSeparator}, {"@@@ Topmost @@@", t=1, ProjectSeparator} ]`, active = 1.
  - `has_undo_snapshot()` scans `[begin, active)` = `{entry0}`, sees `ProjectSeparator` → **true**. But `undo()`'s own SIZE_MAX resolution for this exact state decrements to `begin()` and returns **false**. Direct contradiction.
  - `has_redo_snapshot()` scans `[active, end)` inclusive = `{entry1}` (itself), sees its inherited `ProjectSeparator` tag → **true**, even though entry1 *is* the current state, not a forward step.
- This was verified empirically: `debug_Wed_Aug_05_17_31_46_67012.log.0` shows `can_undo=false can_redo=false` immediately after `"New Project"` is taken (masked by `Plater::IsShown()` being false at that instant during startup), flipping to `can_undo=true can_redo=true` ~48s later with **zero** intervening `take_snapshot()` calls — purely because `IsShown()` became true and let the already-wrong `has_undo_snapshot()`/`has_redo_snapshot()` values surface.
- This is why the bug was never caught: prior to `fix/topbar-undo-redo-state` (separate, already-landed change), the topbar buttons were hardcoded `enabled = (current tab == 3D Editor)` and never actually called `can_undo()`/`can_redo()`. The Edit menu items do call them (via `wxUpdateUIEvent`), so this has likely been silently wrong there too, for as long as this code has existed.
- **No data corruption results from this bug.** `Plater::priv::undo()`/`redo()` (the GUI-facing callers) have their own independent walk-back guards (`while (--it_current != snapshots.begin() && !snapshot_modifies_project(*it_current));` / analogous forward walk) before ever calling `undo_redo_to()`, and `StackImpl::undo()`/`redo()`'s own SIZE_MAX resolution (shown above) is correct. So clicking an incorrectly-enabled Undo/Redo button is a safe no-op at the true boundary — the bug is purely cosmetic/availability-reporting, not a data-loss risk. This significantly de-risks the fix: we are correcting a query, not the state machine that actually moves the cursor.

## Goals / Non-Goals

**Goals:**
- Make `has_undo_snapshot()` / `has_redo_snapshot()` agree with the boundary semantics `undo()`/`redo()` already implement correctly, so `Plater::can_undo()`/`can_redo()` (and therefore every UI affordance built on them) accurately reflects whether a real, distinct state exists to jump to.
- Audit every other `m_active_snapshot_time`-relative range/lookup in `UndoRedo.cpp` (see the call-site list gathered below) for the same class of off-by-boundary or missing-`is_topmost()` bug, and record a disposition (fixed / confirmed unaffected) for each.
- Land automated (Layer 1) coverage of the corrected boundary logic to the extent `UndoRedo::StackImpl`'s wx-type dependencies (`Slic3r::GUI::Selection`, `GLGizmosManager`, `PartPlateList`) allow, plus a Layer 2 manual matrix for what can't be automated.

**Non-Goals:**
- Redesigning `take_snapshot()`'s two-entries-per-action structure, the `topmost_snapshot_name` placeholder mechanism, or `is_topmost()`/`is_topmost_captured()`'s existing semantics — those are sound; only the read-side query built on top of them is wrong.
- Changing the serialized/on-disk snapshot format, or anything that would require project-file or session migration.
- Touching `fix/topbar-undo-redo-state` (the separate, already-committed change that surfaced this bug) — it stays as-is.
- Fixing FDM- or SLA-specific gizmo behavior; this is a technology-agnostic core fix, and no gizmo-specific code should need to change as a result.

## Decisions

### Decision A — Fix the classification in place; do not delegate to `undo()`/`redo()`'s resolution code

Two ways to bring `has_undo_snapshot()`/`has_redo_snapshot()` into agreement with `undo()`/`redo()`:

1. **(Chosen) Patch the existing range scans** to exclude the topmost placeholder / active-entry-itself, mirroring the boundary `undo()`/`redo()` already use:
   - `has_undo_snapshot()`: keep the `[begin, active)` scan (unchanged range), but additionally require that the resolved undo target is not `begin()` itself when decremented — i.e. bring in the same "decrement one from `it`, bail if it lands on `begin()`" check `undo()` uses, rather than scanning the whole exclusive range for *any* modifying entry. Concretely: resolve the immediate predecessor of `active` the same way `undo()`'s `SIZE_MAX` branch does, and only then check whether that resolved entry (not the whole range) is real/reachable.
   - `has_redo_snapshot()`: change the range to start **after** `active` (`++it`, matching `redo()`'s `++it_current`), not at `active` itself, before scanning for a modifying entry.
   - This keeps the two functions structurally similar to today (still simple boolean predicates callable without mutating state), minimizing surface area of the change.
2. **(Rejected for now, flag as Open Question) Have `has_undo_snapshot()`/`has_redo_snapshot()` call the same resolution helper `undo()`/`redo()` use internally**, refactoring the `SIZE_MAX`-branch logic in both into a shared private helper (e.g. `resolve_undo_target()` / `resolve_redo_target()`) that both the mutating jump and the read-only availability query call — guaranteeing they can never drift apart again, at the cost of a slightly larger refactor (extracting shared logic from `undo()`/`redo()`, which are more sensitive/high-traffic than the read-only queries).

Option 2 is structurally more robust (single source of truth prevents this exact class of bug from recurring), but touches `undo()`/`redo()` themselves — the actual state-mutating jump logic — which is higher-risk to touch than a pure read-side predicate. Given this change's goal is a targeted, low-risk fix (not a refactor), **Option 1 is chosen**; Option 2 is recorded as a follow-up candidate (see Open Questions) if this bug class recurs elsewhere.

### Decision B — Audit scope for sibling functions

Every `m_active_snapshot_time`-relative lookup found in `UndoRedo.cpp` (via `grep -n "m_active_snapshot_time\|std::lower_bound\|is_topmost"`), to be individually dispositioned in tasks.md:

| Function | Purpose | Suspect? |
|---|---|---|
| `has_undo_snapshot()` | availability query | **Yes — root cause, Decision A** |
| `has_redo_snapshot()` | availability query | **Yes — root cause, Decision A** |
| `has_undo_snapshot(size_t time_to_load)` | "can I undo to this specific timestamp" | Needs check — uses `time_to_load < m_active_snapshot_time && binary_search(...)`, a different shape; likely fine but must confirm it doesn't also match the placeholder itself when `time_to_load == active`'s predecessor in a topmost-only stack |
| `undo()` / `redo()` (`SIZE_MAX` branch) | actual jump resolution | No — confirmed correct, used as the reference behavior for Decision A |
| `take_snapshot()` (existing-snapshot release, `release_snapshots(it, end)`) | garbage collection on new snapshot | No — this is about releasing stale forward history, not an availability query; needs a sanity check only |
| `temp_snapshot_active()` | `m_snapshots.back().timestamp == m_active_snapshot_time && !back().is_topmost_captured()` | No — already correctly consults `is_topmost_captured()` |
| `has_real_change_from(time)` (dirty-state check, ~line 1299-1320) | "has anything changed since a saved timestamp" | Needs check — similar `lower_bound` + forward/backward scan shape; confirm whether it has the same inclusive/exclusive boundary issue |
| `release_snapshots()`'s internal `it_saved` walk (~line 1150-1160) | bookkeeping during snapshot release | No — internal cleanup, not an availability query, but confirm it doesn't misclassify the placeholder as "saved" |

Full disposition (confirmed-fine vs. needs-fix, with reasoning) is tracked in tasks.md so nothing in this list is silently skipped.

### Decision C — Testing strategy

- **Layer 1 (automated)**: `UndoRedo::StackImpl` takes `Slic3r::Model`, `Slic3r::GUI::Selection`, `Slic3r::GUI::GLGizmosManager`, and `Slic3r::GUI::PartPlateList` by reference in `take_snapshot()`/`undo()`/`redo()`, which are GUI-layer types not linkable into `libslic3r_tests` (same constraint documented in `resin-mode-scoped-undo-redo`'s design.md for `GLGizmoHollow`). No existing test file exercises `UndoRedo::StackImpl` directly (`tests/libslic3r` has no `test_undo_redo*.cpp`; `test_sla_undo_redo_data.cpp` only round-trips `sla::DrainHoles` cereal serialization, unrelated to this bug). Before assuming Layer 1 is infeasible, tasks.md includes a spike to check whether the *boundary logic itself* can be extracted into a small, pure, dependency-free helper (operating only on `std::vector<Snapshot>` + a timestamp) that both `has_undo_snapshot()`/`has_redo_snapshot()` and a new unit test could call — if that extraction is cheap, prefer it, since it would give real regression coverage; if it's not (e.g. because the fix must stay inline with existing structure), fall back to Layer 2 only for this function, consistent with how prior undo/redo changes in this repo have handled GUI-coupled logic.
- **Layer 2 (manual matrix)**: covers the scenarios that need the full GUI (topbar + Edit menu, both must agree): fresh project (both disabled), after 1 action (undo enabled/redo disabled), after undo to start (undo disabled/redo enabled), after redo to top (undo enabled/redo disabled), and the exact repro from this investigation (switch tabs with nothing done, then load+move+undo×2). Documented fully in tasks.md.

### Decision D — Compatibility

This change only touches the **read-side** availability predicates; `take_snapshot()`'s stack shape, `Snapshot`'s fields, and serialization are untouched. There is no on-disk format and no cross-version compatibility concern — an in-progress session's `m_snapshots` vector is pure in-memory runtime state, not persisted across app restarts or embedded in `.3mf`/project files. No migration plan is needed beyond the code fix itself.

## Risks / Trade-offs

- **[Fixing a 3+ish-year-old core function risks an unseen regression in some other caller]** → `has_undo_snapshot()`/`has_redo_snapshot()` are read-only, side-effect-free predicates with exactly two call sites (`Plater::can_undo()`/`can_redo()`); nothing else in the codebase depends on their exact (buggy) return value for state mutation. Mitigate by grep-confirming call sites as a task, and by the Layer 2 matrix explicitly re-testing non-mode, ordinary undo/redo (not just the resin-scoped-stack paths) since this is used by literally every stack, main and gizmo sub-stacks alike.
- **[The audit in Decision B surfaces additional bugs beyond the two functions named in the proposal]** → Scope creep risk. Mitigate by keeping the audit's output a disposition table (fix vs. confirmed-fine) rather than open-endedly fixing everything found; anything beyond `has_undo_snapshot()`/`has_redo_snapshot()` that turns out to need a fix gets called out explicitly as in- or out-of-scope for this change before implementation starts.
- **[Layer 1 automated coverage may not be achievable given `UndoRedo::StackImpl`'s GUI-type dependencies]** → Accept Layer 2 manual matrix as the primary verification if the Decision C spike shows extraction isn't cheap; document why, same precedent as `resin-mode-scoped-undo-redo`.

## Open Questions

- Should `has_undo_snapshot()`/`has_redo_snapshot()` eventually be refactored to share a single resolution helper with `undo()`/`redo()` (Decision A, Option 2) so this class of bug structurally can't recur? Deferred as a follow-up candidate, not blocking this fix.
- Does `has_undo_snapshot(size_t time_to_load)` (the timestamp-parameterized overload) have a live caller that's affected by the same misclassification, or is it dead/lightly-used code? To be confirmed during the Decision B audit (tasks.md).
