///|/ Copyright (c) Prusa Research 2019 - 2023 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoHollow_hpp_
#define slic3r_GLGizmoHollow_hpp_

// Step 4.3: Changed base class from GLGizmoBase to GLGizmoSlaBase.
// GLGizmoSlaBase provides: update_volumes(), render_volumes(), unproject_on_mesh(),
// are_sla_supports_shown(), show_sla_supports(), reslice_until_step(), is_input_enabled().
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/ObjectID.hpp>
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>


namespace Slic3r {

class ConfigOption;
class ConfigOptionDef;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class Selection;

// Step 4.3: GLGizmoHollow now inherits from GLGizmoSlaBase (was GLGizmoBase).
// This gives us: update_volumes(), render_volumes(), unproject_on_mesh(),
// m_volumes management, hole-scene render pipeline, and set_hide_full_scene() access.
class GLGizmoHollow : public GLGizmoSlaBase
{
public:
    GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    void data_changed(bool is_serializing) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
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
    virtual void on_register_raycasters_for_picking() override;   // Step 4.3: new raycaster lifecycle
    virtual void on_unregister_raycasters_for_picking() override; // Step 4.3: new raycaster lifecycle

private:
    bool m_enable_hollowing = true;

    float        m_pending_offset    = 3.0f;
    float        m_pending_quality   = 0.5f;
    float        m_pending_closing_d = 2.f;
    ModelObject* m_pending_owner     = nullptr;

    EState m_old_state = Off;

    std::map<std::string, wxString> m_desc;

    // Kept for is_selection_rectangle_dragging() override called by GLGizmosManager.
    GLSelectionRectangle m_selection_rectangle;

    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> get_config_options(const std::vector<std::string>& keys) const;

    void render_hollow_panel(float x, float y,
                             ModelObject* mo, ConfigOptionMode current_mode,
                             bool& config_changed);

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
    // snapshot recorded when the Hollow session collapses on leave.
    std::string get_mode_leave_snapshot_name() const override { return "Hollow"; }

    // Note: on_get_requirements() is NOT overridden here.
    // Step 4.3: Inherited from GLGizmoSlaBase which provides:
    //   SelectionInfo | InstancesHider | Raycaster | ObjectClipper | SupportsClipper
    // HollowedMesh was in the old PhrozenOrca version but is not needed after the refactor.
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHollow_hpp_
