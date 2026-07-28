///|/ Copyright (c) Prusa Research 2019 - 2023 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoDrill_hpp_
#define slic3r_GLGizmoDrill_hpp_

// GLGizmoDrill: Drill-holes-only gizmo, extracted from GLGizmoHollow.
// Provides the same drain hole placement/editing functionality without the
// hollowing parameters (offset, quality, closing_distance).
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/ObjectID.hpp>
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>


namespace Slic3r {

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class Selection;

class GLGizmoDrill : public GLGizmoSlaBase
{
public:
    GLGizmoDrill(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    void data_changed(bool is_serializing) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points();
    bool is_selection_rectangle_dragging() const override {
        return m_selection_rectangle.is_dragging();
    }

    /// <summary>
    /// Postpone to Grabber for move
    /// Detect move of object by dragging
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

protected:
    bool on_init() override;
    void on_render() override;
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;

private:
    void render_points(const Selection& selection);
    void register_hole_raycasters_for_picking();
    void unregister_hole_raycasters_for_picking();
    void update_hole_raycasters_for_picking_transform();

    ObjectID m_old_mo_id = -1;

    PickingModel m_cylinder;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_hole_raycasters;

    float m_new_hole_radius = 2.f;        // Size of a new hole.
    float m_new_hole_height = 6.f;
    mutable std::vector<bool> m_selected; // which holes are currently selected

    sla::DrainHoles m_working_holes; // pending session working set; only Apply writes to ModelObject

    Vec3f m_hole_before_drag        = Vec3f::Zero();
    Vec3f m_hole_normal_before_drag = Vec3f::Zero();
    sla::DrainHoles m_holes_in_drilled_mesh;

    std::map<std::string, wxString> m_desc;

    GLSelectionRectangle m_selection_rectangle;

    bool m_wait_for_up_event = false;
    bool m_selection_empty = true;
    EState m_old_state = Off;

    bool is_mesh_point_clipped(const Vec3d& point) const;

    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void reload_cache();

    void init_cylinder_model();

    // Renders the secondary drill panel (DrillPanel2) placed below the legacy panel.
    // Only draws the ImGui window and updates action flags; does not execute deletion or refresh.
    void render_new_drill_panel(float x, float y, float legacy_panel_h,
                                ModelObject* mo, bool& remove_selected, bool& remove_all);

protected:
    void on_set_state() override;
    void on_set_hover_id() override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;
    void on_render_input_window(float x, float y, float bottom_limit) override;

    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;

public:
    // Scoped mode undo/redo (resin-mode-scoped-undo-stack): name for the single main-stack
    // snapshot recorded when the Drill session collapses on leave.
    std::string get_mode_leave_snapshot_name() const override { return "Apply drain holes"; }
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoDrill_hpp_
