///|/ Copyright (c) Prusa Research 2022 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLGizmoSlaBase.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/MainFrame.hpp"
// Note: MultipleBeds.hpp intentionally NOT included — PhrozenOrca uses PartPlateList instead.

namespace Slic3r {
namespace GUI {

static const ColorRGBA DISABLED_COLOR = ColorRGBA::DARK_GRAY();
static const int VOLUME_RAYCASTERS_BASE_ID = (int)SceneRaycaster::EIdBase::Gizmo;

GLGizmoSlaBase::GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, SLAPrintObjectStep min_step)
: GLGizmoBase(parent, icon_filename, sprite_id)
, m_min_sla_print_object_step((int)min_step)
{}

// No-step constructor: m_min_sla_print_object_step stays -1.
// update_volumes() treats -1 as "no minimum step" → m_input_enabled = true whenever mesh is available.
// PhrozenOrca: used by GLGizmoSlaSupports because there is no equivalent of PrusaSlicer's
// slaposBase/slaposAssembly (an always-complete initialization step). The first real step
// in PhrozenOrca is slaposHollowing which requires actual computation.
GLGizmoSlaBase::GLGizmoSlaBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
: GLGizmoBase(parent, icon_filename, sprite_id)
// m_min_sla_print_object_step stays at default -1
{}

/*static*/ bool GLGizmoSlaBase::selected_print_object_exists(const GLCanvas3D& canvas, const wxString& text)
{
    const Selection& sel = canvas.get_selection();

    // Basic selection check.
    if (!sel.is_single_full_instance() || !sel.get_model()->objects[sel.get_object_idx()]) {
        if (!text.IsEmpty())
            wxGetApp().CallAfter([text]() {
                MessageDialog dlg(GUI::wxGetApp().mainframe, text,
                    _L("Bed selection mismatch"), wxICON_INFORMATION | wxOK);
                dlg.ShowModal();
            });
        return false;
    }

    // PhrozenOrca: sla_print() may return nullptr when SLAPrint is not initialized
    // (PartPlate system never calls set_sla_print() — Step 2.3 known issue).
    // When null, we cannot verify — return true so the gizmo stays open and shows
    // the fallback model instead of trying to close every frame.
    const SLAPrint* sla_print = canvas.sla_print();
    if (!sla_print)
        return true;

    if (!sla_print->get_print_object_by_model_object_id(
            sel.get_model()->objects[sel.get_object_idx()]->id())) {
        if (!text.IsEmpty())
            wxGetApp().CallAfter([text]() {
                MessageDialog dlg(GUI::wxGetApp().mainframe, text,
                    _L("Bed selection mismatch"), wxICON_INFORMATION | wxOK);
                dlg.ShowModal();
            });
        return false;
    }

    return true;
}

void GLGizmoSlaBase::reslice_until_step(SLAPrintObjectStep step, bool postpone_error_messages)
{
    wxGetApp().CallAfter([this, step, postpone_error_messages]() {
        if (m_c->selection_info())
            wxGetApp().plater()->reslice_SLA_until_step(step, *m_c->selection_info()->model_object(), postpone_error_messages);
        else {
            const Selection& selection = m_parent.get_selection();
            const int object_idx = selection.get_object_idx();
            if (object_idx >= 0 && !selection.is_wipe_tower())
                wxGetApp().plater()->reslice_SLA_until_step(step, *wxGetApp().plater()->model().objects[object_idx], postpone_error_messages);
        }
    });
}

CommonGizmosDataID GLGizmoSlaBase::on_get_requirements() const
{
    // fix-sla-thin-model-support-points: HollowedMesh is requested again.
    //
    // It had been dropped as dead weight, which meant HollowedMesh::on_update()
    // never ran and get_hollowed_mesh() always returned nullptr. The support
    // preview now measures the material available under each point, and it must
    // measure the SAME mesh the slicer does -- po->get_mesh_to_print(), i.e.
    // after hollowing and hole drilling -- or a hollowed object would preview a
    // deep bite into a wall that is not there any more.
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper)
              | int(CommonGizmosDataID::HollowedMesh)
              | int(CommonGizmosDataID::SupportsClipper));
}

void GLGizmoSlaBase::update_volumes()
{
    m_volumes.clear();
    unregister_volume_raycasters_for_picking();

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;

    const SLAPrintObject* po = m_c->selection_info()->print_object();
    // PhrozenOrca: po may be nullptr when SLAPrint is not initialized (PartPlate system issue).
    // Do NOT early-return here — fall through to the selection-based fallback below so the
    // gizmo always renders something instead of making the model appear to disappear.

    m_input_enabled = false;

    if (po != nullptr) {
        // PhrozenOrca: get_mesh_to_print() returns const TriangleMesh& (not shared_ptr<indexed_triangle_set>).
        TriangleMesh backend_mesh = po->get_mesh_to_print();

        if (!backend_mesh.empty()) {
            // PhrozenOrca: m_min_sla_print_object_step == -1 means "no step required".
            // Used by GLGizmoSlaSupports (no PrusaSlicer-equivalent always-done init step exists).
            // For all other gizmos (e.g. GLGizmoHollow with slaposSliceSupports), check step normally.
            if (m_min_sla_print_object_step < 0) {
                m_input_enabled = true; // always enable input when mesh is available
            } else {
                // PhrozenOrca: no last_completed_step(). Use is_step_done() — semantically equivalent
                // because SLA steps are sequential: if step N is done, all steps < N are also done.
                m_input_enabled = po->is_step_done((SLAPrintObjectStep)m_min_sla_print_object_step)
                    || po->model_object()->sla_points_status == sla::PointsStatus::UserModified;
            }

            const int object_idx   = m_parent.get_selection().get_object_idx();
            const int instance_idx = m_parent.get_selection().get_instance_idx();

            // PhrozenOrca: GLVolume::set_render_color() has the `selected` branch commented out (BBS).
            // Copy the original model color from the scene volume so the gizmo renders
            // the mesh in its normal viewport color instead of the default white.
            ColorRGBA original_model_color = ColorRGBA::WHITE();
            {
                const Selection& sel = m_parent.get_selection();
                for (unsigned int idx : sel.get_volume_idxs()) {
                    const GLVolume* sv = sel.get_volume(idx);
                    if (!sv->is_modifier && sv->composite_id.volume_id >= 0) {
                        original_model_color = sv->color;
                        break;
                    }
                }
            }

            const Geometry::Transformation& inst_trafo = po->model_object()->instances[instance_idx]->get_transformation();
            const double current_elevation = m_c->selection_info()->get_sla_shift();

            auto add_volume = [this, object_idx, instance_idx, &inst_trafo, current_elevation, original_model_color](const TriangleMesh& mesh, int volume_id, bool add_mesh_raycaster = false) {
                GLVolume* volume = m_volumes.volumes.emplace_back(new GLVolume());
                volume->model.init_from(mesh);
                volume->set_instance_transformation(inst_trafo);
                volume->set_sla_shift_z(current_elevation);
                if (add_mesh_raycaster)
                    volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
                if (m_input_enabled) {
                    // PhrozenOrca: selected flag has no visual effect (set_render_color selected
                    // branch is commented out in BBS). Set color explicitly to match viewport.
                    volume->selected = true;              // kept for API compatibility
                    volume->color = original_model_color; // actual color driver
                } else {
                    volume->set_color(DISABLED_COLOR);
                }
                volume->composite_id = GLVolume::CompositeID(object_idx, volume_id, instance_idx);
            };

            const Transform3d po_trafo_inverse = po->trafo().inverse();

            // main mesh
            // Note: MultipleBeds translate removed — PhrozenOrca uses PartPlateList, mesh coords need no bed offset.
            backend_mesh.transform(po_trafo_inverse);
            add_volume(backend_mesh, 0, true);

            // supports mesh and pad mesh.
            // Skip when the user explicitly cleared all support points without reslicing
            // (Remove All path). The pipeline cache in SLAPrintObject may still hold the
            // old tree from a previous generation; mo->sla_support_points is the
            // authoritative source of truth for whether a stale tree should be hidden.
            const bool user_cleared_all = (mo->sla_points_status == sla::PointsStatus::UserModified
                                            && mo->sla_support_points.empty());
            if (!user_cleared_all) {
                TriangleMesh supports_mesh = po->support_mesh();
                if (!supports_mesh.empty()) {
                    // Note: MultipleBeds translate removed — see above.
                    supports_mesh.transform(po_trafo_inverse);
                    add_volume(supports_mesh, -int(slaposSupportTree));
                }

                TriangleMesh pad_mesh = po->pad_mesh();
                if (!pad_mesh.empty()) {
                    // Note: MultipleBeds translate removed — see above.
                    pad_mesh.transform(po_trafo_inverse);
                    add_volume(pad_mesh, -int(slaposPad));
                }
            }
        }
    }

    if (m_volumes.volumes.empty()) {
        // No valid mesh found in the backend (po==nullptr or backend_mesh empty).
        // Use the selection to duplicate the volumes so the gizmo always shows something.
        // PhrozenOrca: enable input in fallback so model renders in normal (selected) color,
        // matching PrusaSlicer appearance. Manual support point editing is still possible.
        m_input_enabled = true;
        const Selection& selection = m_parent.get_selection();
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            const GLVolume* v = selection.get_volume(idx);
            if (!v->is_modifier) {
                m_volumes.volumes.emplace_back(new GLVolume());
                GLVolume* new_volume = m_volumes.volumes.back();
                const TriangleMesh& mesh = mo->volumes[v->volume_idx()]->mesh();
                new_volume->model.init_from(mesh);
                new_volume->set_instance_transformation(v->get_instance_transformation());
                new_volume->set_volume_transformation(v->get_volume_transformation());
                new_volume->set_sla_shift_z(m_c->selection_info()->get_sla_shift());
                new_volume->selected = true;  // kept for API compatibility
                new_volume->color = v->color; // PhrozenOrca: copy original color (selected flag has no visual effect)
                new_volume->mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
            }
        }
    }

    register_volume_raycasters_for_picking();
}

void GLGizmoSlaBase::clear_support_volumes()
{
    GLVolumePtrs& vols = m_volumes.volumes;
    GLVolumePtrs remaining;
    for (GLVolume* v : vols) {
        if (v->is_sla_support() || v->is_sla_pad())
            delete v;
        else
            remaining.push_back(v);
    }
    vols = std::move(remaining);
    // Raycasters are registered only for model volumes (not support/pad),
    // so no raycaster re-registration is needed after removing support/pad volumes.
}

void GLGizmoSlaBase::render_volumes()
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light_clip");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("emission_factor", 0.0f);
    const Camera& camera = wxGetApp().plater()->get_camera();

    // Prepare IMSlider dual handles: world-Z band via z_range (same as main scene).
    if (m_parent.sla_oc_clip_slider_session_active()) {
        const std::array<ClippingPlane, 2>& cp = m_parent.get_clipping_planes();
        m_volumes.set_z_range(static_cast<float>(-cp[0].get_data()[3]), static_cast<float>(cp[1].get_data()[3]));
        ClippingPlane no_plane = ClippingPlane::ClipsNothing();
        no_plane.set_offset(FLT_MAX);
        m_volumes.set_clipping_plane(no_plane.get_data());
    } else {
        m_volumes.set_z_range(-FLT_MAX, FLT_MAX);
        ClippingPlane clipping_plane = (m_c->object_clipper()->get_position() == 0.0) ? ClippingPlane::ClipsNothing() : *m_c->object_clipper()->get_clipping_plane();
        if (m_c->object_clipper()->get_position() != 0.0)
            clipping_plane.set_normal(-clipping_plane.get_normal());
        else
            // on Linux the clipping plane does not work when using DBL_MAX
            clipping_plane.set_offset(FLT_MAX);
        m_volumes.set_clipping_plane(clipping_plane.get_data());
    }

    for (GLVolume* v : m_volumes.volumes) {
        v->is_active = m_show_sla_supports || (!v->is_sla_pad() && !v->is_sla_support());
    }

    // PhrozenOrca: render() requires cnv_size as 5th param.
    m_volumes.render(GLVolumeCollection::ERenderType::Opaque, true, camera.get_view_matrix(), camera.get_projection_matrix(), m_parent.get_canvas_size());
    shader->stop_using();
}

void GLGizmoSlaBase::register_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volumes.volumes.size(); ++i) {
        const GLVolume* v = m_volumes.volumes[i];
        if (!v->is_sla_pad() && !v->is_sla_support())
            m_volume_raycasters.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i, *v->mesh_raycaster, v->world_matrix()));
    }
}

void GLGizmoSlaBase::unregister_volume_raycasters_for_picking()
{
    for (size_t i = 0; i < m_volume_raycasters.size(); ++i) {
        m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, VOLUME_RAYCASTERS_BASE_ID + (int)i);
    }
    m_volume_raycasters.clear();
}

// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoSlaBase::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    if (m_c->raycaster()->raycasters().size() != 1)
        return false;
    if (!m_c->raycaster()->raycaster())
        return false;
    if (m_volumes.volumes.empty())
        return false;

    // Use the same transform as gizmo mesh rendering (includes Model Lift Height via sla_shift_z).
    const Transform3d trafo = m_volumes.volumes.front()->world_matrix();

    // The raycaster query
    Vec3f hit;
    Vec3f normal;
    if (m_c->raycaster()->raycaster()->unproject_on_mesh(
        mouse_pos,
        trafo,
        wxGetApp().plater()->get_camera(),
        hit,
        normal,
        m_c->object_clipper()->get_position() != 0.0 ? m_c->object_clipper()->get_clipping_plane() : nullptr)) {
        // Return both the point and the facet normal.
        pos_and_normal = std::make_pair(hit, normal);
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
