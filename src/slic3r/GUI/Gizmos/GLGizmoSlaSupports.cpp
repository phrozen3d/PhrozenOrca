// Step 4.2: Include GLGizmoSlaBase.hpp instead of GLGizmoBase.hpp.
#include "GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <GL/glew.h>

#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stattext.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"

static const double CONE_RADIUS = 0.25;
static const double CONE_HEIGHT = 0.75;

namespace Slic3r {
namespace GUI {

// Step 4.2: Constructor now passes slaposDrillHoles as the minimum SLA step (same as PrusaSlicer).
// slaposDrillHoles exists in PhrozenOrca (unlike slaposAssembly which is PrusaSlicer-only).
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id, slaposDrillHoles)
{
    show_sla_supports(false); // Step 4.2: hide supports when gizmo opens (same as PrusaSlicer)
}


bool GLGizmoSlaSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["head_diameter"]    = _L("Head diameter") + ": ";
    m_desc["lock_supports"]    = _L("Lock supports under new islands");
    m_desc["remove_selected"]  = _L("Remove selected points");
    m_desc["remove_all"]       = _L("Remove all points");
    m_desc["apply_changes"]    = _L("Apply changes");
    m_desc["discard_changes"]  = _L("Discard changes");
    m_desc["minimal_distance"] = _L("Minimal points distance") + ": ";
    m_desc["points_density"]   = _L("Support points density") + ": ";
    m_desc["auto_generate"]    = _L("Auto-generate points");
    m_desc["manual_editing"]   = _L("Manual editing");
    m_desc["clipping_of_view"] = _L("Clipping of view")+ ": ";
    m_desc["reset_direction"]  = _L("Reset direction");

    return true;
}

// Step 4.2: Removed set_sla_support_data() — replaced by data_changed() below.

// Step 4.2: data_changed() now matches PrusaSlicer's full implementation.
// Added: set_hide_full_scene(true), update_volumes(), reslice logic.
void GLGizmoSlaSupports::data_changed(bool is_serializing)
{
    if (! m_c->selection_info())
        return;

    ModelObject* mo = m_c->selection_info()->model_object();

    if (m_state == On && mo && mo->id() != m_old_mo_id) {
        disable_editing_mode();
        reload_cache();
        m_old_mo_id = mo->id();
    }

    // If we triggered autogeneration before, check backend and fetch results if they are there
    if (mo) {
        m_c->instances_hider()->set_hide_full_scene(true); // Step 4.4 dependency

        // PhrozenOrca: no last_completed_step(). Use is_step_done() — semantically equivalent
        // because SLA steps are sequential: if step N is not done, reslice up to N.
        const int required_step = get_min_sla_print_object_step();
        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (po != nullptr && !po->is_step_done((SLAPrintObjectStep)required_step))
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
    const size_t cache_size = m_editing_mode ? m_editing_cache.size() : m_normal_cache.size();
    if (cache_size == 0)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    shader->start_using();
    ScopeGuard guard([shader]() { shader->stop_using(); });

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    const Transform3d instance_scaling_matrix_inverse = vol->get_instance_transformation().get_scaling_factor_matrix().inverse();
    // PhrozenOrca: get_sla_shift() instead of print_object()->get_current_elevation() (model_instance() unavailable)
    const Transform3d instance_matrix = Geometry::assemble_transform(m_c->selection_info()->get_sla_shift() * Vec3d::UnitZ()) * vol->get_instance_transformation().get_matrix();

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

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

        // Color logic — PhrozenOrca uses is_new_island (no SupportPointType enum here)
        if (m_editing_mode && size_t(m_hover_id) == i)
            render_color = ColorRGBA::CYAN();
        else if (m_editing_mode && point_selected)
            render_color = ColorRGBA { 1.f, 0.3f, 0.3f, 1.f }; // REDISH (ColorRGBA::REDISH not in PhrozenOrca)
        else if (m_lock_unique_islands && support_point.is_new_island && m_editing_mode)
            render_color = ColorRGBA::BLUEISH();
        else if (m_editing_mode)
            render_color = ColorRGBA::LIGHT_GRAY();
        else
            render_color = ColorRGBA { 0.5f, 0.5f, 0.5f, 1.f };

        m_cone.model.set_color(render_color);
        m_sphere.model.set_color(render_color);
        shader->set_uniform("emission_factor", 0.5f);

        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        const Transform3d support_matrix = Geometry::assemble_transform(support_point.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (vol->is_left_handed())
            glFrontFace(GL_CW);

        // Matrices set, we can render the point mark now.
        // If in editing mode, we'll also render a cone pointing to the sphere.
        if (m_editing_mode) {
            // in case the normal is not yet cached, find and cache it
            if (m_editing_cache[i].normal == Vec3f::Zero())
                m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * m_editing_cache[i].normal.cast<double>());
            const Eigen::AngleAxisd aa(q);
            const Transform3d model_matrix = instance_matrix * support_matrix * Transform3d(aa.toRotationMatrix()) *
                Geometry::assemble_transform((CONE_HEIGHT + support_point.head_front_radius * RenderPointScale) * Vec3d::UnitZ(),
                    Vec3d(PI, 0.0, 0.0), Vec3d(CONE_RADIUS, CONE_RADIUS, CONE_HEIGHT));

            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            m_cone.model.render();
        }

        const double radius = (double)support_point.head_front_radius * RenderPointScale;
        const Transform3d model_matrix = instance_matrix * support_matrix *
            Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), radius * Vec3d::Ones());

        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_sphere.model.render();

        if (vol->is_left_handed())
            glFrontFace(GL_CCW);
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
            else {
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
                    m_editing_cache.emplace_back(sla::SupportPoint(pos_and_normal.first, m_new_point_head_diameter/2.f, false), false, pos_and_normal.second);
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
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: delete_selected_points called out of editing mode!" << std::endl;
        std::abort();
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Delete support point");

    for (unsigned int idx=0; idx<m_editing_cache.size(); ++idx) {
        if (m_editing_cache[idx].selected && (!m_editing_cache[idx].support_point.is_new_island || !m_lock_unique_islands || force)) {
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
        if (m_hover_id != -1 && (! m_editing_cache[m_hover_id].support_point.is_new_island || !m_lock_unique_islands)) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (! unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
                return;
            m_editing_cache[m_hover_id].support_point.pos = pos_and_normal.first;
            m_editing_cache[m_hover_id].support_point.is_new_island = false;
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
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    ModelObject* mo = m_c->selection_info()->model_object();

    if (! mo)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.
RENDER_AGAIN:
    //m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    //const ImVec2 window_size(m_imgui->scaled(18.f, 16.f));
    //ImGui::SetNextWindowPos(ImVec2(x, y - std::max(0.f, y+window_size.y-bottom_limit) ));
    //ImGui::SetNextWindowSize(ImVec2(window_size));

    m_imgui->begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);
    if ((last_h != win_h) || (last_y != y))
    {
        // ask canvas for another frame to render the window in the correct position
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        m_imgui->set_requires_extra_frame();
#else
        m_parent.request_extra_frame();
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:

    const float settings_sliders_left = std::max(m_imgui->calc_text_size(m_desc.at("minimal_distance")).x, m_imgui->calc_text_size(m_desc.at("points_density")).x) + m_imgui->scaled(1.f);
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x, m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float diameter_slider_left = m_imgui->calc_text_size(m_desc.at("head_diameter")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);
    const float buttons_width_approx = m_imgui->calc_text_size(m_desc.at("apply_changes")).x + m_imgui->calc_text_size(m_desc.at("discard_changes")).x + m_imgui->scaled(1.5f);
    const float lock_supports_width_approx = m_imgui->calc_text_size(m_desc.at("lock_supports")).x + m_imgui->scaled(2.f);

    float window_width = minimal_slider_width + std::max(std::max(settings_sliders_left, clipping_slider_left), diameter_slider_left);
    window_width = std::max(std::max(window_width, buttons_width_approx), lock_supports_width_approx);

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    if (m_editing_mode) {

        float diameter_upper_cap = static_cast<ConfigOptionFloat*>(wxGetApp().preset_bundle->sla_prints.get_edited_preset().config.option("support_pillar_diameter"))->value;
        if (m_new_point_head_diameter > diameter_upper_cap)
            m_new_point_head_diameter = diameter_upper_cap;
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("head_diameter"));
        ImGui::SameLine(diameter_slider_left);
        ImGui::PushItemWidth(window_width - diameter_slider_left);

        // Following is a nasty way to:
        //  - save the initial value of the slider before one starts messing with it
        //  - keep updating the head radius during sliding so it is continuosly refreshed in 3D scene
        //  - take correct undo/redo snapshot after the user is done with moving the slider
        float initial_value = m_new_point_head_diameter;
        m_imgui->slider_float("##head_diameter", &m_new_point_head_diameter, 0.1f, diameter_upper_cap, "%.1f");
        if (m_imgui->get_last_slider_status().clicked) {
            if (m_old_point_head_diameter == 0.f)
                m_old_point_head_diameter = initial_value;
        }
        if (m_imgui->get_last_slider_status().edited) {
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
        }
        if (m_imgui->get_last_slider_status().deactivated_after_edit) {
            // momentarily restore the old value to take snapshot
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_old_point_head_diameter / 2.f;
            float backup = m_new_point_head_diameter;
            m_new_point_head_diameter = m_old_point_head_diameter;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Change point head diameter");
            m_new_point_head_diameter = backup;
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
            m_old_point_head_diameter = 0.f;
        }

        bool changed = m_lock_unique_islands;
        m_imgui->checkbox(m_desc.at("lock_supports"), m_lock_unique_islands);
        force_refresh |= changed != m_lock_unique_islands;

        m_imgui->disabled_begin(m_selection_empty);
        remove_selected = m_imgui->button(m_desc.at("remove_selected"));
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_editing_cache.empty());
        remove_all = m_imgui->button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

        m_imgui->text(" "); // vertical gap

        if (m_imgui->button(m_desc.at("apply_changes"))) {
            editing_mode_apply_changes();
            force_refresh = true;
        }
        ImGui::SameLine();
        bool discard_changes = m_imgui->button(m_desc.at("discard_changes"));
        if (discard_changes) {
            editing_mode_discard_changes();
            force_refresh = true;
        }
    }
    else { // not in editing mode:
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("minimal_distance"));
        ImGui::SameLine(settings_sliders_left);
        ImGui::PushItemWidth(window_width - settings_sliders_left);

        std::vector<const ConfigOption*> opts = get_config_options({"support_points_density_relative", "support_points_minimal_distance"});
        float density = static_cast<const ConfigOptionInt*>(opts[0])->value;
        float minimal_point_distance = static_cast<const ConfigOptionFloat*>(opts[1])->value;

        m_imgui->slider_float("##minimal_point_distance", &minimal_point_distance, 0.f, 20.f, "%.f mm");
        bool slider_clicked = m_imgui->get_last_slider_status().clicked; // someone clicked the slider
        bool slider_edited = m_imgui->get_last_slider_status().edited; // someone is dragging the slider
        bool slider_released = m_imgui->get_last_slider_status().deactivated_after_edit; // someone has just released the slider

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("points_density"));
        ImGui::SameLine(settings_sliders_left);

        m_imgui->slider_float("##points_density", &density, 0.f, 200.f, "%.f %%");
        slider_clicked |= m_imgui->get_last_slider_status().clicked;
        slider_edited |= m_imgui->get_last_slider_status().edited;
        slider_released |= m_imgui->get_last_slider_status().deactivated_after_edit;

        if (slider_clicked) { // stash the values of the settings so we know what to revert to after undo
            m_minimal_point_distance_stash = minimal_point_distance;
            m_density_stash = density;
        }
        if (slider_edited) {
            mo->config.set("support_points_minimal_distance", minimal_point_distance);
            mo->config.set("support_points_density_relative", (int)density);
        }
        if (slider_released) {
            mo->config.set("support_points_minimal_distance", m_minimal_point_distance_stash);
            mo->config.set("support_points_density_relative", (int)m_density_stash);
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Support parameter change");
            mo->config.set("support_points_minimal_distance", minimal_point_distance);
            mo->config.set("support_points_density_relative", (int)density);
            wxGetApp().obj_list()->update_and_show_object_settings_item();
        }

        bool generate = m_imgui->button(m_desc.at("auto_generate"));

        if (generate)
            auto_generate();

        ImGui::Separator();
        if (m_imgui->button(m_desc.at("manual_editing")))
            switch_to_editing_mode();

        m_imgui->disabled_begin(m_normal_cache.empty());
        remove_all = m_imgui->button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

        // m_imgui->text("");
        // m_imgui->text(m_c->m_model_object->sla_points_status == sla::PointsStatus::NoPoints ? _(L("No points  (will be autogenerated)")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::AutoGenerated ? _(L("Autogenerated points (no modifications)")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::UserModified ? _(L("User-modified points")) :
        //              (m_c->m_model_object->sla_points_status == sla::PointsStatus::Generating ? _(L("Generation in progress...")) : "UNKNOWN STATUS"))));
    }


    // Following is rendered in both editing and non-editing mode:
    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    }
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    m_c->object_clipper()->set_position_by_ratio(-1., false);
                });
        }
    }

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    float clp_dist = m_c->object_clipper()->get_position();
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);


    if (m_imgui->button("?")) {
        wxGetApp().CallAfter([]() {
            SlaGizmoHelpDialog help_dlg;
            help_dlg.ShowModal();
        });
    }

    m_imgui->end();

    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();
        bool was_in_editing = m_editing_mode;
        if (! was_in_editing)
            switch_to_editing_mode();
        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points(true); // true - delete regardless of locked status
        }
        if (remove_selected)
            delete_selected_points(false); // leave locked points
        if (! was_in_editing)
            editing_mode_apply_changes();

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();
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


void GLGizmoSlaSupports::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        // Set default head diameter from config.
        const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        m_new_point_head_diameter = static_cast<const ConfigOptionFloat*>(cfg.option("support_head_front_diameter"))->value;
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
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
            m_c->instances_hider()->set_hide_full_scene(false);
            // Note: set_use_shift() not available in PhrozenOrca's SelectionInfo — omitted.
        }
    }
    m_old_state = m_state;
}



void GLGizmoSlaSupports::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_point_before_drag = m_editing_cache[m_hover_id];
    }
    else
        m_point_before_drag = CacheEntry();
}


void GLGizmoSlaSupports::on_stop_dragging()
{
    if (m_hover_id != -1) {
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
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_new_point_head_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoSlaSupports::select_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: select_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    if (i == AllPoints || i == NoPoints) {
        for (auto& point_and_selection : m_editing_cache)
            point_and_selection.selected = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints)
            m_new_point_head_diameter = m_editing_cache[0].support_point.head_front_radius * 2.f;
    }
    else {
        m_editing_cache[i].selected = true;
        m_selection_empty = false;
        m_new_point_head_diameter = m_editing_cache[i].support_point.head_front_radius * 2.f;
    }
}


void GLGizmoSlaSupports::unselect_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: unselect_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    m_editing_cache[i].selected = false;
    m_selection_empty = true;
    for (const CacheEntry& ce : m_editing_cache) {
        if (ce.selected) {
            m_selection_empty = false;
            break;
        }
    }
}




void GLGizmoSlaSupports::editing_mode_discard_changes()
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: editing_mode_discard_changes called when out of editing mode!" << std::endl;
        std::abort();
    }
    select_point(NoPoints);
    disable_editing_mode();
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

        // Step 4.2: use inherited reslice_until_step() instead of removed reslice_SLA_supports().
        reslice_until_step(slaposSupportPoints);
    }
}



void GLGizmoSlaSupports::reload_cache()
{
    const ModelObject* mo = m_c->selection_info()->model_object();
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
            for (unsigned int i=0; i<points.size();++i)
                m_normal_cache.emplace_back(sla::SupportPoint(mat * points[i].pos, points[i].head_front_radius, points[i].is_new_island));

            mo->sla_points_status = sla::PointsStatus::AutoGenerated;
            break;
        }
    }

    // We don't copy the data into ModelObject, as this would stop the background processing.
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
        mo->sla_points_status = sla::PointsStatus::Generating;
        // Step 4.2: use inherited reslice_until_step() instead of removed reslice_SLA_supports().
        // m_show_support_structure: if supports structure visible, reslice to slaposPad; otherwise slaposSupportPoints.
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
    const Transform3d instance_scaling_matrix_inverse = vol->get_instance_transformation().get_scaling_factor_matrix().inverse();
    const Transform3d instance_matrix = Geometry::assemble_transform(m_c->selection_info()->get_sla_shift() * Vec3d::UnitZ()) * vol->get_instance_transformation().get_matrix();

    const double cone_radius = 0.25; // mm — matches render_points()
    const double cone_height = 0.75;

    for (size_t i = 0; i < m_editing_cache.size() && i < m_point_raycasters.size(); ++i) {
        const sla::SupportPoint& sp = m_editing_cache[i].support_point;
        const Transform3d support_matrix = Geometry::translation_transform(sp.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (m_editing_cache[i].normal == Vec3f::Zero())
            m_c->raycaster()->raycaster()->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

        Eigen::Quaterniond q;
        q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * m_editing_cache[i].normal.cast<double>());
        const Eigen::AngleAxisd aa(q);

        // Cone transform — matches render_points() cone rendering
        const Transform3d cone_matrix = instance_matrix * support_matrix * Transform3d(aa.toRotationMatrix()) *
            Geometry::assemble_transform((cone_height + sp.head_front_radius * RenderPointScale) * Vec3d::UnitZ(),
                Vec3d(PI, 0.0, 0.0), Vec3d(cone_radius, cone_radius, cone_height));
        m_point_raycasters[i].second->set_transform(cone_matrix);

        // Sphere transform — matches render_points() sphere rendering
        const double radius = (double)sp.head_front_radius * RenderPointScale;
        const Transform3d sphere_matrix = instance_matrix * support_matrix *
            Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), radius * Vec3d::Ones());
        m_point_raycasters[i].first->set_transform(sphere_matrix);
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
