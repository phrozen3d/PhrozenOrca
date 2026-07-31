## Why

The SLA Support gizmo exposes two preview view modes — **Points** (per-support-point cones / pillars previewing the head + pillar that will be generated) and **Structure** (the actual sliced support tree + pad mesh). The two modes use independent data flows:

- Points preview is drawn by `GLGizmoSlaSupports::render_points()` from `m_normal_cache` / `m_editing_cache`.
- Structure preview is drawn by `GLGizmoSlaBase::render_volumes()` from `m_volumes` and is gated by `m_show_sla_supports`.

Three correctness defects existed in the Points preview path:

1. **Missing mode gate.** `on_render()` called `render_points(selection)` unconditionally. `render_points()` itself never consulted `m_show_support_structure`. As a result, after the user clicked **Structure** (or after `activate_structure_view()` was triggered from Overhang Detection), Points cones kept rendering on top of the actual support / pad mesh — the symptom reported as "支撐預覽柱…切換到「支撐結構」模式後仍殘留".

2. **Position misalignment under instance scale.** `sla::SupportPoint::pos` is stored in raw model-object coordinates (same space as `CommonGizmosDataObjects::Raycaster` and `unproject_on_mesh`), while the visible mesh in `update_volumes()` is rendered with the full instance transform including scale. The original render / picking formulas applied `instance_matrix * instance_scaling_matrix_inverse`, which removed scale uniformly and left cones anchored to the unscaled position. When the instance was scaled, cones detached from the visible surface.

3. **Cone / picking dimensions scaling with the object.** The intuitive "preserve full instance scale" fix anchors the cone correctly but also stretches its mm-defined head / pillar / contact dimensions — and the picking sphere radius — by the same factor. Preview sizes should reflect the support parameters in mm, not the object's display scale.

[KB-2] in archived `2026-05-15-fix-sla-supports-apply-undo-clear` previously noted that "Structure 顯示模式 undo/redo 不刷新" and proposed a separate follow-up `fix-sla-supports-structure-view-undo-refresh`. The present change is independent of that work.

## What Changes

### Mode gate

- **Gate `render_points()` on view mode**: when `m_show_support_structure == true` and `m_editing_mode == false`, return early before any Points-preview cone is rendered.
- **Sync picking on view-mode toggle**: the in-panel Structure / Points buttons drop / restore the point raycaster registration outside of editing mode and request a redraw via `set_as_dirty()`.
- **Apply the same rules to `activate_structure_view()`**: the external entry path used by `GLGizmoLcdOverhangDetection` mirrors the in-panel button behaviour.
- **Editing mode is preserved**: `m_editing_mode == true` keeps cones rendered and picking registered so manual point selection / drag / right-click delete continue to work even if the user clicks Structure while editing.

### Position / size split for Points preview

- **Anchor follows instance scale**: per-point, `head.pos` is overwritten with `instance_scaling_matrix * support_point.pos`. The head ITS therefore sits at the scaled anchor in raw frame.
- **Geometry stays in mm**: render uses `model_matrix = instance_matrix * instance_scaling_matrix_inverse`, which keeps `T_zshift * T * R * mirror` and excludes positive scale. The mm-sized vertex offsets in the head ITS are not stretched by the instance scale.
- **Picking matches**: `update_point_raycasters_for_picking_transform()` uses the same split — sphere centre at `instance_scaling_matrix * sp.pos`, `pick_matrix = instance_matrix * instance_scaling_matrix_inverse`, sphere radius from `head.r_pin_mm` / `head.r_contact_mm`. Hover / click stays aligned with what the user sees.
- **Non-uniform-scale normal correction**: cone axis uses `(S^-T * raw_normal)` so it follows the scaled-mesh visible surface normal under non-uniform instance scale. For uniform scale this is direction-preserving and behaviour is unchanged.

### Non-goals

- **Structure-mode async reslice refresh** ([KB-2]). Re-triggering reslice on undo/redo while in Structure view is tracked separately.
- **Points preview performance** (frame-rate / GPU buffer churn). The render path still rebuilds cone ITS and GL buffers per point per frame; that is a known cost but out of scope for this correctness fix. **Followed up by change `perf-sla-support-points-preview-render`**, which caches pinhead geometry / `GLModel` by parameter key and moves per-point placement into the model matrix, building on the position / size split established here.
- `GLGizmoSlaBase::render_volumes()`, `get_sla_shift()`, `get_data_from_backend()`, the support generation pipeline, and the Prepare Z-clip / Hollow / Drill sync paths are untouched.

## Capabilities

### New Capabilities

- `sla-support-points-preview`: umbrella capability for the SLA Support gizmo Points-preview render and picking. Covers visibility under the Structure / Points view-mode toggle, cone anchor following instance scale, cone / pillar / picking dimensions remaining in support-parameter mm under instance scale, and consistency between render and picking transforms.

### Modified Capabilities

<!-- No existing spec covers the Points / Structure preview gate or preview transform. -->

## Impact

- **Primary**: `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `render_points()` — view-mode gate; per-point head.pos override; scale-free model_matrix; inverse-transpose normal for non-uniform scale.
  - Structure / Points view buttons (`render_auto_support_panel` / `render_manual_support_panel`) — unregister / register raycasters, set dirty.
  - `activate_structure_view()` — mirror in-panel behaviour.
  - `update_point_raycasters_for_picking_transform()` — picking sphere centre at scaled position; pick_matrix excludes positive scale; sphere radius in mm.
- **Secondary**: none. Header file, `GLGizmoSlaBase`, support-generation backend, Hollow / Drill / Prepare Z-clip paths are untouched.
- No public API, file-format, or profile changes.
