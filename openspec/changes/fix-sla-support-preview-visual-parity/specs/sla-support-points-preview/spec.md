## ADDED Requirements

### Requirement: Preview cone shape and shading do not depend on support point type

The mesh construction function and shader used to render a Points-preview cone SHALL be the same regardless of `support_point.type`. Manual (`manual_add`) points and auto-generated (`island` / `slope`) points SHALL use the same pinhead mesh construction (`pinhead()`) and the same lit shader (`gouraud_light`).

`render_points()` SHALL NOT select a different mesh-building function or a different shader based on whether a point is `manual_add`.

#### Scenario: Manual point uses the same mesh shape as auto points

- **GIVEN** a `manual_add` support point and an auto-generated support point with identical size parameters
- **WHEN** both are rendered in Points preview
- **THEN** both cones have the same geometry (rounded back-sphere tip via the tangent-circle robe), not the simplified flat-disc shape

#### Scenario: Manual point is lit like auto points

- **GIVEN** a `manual_add` support point rendered in Points preview
- **WHEN** the user rotates the camera around it
- **THEN** the cone's shading changes with view angle (directional lighting), the same as an auto-generated point
- **AND** the cone is NOT rendered with a flat, view-independent uniform color

#### Scenario: Slicing output is unaffected

- **GIVEN** any support point, manual or auto-generated
- **WHEN** the object is sliced
- **THEN** the generated support geometry is identical to the behavior before this change
- **AND** this requirement governs only the Points-preview rendering path, not `SupportTreeBuildsteps`

### Requirement: Support point type coloring applies regardless of editing mode

The color differentiation by `support_point.type` (`manual_add` → CYAN, `island` → ORANGE, other → LIGHT_GRAY) SHALL apply whether or not `m_editing_mode` is true. Non-editing mode (Points view) SHALL NOT collapse all points to a single uniform color.

Interaction-driven colors (hover → CYAN, selected → REDISH) SHALL remain gated on `m_editing_mode`, since hover and selection have no meaning outside editing mode.

The locked-island indicator (BLUEISH, gated on `m_lock_unique_islands`) SHALL remain gated on `m_editing_mode` — locking is an editing-mode-only operation and has no corresponding state to display outside editing mode.

#### Scenario: Points view shows type-differentiated colors

- **GIVEN** Points view (non-editing mode) with a mix of auto-generated island points, auto-generated slope points, and manual points
- **WHEN** the gizmo renders them
- **THEN** manual points are CYAN, island points are ORANGE, slope points are LIGHT_GRAY
- **AND** the points are NOT all rendered in the same uniform gray

#### Scenario: Locked-island indicator stays editing-mode-only

- **GIVEN** `m_lock_unique_islands` is enabled and an island point exists
- **WHEN** the gizmo renders in Points view (non-editing mode)
- **THEN** the island point is rendered ORANGE (the type-based color), not BLUEISH
- **AND** BLUEISH is only used when `m_editing_mode` is true

#### Scenario: Hover and selection colors unaffected

- **GIVEN** Manual editing mode with a hovered point and a separately selected point
- **WHEN** the gizmo renders them
- **THEN** the hovered point is CYAN and the selected point is REDISH, matching the behavior before this change

### Requirement: Preview geometry cache is unaffected by shape/shading unification

The per-point geometry cache (`m_head_model_cache`, keyed by `HeadGeomKey`) SHALL continue to avoid rebuilding `GLModel` instances every frame after this change. Points with identical size parameters SHALL share a cache entry regardless of `support_point.type`.

#### Scenario: Auto and manual points with matching size share one cache entry

- **GIVEN** an auto-generated point and a manual point with identical head/pillar/contact dimensions
- **WHEN** both are rendered in the same frame
- **THEN** they resolve to the same `HeadGeomKey` cache entry
- **AND** `GLModel::init_from()` is called at most once for that entry, not once per point

#### Scenario: Steady-state frame cost is unchanged

- **GIVEN** a scene with several hundred support points already rendered once (cache warmed)
- **WHEN** the camera is rotated across further frames with no parameter changes
- **THEN** `GLModel::init_from()` is called 0 times per frame, matching the behavior established by `perf-sla-support-points-preview-render`
