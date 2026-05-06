## Requirements

### Requirement: Island overlay uses extruded solid with lighting

The island overlay mesh SHALL be an extruded solid (`P3N3` layout, `gouraud_light` shader) with a top face, bottom face, and side walls. The extrusion direction SHALL be downward (negative Z). The thickness and XY scale factor SHALL be configurable via member parameters. Back-face culling and depth test SHALL be enabled for the extruded solid.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `m_island_overlay_thickness` | 0.5 mm | Downward extrusion depth |
| `m_island_overlay_scale` | 1.5 | XY scale factor around centroid |
| `m_island_overlay_z_offset` | -0.05 mm | Z offset for top face (existing) |

#### Scenario: Extruded solid visible from oblique angle
- **WHEN** island detection results are displayed and the user views from a low angle
- **THEN** island side walls SHALL be visible as a colored band extending below the model surface
- **THEN** the side walls SHALL render with gouraud lighting (light/dark variation by face angle)
- **THEN** depth test ON means the solid is correctly occluded by model geometry in front of it

#### Scenario: Correct normals for lighting
- **WHEN** the extruded solid mesh is built via `build_island_extrude_p3n3()`
- **THEN** top face vertices SHALL have normal `(0, 0, 1)`
- **THEN** bottom face vertices SHALL have normal `(0, 0, -1)`
- **THEN** side wall vertices SHALL use per-edge outward horizontal normals (unshared vertices for flat shading)
- **THEN** the outward normal of edge j SHALL be `normalize(dy, -dx, 0)` where `d = top_verts[j+1] - top_verts[j]`

#### Scenario: XY extent is scaled around centroid
- **WHEN** the overlay mesh is built for an island
- **THEN** each perimeter vertex SHALL be positioned at `centroid + (original_vertex - centroid) * m_island_overlay_scale`
- **THEN** the mesh SHALL cover a larger area than the raw island contour

### Requirement: Original flat contour always visible

A separate flat polygon mesh SHALL be rendered for ALL islands using the original (unscaled) contour. This mesh SHALL always be visible regardless of model geometry (depth test disabled for this pass).

| Parameter | Value | Description |
|-----------|-------|-------------|
| Color | `island_contour_color()` | Bright orange, alpha 0.75 |
| Z | `print_z + m_island_overlay_z_offset` | Same as overlay top face |
| Depth test | OFF | Always visible over model |

#### Scenario: Original contour visible from above
- **WHEN** the user views the model from above
- **THEN** the original unscaled island outline SHALL be visible as an orange flat polygon
- **THEN** it SHALL be rendered with depth test disabled (not occluded by model geometry)

### Requirement: Island highlight uses gray extruded solid

The selected island highlight SHALL be an extruded solid (same geometry as overlay) rendered in bright gray. It SHALL replace the yellow overlay for the selected island only. There SHALL be no separate flat polygon for the highlight.

| Parameter | Value | Description |
|-----------|-------|-------------|
| Color | `island_selected_color()` | Bright gray, alpha 0.65 |
| Scale | `m_island_overlay_scale` | Same scale as yellow overlay |
| Shader | `gouraud_light` | Same lighting as overlay |

#### Scenario: Selected island shown in gray
- **WHEN** an island is selected (m_current_overhang_area_index)
- **THEN** the selected island SHALL be excluded from the yellow overlay model
- **THEN** the selected island SHALL be rendered separately in gray via `m_island_highlight_model`
- **THEN** both yellow and gray solids SHALL use the same scale and extrusion depth

### Requirement: Named color functions

All island visualization colors SHALL be defined as private static member functions of `GLGizmoLcdOverhangDetection`, named by their visual purpose:

```cpp
static ColorRGBA island_contour_color();   // orange flat original contour
static ColorRGBA island_overlay_color();   // yellow extruded solid (unselected)
static ColorRGBA island_selected_color();  // gray extruded solid (selected)
```
