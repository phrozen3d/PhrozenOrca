#ifndef slic3r_GLGizmoSlaSupports_hpp_
#define slic3r_GLGizmoSlaSupports_hpp_

// Step 4.2: Changed base class from GLGizmoBase to GLGizmoSlaBase.
// GLGizmoSlaBase provides: update_volumes(), render_volumes(), unproject_on_mesh(),
// are_sla_supports_shown(), show_sla_supports(), reslice_until_step(), is_input_enabled().
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"
#include "slic3r/GUI/IconManager.hpp"

#include "libslic3r/SLA/SupportPoint.hpp"
#include "libslic3r/ObjectID.hpp"
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>


namespace Slic3r {

class ConfigOption;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

// Step 4.2: GLGizmoSlaSupports now inherits from GLGizmoSlaBase (was GLGizmoBase).
// This gives us: update_volumes(), render_volumes(), unproject_on_mesh(),
// register/unregister_volume_raycasters_for_picking(), and set_hide_full_scene() access.
class GLGizmoSlaSupports : public GLGizmoSlaBase
{
private:
    static constexpr float RenderPointScale = 1.f; // Step 4.2: was 'const float' instance var

    class CacheEntry {
    public:
        CacheEntry() :
            support_point(sla::SupportPoint()), selected(false), normal(Vec3f::Zero()) {}

        CacheEntry(const sla::SupportPoint& point, bool sel = false, const Vec3f& norm = Vec3f::Zero()) :
            support_point(point), selected(sel), normal(norm) {}

        bool operator==(const CacheEntry& rhs) const {
            return (support_point == rhs.support_point);
        }

        bool operator!=(const CacheEntry& rhs) const {
            return ! ((*this) == rhs);
        }

        sla::SupportPoint support_point;
        bool selected; // whether the point is selected
        Vec3f normal;

        template<class Archive>
        void serialize(Archive & ar)
        {
            ar(support_point, selected, normal);
        }
    };

public:
    GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoSlaSupports() = default;
    // Step 4.2: data_changed() replaces set_sla_support_data().
    void data_changed(bool is_serializing) override;
    bool on_mouse(const wxMouseEvent &mouse_event) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    void delete_selected_points(bool force = false);

    bool is_in_editing_mode() const override { return m_editing_mode; }
    bool is_selection_rectangle_dragging() const override { return m_selection_rectangle.is_dragging(); }
    bool has_backend_supports() const;

    // Switch to Structure view and trigger support tree + pad generation.
    // Called externally (e.g. from GLGizmoLcdOverhangDetection) after island
    // support points have been injected into mo->sla_support_points.
    void activate_structure_view();

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return "Entering SLA support points"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving SLA support points"; }

private:
    bool on_init() override;
    void on_render() override;
    // Step 4.2: new raycaster lifecycle — now also includes volume raycasters from GLGizmoSlaBase
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;

    // Step 4.2: removed 'bool picking' param (PickingModel handles it)
    void render_points(const Selection& selection);
    bool unsaved_changes() const;

    void register_point_raycasters_for_picking();
    void unregister_point_raycasters_for_picking();
    void update_point_raycasters_for_picking_transform();

    bool m_show_support_structure = false; // Step 4.2: controls supports_clipper visibility in on_render
    bool m_lock_unique_islands = false;
    bool m_editing_mode = false;            // Is editing mode active?
    float m_new_point_head_diameter;
    float m_new_point_pillar_diameter;
    sla::SupportWeight m_new_point_weight = sla::SupportWeight::Medium;
    CacheEntry m_point_before_drag;         // undo/redo - so we know what state was edited
    float m_old_point_head_diameter = 0.;   // the same
    mutable std::vector<CacheEntry> m_editing_cache; // a support point and whether it is currently selected
    std::vector<sla::SupportPoint> m_normal_cache; // to restore after discarding changes or undo/redo
    ObjectID m_old_mo_id;

    IconManager m_icon_manager;   // Step 4.5+: manages texture atlas for support structure view-mode icons
    IconManager::Icons m_icons;   // Step 4.5+: loaded icons (support_structure / support_points toggle)

    PickingModel m_cone;
    PickingModel m_sphere;
    // Step 4.2: removed GLModel m_cylinder (drain holes are GLGizmoHollow's responsibility)
    std::vector<std::pair<std::shared_ptr<SceneRaycasterItem>, std::shared_ptr<SceneRaycasterItem>>> m_point_raycasters;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // Note: kept as wxString (PhrozenOrca uses m_imgui->xxx() API which expects wxString).
    std::map<std::string, wxString> m_desc;

    GLSelectionRectangle m_selection_rectangle;

    bool m_wait_for_up_event = false;
    bool m_selection_empty = true;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    bool is_mesh_point_clipped(const Vec3d& point) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void unselect_point(int i);
    void editing_mode_apply_changes();
    void editing_mode_discard_changes();
    // Commit "no support points" state to model without triggering reslice/validation.
    // Used by both Auto and Manual Remove All handlers.
    void apply_remove_all();
    void reload_cache();
    void get_data_from_backend();
    void auto_generate();
    void switch_to_editing_mode();
    void disable_editing_mode();
    void ask_about_changes_call_after(std::function<void()> on_yes, std::function<void()> on_no);
    void apply_weight_preset(sla::SupportWeight w);

    // New Support UI panels — rendered below the legacy panel as a preview skeleton.
    void render_auto_support_panel(float x, float y, float legacy_panel_h, ModelObject* mo);
    void render_manual_support_panel(float x, float y, float legacy_panel_h, ModelObject* mo);

protected:
    void on_set_state() override;
    void on_set_hover_id() override
    {
        if (! m_editing_mode || (int)m_editing_cache.size() <= m_hover_id)
            m_hover_id = -1;
    }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData& data) override;
    void on_render_input_window(float x, float y, float bottom_limit) override;

    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;

    // Note: on_get_requirements() is NOT overridden here.
    // Step 4.2: Inherited from GLGizmoSlaBase which provides:
    //   SelectionInfo | InstancesHider | Raycaster | ObjectClipper | SupportsClipper
    // HollowedMesh was in the old PhrozenOrca version but is not needed after the refactor.
};


class SlaGizmoHelpDialog : public wxDialog
{
public:
    SlaGizmoHelpDialog();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSlaSupports_hpp_
