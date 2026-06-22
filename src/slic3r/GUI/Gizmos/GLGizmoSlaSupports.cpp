// Step 4.2: Include GLGizmoSlaBase.hpp instead of GLGizmoBase.hpp.
#include "GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"

#include <GL/glew.h>

#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stattext.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLA/SupportTreeMesher.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp" // ScopeGuard

#include <array>

struct SupportWeightPreset {
    float pillar_diameter;
    float head_front_diameter;
    float contact_diameter;
    float base_diameter;
    float base_height;
    float head_width;
};

static constexpr SupportWeightPreset k_weight_presets[3] = {
    { 0.6f, 0.3f, 0.4f, 2.0f, 0.5f, 0.5f },  // Light
    { 1.0f, 0.4f, 0.6f, 3.0f, 1.0f, 1.0f },  // Medium
    { 1.5f, 0.6f, 0.8f, 4.0f, 1.5f, 1.5f },  // Heavy
};

namespace Slic3r {
namespace GUI {

// Sync per-object generate_support with committed support points so the SLA pipeline
// runs support_points / support_tree when points exist and skips them when cleared.
static void sync_generate_support_for_object(ModelObject* mo, bool enable)
{
    if (!mo)
        return;
    mo->config.set("generate_support", enable);
}

static const std::array<const char *, 7> k_sla_support_top_opts = {
    "support_contact_type",
    "support_contact_diameter",
    "support_head_penetration",
    "support_head_front_diameter",
    "support_head_back_diameter",
    "support_pillar_diameter",
    "support_segment_length",
};

// Live Process tab config (includes edits not yet committed on kill-focus).
static const DynamicPrintConfig &sla_process_config()
{
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_SLA_PRINT))
        if (DynamicPrintConfig *cfg = tab->get_config())
            return *cfg;
    return wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
}

static GLGizmoSlaSupports *active_sla_supports_gizmo()
{
    Plater *plater = wxGetApp().plater();
    if (!plater)
        return nullptr;
    GLCanvas3D *canvas = plater->get_view3D_canvas3D();
    if (!canvas)
        return nullptr;
    return dynamic_cast<GLGizmoSlaSupports *>(canvas->get_gizmos_manager().get_gizmo(GLGizmosManager::EType::SlaSupports));
}

GLGizmoSlaSupports *GLGizmoSlaSupports::active_instance()
{
    return active_sla_supports_gizmo();
}

static bool process_contact_type_is_sphere()
{
    TabSLAPrint *tab = dynamic_cast<TabSLAPrint *>(wxGetApp().get_tab(Preset::TYPE_SLA_PRINT));
    if (tab) {
        Page *page = nullptr;
        if (Field *field = tab->get_field("support_contact_type", &page)) {
            const boost::any val = field->get_value();
            if (!val.empty()) {
                try {
                    if (val.type() == typeid(int))
                        return boost::any_cast<int>(val) == int(spSphere);
                } catch (const std::exception &) {
                }
            }
        }
    }

    const DynamicPrintConfig &cfg = sla_process_config();
    if (const auto *ct = cfg.option<ConfigOptionEnum<ContactType>>("support_contact_type"))
        if (ct->value == spSphere)
            return true;
    const DynamicPrintConfig &preset = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    if (const auto *ct = preset.option<ConfigOptionEnum<ContactType>>("support_contact_type"))
        return ct->value == spSphere;
    return false;
}

// Read the latest value from Process tab fields (works even before focus leaves TextCtrl).
static float process_top_float_live(const char *key, float fallback)
{
    TabSLAPrint *tab = dynamic_cast<TabSLAPrint *>(wxGetApp().get_tab(Preset::TYPE_SLA_PRINT));
    if (tab) {
        Page *page = nullptr;
        Field *field = tab->get_field(key, &page);
        if (field) {
            boost::any val = field->get_value();
            if (!val.empty()) {
                try {
                    if (val.type() == typeid(double))
                        return float(boost::any_cast<double>(val));
                    if (val.type() == typeid(int))
                        return float(boost::any_cast<int>(val));
                    if (val.type() == typeid(wxString)) {
                        double parsed = 0.;
                        if (boost::any_cast<wxString>(val).ToDouble(&parsed))
                            return float(parsed);
                    }
                } catch (const std::exception &) {
                }
            }
        }
    }

    const DynamicPrintConfig &cfg = sla_process_config();
    if (cfg.has(key))
        return float(cfg.opt_float(key));
    return fallback;
}

void GLGizmoSlaSupports::flush_process_top_fields_to_config()
{
    TabSLAPrint *tab = dynamic_cast<TabSLAPrint *>(wxGetApp().get_tab(Preset::TYPE_SLA_PRINT));
    if (!tab || !tab->get_config())
        return;
    // While a support point is selected, Top fields show per-point values — do not overwrite preset.
    if (GLGizmoSlaSupports *gizmo = active_sla_supports_gizmo()) {
        if (gizmo->has_selected_support_points())
            return;
    }

    DynamicPrintConfig *cfg = tab->get_config();
    for (const char *key : k_sla_support_top_opts) {
        // Search all Process pages (not only m_active_page) so values are flushed while the
        // user places support points from Prepare with another tab/page focused.
        Page *page = nullptr;
        Field *field = tab->get_field(key, &page);
        if (!field)
            continue;
        boost::any val = field->get_value();
        if (val.empty())
            continue;
        try {
            change_opt_value(*cfg, key, val);
        } catch (const std::exception &) {
            // Field may still hold a stale type; skip this key.
        }
    }
}

static constexpr float k_min_support_size_mm = 0.01f;

static float clamp_contact_depth(float depth_mm)
{
    return depth_mm < k_min_support_size_mm ? k_min_support_size_mm : depth_mm;
}

static float clamp_support_diameter_mm(float diameter_mm)
{
    return diameter_mm < k_min_support_size_mm ? k_min_support_size_mm : diameter_mm;
}

static float clamp_segment_length_mm(float length_mm)
{
    return length_mm < k_min_support_size_mm ? k_min_support_size_mm : length_mm;
}

static float default_contact_sphere_radius_mm()
{
    return clamp_support_diameter_mm(process_top_float_live("support_contact_diameter", 0.8f)) * 0.5f;
}

// Match slice: manual points use per-point TOP stored at placement/edit; live Process Top is only for the next new point.
static bool preview_use_stored_top(const sla::SupportPoint &sp, bool point_selected)
{
    if (point_selected)
        return true;
    return sp.type == sla::SupportPointType::manual_add && sp.has_explicit_geometry();
}

// Same TOP parameter resolution as SupportTreeBuildsteps (manual_add back radius, mesh penetration, contact sphere).
static sla::Head preview_sla_head_for_point(const sla::SupportPoint &sp, const Vec3f &normal, bool use_stored_point)
{
    const float live_seg     = clamp_segment_length_mm(process_top_float_live("support_segment_length", 2.f));
    const float live_pen     = clamp_contact_depth(process_top_float_live("support_head_penetration", 0.4f));
    const double live_upper_r = double(clamp_support_diameter_mm(process_top_float_live("support_head_front_diameter", 0.4f)) * 0.5f);
    const double live_lower_r = double(clamp_support_diameter_mm(process_top_float_live("support_head_back_diameter", 1.f)) * 0.5f);
    const bool   preset_sphere = process_contact_type_is_sphere();

    const double pin_r = use_stored_point ? double(sp.head_front_radius) : live_upper_r;

    double back_r = live_lower_r;
    if (use_stored_point) {
        if (sp.head_back_radius_mm >= 0.f)
            back_r = double(sp.head_back_radius_mm);
        else if (sp.pillar_radius > 0.f)
            back_r = double(sp.pillar_radius);
    }

    const double width_mm = use_stored_point
        ? double(clamp_segment_length_mm(sla::point_head_width_mm(sp, live_seg)))
        : double(live_seg);

    double contact_r = 0.;
    if (use_stored_point) {
        if (sla::point_uses_contact_sphere(sp, preset_sphere))
            contact_r = double(sla::point_contact_sphere_radius_mm(sp, default_contact_sphere_radius_mm()));
    } else if (preset_sphere) {
        contact_r = double(default_contact_sphere_radius_mm());
    }

    const double mesh_pen = double(sla::point_head_penetration_mesh_mm(
        sp, live_pen, float(pin_r), float(contact_r)));

    Vec3d dir = normal.cast<double>();
    if (dir.squaredNorm() < EPSILON)
        dir = Vec3d(0., 0., -1.);
    else
        dir.normalize();

    sla::Head h(back_r, pin_r, width_mm, mesh_pen, dir, sp.pos.cast<double>());
    h.r_contact_mm = contact_r;
    return h;
}

static void support_top_apply_point(const sla::SupportPoint &sp, DynamicPrintConfig &cfg)
{
    const DynamicPrintConfig &preset = sla_process_config();

    const bool preset_sphere = process_contact_type_is_sphere();
    const bool use_sphere    = sla::point_uses_contact_sphere(sp, preset_sphere);

    cfg.set("support_contact_type", use_sphere ? spSphere : spNone2);

    float contact_d = 0.8f;
    if (sp.contact_sphere_radius > float(EPSILON))
        contact_d = sp.contact_sphere_radius * 2.f;
    else if (const auto *cd = preset.option<ConfigOptionFloat>("support_contact_diameter"))
        contact_d = float(cd->value);
    cfg.set("support_contact_diameter", double(contact_d));

    float penetration = 0.2f;
    if (sp.head_penetration_mm >= 0.f)
        penetration = sp.head_penetration_mm;
    else if (const auto *opt = preset.option<ConfigOptionFloat>("support_head_penetration"))
        penetration = float(opt->value);
    cfg.set("support_head_penetration", double(penetration));

    cfg.set("support_head_front_diameter", double(clamp_support_diameter_mm(sp.head_front_radius * 2.f)));

    // Pillar diameter: per-point pillar radius override (sp.pillar_radius holds radius, not diameter).
    // For manual points we always set pillar_radius > 0, but keep a safe fallback anyway.
    {
        float pillar_d = 0.f;
        if (sp.pillar_radius > float(EPSILON))
            pillar_d = sp.pillar_radius * 2.f;
        else if (const auto *pd = preset.option<ConfigOptionFloat>("support_pillar_diameter"))
            pillar_d = float(pd->value);
        cfg.set("support_pillar_diameter", double(clamp_support_diameter_mm(pillar_d)));
    }

    float lower_d = 1.f;
    if (sp.head_back_radius_mm >= 0.f)
        lower_d = sp.head_back_radius_mm * 2.f;
    else if (sp.pillar_radius > float(EPSILON))
        // When lower diameter is unset, geometry can fall back to pillar diameter.
        // Keep UI consistent with the actual support geometry.
        lower_d = sp.pillar_radius * 2.f;
    else if (const auto *opt = preset.option<ConfigOptionFloat>("support_head_back_diameter"))
        lower_d = float(opt->value);
    else if (const auto *opt = preset.option<ConfigOptionFloat>("support_pillar_diameter"))
        lower_d = float(opt->value);
    cfg.set("support_head_back_diameter", double(clamp_support_diameter_mm(lower_d)));

    float segment_len = 3.f;
    if (sp.head_width_mm >= 0.f)
        segment_len = sp.head_width_mm;
    else if (const auto *opt = preset.option<ConfigOptionFloat>("support_segment_length"))
        segment_len = float(opt->value);
    else if (const auto *opt = preset.option<ConfigOptionFloat>("support_head_width"))
        segment_len = float(opt->value);
    cfg.set("support_segment_length", double(clamp_segment_length_mm(segment_len)));
}

// Icon loading for the support view-mode toggle (support points vs support structure).
// Resource path is /images/, not /icons/.
namespace {

enum class IconType : unsigned {
    show_support_points_selected,
    show_support_points_unselected,
    show_support_points_hovered,
    show_support_structure_selected,
    show_support_structure_unselected,
    show_support_structure_hovered,
    _count
};

IconManager::Icons init_support_icons(IconManager &mng, ImVec2 size = ImVec2{50, 50})
{
    mng.release();
    const std::string path = resources_dir() + "/images/";
    IconManager::InitTypes init_types {
        {path + "support_structure_invisible.svg", size, IconManager::RasterType::color},          // show_support_points_selected
        {path + "support_structure_invisible.svg", size, IconManager::RasterType::gray_only_data}, // show_support_points_unselected
        {path + "support_structure_invisible.svg", size, IconManager::RasterType::color},          // show_support_points_hovered
        {path + "support_structure.svg",           size, IconManager::RasterType::color},          // show_support_structure_selected
        {path + "support_structure.svg",           size, IconManager::RasterType::gray_only_data}, // show_support_structure_unselected
        {path + "support_structure.svg",           size, IconManager::RasterType::color},          // show_support_structure_hovered
    };
    assert(init_types.size() == static_cast<size_t>(IconType::_count));
    return mng.init(init_types);
}

} // anonymous namespace

// PhrozenOrca: Use no-step constructor (m_min_sla_print_object_step = -1).
// PrusaSlicer uses slaposBase/slaposAssembly — an always-complete init step — so
// m_input_enabled is true as soon as the object is loaded. PhrozenOrca has no equivalent:
// the first real step (slaposHollowing) requires actual computation and may not be done.
// Using the no-step variant ensures the model renders in normal selected color immediately
// upon entering the gizmo, matching PrusaSlicer UX behavior (model never appears gray).
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id)  // no minimum step — always enable input
{
    show_sla_supports(false); // Step 4.2: hide supports when gizmo opens (same as PrusaSlicer)
}


bool GLGizmoSlaSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_P;

    m_desc["head_diameter"]    = _L("Upper Diameter") + ": ";
    m_desc["lock_supports"]    = _L("Lock supports under new islands");
    m_desc["remove_selected"]  = _L("Remove selected points");
    m_desc["remove_all"]       = _L("Remove all points");
    m_desc["apply_changes"]    = _L("Apply changes");
    m_desc["discard_changes"]  = _L("Discard changes");
    m_desc["points_density"]   = _L("Support points density") + ": ";
    m_desc["auto_generate"]    = _L("Auto-generate points");
    m_desc["manual_editing"]   = _L("Manual editing");
    return true;
}

// Step 4.2: Removed set_sla_support_data() — replaced by data_changed() below.

// Step 4.2: data_changed() now matches PrusaSlicer's full implementation.
// Added: set_hide_full_scene(true), update_volumes(), reslice logic.
void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    if (! m_c->selection_info())
        return;

    if (m_state == On)
        m_c->selection_info()->set_use_config_elevation(true);

    ModelObject* mo = m_c->selection_info()->model_object();

    if (m_state == On && mo && mo->id() != m_old_mo_id) {
        disable_editing_mode();
        reload_cache();
        m_old_mo_id = mo->id();
        m_auto_baseline_initialized = false;
    }

    // If we triggered autogeneration before, check backend and fetch results if they are there
    if (mo) {
        sync_new_point_params_from_config();
        m_c->instances_hider()->set_hide_full_scene(true); // Step 4.4 dependency

        // PhrozenOrca: required_step < 0 means no minimum step (no-step constructor was used).
        // In that case skip the reslice trigger — update_volumes() will use get_mesh_to_print()
        // which always returns the raw model mesh when nothing has been sliced.
        const int required_step = get_min_sla_print_object_step();
        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (required_step >= 0 && po != nullptr && !po->is_step_done((SLAPrintObjectStep)required_step))
            reslice_until_step((SLAPrintObjectStep)required_step, false);

        update_volumes(); // Step 4.2: load SLA volumes from GLGizmoSlaBase

        if (mo->sla_points_status == sla::PointsStatus::Generating)
            get_data_from_backend();

        if (m_point_raycasters.empty())
            register_point_raycasters_for_picking();
        else
            update_point_raycasters_for_picking_transform();

        m_c->instances_hider()->set_hide_full_scene(true); // Step 4.4 dependency (twice, same as PrusaSlicer)
    }
}

// Step 4.2: Added is_input_enabled() guard (was TODO in Phase 2.3 — now GLGizmoSlaBase provides it).
bool GLGizmoSlaSupports::on_mouse(const wxMouseEvent &mouse_event)
{
    if (!is_input_enabled()) return true; // Step 4.2: gate all mouse input on SLA step completion
    if (mouse_event.Moving()) return false;

    // Support-point pick/drag: do not route through use_grabbers() / Selection::setup_cache().
    if (m_editing_mode && m_hover_id >= 0 && m_hover_id < (int) m_editing_cache.size()
        && !mouse_event.ShiftDown() && !mouse_event.AltDown()) {
        if (mouse_event.LeftDown()) {
            for (size_t j = 0; j < m_editing_cache.size(); ++j)
                m_editing_cache[j].selected = (j == size_t(m_hover_id));
            m_selection_empty = false;
            const float pr = m_editing_cache[m_hover_id].support_point.pillar_radius;
            if (pr > 0.f)
                m_new_point_pillar_diameter = pr * 2.f;
            else
                sync_new_point_params_from_config();
            notify_process_tab_selection_changed();
            m_dragging = true;
            m_point_before_drag = m_editing_cache[m_hover_id];
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));
            m_parent.set_as_dirty();
            return true;
        }
        if (m_dragging) {
            if (mouse_event.Dragging()) {
                const Point mouse_coord(mouse_event.GetX(), mouse_event.GetY());
                const auto  ray = m_parent.mouse_ray(mouse_coord);
                on_dragging(UpdateData(ray, mouse_coord));
                m_parent.set_as_dirty();
                return true;
            }
            if (mouse_event.LeftUp()) {
                do_stop_dragging(false);
                return true;
            }
        }
    }

    if (!mouse_event.ShiftDown() && !mouse_event.AltDown()
        && use_grabbers(mouse_event)) return true;

    Vec2i64 mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    static bool pending_right_up = false;
    if (mouse_event.LeftDown()) {
        bool grabber_contains_mouse = (get_hover_id() != -1);
        bool control_down = mouse_event.CmdDown();
        if ((!control_down || grabber_contains_mouse) &&
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos,
                       mouse_event.ShiftDown(), mouse_event.AltDown(), false))
            return true;
    } else if (mouse_event.Dragging()) {
        bool control_down = mouse_event.CmdDown();
        if (m_parent.get_move_volume_id() != -1) {
            return true;
        } else if (!control_down &&
                gizmo_event(SLAGizmoEventType::Dragging, mouse_pos,
                           mouse_event.ShiftDown(), mouse_event.AltDown(), false)) {
            m_parent.set_as_dirty();
            return true;
        } else if (control_down && (mouse_event.LeftIsDown() || mouse_event.RightIsDown())) {
            if (mouse_event.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos,
                           mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            else if (mouse_event.RightIsDown())
                pending_right_up = false;
        }
    } else if (mouse_event.LeftUp() && !m_parent.is_mouse_dragging()) {
        gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos,
                   mouse_event.ShiftDown(), mouse_event.AltDown(), mouse_event.CmdDown());
        return true;
    } else if (mouse_event.RightDown()) {
        if (m_parent.get_selection().get_object_idx() != -1 &&
            gizmo_event(SLAGizmoEventType::RightDown, mouse_pos, false, false, false)) {
            pending_right_up = true;
            return true;
        }
    } else if (pending_right_up && mouse_event.RightUp()) {
        pending_right_up = false;
        return true;
    }
    return false;
}

// Step 4.2: on_render() now matches PrusaSlicer's implementation.
// Added: selected_print_object_exists() check, render_volumes(), conditional supports_clipper.
// Removed: m_cylinder init (drain holes are GLGizmoHollow's responsibility).
void GLGizmoSlaSupports::on_render()
{
    if (! selected_print_object_exists(m_parent, wxEmptyString)) {
        wxGetApp().CallAfter([this]() {
            // Close current gizmo.
            m_parent.get_gizmos_manager().open_gizmo(m_parent.get_gizmos_manager().get_current_type());
        });
    }

    // Step 4.2: PhrozenOrca has no set_use_shift() on SelectionInfo — omitted (same as GLGizmoHollow).

    // Initialize PickingModel with both GLModel and MeshRaycaster (lazy init)
    if (!m_sphere.model.is_initialized()) {
        indexed_triangle_set its = its_make_sphere(1.0, double(PI) / 12.0);
        m_sphere.model.init_from(its);
        m_sphere.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }
    if (!m_cone.model.is_initialized()) {
        indexed_triangle_set its = its_make_cone(1.0, 1.0, double(PI) / 12.0);
        m_cone.model.init_from(its);
        m_cone.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    ModelObject* mo = m_c->selection_info()->model_object();
    const Selection& selection = m_parent.get_selection();

    // If current m_c->m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (mo != selection.get_model()->objects[selection.get_object_idx()]
      || m_c->selection_info()->get_active_instance() != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_volumes(); // Step 4.2: show the SLA mesh via GLGizmoSlaBase
    render_points(selection); // Step 4.2: removed 'false' picking param

    m_selection_rectangle.render(m_parent);
    m_c->object_clipper()->render_cut();
    if (are_sla_supports_shown()) // Step 4.2: conditional on m_show_sla_supports (via GLGizmoSlaBase)
        m_c->supports_clipper()->render_cut();

    glsafe(::glDisable(GL_BLEND));
}

// Step 4.2: render_points() rewritten to match PrusaSlicer.
// Removed: bool picking parameter (PickingModel handles picking), drain hole rendering (GLGizmoHollow's job).
// Added: raycaster active/clipped state management.
void GLGizmoSlaSupports::render_points(const Selection& selection)
{
    // View-mode gate: when the user has switched to "Structure" view outside of
    // manual editing, the Points preview cones must not be drawn. They would
    // otherwise overlap the actual support/pad mesh rendered by render_volumes()
    // and visually leak through after the toggle. Editing mode keeps drawing
    // cones unconditionally so manual selection / drag still works.
    if (m_show_support_structure && !m_editing_mode)
        return;

    const size_t cache_size = m_editing_mode ? m_editing_cache.size() : m_normal_cache.size();
    if (cache_size == 0)
        return;

    GLShaderProgram* gouraud_shader = wxGetApp().get_shader("gouraud_light");
    GLShaderProgram* flat_shader    = wxGetApp().get_shader("flat");
    if (gouraud_shader == nullptr)
        return;

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();

    GLShaderProgram* active_shader = nullptr;
    auto use_shader = [&](GLShaderProgram *target) {
        if (target == nullptr)
            target = gouraud_shader;
        if (active_shader != target) {
            if (active_shader != nullptr)
                active_shader->stop_using();
            target->start_using();
            target->set_uniform("projection_matrix", camera.get_projection_matrix());
            active_shader = target;
        }
    };
    ScopeGuard guard([&]() {
        if (active_shader != nullptr)
            active_shader->stop_using();
    });

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    // Coordinate convention:
    //   - sla::SupportPoint::pos is in raw model-object coordinates (the same
    //     space as the raycaster in CommonGizmosDataObjects::Raycaster, which is
    //     built from the raw ModelVolume mesh).
    //   - The visible mesh shown by GLGizmoSlaBase::update_volumes() is rendered
    //     with the full instance transform including scale.
    //
    // Anchor position must follow instance scale so the preview cone sits on the
    // visible surface, but cone diameter / length must stay in mm regardless of
    // instance scale (those are user-facing support-parameter sizes, not model
    // dimensions). The render path therefore separates the two:
    //   - head.pos is built in raw frame but scaled by S below, so the head ITS
    //     vertices land at (S * raw_pos + mm_offset).
    //   - model_matrix uses the instance transform with positive scale removed
    //     (T_zshift * T * R, with mirror preserved through signed get_matrix()
    //     vs. unsigned get_scaling_factor_matrix()).
    // Per-vertex world position is then T_zshift * T * R * (S * raw_pos + mm_offset).
    //
    // PhrozenOrca: get_sla_shift() replaces PrusaSlicer's print_object()->
    // get_current_elevation() — the latter requires a ModelInstance pointer that
    // SelectionInfo here does not expose.
    const Transform3d instance_scaling_matrix         = vol->get_instance_transformation().get_scaling_factor_matrix();
    const Transform3d instance_scaling_matrix_inverse = instance_scaling_matrix.inverse();
    const Transform3d instance_matrix          = Geometry::assemble_transform(m_c->selection_info()->get_sla_shift() * Vec3d::UnitZ()) * vol->get_instance_transformation().get_matrix();
    const Transform3d instance_matrix_no_scale = instance_matrix * instance_scaling_matrix_inverse;
    // Surface-normal transform under instance scale: inverse-transpose of S.
    // Identity-direction for uniform scale; corrects orientation under non-uniform
    // scale so the cone axis follows the scaled-mesh's visible surface normal.
    const Matrix3d    normal_xform             = instance_scaling_matrix_inverse.linear().transpose();

    ColorRGBA render_color;
    for (size_t i = 0; i < cache_size; ++i) {
        const sla::SupportPoint& support_point = m_editing_mode ? m_editing_cache[i].support_point : m_normal_cache[i];
        const bool point_selected = m_editing_mode ? m_editing_cache[i].selected : false;

        // Step 4.2: manage raycaster active state based on clipping (same as PrusaSlicer)
        const bool clipped = is_mesh_point_clipped(support_point.pos.cast<double>());
        if (i < m_point_raycasters.size()) {
            m_point_raycasters[i].first->set_active(!clipped);
            m_point_raycasters[i].second->set_active(!clipped);
        }
        if (clipped)
            continue;

        // Color logic based on SupportPointType (ported from PrusaSlicer):
        //   manual_add → CYAN (user-placed)
        //   island     → ORANGE-ish / BLUEISH when locked (auto-generated critical)
        //   slope      → LIGHT_GRAY (auto-generated ordinary)
        if (m_editing_mode && size_t(m_hover_id) == i)
            render_color = ColorRGBA::CYAN();
        else if (m_editing_mode && point_selected)
            render_color = ColorRGBA { 1.f, 0.3f, 0.3f, 1.f }; // REDISH
        else if (m_lock_unique_islands && support_point.is_island() && m_editing_mode)
            render_color = ColorRGBA::BLUEISH();
        else if (m_editing_mode && support_point.type == sla::SupportPointType::manual_add)
            render_color = ColorRGBA::CYAN();
        else if (m_editing_mode && support_point.type == sla::SupportPointType::island)
            render_color = ColorRGBA { 1.f, 0.6f, 0.0f, 1.f }; // orange — auto island
        else if (m_editing_mode)
            render_color = ColorRGBA::LIGHT_GRAY(); // slope
        else
            render_color = ColorRGBA { 0.5f, 0.5f, 0.5f, 1.f };

        m_cone.model.set_color(render_color);
        m_sphere.model.set_color(render_color);

        Vec3f raw_normal = Vec3f::UnitZ();
        if (m_editing_mode) {
            if (m_editing_cache[i].normal == Vec3f::Zero())
                m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);
            raw_normal = m_editing_cache[i].normal;
        } else if (m_normal_cache.size() > i) {
            m_c->raycaster()->raycaster()->get_closest_point(support_point.pos, &raw_normal);
        }
        // Scaled-mesh surface normal direction for orienting the cone axis.
        const Vec3f scaled_normal = (normal_xform * raw_normal.cast<double>()).cast<float>();

        const bool use_stored_geometry = m_editing_mode && preview_use_stored_top(support_point, point_selected);

        // Manual points: simplified preview (no back-sphere bulge). Auto points: full pinhead mesh.
        // head.pos is overwritten with the scale-applied anchor so the head ITS sits
        // on the visible surface. Size fields (head/pillar/contact radii, width,
        // penetration) come from the support-parameter mm values and stay untouched.
        sla::Head head = preview_sla_head_for_point(support_point, scaled_normal, use_stored_geometry);
        head.pos = instance_scaling_matrix * support_point.pos.cast<double>();
        const bool manual_preview = support_point.type == sla::SupportPointType::manual_add;
        static constexpr size_t kManualPreviewSteps = 45;
        indexed_triangle_set top_its = manual_preview
            ? sla::get_mesh_preview(head, kManualPreviewSteps)
            : sla::get_mesh(head, 24);
        if (!top_its.vertices.empty()) {
            if (vol->is_left_handed())
                glFrontFace(GL_CW);

            m_cone.model.reset();
            m_cone.model.init_from(top_its, manual_preview);
            // Render with the scale-free instance transform: cone geometry stays
            // mm-sized; the anchor is already scaled into head.pos above.
            const Transform3d model_matrix = instance_matrix_no_scale;

            // Manual preview: flat shader (uniform color, no directional shading).
            use_shader(manual_preview ? flat_shader : gouraud_shader);
            active_shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            if (active_shader != flat_shader) {
                const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                    model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
                active_shader->set_uniform("view_normal_matrix", view_normal_matrix);
                active_shader->set_uniform("emission_factor", 0.5f);
            }
            m_cone.model.render();

            if (vol->is_left_handed())
                glFrontFace(GL_CCW);
        }
    }
    // Note: drain hole rendering removed — that is GLGizmoHollow's responsibility (Step 4.2).
}



bool GLGizmoSlaSupports::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    int active_inst = m_c->selection_info()->get_active_instance();
    const ModelInstance* mi = sel_info->model_object()->instances[active_inst];
    const Transform3d& trafo = mi->get_transformation().get_matrix();

    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}



// Step 4.2: unproject_on_mesh() removed — inherited from GLGizmoSlaBase.
// The base class version is sufficient; drain hole checks are no longer needed here
// because HollowedMesh is not in GLGizmoSlaBase requirements.

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoSlaSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    int active_inst = m_c->selection_info()->get_active_instance();

    if (m_editing_mode) {

        // left down with shift - show the selection rectangle:
        if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
            if (m_hover_id == -1) {
                if (shift_down || alt_down) {
                    m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::Select : GLSelectionRectangle::Deselect);
                }
            }
            else if (m_hover_id >= 0 && m_hover_id < (int) m_editing_cache.size()) {
                if (m_editing_cache[m_hover_id].selected)
                    unselect_point(m_hover_id);
                else {
                    if (!alt_down)
                        select_point(m_hover_id);
                }
            }

            return true;
        }

        // left down without selection rectangle - place point on the mesh:
        if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle.is_dragging() && !shift_down) {
            // If any point is in hover state, this should initiate its move - return control back to GLCanvas:
            if (m_hover_id != -1)
                return false;

            // If there is some selection, don't add new point and deselect everything instead.
            if (m_selection_empty) {
                std::pair<Vec3f, Vec3f> pos_and_normal;
                if (unproject_on_mesh(mouse_position, pos_and_normal)) { // we got an intersection
                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Add support point");
                    {
                        flush_process_top_fields_to_config();
                        sync_new_point_params_from_config();
                        sla::SupportPoint sp(pos_and_normal.first, m_new_point_head_diameter / 2.f, sla::SupportPointType::manual_add);
                        sp.weight = m_new_point_weight;
                        freeze_process_top_into_point(sp);
                        m_editing_cache.emplace_back(sp, false, pos_and_normal.second);
                    }
                    // Step 2.3 Mod 6: Re-register raycasters after adding a point
                    unregister_point_raycasters_for_picking();
                    register_point_raycasters_for_picking();
                    m_parent.set_as_dirty();
                    m_wait_for_up_event = true;
                }
                else
                    return false;
            }
            else
                select_point(NoPoints);

            return true;
        }

        // left up with selection rectangle - select points inside the rectangle:
        if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp || action == SLAGizmoEventType::AltUp) && m_selection_rectangle.is_dragging()) {
            // Is this a selection or deselection rectangle?
            GLSelectionRectangle::EState rectangle_status = m_selection_rectangle.get_state();

            // First collect positions of all the points in world coordinates.
            Geometry::Transformation trafo = mo->instances[active_inst]->get_transformation();
            trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_c->selection_info()->get_sla_shift()));
            std::vector<Vec3d> points;
            for (unsigned int i=0; i<m_editing_cache.size(); ++i)
                points.push_back(trafo.get_matrix() * m_editing_cache[i].support_point.pos.cast<double>());

            // Now ask the rectangle which of the points are inside.
            std::vector<Vec3f> points_inside;
            std::vector<unsigned int> points_idxs = m_selection_rectangle.contains(points);
            m_selection_rectangle.stop_dragging();
            for (size_t idx : points_idxs)
                points_inside.push_back(points[idx].cast<float>());

            // Only select/deselect points that are actually visible. We want to check not only
            // the point itself, but also the center of base of its cone, so the points don't hide
            // under every miniature irregularity on the model. Remember the actual number and
            // append the cone bases.
            size_t orig_pts_num = points_inside.size();
            for (size_t idx : points_idxs)
                points_inside.emplace_back((trafo.get_matrix().cast<float>() * (m_editing_cache[idx].support_point.pos + m_editing_cache[idx].normal)).cast<float>());

            for (size_t idx : m_c->raycaster()->raycaster()->get_unobscured_idxs(
                     trafo, wxGetApp().plater()->get_camera(), points_inside,
                     m_c->object_clipper()->get_clipping_plane()))
            {
                if (idx >= orig_pts_num) // this is a cone-base, get index of point it belongs to
                    idx -= orig_pts_num;
                if (rectangle_status == GLSelectionRectangle::Deselect)
                    unselect_point(points_idxs[idx]);
                else
                    select_point(points_idxs[idx]);
            }
            return true;
        }

        // left up with no selection rectangle
        if (action == SLAGizmoEventType::LeftUp) {
            if (m_wait_for_up_event) {
                m_wait_for_up_event = false;
            }
            return true;
        }

        // dragging the selection rectangle:
        if (action == SLAGizmoEventType::Dragging) {
            if (m_wait_for_up_event)
                return true; // point has been placed and the button not released yet
                             // this prevents GLCanvas from starting scene rotation

            if (m_selection_rectangle.is_dragging())  {
                m_selection_rectangle.dragging(mouse_position);
                return true;
            }

            return false;
        }

        if (action == SLAGizmoEventType::Delete) {
            // delete key pressed
            delete_selected_points();
            return true;
        }

        if (action ==  SLAGizmoEventType::ApplyChanges) {
            editing_mode_apply_changes();
            return true;
        }

        if (action ==  SLAGizmoEventType::DiscardChanges) {
            ask_about_changes_call_after([this](){ editing_mode_apply_changes(); },
                                         [this](){ editing_mode_discard_changes(); });
            return true;
        }

        if (action == SLAGizmoEventType::RightDown) {
            if (m_hover_id != -1) {
                select_point(NoPoints);
                select_point(m_hover_id);
                delete_selected_points();
                return true;
            }
            return false;
        }

        if (action == SLAGizmoEventType::SelectAll) {
            select_point(AllPoints);
            return true;
        }
    }

    if (!m_editing_mode) {
        if (action == SLAGizmoEventType::AutomaticGeneration) {
            auto_generate();
            return true;
        }

        if (action == SLAGizmoEventType::ManualEditing) {
            switch_to_editing_mode();
            return true;
        }
    }

    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::min(1., pos + 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::max(0., pos - 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position_by_ratio(-1., false);
        return true;
    }

    return false;
}

void GLGizmoSlaSupports::delete_selected_points(bool force)
{
    if (! m_editing_mode)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Delete support point");

    for (unsigned int idx=0; idx<m_editing_cache.size(); ++idx) {
        if (m_editing_cache[idx].selected && (!m_editing_cache[idx].support_point.is_island() || !m_lock_unique_islands || force)) {
            m_editing_cache.erase(m_editing_cache.begin() + (idx--));
        }
    }

    // Step 2.3 Mod 6: Re-register raycasters after deleting points
    unregister_point_raycasters_for_picking();
    register_point_raycasters_for_picking();
    select_point(NoPoints);
}

void GLGizmoSlaSupports::on_dragging(const UpdateData& data)
{
    if (! m_editing_mode)
        return;
    else {
        if (m_hover_id >= 0 && m_hover_id < (int) m_editing_cache.size()
            && (! m_editing_cache[m_hover_id].support_point.is_island() || !m_lock_unique_islands)) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (! unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
                return;
            m_editing_cache[m_hover_id].support_point.pos = pos_and_normal.first;
            // Dragging promotes any auto-generated point to manual_add (user takes responsibility)
            m_editing_cache[m_hover_id].support_point.type = sla::SupportPointType::manual_add;
            freeze_process_top_into_point(m_editing_cache[m_hover_id].support_point);
            m_editing_cache[m_hover_id].normal = pos_and_normal.second;
        }
    }
}

std::vector<const ConfigOption*> GLGizmoSlaSupports::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<const ConfigOption*> out;
    const ModelObject* mo = m_c->selection_info()->model_object();

    if (! mo)
        return out;

    const DynamicPrintConfig& object_cfg = mo->config.get();
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        if (object_cfg.has(key))
            out.push_back(object_cfg.option(key));
        else
            if (print_cfg.has(key))
                out.push_back(print_cfg.option(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.push_back(default_cfg->option(key));
            }
    }

    return out;
}



/*
void GLGizmoSlaSupports::find_intersecting_facets(const igl::AABB<Eigen::MatrixXf, 3>* aabb, const Vec3f& normal, double offset, std::vector<unsigned int>& idxs) const
{
    if (aabb->is_leaf()) { // this is a facet
        // corner.dot(normal) - offset
        idxs.push_back(aabb->m_primitive);
    }
    else { // not a leaf
    using CornerType = Eigen::AlignedBox<float, 3>::CornerType;
        bool sign = std::signbit(offset - normal.dot(aabb->m_box.corner(CornerType(0))));
        for (unsigned int i=1; i<8; ++i)
            if (std::signbit(offset - normal.dot(aabb->m_box.corner(CornerType(i)))) != sign) {
                find_intersecting_facets(aabb->m_left, normal, offset, idxs);
                find_intersecting_facets(aabb->m_right, normal, offset, idxs);
            }
    }
}



void GLGizmoSlaSupports::make_line_segments() const
{
    TriangleMeshSlicer tms(&m_c->m_model_object->volumes.front()->mesh);
    Vec3f normal(0.f, 1.f, 1.f);
    double d = 0.;

    std::vector<IntersectionLine> lines;
    find_intersections(&m_AABB, normal, d, lines);
    ExPolygons expolys;
    tms.make_expolygons_simple(lines, &expolys);

    SVG svg("slice_loops.svg", get_extents(expolys));
    svg.draw(expolys);
    //for (const IntersectionLine &l : lines[i])
    //    svg.draw(l, "red", 0);
    //svg.draw_outline(expolygons, "black", "blue", 0);
    svg.Close();
}
*/


void GLGizmoSlaSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    // Re-init icons when scale or line height changes.
    static float rendered_line_height = 0.f;
    if (float line_height = ImGui::GetTextLineHeightWithSpacing();
        m_icons.empty() || rendered_line_height != line_height) {
        rendered_line_height = line_height;
        float width = std::round(line_height / 8.f + 1.f) * 8.f;
        m_icons = init_support_icons(m_icon_manager, ImVec2{width, width});
    }

    if (!m_c->selection_info())
        return;
    ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo)
        return;
    // Stale detection: SelectionInfo may hold a dangling pointer after undo replaces
    // the model. Compare against the current selection before passing mo to render functions.
    {
        const Selection& sel = m_parent.get_selection();
        const int obj_idx = sel.get_object_idx();
        if (obj_idx < 0 || !sel.get_model() || obj_idx >= (int)sel.get_model()->objects.size()
            || mo != sel.get_model()->objects[obj_idx])
            return;
    }

    (void)bottom_limit;

    if (m_editing_mode)
        render_manual_support_panel(x, y, 0.f, mo);
    else
        render_auto_support_panel(x, y, 0.f, mo);
}

// ─────────────────────────────────────────────────────────────────────────────
// sp_draw_custom_slider — orange/gray triangle-pointer drag slider.
// s_min/s_max: visual drag range.  v_min/v_max: final value clamp (keyboard input).
// Returns true when the value changed this frame via mouse drag.
static bool sp_draw_custom_slider(const char* id, float& v,
                                   float s_min, float s_max,
                                   float v_min, float v_max,
                                   float bar_w, bool enabled = true)
{
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  fh  = ImGui::GetFrameHeight();

    constexpr float kBarH     = 3.f;
    constexpr float kGap      = 2.f;
    constexpr float kTriH     = 7.f;
    constexpr float kTriW     = 10.f;
    constexpr float kRounding = 1.5f;

    const float block_h = kBarH + kGap + kTriH;
    const float v_off   = std::floor((fh - block_h) * 0.5f);
    const float bar_y0  = pos.y + v_off;
    const float bar_y1  = bar_y0 + kBarH;
    const float tri_ay  = bar_y1 + kGap;
    const float tri_by  = tri_ay + kTriH;

    ImGui::InvisibleButton(id, ImVec2(bar_w, fh));
    const bool active  = ImGui::IsItemActive();
    bool       changed = false;

    if (active && enabled) {
        const float t  = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / bar_w, 0.f, 1.f);
        float       nv = s_min + t * (s_max - s_min);
        nv = std::clamp(nv, v_min, v_max);
        if (nv != v) { v = nv; changed = true; }
    }

    const float t_draw = (s_max > s_min)
        ? std::clamp((v - s_min) / (s_max - s_min), 0.f, 1.f) : 0.f;
    const float hx = pos.x + t_draw * bar_w;

    constexpr ImU32 kOrangeU32 = IM_COL32(232, 107, 32, 255);
    constexpr ImU32 kGrayU32   = IM_COL32(190, 190, 190, 255);

    if (hx > pos.x)
        dl->AddRectFilled(ImVec2(pos.x, bar_y0), ImVec2(hx,          bar_y1), kOrangeU32, kRounding);
    if (hx < pos.x + bar_w)
        dl->AddRectFilled(ImVec2(hx,    bar_y0), ImVec2(pos.x+bar_w, bar_y1), kGrayU32,   kRounding);

    dl->AddTriangleFilled(
        ImVec2(hx,               tri_ay),
        ImVec2(hx - kTriW * .5f, tri_by),
        ImVec2(hx + kTriW * .5f, tri_by),
        kOrangeU32);

    return changed;
}


// ─────────────────────────────────────────────────────────────────────────────
// render_auto_support_panel — Auto Support UI panel
// ─────────────────────────────────────────────────────────────────────────────
void GLGizmoSlaSupports::render_auto_support_panel(float x, float y, float legacy_panel_h, ModelObject* mo)
{
    // Existing auto-generated points: treat current UI as already applied until user changes preset/density.
    if (!m_auto_baseline_initialized && mo && !m_normal_cache.empty()) {
        mark_auto_settings_applied(mo);
        m_auto_baseline_initialized = true;
    }

    const float scale       = m_parent.get_scale();
    const float gap         = 8.f * scale;
    const float value_box_w = m_imgui->scaled(4.f);
    const float slider_w    = m_imgui->scaled(8.f);
    const float spacing_x   = m_imgui->get_item_spacing().x;
    const float fp          = ImGui::GetStyle().FramePadding.x * 2.f;

    const float label_col_w = m_imgui->calc_text_size(_L("Lock supports under new islands")).x + m_imgui->scaled(1.5f);
    const float content_w   = label_col_w + slider_w + spacing_x + value_box_w;
    const float preset_w    = (content_w - spacing_x * 2.f) / 3.f;
    const float view_btn_w  = (content_w - spacing_x) * 0.5f;
    const float btn_rem_w   = m_imgui->calc_text_size(_L("Remove all")).x + fp + m_imgui->scaled(1.f);
    const float btn_apply_w = m_imgui->calc_text_size(_L("Apply")).x      + fp + m_imgui->scaled(1.f);

    // Color palette
    const ImVec4 kOrange = {0.91f, 0.42f, 0.13f, 1.f};
    const ImVec4 kWhite  = {1.f,   1.f,   1.f,   1.f};

    ImGuiWrapper::push_toolbar_style(scale);
    const ImVec4 kWindowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool   is_dark   = (kWindowBg.x < 0.5f);

    // Preserve x padding from style push; zero top padding so tabs flush to window top.
    const float win_pad_x = ImGui::GetStyle().WindowPadding.x;
    const float win_pad_y = ImGui::GetStyle().WindowPadding.y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_pad_x, 0.f));

    // min_panel_w: sized to fit Manual panel's wider 5-button bottom row.
    const float btn_sp         = 6.f * scale;
    const float icon_sz_btm    = 25.f * scale;
    const float btn_rem_sel_w2 = m_imgui->calc_text_size(_L("Remove selected")).x + fp + m_imgui->scaled(1.f);
    const float btn_rem_all_w2 = m_imgui->calc_text_size(_L("Remove all")).x      + fp + m_imgui->scaled(1.f);
    const float btn_apply_w2   = m_imgui->calc_text_size(_L("Apply")).x           + fp + m_imgui->scaled(1.f);
    const float btn_discard_w2 = m_imgui->calc_text_size(_L("Discard")).x         + fp + m_imgui->scaled(1.f);
    const float min_panel_w    = 2.f * win_pad_x + icon_sz_btm + btn_sp
        + btn_rem_sel_w2 + btn_sp + btn_rem_all_w2 + btn_sp + btn_apply_w2 + btn_sp + btn_discard_w2;
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_panel_w, 0.f), ImVec2(FLT_MAX, FLT_MAX));

    static float panel_w = 0.f;
    float panel_x = x;
    GizmoImguiSetNextWIndowPos(panel_x, y + legacy_panel_h + gap, panel_w, 0.f, ImGuiCond_Always);
    m_imgui->begin(wxString("SupportNewUI_Auto"),
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // ── Row 1: Tab header — Auto (active) | Manual (inactive) ───────────────
    {
        ImDrawList*  dl       = ImGui::GetWindowDrawList();
        const ImVec2 win_pos  = ImGui::GetWindowPos();
        const float  full_w   = ImGui::GetWindowWidth();
        const float  half_w   = full_w * 0.5f;
        const float  font_h   = ImGui::GetTextLineHeight();
        const float  tab_h    = font_h + 8.f * scale;
        const float  rounding = 4.f * scale;

        const ImVec4 bg_vec    = kWindowBg;
        const ImVec4 act_bg    = is_dark ? ImVec4{0.30f, 0.20f, 0.08f, 1.f} : ImVec4{1.0f, 0.9216f, 0.8863f, 1.0f};
        const ImVec4 act_txt   = kOrange;
        const ImVec4 inact_txt = is_dark ? ImVec4{0.72f, 0.72f, 0.72f, 1.f} : ImVec4{0.35f, 0.35f,  0.35f,   1.f};
        const ImVec4 sep_col   = is_dark ? ImVec4{0.35f, 0.35f, 0.35f, 1.f} : ImVec4{0.80f, 0.80f,  0.80f,   1.f};

        auto to_u32 = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };

        const ImVec2 l0 = {win_pos.x,         win_pos.y};
        const ImVec2 l1 = {win_pos.x + half_w, win_pos.y + tab_h};
        const ImVec2 r0 = {win_pos.x + half_w, win_pos.y};
        const ImVec2 r1 = {win_pos.x + full_w, win_pos.y + tab_h};

        // Widen clip to full window width so rects are not trimmed by window padding
        dl->PushClipRect({win_pos.x, win_pos.y}, {win_pos.x + full_w, win_pos.y + tab_h}, false);

        // Left (Auto) — active: flush left→midline, rounded top-left outer corner
        dl->AddRectFilled(l0, l1, to_u32(act_bg), rounding, ImDrawFlags_RoundCornersTopLeft);
        // Right (Manual) — inactive: window bg, no rounding needed
        dl->AddRectFilled(r0, r1, to_u32(bg_vec));

        const std::string auto_str   = _u8L("Auto Support");
        const std::string manual_str = _u8L("Manual Support");
        const ImVec2 auto_sz   = ImGui::CalcTextSize(auto_str.c_str());
        const ImVec2 manual_sz = ImGui::CalcTextSize(manual_str.c_str());

        dl->AddText({l0.x + (half_w - auto_sz.x)  * 0.5f, l0.y + (tab_h - auto_sz.y)  * 0.5f},
                    to_u32(act_txt),   auto_str.c_str());
        dl->AddText({r0.x + (half_w - manual_sz.x) * 0.5f, r0.y + (tab_h - manual_sz.y) * 0.5f},
                    to_u32(inact_txt), manual_str.c_str());

        dl->AddLine({l0.x, l1.y}, {r1.x, r1.y}, to_u32(sep_col), 1.f);

        dl->PopClipRect();

        // InvisibleButton click detection positioned at content start
        ImGui::SetCursorScreenPos({win_pos.x + win_pad_x, win_pos.y});
        ImGui::InvisibleButton("##sp_auto_tab_l", {half_w - win_pad_x,          tab_h});
        // Left tab = already active, no click action needed

        ImGui::SameLine(0.f, 0.f);
        ImGui::InvisibleButton("##sp_auto_tab_r", {full_w - half_w - win_pad_x, tab_h});
        if (ImGui::IsItemClicked())
            switch_to_editing_mode();

        // Restore cursor below the tab band with one ItemSpacing.y gap
        ImGui::SetCursorScreenPos({win_pos.x + win_pad_x, win_pos.y + tab_h + ImGui::GetStyle().ItemSpacing.y});
    }

    // ── Row 2: Preset buttons — Light / Middle / Heavy ───────────────────
    {
        auto draw_preset_btn = [&](const char* label, sla::SupportWeight w) {
            const bool active = (m_new_point_weight == w);
            if (active) {
                const ImVec4 pbg = is_dark ? ImVec4{0.30f, 0.20f, 0.08f, 1.f} : kWhite;
                const ImVec4 phv = is_dark ? ImVec4{0.38f, 0.26f, 0.12f, 1.f} : ImVec4{0.98f, 0.95f, 0.92f, 1.f};
                ImGui::PushStyleColor(ImGuiCol_Button,        pbg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, phv);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pbg);
                ImGui::PushStyleColor(ImGuiCol_Text,          kOrange);
                ImGui::PushStyleColor(ImGuiCol_Border,        kOrange);
            }
            if (ImGui::Button(label, ImVec2(preset_w, 0.f))) {
                m_new_point_weight = w;
                apply_weight_preset(w);
            }
            if (active) ImGui::PopStyleColor(5);
        };
        draw_preset_btn(_u8L("Light").c_str(),  sla::SupportWeight::Light);
        ImGui::SameLine();
        draw_preset_btn(_u8L("Middle").c_str(), sla::SupportWeight::Medium);
        ImGui::SameLine();
        draw_preset_btn(_u8L("Heavy").c_str(),  sla::SupportWeight::Heavy);
    }

    // ── Row 3: Structure / Points view-mode toggle ────────────────────────
    {
        auto draw_view_btn = [&](const char* label, bool is_active, auto on_click) {
            if (is_active) {
                const ImVec4 vbg = is_dark ? ImVec4{0.30f, 0.20f, 0.08f, 1.f} : kWhite;
                const ImVec4 vhv = is_dark ? ImVec4{0.38f, 0.26f, 0.12f, 1.f} : ImVec4{0.98f, 0.95f, 0.92f, 1.f};
                ImGui::PushStyleColor(ImGuiCol_Button,        vbg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vhv);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  vbg);
                ImGui::PushStyleColor(ImGuiCol_Text,          kOrange);
                ImGui::PushStyleColor(ImGuiCol_Border,        kOrange);
            }
            const bool clicked = ImGui::Button(label, ImVec2(view_btn_w, 0.f));
            if (is_active) ImGui::PopStyleColor(5);
            if (clicked && !is_active) on_click();
        };

        draw_view_btn(_u8L("Structure").c_str(), m_show_support_structure, [&](){
            m_show_support_structure = true;
            show_sla_supports(true);
            // Drop Points-preview picking raycasters so the (now hidden) cones do
            // not intercept hover/click on the support/pad mesh. In editing mode
            // we keep them: the user still needs to interact with points.
            if (!m_editing_mode)
                unregister_point_raycasters_for_picking();
            if (!m_normal_cache.empty())
                reslice_until_step(slaposPad);
            m_parent.set_as_dirty();
        });
        ImGui::SameLine();
        draw_view_btn(_u8L("Points").c_str(), !m_show_support_structure, [&](){
            m_show_support_structure = false;
            show_sla_supports(false);
            // Restore Points-preview picking. register_point_raycasters_for_picking()
            // is a no-op outside editing mode and when the list is already populated,
            // so the call is safe in both Auto and Manual contexts.
            register_point_raycasters_for_picking();
            m_parent.set_as_dirty();
        });
    }

    // ── Row 4: Density — custom slider + InputFloat ───────────────────────
    {
        const char* density_key = "support_points_density_relative";
        float density = static_cast<float>(
            static_cast<const ConfigOptionInt*>(get_config_options({density_key})[0])->value);

        static bool  sp_density_dragging = false;
        static float sp_density_stash    = 100.f;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(_L("Density"));
        ImGui::SameLine(label_col_w);

        const bool sl_ch  = sp_draw_custom_slider("##sp_density", density,
                                                   50.f, 200.f, 50.f, 200.f,
                                                   slider_w, is_input_enabled());
        if (ImGui::IsItemActivated())  { sp_density_stash = density; sp_density_dragging = false; }
        if (sl_ch)                     { sp_density_dragging = true; mo->config.set(density_key, (int)density); }
        if (ImGui::IsItemDeactivated() && sp_density_dragging) {
            mo->config.set(density_key, (int)sp_density_stash);
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Support density change");
            mo->config.set(density_key, (int)density);
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            sp_density_dragging = false;
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(value_box_w);
        float display_density = density;
        ImGui::InputFloat("##sp_density_in", &display_density, 0.f, 0.f, "%.f%%");
        if (ImGui::IsItemDeactivatedAfterEdit() && is_input_enabled()) {
            density = std::clamp(display_density, 50.f, 200.f);
            mo->config.set(density_key, (int)density);
            wxGetApp().obj_list()->update_and_show_object_settings_item();
        }
        ImGui::PopItemWidth();
    }

    // ── Row 5: [?] | Remove all | Apply ──────────────────────────────────
    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * scale, 4.f * scale));
    {
        ImTextureID nid = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
        ImTextureID hid = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);
        const float icon_sz = 25.f * scale;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {0.f, 0.f});
        ImGui::ImageButton3(nid, hid, ImVec2(icon_sz, icon_sz));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            m_imgui->text(_L("Auto-generate support points using density and Support Angle (Process → Bridge)."));
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar(2);

        ImGui::SameLine();
        m_imgui->disabled_begin(m_normal_cache.empty());
        if (ImGui::Button((_u8L("Remove all") + "##sp_auto_rem").c_str(), ImVec2(btn_rem_w, 0.f))) {
            bool was_editing = m_editing_mode;
            if (!was_editing) switch_to_editing_mode();
            select_point(AllPoints);
            delete_selected_points(true);
            if (!was_editing) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Remove all support points");
                apply_remove_all();
                disable_editing_mode();
            }
        }
        m_imgui->disabled_end();

        ImGui::SameLine();
        m_imgui->disabled_begin(!auto_settings_need_apply(mo));
        if (ImGui::Button((_u8L("Apply") + "##sp_auto_apply").c_str(), ImVec2(btn_apply_w, 0.f)))
            auto_generate();
        m_imgui->disabled_end();
    }
    ImGui::PopStyleVar(); // ItemSpacing

    ImGui::Dummy(ImVec2(0.f, win_pad_y));

    panel_w = ImGui::GetWindowWidth();
    m_imgui->end();
    ImGui::PopStyleVar(); // WindowPadding (y=0 override)
    ImGuiWrapper::pop_toolbar_style();
}


bool GLGizmoSlaSupports::auto_settings_need_apply(const ModelObject* mo) const
{
    if (m_normal_cache.empty())
        return true;

    const char* density_key = "support_points_density_relative";
    const auto  opts        = get_config_options({density_key});
    const int   density     = (opts.empty() || !mo)
        ? 100
        : static_cast<const ConfigOptionInt*>(opts[0])->value;

    const char* angle_key = "support_critical_angle";
    const auto  angle_opts = get_config_options({angle_key});
    const float critical_angle = (angle_opts.empty() || !mo)
        ? 0.f
        : static_cast<float>(static_cast<const ConfigOptionFloat*>(angle_opts[0])->value);

    if (m_new_point_weight != m_applied_auto_weight)
        return true;
    if (density != m_applied_auto_density)
        return true;
    if (std::abs(critical_angle - m_applied_auto_critical_angle) > 1e-3f)
        return true;
    return false;
}

void GLGizmoSlaSupports::mark_auto_settings_applied(const ModelObject* mo)
{
    m_applied_auto_weight = m_new_point_weight;

    const char* density_key = "support_points_density_relative";
    const auto  opts        = get_config_options({density_key});
    m_applied_auto_density  = (opts.empty() || !mo)
        ? 100
        : static_cast<const ConfigOptionInt*>(opts[0])->value;

    const char* angle_key = "support_critical_angle";
    const auto  angle_opts = get_config_options({angle_key});
    m_applied_auto_critical_angle = (angle_opts.empty() || !mo)
        ? 0.f
        : static_cast<float>(static_cast<const ConfigOptionFloat*>(angle_opts[0])->value);
}


// ─────────────────────────────────────────────────────────────────────────────
// render_manual_support_panel — Manual Support UI panel
// ─────────────────────────────────────────────────────────────────────────────
void GLGizmoSlaSupports::render_manual_support_panel(float x, float y, float legacy_panel_h, ModelObject* mo)
{
    const float scale       = m_parent.get_scale();
    const float gap         = 8.f * scale;
    const float spacing_x   = m_imgui->get_item_spacing().x;
    const float fp          = ImGui::GetStyle().FramePadding.x * 2.f;

    const float btn_rem_sel_w = m_imgui->calc_text_size(_L("Remove selected")).x + fp + m_imgui->scaled(1.f);
    const float btn_rem_all_w = m_imgui->calc_text_size(_L("Remove all")).x      + fp + m_imgui->scaled(1.f);
    const float btn_apply_w   = m_imgui->calc_text_size(_L("Apply")).x           + fp + m_imgui->scaled(1.f);
    const float btn_discard_w = m_imgui->calc_text_size(_L("Discard")).x         + fp + m_imgui->scaled(1.f);

    const ImVec4 kOrange = {0.91f, 0.42f, 0.13f, 1.f};
    const ImVec4 kWhite  = {1.f,   1.f,   1.f,   1.f};

    ImGuiWrapper::push_toolbar_style(scale);
    const ImVec4 kWindowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool   is_dark   = (kWindowBg.x < 0.5f);

    // Preserve x padding from style push; zero top padding so tabs flush to window top.
    const float win_pad_x = ImGui::GetStyle().WindowPadding.x;
    const float win_pad_y = ImGui::GetStyle().WindowPadding.y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_pad_x, 0.f));

    // min_panel_w: sized to fit Manual panel's own 5-button bottom row.
    const float btn_sp      = 6.f * scale;
    const float icon_sz_btm = 25.f * scale;
    const float min_panel_w = 2.f * win_pad_x + icon_sz_btm + btn_sp
        + btn_rem_sel_w + btn_sp + btn_rem_all_w + btn_sp + btn_apply_w + btn_sp + btn_discard_w;
    // Light / Middle / Heavy: equal width, span full panel content area.
    const float content_w = min_panel_w - 2.f * win_pad_x;
    const float preset_w  = (content_w - spacing_x * 2.f) / 3.f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_panel_w, 0.f), ImVec2(FLT_MAX, FLT_MAX));

    static float panel_w = 0.f;
    float panel_x = x;
    GizmoImguiSetNextWIndowPos(panel_x, y + legacy_panel_h + gap, panel_w, 0.f, ImGuiCond_Always);
    m_imgui->begin(wxString("SupportNewUI_Manual"),
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // ── Row 1: Tab header — Auto (inactive) | Manual (active) ───────────────
    {
        ImDrawList*  dl       = ImGui::GetWindowDrawList();
        const ImVec2 win_pos  = ImGui::GetWindowPos();
        const float  full_w   = ImGui::GetWindowWidth();
        const float  half_w   = full_w * 0.5f;
        const float  font_h   = ImGui::GetTextLineHeight();
        const float  tab_h    = font_h + 8.f * scale;
        const float  rounding = 4.f * scale;

        const ImVec4 bg_vec    = kWindowBg;
        const ImVec4 act_bg    = is_dark ? ImVec4{0.30f, 0.20f, 0.08f, 1.f} : ImVec4{1.0f, 0.9216f, 0.8863f, 1.0f};
        const ImVec4 act_txt   = kOrange;
        const ImVec4 inact_txt = is_dark ? ImVec4{0.72f, 0.72f, 0.72f, 1.f} : ImVec4{0.35f, 0.35f,  0.35f,   1.f};
        const ImVec4 sep_col   = is_dark ? ImVec4{0.35f, 0.35f, 0.35f, 1.f} : ImVec4{0.80f, 0.80f,  0.80f,   1.f};

        auto to_u32 = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };

        const ImVec2 l0 = {win_pos.x,         win_pos.y};
        const ImVec2 l1 = {win_pos.x + half_w, win_pos.y + tab_h};
        const ImVec2 r0 = {win_pos.x + half_w, win_pos.y};
        const ImVec2 r1 = {win_pos.x + full_w, win_pos.y + tab_h};

        // Widen clip to full window width so rects are not trimmed by window padding
        dl->PushClipRect({win_pos.x, win_pos.y}, {win_pos.x + full_w, win_pos.y + tab_h}, false);

        // Left (Auto) — inactive: window bg, no rounding needed
        dl->AddRectFilled(l0, l1, to_u32(bg_vec));
        // Right (Manual) — active: flush midline→right edge, rounded top-right outer corner
        dl->AddRectFilled(r0, r1, to_u32(act_bg), rounding, ImDrawFlags_RoundCornersTopRight);

        const std::string auto_str   = _u8L("Auto Support");
        const std::string manual_str = _u8L("Manual Support");
        const ImVec2 auto_sz   = ImGui::CalcTextSize(auto_str.c_str());
        const ImVec2 manual_sz = ImGui::CalcTextSize(manual_str.c_str());

        dl->AddText({l0.x + (half_w - auto_sz.x)  * 0.5f, l0.y + (tab_h - auto_sz.y)  * 0.5f},
                    to_u32(inact_txt), auto_str.c_str());
        dl->AddText({r0.x + (half_w - manual_sz.x) * 0.5f, r0.y + (tab_h - manual_sz.y) * 0.5f},
                    to_u32(act_txt),   manual_str.c_str());

        dl->AddLine({l0.x, l1.y}, {r1.x, r1.y}, to_u32(sep_col), 1.f);

        dl->PopClipRect();

        // InvisibleButton click detection positioned at content start
        ImGui::SetCursorScreenPos({win_pos.x + win_pad_x, win_pos.y});
        ImGui::InvisibleButton("##sp_man_tab_l", {half_w - win_pad_x,          tab_h});
        if (ImGui::IsItemClicked())
            disable_editing_mode();

        ImGui::SameLine(0.f, 0.f);
        ImGui::InvisibleButton("##sp_man_tab_r", {full_w - half_w - win_pad_x, tab_h});
        // Right tab = already active, no click action needed

        // Restore cursor below the tab band with one ItemSpacing.y gap
        ImGui::SetCursorScreenPos({win_pos.x + win_pad_x, win_pos.y + tab_h + ImGui::GetStyle().ItemSpacing.y});
    }

    // ── Row 2: Preset buttons — Light / Middle / Heavy ───────────────────
    {
        auto draw_preset_btn = [&](const char* label, sla::SupportWeight w) {
            const bool active = (m_new_point_weight == w);
            if (active) {
                const ImVec4 pbg = is_dark ? ImVec4{0.30f, 0.20f, 0.08f, 1.f} : kWhite;
                const ImVec4 phv = is_dark ? ImVec4{0.38f, 0.26f, 0.12f, 1.f} : ImVec4{0.98f, 0.95f, 0.92f, 1.f};
                ImGui::PushStyleColor(ImGuiCol_Button,        pbg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, phv);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pbg);
                ImGui::PushStyleColor(ImGuiCol_Text,          kOrange);
                ImGui::PushStyleColor(ImGuiCol_Border,        kOrange);
            }
            if (ImGui::Button(label, ImVec2(preset_w, 0.f))) {
                m_new_point_weight = w;
                apply_weight_preset(w);
            }
            if (active) ImGui::PopStyleColor(5);
        };
        draw_preset_btn(_u8L("Light").c_str(),  sla::SupportWeight::Light);
        ImGui::SameLine();
        draw_preset_btn(_u8L("Middle").c_str(), sla::SupportWeight::Medium);
        ImGui::SameLine();
        draw_preset_btn(_u8L("Heavy").c_str(),  sla::SupportWeight::Heavy);
    }

    // ── Row 3: Lock — label on left, checkbox pushed to right edge ──────────
    {
        const bool was_locked = m_lock_unique_islands;
        ImGui::AlignTextToFramePadding();
        m_imgui->text(_L("Lock supports under new islands"));
        ImGui::SameLine(content_w - ImGui::GetFrameHeight());
        if (was_locked) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.91f, 0.42f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.91f, 0.42f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.91f, 0.42f, 0.13f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark,      ImVec4(1.f,   1.f,   1.f,   1.f));
        }
        bool lock_val = m_lock_unique_islands;
        if (ImGui::BBLCheckbox("##sp_lock", &lock_val))
            m_lock_unique_islands = lock_val;
        if (was_locked) ImGui::PopStyleColor(4);
    }

    // ── Row 5: [?] | Remove selected | Remove all | Apply | Discard ──────
    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * scale, 4.f * scale));
    {
        ImTextureID nid = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
        ImTextureID hid = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);
        const float icon_sz = 25.f * scale;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {0.f, 0.f});
        ImGui::ImageButton3(nid, hid, ImVec2(icon_sz, icon_sz));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            m_imgui->text(_L("Left-click: add support point\nRight-click: remove point"));
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar(2);

        ImGui::SameLine();
        m_imgui->disabled_begin(m_selection_empty);
        if (ImGui::Button((_u8L("Remove selected") + "##sp_man_rem_sel").c_str(), ImVec2(btn_rem_sel_w, 0.f)))
            delete_selected_points(false);
        m_imgui->disabled_end();

        ImGui::SameLine();
        m_imgui->disabled_begin(m_editing_cache.empty());
        if (ImGui::Button((_u8L("Remove all") + "##sp_man_rem_all").c_str(), ImVec2(btn_rem_all_w, 0.f))) {
            select_point(AllPoints);
            delete_selected_points(true);
        }
        m_imgui->disabled_end();

        ImGui::SameLine();
        m_imgui->disabled_begin(!unsaved_changes());
        if (ImGui::Button((_u8L("Apply") + "##sp_man_apply").c_str(), ImVec2(btn_apply_w, 0.f))) {
            commit_manual_edits_keep_editing(true);
        }
        m_imgui->disabled_end();

        ImGui::SameLine();
        m_imgui->disabled_begin(!unsaved_changes());
        if (ImGui::Button((_u8L("Discard") + "##sp_man_discard").c_str(), ImVec2(btn_discard_w, 0.f))) {
            revert_manual_edits_keep_editing();
        }
        m_imgui->disabled_end();
    }
    ImGui::PopStyleVar(); // ItemSpacing

    ImGui::Dummy(ImVec2(0.f, win_pad_y));

    panel_w = ImGui::GetWindowWidth();
    m_imgui->end();
    ImGui::PopStyleVar(); // WindowPadding (y=0 override)
    ImGuiWrapper::pop_toolbar_style();
}


bool GLGizmoSlaSupports::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_from_single_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    return true;
}

bool GLGizmoSlaSupports::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoSlaSupports::on_get_name() const
{
    return _u8L("SLA Support Points");
}

// Step 4.2: on_get_requirements() removed — inherited from GLGizmoSlaBase.
// The base class provides: SelectionInfo | InstancesHider | Raycaster | ObjectClipper | SupportsClipper.
// HollowedMesh is intentionally excluded (not needed after the refactor).



void GLGizmoSlaSupports::ask_about_changes_call_after(std::function<void()> on_yes, std::function<void()> on_no)
{
    wxGetApp().CallAfter([on_yes, on_no]() {
        // Following is called through CallAfter, because otherwise there was a problem
        // on OSX with the wxMessageDialog being shown several times when clicked into.
        MessageDialog dlg(GUI::wxGetApp().mainframe, _L("Do you want to save your manually "
            "edited support points?") + "\n",_L("Save support points?"), wxICON_QUESTION | wxYES | wxNO | wxCANCEL );
        int ret = dlg.ShowModal();
            if (ret == wxID_YES)
                on_yes();
            else if (ret == wxID_NO)
                on_no();
    });
}


void GLGizmoSlaSupports::sync_new_point_params_from_config()
{
    m_new_point_head_diameter = clamp_support_diameter_mm(
        process_top_float_live("support_head_front_diameter", 0.4f));
    m_new_point_pillar_diameter = process_top_float_live("support_pillar_diameter", 1.f);
}

void GLGizmoSlaSupports::freeze_process_top_into_point(sla::SupportPoint &sp) const
{
    sp.head_front_radius = clamp_support_diameter_mm(
        process_top_float_live("support_head_front_diameter", m_new_point_head_diameter)) / 2.f;
    sp.pillar_radius = process_top_float_live("support_pillar_diameter", m_new_point_pillar_diameter) / 2.f;
    sp.head_penetration_mm = clamp_contact_depth(process_top_float_live("support_head_penetration", 0.4f));
    sp.head_width_mm = clamp_segment_length_mm(process_top_float_live("support_segment_length", 2.0f));
    sp.base_radius_mm = clamp_support_diameter_mm(
        process_top_float_live("support_base_diameter", 2.0f)) * 0.5f;
    sp.support_bracing_angle_deg = process_top_float_live("angle_between_top_and_middle", 45.0f);
    // Make lower diameter (head_back_radius_mm) opt-in so that Pillar Diameter
    // (sp.pillar_radius) can drive the back/base radius by default.
    // Users can still override by editing "Lower Diameter" after selecting a point.
    sp.head_back_radius_mm = sla::SUPPORT_POINT_USE_PRESET;
    if (process_contact_type_is_sphere()) {
        float r = default_contact_sphere_radius_mm();
        if (r < k_min_support_size_mm)
            r = k_min_support_size_mm;
        sp.contact_sphere_radius = r;
    } else {
        sp.contact_sphere_radius = 0.f;
    }
}

bool GLGizmoSlaSupports::is_sla_support_top_option(const std::string &opt_key)
{
    for (const char *k : k_sla_support_top_opts)
        if (opt_key == k)
            return true;
    return false;
}

DynamicPrintConfig GLGizmoSlaSupports::support_top_config_from_selection() const
{
    DynamicPrintConfig cfg = sla_process_config();
    for (const CacheEntry &ce : m_editing_cache) {
        if (ce.selected) {
            support_top_apply_point(ce.support_point, cfg);
            break;
        }
    }
    return cfg;
}

bool GLGizmoSlaSupports::apply_process_top_option(const std::string &opt_key, const boost::any &value)
{
    if (!m_editing_mode || m_selection_empty)
        return false;

    auto apply_one = [&](sla::SupportPoint &sp) {
        if (opt_key == "support_contact_type") {
            const int ct = boost::any_cast<int>(value);
            if (ct == int(spSphere)) {
                sp.contact_sphere_radius = default_contact_sphere_radius_mm();
                sp.head_penetration_mm = clamp_contact_depth(sla::point_contact_front_depth_mm(sp, 0.4));
            } else {
                sp.contact_sphere_radius = 0.f;
            }
        } else if (opt_key == "support_contact_diameter") {
            sp.contact_sphere_radius = float(boost::any_cast<double>(value) * 0.5);
        } else if (opt_key == "support_head_penetration") {
            sp.head_penetration_mm = clamp_contact_depth(float(boost::any_cast<double>(value)));
        } else if (opt_key == "support_head_front_diameter") {
            const float d = clamp_support_diameter_mm(float(boost::any_cast<double>(value)));
            sp.head_front_radius = d * 0.5f;
            m_new_point_head_diameter = d;
        } else if (opt_key == "support_head_back_diameter") {
            sp.head_back_radius_mm = clamp_support_diameter_mm(float(boost::any_cast<double>(value))) * 0.5f;
        } else if (opt_key == "support_pillar_diameter") {
            // Pillar diameter controls sp.pillar_radius (radius = diameter/2).
            // Also clear explicit lower diameter override so the geometry follows the pillar diameter
            // by default.
            sp.pillar_radius = float(boost::any_cast<double>(value)) * 0.5f;
            sp.head_back_radius_mm = sla::SUPPORT_POINT_USE_PRESET;
            m_new_point_pillar_diameter = float(boost::any_cast<double>(value));
        } else if (opt_key == "support_segment_length") {
            sp.head_width_mm = clamp_segment_length_mm(float(boost::any_cast<double>(value)));
        }
    };

    bool changed = false;
    for (auto &ce : m_editing_cache) {
        if (!ce.selected)
            continue;
        apply_one(ce.support_point);
        changed = true;
    }
    if (changed)
        m_parent.set_as_dirty();
    return changed;
}

void GLGizmoSlaSupports::notify_process_tab_selection_changed()
{
    // Defer sidebar updates: calling Tab field set_value synchronously from gizmo mouse
    // handlers can re-enter wx event processing and crash.
    const bool editing       = m_editing_mode;
    const bool has_selection = !m_selection_empty;
    wxTheApp->CallAfter([this, editing, has_selection]() {
        if (m_state != On || !wxGetApp().plater())
            return;
        Tab *tab = wxGetApp().get_tab(Preset::TYPE_SLA_PRINT);
        auto *sla_tab = dynamic_cast<TabSLAPrint *>(tab);
        if (!sla_tab || !wxGetApp().preset_bundle)
            return;
        try {
            if (!editing || !has_selection) {
                sla_tab->end_support_point_top_field_display();
                return;
            }
            DynamicPrintConfig cfg = support_top_config_from_selection();
            sla_tab->begin_support_point_top_field_display(cfg);
        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(error) << "notify_process_tab_selection_changed: " << ex.what();
        }
    });
}

void GLGizmoSlaSupports::apply_weight_preset(sla::SupportWeight w)
{
    const auto &p = k_weight_presets[static_cast<int>(w)];
    auto &cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    cfg.set("support_pillar_diameter",     (double)p.pillar_diameter,     true);
    cfg.set("support_head_front_diameter", (double)p.head_front_diameter, true);
    cfg.set("support_contact_diameter",    (double)p.contact_diameter,    true);
    cfg.set("support_base_diameter",       (double)p.base_diameter,       true);
    cfg.set("support_base_height",         (double)p.base_height,         true);
    cfg.set("support_head_width",          (double)p.head_width,          true);
    cfg.set("support_head_back_diameter",  (double)p.pillar_diameter,     true);
    m_new_point_head_diameter   = p.head_front_diameter;
    m_new_point_pillar_diameter = p.pillar_diameter;
    m_new_point_weight          = w;
    wxTheApp->CallAfter([]() {
        auto *tab = wxGetApp().get_tab(Preset::TYPE_SLA_PRINT);
        if (!tab) return;
        tab->update();
        tab->reload_config();
    });
}

void GLGizmoSlaSupports::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on

        m_auto_baseline_initialized = false;
        // Set default head diameter from config.
        const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        const auto *opt_hfd = cfg.option<ConfigOptionFloat>("support_head_front_diameter");
        m_new_point_head_diameter = opt_hfd ? opt_hfd->value : k_weight_presets[1].head_front_diameter;
        // Match current pillar diameter against weight presets to restore radio state.
        const auto *opt_pd = cfg.option<ConfigOptionFloat>("support_pillar_diameter");
        float cur_pillar = opt_pd ? opt_pd->value : k_weight_presets[1].pillar_diameter;
        m_new_point_pillar_diameter = cur_pillar;

        sync_new_point_params_from_config();

        int matched = -1;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(k_weight_presets[i].pillar_diameter - m_new_point_pillar_diameter) < 1e-4f) {
                matched = i;
                break;
            }
        }
        m_new_point_weight = (matched >= 0)
            ? static_cast<sla::SupportWeight>(matched)
            : sla::SupportWeight::Medium;

        // Baseline for global support_base_diameter while manual editing is active.
        const DynamicPrintConfig &cfg_live = sla_process_config();
        if (const auto *opt_bd = cfg_live.option<ConfigOptionFloat>("support_base_diameter"))
            m_base_diameter_before_change = opt_bd->value;
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        bool will_ask = m_editing_mode && unsaved_changes() && on_is_activable();
        if (will_ask) {
            ask_about_changes_call_after([this](){ editing_mode_apply_changes(); },
                                         [this](){ editing_mode_discard_changes(); });
            // refuse to be turned off so the gizmo is active when the CallAfter is executed
            m_state = m_old_state;
        }
        else {
            // we are actually shutting down
            disable_editing_mode(); // so it is not active next time the gizmo opens
            m_old_mo_id = -1;
            // Step 4.2: restore full scene visibility when gizmo actually closes
            if (m_c && m_c->selection_info())
                m_c->selection_info()->set_use_config_elevation(false);
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
            m_c->instances_hider()->set_hide_full_scene(false);
        }
    }
    m_old_state = m_state;
}



void GLGizmoSlaSupports::on_start_dragging()
{
    if (!m_editing_mode || m_hover_id < 0 || m_hover_id >= (int) m_editing_cache.size()) {
        m_point_before_drag = CacheEntry();
        return;
    }

    for (size_t j = 0; j < m_editing_cache.size(); ++j)
        m_editing_cache[j].selected = (j == size_t(m_hover_id));
    m_selection_empty = false;
    const float pr = m_editing_cache[m_hover_id].support_point.pillar_radius;
    if (pr > 0.f)
        m_new_point_pillar_diameter = pr * 2.f;
    else
        sync_new_point_params_from_config();
    notify_process_tab_selection_changed();
    m_point_before_drag = m_editing_cache[m_hover_id];
}


void GLGizmoSlaSupports::on_stop_dragging()
{
    if (m_hover_id >= 0 && m_hover_id < (int) m_editing_cache.size()) {
        CacheEntry backup = m_editing_cache[m_hover_id];

        if (m_point_before_drag.support_point.pos != Vec3f::Zero() // some point was touched
         && backup.support_point.pos != m_point_before_drag.support_point.pos) // and it was moved, not just selected
        {
            m_editing_cache[m_hover_id] = m_point_before_drag;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Move support point");
            m_editing_cache[m_hover_id] = backup;
        }
    }
    m_point_before_drag = CacheEntry();
}



void GLGizmoSlaSupports::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_new_point_head_diameter,
       m_new_point_pillar_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_new_point_head_diameter,
       m_new_point_pillar_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::select_point(int i)
{
    if (! m_editing_mode)
        return;

    if (i == AllPoints || i == NoPoints) {
        for (auto& point_and_selection : m_editing_cache)
            point_and_selection.selected = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints && !m_editing_cache.empty()) {
            const float pr = m_editing_cache[0].support_point.pillar_radius;
            if (pr > 0.f)
                m_new_point_pillar_diameter = pr * 2.f;
            else
                sync_new_point_params_from_config();
        }
    }
    else {
        if (i < 0 || i >= (int) m_editing_cache.size())
            return;
        m_editing_cache[i].selected = true;
        m_selection_empty = false;
        const float pr = m_editing_cache[i].support_point.pillar_radius;
        if (pr > 0.f)
            m_new_point_pillar_diameter = pr * 2.f;
        else
            sync_new_point_params_from_config();
    }
    notify_process_tab_selection_changed();
}


void GLGizmoSlaSupports::unselect_point(int i)
{
    if (! m_editing_mode)
        return;

    if (i < 0 || i >= (int) m_editing_cache.size())
        return;

    m_editing_cache[i].selected = false;
    m_selection_empty = true;
    for (const CacheEntry& ce : m_editing_cache) {
        if (ce.selected) {
            m_selection_empty = false;
            break;
        }
    }
    notify_process_tab_selection_changed();
}




void GLGizmoSlaSupports::editing_mode_discard_changes()
{
    if (! m_editing_mode)
        return;
    select_point(NoPoints);
    disable_editing_mode();
}

void GLGizmoSlaSupports::commit_manual_edits_keep_editing(bool reslice_preview)
{
    if (!m_editing_mode || !unsaved_changes())
        return;

    m_normal_cache.clear();
    for (const CacheEntry& ce : m_editing_cache)
        m_normal_cache.push_back(ce.support_point);

    wxGetApp().plater()->leave_gizmos_stack();
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Support points edit");

    ModelObject* mo_apply = m_c->selection_info()->model_object();
    mo_apply->sla_points_status = sla::PointsStatus::UserModified;
    mo_apply->sla_support_points.clear();
    mo_apply->sla_support_points = m_normal_cache;
    sync_generate_support_for_object(mo_apply, !m_normal_cache.empty());

    wxGetApp().plater()->enter_gizmos_stack();

    m_editing_cache.clear();
    for (const sla::SupportPoint& sp : m_normal_cache)
        m_editing_cache.emplace_back(sp);

    if (reslice_preview) {
        if (m_normal_cache.empty()) {
            sync_generate_support_for_object(mo_apply, false);
            clear_support_volumes();
            m_parent.set_as_dirty();
        } else {
            reslice_until_step(m_show_support_structure ? slaposPad : slaposSupportPoints);
        }
    }

    const DynamicPrintConfig &cfg = sla_process_config();
    if (const auto *opt_bd = cfg.option<ConfigOptionFloat>("support_base_diameter"))
        m_base_diameter_before_change = opt_bd->value;
}

void GLGizmoSlaSupports::revert_manual_edits_keep_editing()
{
    if (!m_editing_mode)
        return;

    select_point(NoPoints);
    m_editing_cache.clear();
    for (const sla::SupportPoint& sp : m_normal_cache)
        m_editing_cache.emplace_back(sp);
    unregister_point_raycasters_for_picking();
    register_point_raycasters_for_picking();
    m_parent.set_as_dirty();
}

bool GLGizmoSlaSupports::resolve_unsaved_manual_edits_before_slice()
{
    if (!m_editing_mode || !unsaved_changes() || !on_is_activable())
        return true;

    MessageDialog dlg(GUI::wxGetApp().mainframe,
        _L("Do you want to save your manually edited support points?") + "\n",
        _L("Save support points?"), wxICON_QUESTION | wxYES | wxNO | wxCANCEL);
    const int ret = dlg.ShowModal();
    if (ret == wxID_YES) {
        commit_manual_edits_keep_editing(false);
        return true;
    }
    if (ret == wxID_NO) {
        revert_manual_edits_keep_editing();
        return true;
    }
    return false;
}



void GLGizmoSlaSupports::editing_mode_apply_changes()
{
    // If there are no changes, don't touch the front-end. The data in the cache could have been
    // taken from the backend and copying them to ModelObject would needlessly invalidate them.
    disable_editing_mode(); // this leaves the editing mode undo/redo stack and must be done before the snapshot is taken

    if (unsaved_changes()) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Support points edit");

        m_normal_cache.clear();
        for (const CacheEntry& ce : m_editing_cache)
            m_normal_cache.push_back(ce.support_point);

        ModelObject* mo = m_c->selection_info()->model_object();
        mo->sla_points_status = sla::PointsStatus::UserModified;
        mo->sla_support_points.clear();
        mo->sla_support_points = m_normal_cache;
        sync_generate_support_for_object(mo, !m_normal_cache.empty());

        // Step 4.2: use inherited reslice_until_step() instead of removed reslice_SLA_supports().
        if (!m_normal_cache.empty())
            reslice_until_step(slaposSupportPoints);
    }
}



void GLGizmoSlaSupports::apply_remove_all()
{
    // Commit empty support-point state to model without triggering reslice/validate.
    // Callers handle the snapshot; this function only syncs data and clears the mesh.
    m_normal_cache.clear();
    m_auto_baseline_initialized = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    mo->sla_points_status = sla::PointsStatus::UserModified;
    mo->sla_support_points.clear();
    sync_generate_support_for_object(mo, false);
    // Remove support/pad volumes from the gizmo's volume collection so the Structure
    // view shows no residual mesh after all points have been removed.
    clear_support_volumes();
    m_parent.set_as_dirty();
}



void GLGizmoSlaSupports::reload_cache()
{
    if (!m_c->selection_info())
        return;
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo)
        return;
    m_normal_cache.clear();
    if (mo->sla_points_status == sla::PointsStatus::AutoGenerated || mo->sla_points_status == sla::PointsStatus::Generating)
        get_data_from_backend();
    else
        for (const sla::SupportPoint& point : mo->sla_support_points)
            m_normal_cache.emplace_back(point);
}


bool GLGizmoSlaSupports::has_backend_supports() const
{
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (! mo)
        return false;

    // find SlaPrintObject with this ID
    for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
        if (po->model_object()->id() == mo->id())
        	return po->is_step_done(slaposSupportPoints);
    }
    return false;
}

// Step 4.2: reslice_SLA_supports() removed — use inherited reslice_until_step() instead.
// auto_generate() now calls reslice_until_step(slaposSupportPoints) directly.

void GLGizmoSlaSupports::get_data_from_backend()
{
    if (! has_backend_supports())
        return;
    ModelObject* mo = m_c->selection_info()->model_object();

    // find the respective SLAPrintObject, we need a pointer to it
    for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
        if (po->model_object()->id() == mo->id()) {
            m_normal_cache.clear();
            const std::vector<sla::SupportPoint>& points = po->get_support_points();
            auto mat = po->trafo().inverse().cast<float>();
            for (unsigned int i = 0; i < points.size(); ++i) {
                sla::SupportPoint sp = points[i];
                sp.pos = mat * points[i].pos;
                m_normal_cache.emplace_back(sp);
            }

            mo->sla_points_status = sla::PointsStatus::AutoGenerated;
            break;
        }
    }

    // We don't copy the data into ModelObject, as this would stop the background processing.
}



void GLGizmoSlaSupports::activate_structure_view()
{
    m_show_support_structure = true;
    show_sla_supports(true);
    // Match the in-panel Structure button: drop Points-preview picking raycasters
    // outside editing mode so cones do not intercept hover/click after the switch.
    if (!m_editing_mode)
        unregister_point_raycasters_for_picking();
    if (!m_normal_cache.empty())
        reslice_until_step(slaposPad);
    m_parent.set_as_dirty();
}

void GLGizmoSlaSupports::auto_generate()
{
    //wxMessageDialog dlg(GUI::wxGetApp().plater(), 
    MessageDialog dlg(GUI::wxGetApp().plater(), 
                        _L("Autogeneration will erase all manually edited points.") + "\n\n" +
                        _L("Are you sure you want to do it?") + "\n",
                        _L("Warning"), wxICON_WARNING | wxYES | wxNO);

    ModelObject* mo = m_c->selection_info()->model_object();

    if (mo->sla_points_status != sla::PointsStatus::UserModified || m_normal_cache.empty() || dlg.ShowModal() == wxID_YES) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Autogenerate support points");
        sync_generate_support_for_object(mo, true);
        mo->sla_points_status = sla::PointsStatus::Generating;
        mark_auto_settings_applied(mo);
        m_auto_baseline_initialized = true;
        // Auto Apply: regenerate support points (includes Support Angle filtering at slaposSupportPoints).
        reslice_until_step(m_show_support_structure ? slaposPad : slaposSupportPoints);
    }
}



void GLGizmoSlaSupports::switch_to_editing_mode()
{
    wxGetApp().plater()->enter_gizmos_stack();
    m_editing_mode = true;
    m_editing_cache.clear();
    for (const sla::SupportPoint& sp : m_normal_cache)
        m_editing_cache.emplace_back(sp);
    select_point(NoPoints);

    unregister_point_raycasters_for_picking();
    register_point_raycasters_for_picking();

    show_sla_supports(false); // Step 4.2: hide support structure when editing points (same as PrusaSlicer)
    m_parent.set_as_dirty();
}


void GLGizmoSlaSupports::disable_editing_mode()
{
    if (m_editing_mode) {
        m_editing_mode = false;
        notify_process_tab_selection_changed();
        unregister_point_raycasters_for_picking();
        wxGetApp().plater()->leave_gizmos_stack();
        show_sla_supports(m_show_support_structure); // Step 4.2: restore support structure visibility
        m_parent.set_as_dirty();
    }
    wxGetApp().plater()->get_notification_manager()->close_notification_of_type(NotificationType::QuitSLAManualMode);
}



bool GLGizmoSlaSupports::unsaved_changes() const
{
    if (m_editing_cache.size() != m_normal_cache.size())
        return true;

    for (size_t i=0; i<m_editing_cache.size(); ++i)
        if (m_editing_cache[i].support_point != m_normal_cache[i])
            return true;

    // Global support base diameter affects generated support/pad geometry.
    // While editing, changes to this parameter must enable Apply even if
    // per-point caches are unchanged.
    if (m_editing_mode) {
        const DynamicPrintConfig &cfg = sla_process_config();
        if (const auto *opt_bd = cfg.option<ConfigOptionFloat>("support_base_diameter")) {
            if (std::abs(opt_bd->value - m_base_diameter_before_change) > 1e-6)
                return true;
        }
    }

    return false;
}

// Step 2.3 Mod 6: Raycaster management for support point hover/picking.
// Ported from PrusaSlicer GLGizmoSlaSupports to enable hover detection,
// dragging, and right-click deletion of support points.

void GLGizmoSlaSupports::on_register_raycasters_for_picking()
{
    register_point_raycasters_for_picking();
    register_volume_raycasters_for_picking(); // Step 4.2: also register mesh volume raycasters from GLGizmoSlaBase
}

void GLGizmoSlaSupports::on_unregister_raycasters_for_picking()
{
    unregister_point_raycasters_for_picking();
    unregister_volume_raycasters_for_picking(); // Step 4.2: also unregister volume raycasters
}

void GLGizmoSlaSupports::register_point_raycasters_for_picking()
{
    if (!m_point_raycasters.empty())
        return; // already registered

    // Guard: mesh_raycaster is only created in on_render() — skip if not yet initialized
    if (!m_sphere.mesh_raycaster || !m_cone.mesh_raycaster)
        return;

    if (m_editing_mode && !m_editing_cache.empty()) {
        for (size_t i = 0; i < m_editing_cache.size(); ++i) {
            m_point_raycasters.emplace_back(
                m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_sphere.mesh_raycaster, Transform3d::Identity()),
                m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_cone.mesh_raycaster, Transform3d::Identity()));
        }
        update_point_raycasters_for_picking_transform();
    }
}

void GLGizmoSlaSupports::unregister_point_raycasters_for_picking()
{
    for (size_t i = 0; i < m_point_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, i);
    }
    m_point_raycasters.clear();
}

void GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()
{
    if (m_editing_cache.empty() || m_point_raycasters.empty())
        return;

    const Selection& selection = m_parent.get_selection();
    const GLVolume* vol = selection.get_first_volume();
    if (!vol)
        return;  // Selection not ready (e.g. during Undo/Redo before set_deserialized()); transforms update next frame.
    // Picking must use the same convention as render_points(): the sphere centre
    // lands on the scaled-mesh surface (S * raw_pos), and the sphere radius stays
    // in mm because pick_matrix excludes positive instance scale. Mirror flows
    // through the signed instance matrix so hit-test still matches a mirrored
    // visible cone.
    const Transform3d instance_scaling_matrix         = vol->get_instance_transformation().get_scaling_factor_matrix();
    const Transform3d instance_scaling_matrix_inverse = instance_scaling_matrix.inverse();
    const Transform3d instance_matrix          = Geometry::assemble_transform(m_c->selection_info()->get_sla_shift() * Vec3d::UnitZ()) * vol->get_instance_transformation().get_matrix();
    const Transform3d pick_matrix              = instance_matrix * instance_scaling_matrix_inverse;

    for (size_t i = 0; i < m_editing_cache.size() && i < m_point_raycasters.size(); ++i) {
        const sla::SupportPoint& sp = m_editing_cache[i].support_point;

        if (m_editing_cache[i].normal == Vec3f::Zero())
            m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

        const bool use_stored_geometry = preview_use_stored_top(sp, m_editing_cache[i].selected);

        // head is only consulted for pick_r (the visible cone's pin / contact
        // radius in mm). The picking sphere is radially symmetric, so head.dir
        // does not need the non-uniform-scale correction used by render_points().
        const sla::Head head = preview_sla_head_for_point(sp, m_editing_cache[i].normal, use_stored_geometry);
        const double pick_r = std::max(head.r_pin_mm, head.r_contact_mm > head.r_pin_mm ? head.r_contact_mm : 0.);
        const Vec3d scaled_pos = instance_scaling_matrix * sp.pos.cast<double>();

        m_point_raycasters[i].second->set_active(false);
        const Transform3d sphere_matrix = pick_matrix *
            Geometry::assemble_transform(scaled_pos, Vec3d::Zero(), pick_r * RenderPointScale * Vec3d::Ones());
        m_point_raycasters[i].first->set_transform(sphere_matrix);
        m_point_raycasters[i].first->set_active(true);
    }
}

SlaGizmoHelpDialog::SlaGizmoHelpDialog()
: wxDialog(nullptr, wxID_ANY, _L("SLA gizmo keyboard shortcuts"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    const wxString ctrl = GUI::shortkey_ctrl_prefix();
    const wxString alt  = GUI::shortkey_alt_prefix();
    const wxString shift = _L("Shift+");

    // fonts
    const wxFont& font = wxGetApp().small_font();
    const wxFont& bold_font = wxGetApp().bold_font();

    auto note_text = new wxStaticText(this, wxID_ANY, _L("Note: some shortcuts work in (non)editing mode only."));
    note_text->SetFont(font);

    auto vsizer    = new wxBoxSizer(wxVERTICAL);
    auto gridsizer = new wxFlexGridSizer(2, 5, 15);
    auto hsizer    = new wxBoxSizer(wxHORIZONTAL);

    hsizer->AddSpacer(20);
    hsizer->Add(vsizer);
    hsizer->AddSpacer(20);

    vsizer->AddSpacer(20);
    vsizer->Add(note_text, 1, wxALIGN_CENTRE_HORIZONTAL);
    vsizer->AddSpacer(20);
    vsizer->Add(gridsizer);
    vsizer->AddSpacer(20);

    std::vector<std::pair<wxString, wxString>> shortcuts;
    shortcuts.push_back(std::make_pair(_L("Left click"),              _L("Add point")));
    shortcuts.push_back(std::make_pair(_L("Right click"),             _L("Remove point")));
    shortcuts.push_back(std::make_pair(_L("Drag"),                    _L("Move point")));
    shortcuts.push_back(std::make_pair(ctrl+_L("Left click"),         _L("Add point to selection")));
    shortcuts.push_back(std::make_pair(alt+_L("Left click"),          _L("Remove point from selection")));
    shortcuts.push_back(std::make_pair(shift+_L("Drag"),              _L("Select by rectangle")));
    shortcuts.push_back(std::make_pair(alt+_(L("Drag")),              _L("Deselect by rectangle")));
    shortcuts.push_back(std::make_pair(ctrl+"A",                      _L("Select all points")));
    shortcuts.push_back(std::make_pair(_L("Del"),                     _L("Remove selected points")));
    shortcuts.push_back(std::make_pair(ctrl+_L("Mouse wheel"),        _L("Move clipping plane")));
    shortcuts.push_back(std::make_pair("R",                           _L("Reset clipping plane")));
    shortcuts.push_back(std::make_pair(_L("Enter"),                   _L("Apply changes")));
    shortcuts.push_back(std::make_pair(_L("Esc"),                     _L("Discard changes")));
    shortcuts.push_back(std::make_pair("M",                           _L("Switch to editing mode")));
    shortcuts.push_back(std::make_pair("A",                           _L("Auto-generate points")));

    for (const auto& pair : shortcuts) {
        auto shortcut = new wxStaticText(this, wxID_ANY, pair.first);
        auto desc = new wxStaticText(this, wxID_ANY, pair.second);
        shortcut->SetFont(bold_font);
        desc->SetFont(font);
        gridsizer->Add(shortcut, -1, wxALIGN_CENTRE_VERTICAL);
        gridsizer->Add(desc, -1, wxALIGN_CENTRE_VERTICAL);
    }

    SetSizer(hsizer);
    hsizer->SetSizeHints(this);
}



} // namespace GUI
} // namespace Slic3r
