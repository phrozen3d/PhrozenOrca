///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// GLGizmoDrill: Drill-holes-only gizmo extracted from GLGizmoHollow.
// Inherits GLGizmoSlaBase; provides drain hole placement/editing without hollowing parameters.

#include "GLGizmoDrill.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "libslic3r/Model.hpp"


namespace Slic3r {
namespace GUI {

GLGizmoDrill::GLGizmoDrill(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id)  // no minimum step — always enable input when mesh exists
{
}


bool GLGizmoDrill::on_init()
{
    m_shortcut_key = WXK_CONTROL_D;
    m_desc["apply"]            = _(L("Apply"));
    m_desc["diameter"]         = _(L("Diameter"));
    m_desc["depth"]            = _(L("Depth"));
    m_desc["remove_selected"]  = _(L("Remove selected"));
    m_desc["remove_all"]       = _(L("Remove all"));

    m_desc["left_click"]       = _(L("Left click:"));
    m_desc["right_click"]      = _(L("Right click:"));
    m_desc["add_hole"]         = _(L("Add hole"));
    m_desc["remove_hole"]      = _(L("Remove hole"));

    return true;
}

void GLGizmoDrill::data_changed(bool is_serializing)
{
    if (!m_c->selection_info())
        return;

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (m_state == On && mo) {
        if (m_old_mo_id != mo->id()) {
            // New object: discard pending working set and initialize from the new object's applied baseline.
            // The previous object's sla_drain_holes is already the applied state — no restore needed.
            m_working_holes = mo->sla_drain_holes;
            m_last_resliced_holes = mo->sla_drain_holes;
            reload_cache();
            m_old_mo_id = mo->id();
        } else if (is_serializing) {
            // Undo/Redo: cereal has already restored sla_drain_holes to the applied state at the
            // snapshot point. Rebuild the working set from that restored applied state.
            m_working_holes = mo->sla_drain_holes;
            reload_cache();
            unregister_hole_raycasters_for_picking();
            // register_hole_raycasters_for_picking() is called by the common path below.
            // Trigger DrillHoles reslice only when the applied holes actually changed vs the last
            // reslice baseline — avoids re-running CGAL for unrelated Undo/Redo while Drill is open.
            if (mo->sla_drain_holes != m_last_resliced_holes) {
                reslice_until_step(slaposDrillHoles, true);
                m_last_resliced_holes = mo->sla_drain_holes;
            }
        }

        const int required_step = get_min_sla_print_object_step();
        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (required_step >= 0 && po != nullptr && po->get_mesh_to_print().empty())
            reslice_until_step((SLAPrintObjectStep)required_step);

        update_volumes();

        if (m_hole_raycasters.empty())
            register_hole_raycasters_for_picking();
        else
            update_hole_raycasters_for_picking_transform();

        m_c->instances_hider()->set_hide_full_scene(true);
    }
}


void GLGizmoDrill::on_render()
{
    // Safety check: if selected print object doesn't exist on active bed, close gizmo.
    if (!selected_print_object_exists(m_parent, wxEmptyString)) {
        wxGetApp().CallAfter([this]() {
            m_parent.get_gizmos_manager().open_gizmo(m_parent.get_gizmos_manager().get_current_type());
        });
    }
    const Selection& selection = m_parent.get_selection();
    const CommonGizmosDataObjects::SelectionInfo* sel_info = m_c->selection_info();

    if (m_state == On
     && (sel_info->model_object() != selection.get_model()->objects[selection.get_object_idx()]
      || sel_info->get_active_instance() != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_volumes();
    render_points(selection);

    m_selection_rectangle.render(m_parent);
    m_c->object_clipper()->render_cut();

    if (are_sla_supports_shown())
        m_c->supports_clipper()->render_cut();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoDrill::on_register_raycasters_for_picking()
{
    register_hole_raycasters_for_picking();
    register_volume_raycasters_for_picking();
}

void GLGizmoDrill::on_unregister_raycasters_for_picking()
{
    unregister_hole_raycasters_for_picking();
    unregister_volume_raycasters_for_picking();
}

void GLGizmoDrill::render_points(const Selection& selection)
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    shader->start_using();
    ScopeGuard guard([shader]() { shader->stop_using(); });

    auto *mo_render = m_c->selection_info()->model_object();
    int inst_idx_render = m_c->selection_info()->get_active_instance();
    if (!mo_render || inst_idx_render < 0 || inst_idx_render >= (int)mo_render->instances.size())
        return;

    double shift_z = m_c->selection_info()->print_object() ? m_c->selection_info()->print_object()->get_current_elevation() : 0.;
    Transform3d trafo(mo_render->instances[inst_idx_render]->get_transformation().get_matrix());
    trafo.translation()(2) += shift_z;
    const Geometry::Transformation transformation{trafo};

    const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    ColorRGBA render_color;
    const sla::DrainHoles& drain_holes = m_working_holes;
    const size_t cache_size = drain_holes.size();

    for (size_t i = 0; i < cache_size; ++i) {
        const sla::DrainHole& drain_hole = drain_holes[i];
        const bool point_selected = m_selected[i];

        const bool clipped = is_mesh_point_clipped(drain_hole.pos.cast<double>());
        m_hole_raycasters[i]->set_active(!clipped);
        if (clipped)
            continue;

        if (size_t(m_hover_id) == i)
            render_color = ColorRGBA::CYAN();
        else
            render_color = point_selected ? ColorRGBA(1.0f, 0.3f, 0.3f, 0.5f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);

        m_cylinder.model.set_color(render_color);
        const Transform3d hole_matrix = Geometry::translation_transform(drain_hole.pos.cast<double>()) * instance_scaling_matrix_inverse;

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CW));

        Eigen::Quaterniond q;
        q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * (-drain_hole.normal).cast<double>());
        const Eigen::AngleAxisd aa(q);
        const Transform3d model_matrix = trafo * hole_matrix * Transform3d(aa.toRotationMatrix()) *
            Geometry::translation_transform(-drain_hole.height * Vec3d::UnitZ()) * Geometry::scale_transform(Vec3d(drain_hole.radius, drain_hole.radius, drain_hole.height + sla::HoleStickOutLength));
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        m_cylinder.model.render();

        if (transformation.is_left_handed())
            glsafe(::glFrontFace(GL_CCW));
    }
}

bool GLGizmoDrill::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    int active_inst = m_c->selection_info()->get_active_instance();
    const ModelInstance* mi = sel_info->model_object()->instances[active_inst];
    const Transform3d& trafo = mi->get_transformation().get_matrix() * sel_info->model_object()->volumes.front()->get_matrix();

    Vec3d transformed_point = trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}


bool GLGizmoDrill::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    int active_inst = m_c->selection_info()->get_active_instance();

    if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
        if (m_hover_id == -1) {
            if (shift_down || alt_down) {
                m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::Select : GLSelectionRectangle::Deselect);
            }
        }
        else {
            if (m_selected[m_hover_id])
                unselect_point(m_hover_id);
            else {
                if (!alt_down)
                    select_point(m_hover_id);
            }
        }

        return true;
    }

    if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle.is_dragging() && !shift_down) {
        if (m_hover_id != -1)
            return false;

        if (m_selection_empty) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (unproject_on_mesh(mouse_position, pos_and_normal)) {
                m_working_holes.emplace_back(pos_and_normal.first,
                                             -pos_and_normal.second, m_new_hole_radius, m_new_hole_height);
                m_selected.push_back(false);
                assert(m_selected.size() == m_working_holes.size());
                m_parent.set_as_dirty();
                m_wait_for_up_event = true;
                unregister_hole_raycasters_for_picking();
                register_hole_raycasters_for_picking();
            }
            else
                return false;
        }
        else
            select_point(NoPoints);

        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp || action == SLAGizmoEventType::AltUp) && m_selection_rectangle.is_dragging()) {
        GLSelectionRectangle::EState rectangle_status = m_selection_rectangle.get_state();

        Geometry::Transformation trafo = mo->instances[active_inst]->get_transformation();
        trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_c->selection_info()->get_sla_shift()));
        std::vector<Vec3d> points;
        for (unsigned int i=0; i<m_working_holes.size(); ++i)
            points.push_back(trafo.get_matrix() * m_working_holes[i].pos.cast<double>());

        std::vector<Vec3f> points_inside;
        std::vector<unsigned int> points_idxs = m_selection_rectangle.contains(points);
        m_selection_rectangle.stop_dragging();
        for (size_t idx : points_idxs)
            points_inside.push_back(points[idx].cast<float>());

        for (size_t idx : m_c->raycaster()->raycaster()->get_unobscured_idxs(
                 trafo, wxGetApp().plater()->get_camera(), points_inside,
                 m_c->object_clipper()->get_clipping_plane()))
        {
            if (rectangle_status == GLSelectionRectangle::Deselect)
                unselect_point(points_idxs[idx]);
            else
                select_point(points_idxs[idx]);
        }
        return true;
    }

    if (action == SLAGizmoEventType::LeftUp) {
        if (m_wait_for_up_event) {
            m_wait_for_up_event = false;
            return true;
        }
    }

    if (action == SLAGizmoEventType::Dragging) {
        if (m_wait_for_up_event)
            return true;

        if (m_selection_rectangle.is_dragging()) {
            m_selection_rectangle.dragging(mouse_position);
            return true;
        }

        return false;
    }

    if (action == SLAGizmoEventType::Delete) {
        delete_selected_points();
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

void GLGizmoDrill::delete_selected_points()
{
    for (unsigned int idx=0; idx<m_working_holes.size(); ++idx) {
        if (m_selected[idx]) {
            m_selected.erase(m_selected.begin()+idx);
            m_working_holes.erase(m_working_holes.begin() + (idx--));
        }
    }

    unregister_hole_raycasters_for_picking();
    register_hole_raycasters_for_picking();
    select_point(NoPoints);
}

bool GLGizmoDrill::on_mouse(const wxMouseEvent &mouse_event)
{
    if (!is_input_enabled()) return true;
    if (mouse_event.Moving()) return false;
    if (use_grabbers(mouse_event)) return true;

    Vec2i32 mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    static bool pending_right_up = false;
    if (mouse_event.LeftDown()) {
        bool control_down = mouse_event.CmdDown();
        bool grabber_contains_mouse = (get_hover_id() != -1);
        if ((!control_down || grabber_contains_mouse) &&
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false))
            return true;
    } else if (mouse_event.Dragging()) {
        if (m_parent.get_move_volume_id() != -1)
            return true;

        bool control_down = mouse_event.CmdDown();
        if (control_down) {
            if (mouse_event.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            else if (mouse_event.RightIsDown()) {
                pending_right_up = false;
            }
        } else if(gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false)) {
            m_parent.set_as_dirty();
            return true;
        }
    } else if (mouse_event.LeftUp()) {
        if (!m_parent.is_mouse_dragging()) {
            bool control_down = mouse_event.CmdDown();
            gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), control_down);
            return true;
        }
    } else if (mouse_event.RightDown()) {
        if (m_parent.get_selection().get_object_idx() != -1 &&
            gizmo_event(SLAGizmoEventType::RightDown, mouse_pos, false, false, false)) {
            pending_right_up = true;
            return true;
        }
    } else if (mouse_event.RightUp()) {
        if (pending_right_up) {
            pending_right_up = false;
            return true;
        }
    }
    return false;
}


void GLGizmoDrill::register_hole_raycasters_for_picking()
{
    assert(m_hole_raycasters.empty());

    init_cylinder_model();

    // Use Selection directly instead of m_c->selection_info() to avoid stale/dangling
    // ModelObject pointer during undo/redo restore (m_c is refreshed after this call).
    const Selection& sel = m_parent.get_selection();
    const int obj_idx = sel.get_object_idx();
    const Model* model = sel.get_model();
    if (obj_idx < 0 || model == nullptr || obj_idx >= (int)model->objects.size())
        return;
    const ModelObject* mo = model->objects[obj_idx];
    if (mo == nullptr || m_working_holes.empty())
        return;
    for (int i = 0; i < (int)m_working_holes.size(); ++i) {
        m_hole_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, i, *m_cylinder.mesh_raycaster, Transform3d::Identity()));
    }
    update_hole_raycasters_for_picking_transform();
}

void GLGizmoDrill::unregister_hole_raycasters_for_picking()
{
    for (size_t i = 0; i < m_hole_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, i);
    }
    m_hole_raycasters.clear();
}

void GLGizmoDrill::update_hole_raycasters_for_picking_transform()
{
    const CommonGizmosDataObjects::SelectionInfo* info = m_c->selection_info();
    if (info != nullptr) {
        const sla::DrainHoles& drain_holes = m_working_holes;
        if (!drain_holes.empty()) {
            assert(!m_hole_raycasters.empty());

            const GLVolume* vol = m_parent.get_selection().get_first_volume();
            if (!vol)
                return;  // Selection not ready (e.g. during Undo/Redo); transforms update next frame.
            Geometry::Transformation transformation(vol->get_instance_transformation());

            auto *mo_upd = m_c->selection_info()->model_object();
            int inst_idx_upd = m_c->selection_info()->get_active_instance();
            if (mo_upd && inst_idx_upd >= 0 && inst_idx_upd < (int)mo_upd->instances.size()
                && m_c->selection_info()->print_object()) {
                double shift_z = m_c->selection_info()->print_object()->get_current_elevation();
                auto trafo = mo_upd->instances[inst_idx_upd]->get_transformation().get_matrix();
                trafo.translation()(2) += shift_z;
                transformation.set_matrix(trafo);
            }
            const Transform3d instance_scaling_matrix_inverse = transformation.get_scaling_factor_matrix().inverse();

            for (size_t i = 0; i < drain_holes.size(); ++i) {
                const sla::DrainHole& drain_hole = drain_holes[i];
                const Transform3d hole_matrix = Geometry::translation_transform(drain_hole.pos.cast<double>()) * instance_scaling_matrix_inverse;
                Eigen::Quaterniond q;
                q.setFromTwoVectors(Vec3d::UnitZ(), instance_scaling_matrix_inverse * (-drain_hole.normal).cast<double>());
                const Eigen::AngleAxisd aa(q);
                const Transform3d matrix = transformation.get_matrix() * hole_matrix * Transform3d(aa.toRotationMatrix()) *
                    Geometry::translation_transform(-drain_hole.height * Vec3d::UnitZ()) * Geometry::scale_transform(Vec3d(drain_hole.radius, drain_hole.radius, drain_hole.height + sla::HoleStickOutLength));
                m_hole_raycasters[i]->set_transform(matrix);
            }
        }
    }
}


// Custom horizontal slider for DrillPanel2.
// Draws a bar (left=orange, right=gray) with a triangle handle (▲) below it.
//
// IMPORTANT — two separate range concepts:
//   s_min / s_max : slider bar interaction range.
//                   Controls the drag-to-value mapping and the handle's visual position.
//                   The handle is clamped to [s_min, s_max] for drawing purposes, so a value
//                   outside this range is shown at the left or right endpoint — the value itself
//                   is NOT silently overwritten just because it falls outside the slider range.
//   v_min / v_max : value / final-clamp range.
//                   The value produced by a slider interaction is clamped here before being
//                   written to v. Typically wider than the slider range (matches old Hollow UI:
//                   "allows entering off-scale values and still protects against complete nonsense").
//
// enabled : when false, draws the widget but does not modify v.
// step    : if > 0, snaps to multiples of step (reserved for integer-step mode; pass 0 for continuous).
// Returns true if v was changed this frame by slider interaction.
static bool draw_custom_slider(const char* id, float& v,
                               float s_min, float s_max,  // slider bar range (drag + visual)
                               float v_min, float v_max,  // value clamp range (final allowed)
                               float bar_w,
                               bool enabled = true, float step = 0.f)
{
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  fh  = ImGui::GetFrameHeight();

    // Geometry constants (fixed screen-pixel sizes; not DPI-scaled — stays sharp at any resolution)
    constexpr float kBarH     = 3.f;
    constexpr float kGap      = 2.f;   // gap between bar bottom and triangle apex
    constexpr float kTriH     = 7.f;
    constexpr float kTriW     = 10.f;
    constexpr float kRounding = 1.5f;

    // Vertically center the bar+gap+triangle block within the frame height
    const float block_h = kBarH + kGap + kTriH;
    const float v_off   = std::floor((fh - block_h) * 0.5f);
    const float bar_y0  = pos.y + v_off;
    const float bar_y1  = bar_y0 + kBarH;
    const float tri_ay  = bar_y1 + kGap;   // apex y (top of triangle, touching bar bottom)
    const float tri_by  = tri_ay + kTriH;  // base  y (bottom of triangle)

    // Full bar_w × fh hit area — captures both click-to-set and drag
    ImGui::InvisibleButton(id, ImVec2(bar_w, fh));
    const bool active  = ImGui::IsItemActive();
    bool       changed = false;

    if (active && enabled) {
        // Map mouse X within slider range [s_min, s_max]; Y axis is intentionally ignored.
        // Then clamp to the (possibly wider) value range [v_min, v_max] as a safety net.
        const float t  = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / bar_w, 0.f, 1.f);
        float       nv = s_min + t * (s_max - s_min);
        if (step > 0.f)
            nv = std::round(nv / step) * step;  // integer-step snap (reserved)
        nv = std::clamp(nv, v_min, v_max);
        if (nv != v) { v = nv; changed = true; }
    }

    // Compute handle draw position using slider range [s_min, s_max].
    // std::clamp to [0,1] means values outside the slider range show the handle at the
    // left or right endpoint — the bar visually saturates without changing v.
    const float t_draw = (s_max > s_min)
        ? std::clamp((v - s_min) / (s_max - s_min), 0.f, 1.f) : 0.f;
    const float hx = pos.x + t_draw * bar_w;

    constexpr ImU32 kOrange = IM_COL32(232, 107, 32, 255);  // OrcaSlicer orange
    constexpr ImU32 kGray   = IM_COL32(190, 190, 190, 255);

    // Bar: left segment (orange) + right segment (gray)
    if (hx > pos.x)
        dl->AddRectFilled(ImVec2(pos.x, bar_y0), ImVec2(hx,          bar_y1), kOrange, kRounding);
    if (hx < pos.x + bar_w)
        dl->AddRectFilled(ImVec2(hx,    bar_y0), ImVec2(pos.x+bar_w, bar_y1), kGray,   kRounding);

    // Triangle handle ▲: apex up (near bar), base down
    dl->AddTriangleFilled(
        ImVec2(hx,               tri_ay),
        ImVec2(hx - kTriW * .5f, tri_by),
        ImVec2(hx + kTriW * .5f, tri_by),
        kOrange);

    return changed;
}


void GLGizmoDrill::render_new_drill_panel(float x, float y, float legacy_panel_h,
                                          ModelObject* mo, bool& remove_selected, bool& remove_all)
{
    const float scale         = m_parent.get_scale();
    const float new_panel_gap = 8.f * scale;           // gap between panels — adjustable

    // Layout metrics — use translated strings so widths remain correct after locale switch
    const float label_col_w = std::max(
        m_imgui->calc_text_size(m_desc.at("diameter")).x,
        m_imgui->calc_text_size(m_desc.at("depth")).x
    ) + m_imgui->scaled(1.5f);

    const float value_box_w   = m_imgui->scaled(4.f);  // value box width — adjustable
    const float slider_w      = m_imgui->scaled(8.f);  // custom slider bar width — adjustable

    const float fp    = ImGui::GetStyle().FramePadding.x * 2.f;
    const float btn_w = std::max(
        std::max(m_imgui->calc_text_size(m_desc.at("remove_selected")).x,
                 m_imgui->calc_text_size(m_desc.at("remove_all")).x),
        m_imgui->calc_text_size(m_desc.at("apply")).x
    ) + fp + m_imgui->scaled(1.f);

    // push_toolbar_style before set_next_window_pos/begin so WindowRounding etc. take effect.
    // Provides: WindowRounding=3*scale, WindowBorderSize=0, WindowPadding=(20,10)*scale,
    //           FrameBorderSize=1, FrameRounding=2*scale, ItemSpacing=(10,10)*scale,
    //           WindowBg=COL_WINDOW_BG(white), Button=white, ButtonHovered=COL_HOVER,
    //           Separator=COL_SEPARATOR, FrameBg(transparent+bordered), Text, etc.
    ImGuiWrapper::push_toolbar_style(scale);            // (TOOLBAR) pushes 6 vars + 16 colors

    // X-axis right-edge correction: mirrors GizmoImguiSetNextWIndowPos 5-param logic.
    // Uses new_panel_w captured from the previous frame (0 on first frame = no correction).
    // Y-axis intentionally not corrected; also no handling if panel wider than canvas.
    static float new_panel_w = 0.f;   // width from previous frame — updated after window renders
    float new_panel_x = x;
    GizmoImguiSetNextWIndowPos(new_panel_x, y + legacy_panel_h + new_panel_gap,
                               new_panel_w, 0, ImGuiCond_Always);
    m_imgui->begin(wxString("DrillPanel2"),
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Row 1: Diameter
    // Source: m_new_hole_radius*2.  slider range [1, 25] / final clamp [0.1, 60].
    // Commit: only radius is written to selected holes — height is intentionally untouched.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("diameter"));
    ImGui::SameLine(label_col_w);
    // slider range [1, 25]: common drag range (matches old Hollow UI).
    // final clamp [0.1, 60]: allows keyboard input beyond slider range, protects against nonsense.
    // If current value exceeds slider range, handle visually saturates at the endpoint.
    float display_diam = m_new_hole_radius * 2.f;       // shared by slider and InputFloat below
    float pre_radius   = m_new_hole_radius;             // save before slider may change it
    if (draw_custom_slider("##sl_diameter", display_diam, 1.f, 25.f, 0.1f, 60.f, slider_w, is_input_enabled())) {
        m_new_hole_radius = display_diam / 2.f;         // slider: immediate apply, every frame
        begin_size_change(pre_radius, m_new_hole_height); // no-op after first call
        if (!m_selection_empty) {
            for (size_t idx = 0; idx < m_selected.size(); ++idx)
                if (m_selected[idx])
                    m_working_holes[idx].radius = m_new_hole_radius; // radius only — pending until Apply
            m_parent.set_as_dirty();
        }
    }
    if (ImGui::IsItemDeactivated())
        apply_size_change("Change hole radius");
    ImGui::SameLine();
    ImGui::PushItemWidth(value_box_w);
    ImGui::InputFloat("##ph_diameter", &display_diam, 0.f, 0.f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemActivated() && is_input_enabled())
        begin_size_change(m_new_hole_radius, m_new_hole_height);
    if (ImGui::IsItemDeactivatedAfterEdit() && is_input_enabled()) {
        m_new_hole_radius = std::clamp(display_diam, 0.1f, 60.f) / 2.f;  // final clamp [0.1, 60]
        if (!m_selection_empty) {
            for (size_t idx = 0; idx < m_selected.size(); ++idx)
                if (m_selected[idx])
                    m_working_holes[idx].radius = m_new_hole_radius; // radius only — pending until Apply
            m_parent.set_as_dirty();
        }
        apply_size_change("Change hole radius");
    }
    ImGui::PopItemWidth();

    // Row 2: Depth
    // Source: m_new_hole_height.  slider range [0, 10] / final clamp [0, 100].
    // Commit: only height is written to selected holes — radius is intentionally untouched.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("depth"));
    ImGui::SameLine(label_col_w);
    // slider range [0, 10]: common drag range (matches old Hollow UI).
    // final clamp [0, 100]: allows keyboard input beyond slider range, protects against nonsense.
    float display_depth = m_new_hole_height;            // shared by slider and InputFloat below
    float pre_height    = m_new_hole_height;            // save before slider may change it
    if (draw_custom_slider("##sl_depth", display_depth, 0.f, 10.f, 0.f, 100.f, slider_w, is_input_enabled())) {
        m_new_hole_height = display_depth;              // slider: immediate apply, every frame
        begin_size_change(m_new_hole_radius, pre_height); // no-op after first call
        if (!m_selection_empty) {
            for (size_t idx = 0; idx < m_selected.size(); ++idx)
                if (m_selected[idx])
                    m_working_holes[idx].height = m_new_hole_height; // height only — pending until Apply
            m_parent.set_as_dirty();
        }
    }
    if (ImGui::IsItemDeactivated())
        apply_size_change("Change hole depth");
    ImGui::SameLine();
    ImGui::PushItemWidth(value_box_w);
    ImGui::InputFloat("##ph_depth", &display_depth, 0.f, 0.f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemActivated() && is_input_enabled())
        begin_size_change(m_new_hole_radius, m_new_hole_height);
    if (ImGui::IsItemDeactivatedAfterEdit() && is_input_enabled()) {
        m_new_hole_height = std::clamp(display_depth, 0.f, 100.f);
        if (!m_selection_empty) {
            for (size_t idx = 0; idx < m_selected.size(); ++idx)
                if (m_selected[idx])
                    m_working_holes[idx].height = m_new_hole_height; // height only — pending until Apply
            m_parent.set_as_dirty();
        }
        apply_size_change("Change hole depth");
    }
    ImGui::PopItemWidth();

    // Button section: tighter spacing (overrides push_toolbar_style's ItemSpacing=(10,10)*scale)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f)); // +1 var

    // Row 4: [?] | Remove selected | Remove all
    // "?" help icon: same ImageButton3 + BeginTooltip2 pattern as GLGizmoAdvancedCut / GLGizmoFdmSupports.
    // PushStyleVar(2) scoped tightly around ImageButton3 + its tooltip block.
    {
        ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
        ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

        const float  icon_sz    = 25.f * scale;    // matches FdmSupports pattern — adjustable
        ImVec2       button_size(icon_sz, icon_sz);

        // Caption column width: max of caption strings (colons already included) + margin
        const float caption_max =
            std::max(m_imgui->calc_text_size(m_desc.at("left_click")).x,
                     m_imgui->calc_text_size(m_desc.at("right_click")).x)
            + 15.f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);    // no border on icon button
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {0, 0});   // no padding on icon button
        ImGui::ImageButton3(normal_id, hover_id, button_size);        // click = no-op (return value ignored)

        if (ImGui::IsItemHovered()) {
            // Tooltip anchored below the new panel — same y formula as FdmSupports::show_tooltip_information
            const float tooltip_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight()
                                    + y + legacy_panel_h + new_panel_gap;
            ImGui::BeginTooltip2(ImVec2(new_panel_x, tooltip_y));

            auto draw_row = [&](const wxString& caption, const wxString& text) {
                m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE,     caption);
                ImGui::SameLine(caption_max);
                m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG,  text);
            };
            draw_row(m_desc.at("left_click"),  m_desc.at("add_hole"));
            draw_row(m_desc.at("right_click"), m_desc.at("remove_hole"));

            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar(2);  // FrameBorderSize + FramePadding

        ImGui::SameLine();
        // Guard: same condition as old UI disabled_begin for "remove selected"
        if (ImGui::Button((m_desc.at("remove_selected") + "##new").ToUTF8().data(), ImVec2(btn_w, 0.f)))
            if (is_input_enabled() && !m_selection_empty)
                remove_selected = true;   // post-render block in on_render_input_window handles deletion
        ImGui::SameLine();
        // Guard: same condition as old UI disabled_begin for "remove all"
        if (ImGui::Button((m_desc.at("remove_all") + "##new").ToUTF8().data(), ImVec2(btn_w, 0.f)))
            if (is_input_enabled() && !m_working_holes.empty())
                remove_all = true;        // post-render block in on_render_input_window handles deletion

        // Row 5: [indent = icon width] | Preview | Reset direction
        ImGui::Dummy(ImVec2(button_size.x, 0.f)); // aligned to icon width
        ImGui::SameLine();
        if (ImGui::Button((m_desc.at("apply") + "##new").ToUTF8().data(), ImVec2(btn_w, 0.f))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Apply drain holes");
            mo->sla_drain_holes = m_working_holes;
            reslice_until_step(slaposDrillHoles);
            m_last_resliced_holes = mo->sla_drain_holes; // = m_working_holes; baseline synced to newly applied holes
        }
    } // end Row 4-5 help icon block

    ImGui::PopStyleVar(1);       // end ItemSpacing override (+1 var popped, net var stack balanced)

    new_panel_w = ImGui::GetWindowWidth(); // capture actual width for next frame's X correction

    m_imgui->end();

    ImGuiWrapper::pop_toolbar_style(); // (TOOLBAR) pops 6 vars + 16 colors
}


void GLGizmoDrill::on_render_input_window(float x, float y, float bottom_limit)
{
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

    bool first_run = true;

RENDER_AGAIN:
    const float approx_height = m_imgui->scaled(14.0f);
    y = std::min(y, bottom_limit - approx_height);

    bool remove_selected = false;
    bool remove_all = false;

    render_new_drill_panel(x, y, 0.f, mo, remove_selected, remove_all);

    if (remove_selected || remove_all) {
        m_parent.set_as_dirty();

        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points();
        }
        if (remove_selected)
            delete_selected_points();

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }
}

bool GLGizmoDrill::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_single_full_instance())
        return false;

    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    for (const auto& idx : list) {
        if (!selection.get_volume(idx)->printable)
            return false;
    }

    return true;
}

bool GLGizmoDrill::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoDrill::on_get_name() const
{
    return _u8L("Drill");
}

void GLGizmoDrill::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On) {
        // m_old_mo_id is intentionally NOT reset here: data_changed() detects the object-switch
        // via m_old_mo_id != mo->id() and initializes m_working_holes from sla_drain_holes.
        if (!selected_print_object_exists(m_parent, _L("Selected object has to be on the active bed."))) {
            m_state = Off;
            return;
        }
    }

    if (m_state == Off && m_old_state != Off) {
        // Discard pending working set. sla_drain_holes already holds the applied baseline;
        // no restore write-back is needed (Design B: sla_drain_holes = applied state at all times).
        m_working_holes.clear();
        m_last_resliced_holes.clear();
        m_old_mo_id = ObjectID{};
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
        m_c->instances_hider()->set_hide_full_scene(false);
    }

    m_old_state = m_state;
}


void GLGizmoDrill::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_hole_before_drag        = m_working_holes[m_hover_id].pos;
        m_hole_normal_before_drag = m_working_holes[m_hover_id].normal;
    }
    else {
        m_hole_before_drag        = Vec3f::Zero();
        m_hole_normal_before_drag = Vec3f::Zero();
    }
}


void GLGizmoDrill::on_stop_dragging()
{
    // m_working_holes[m_hover_id] already holds the final drag result written by on_dragging().
    // No TakeSnapshot — the move is pending in the working set until Apply.
    m_hole_before_drag        = Vec3f::Zero();
    m_hole_normal_before_drag = Vec3f::Zero();
}


void GLGizmoDrill::on_dragging(const UpdateData &data)
{
    assert(m_hover_id != -1);
    std::pair<Vec3f, Vec3f> pos_and_normal;
    if (!unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
        return;
    m_working_holes[m_hover_id].pos    = pos_and_normal.first;
    m_working_holes[m_hover_id].normal = -pos_and_normal.second;
}


void GLGizmoDrill::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}


void GLGizmoDrill::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}


void GLGizmoDrill::begin_size_change(float old_radius, float old_height)
{
    if (m_radius_before_change == 0.f && m_height_before_change == 0.f) {
        m_radius_before_change = old_radius;
        m_height_before_change = old_height;
        m_holes_before_change  = m_working_holes;
    }
}

void GLGizmoDrill::apply_size_change(const std::string& /*snapshot_name*/)
{
    if (m_radius_before_change == 0.f && m_height_before_change == 0.f)
        return;

    // m_working_holes already has the final size values from the per-frame slider writes.
    // No TakeSnapshot — size change is pending in the working set until Apply.
    m_parent.set_as_dirty();

    m_radius_before_change = 0.f;
    m_height_before_change = 0.f;
    m_holes_before_change.clear();
}


void GLGizmoDrill::select_point(int i)
{
    if (i == AllPoints || i == NoPoints) {
        m_selected.assign(m_selected.size(), i == AllPoints);
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints && !m_working_holes.empty()) {
            m_new_hole_radius = m_working_holes[0].radius;
            m_new_hole_height = m_working_holes[0].height;
        }
    }
    else {
        while (size_t(i) >= m_selected.size())
            m_selected.push_back(false);
        m_selected[i] = true;
        m_selection_empty = false;
        m_new_hole_radius = m_working_holes[i].radius;
        m_new_hole_height = m_working_holes[i].height;
    }
}


void GLGizmoDrill::unselect_point(int i)
{
    m_selected[i] = false;
    m_selection_empty = true;
    for (const bool sel : m_selected) {
        if (sel) {
            m_selection_empty = false;
            break;
        }
    }
}

void GLGizmoDrill::reload_cache()
{
    m_selected.clear();
    m_selected.assign(m_working_holes.size(), false);
}


void GLGizmoDrill::on_set_hover_id()
{
    if (m_c->selection_info()->model_object() == nullptr)
        return;

    if (int(m_working_holes.size()) <= m_hover_id)
        m_hover_id = -1;
}

void GLGizmoDrill::init_cylinder_model()
{
    if (!m_cylinder.model.is_initialized()) {
        indexed_triangle_set its = its_make_cylinder(1.0, 1.0);
        m_cylinder.model.init_from(its);
        m_cylinder.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }
}


bool GLGizmoDrill::has_pending_changes() const
{
    if (!m_c || !m_c->selection_info())
        return false;
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo)
        return false;
    return m_working_holes != mo->sla_drain_holes;
}


bool GLGizmoDrill::discard_pending_changes()
{
    if (!m_c || !m_c->selection_info())
        return false;
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo || m_working_holes == mo->sla_drain_holes)
        return false;

    m_working_holes = mo->sla_drain_holes;
    reload_cache();
    unregister_hole_raycasters_for_picking();
    register_hole_raycasters_for_picking();
    m_parent.set_as_dirty();
    return true;
}



} // namespace GUI
} // namespace Slic3r
