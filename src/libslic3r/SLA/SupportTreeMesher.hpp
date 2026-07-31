#ifndef SUPPORTTREEMESHER_HPP
#define SUPPORTTREEMESHER_HPP

#include "libslic3r/Point.hpp"

#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/TriangleMesh.hpp"
//#include "libslic3r/SLA/Contour3D.hpp"

namespace Slic3r { namespace sla {

using Portion = std::tuple<double, double>;

inline Portion make_portion(double a, double b)
{
    return std::make_tuple(a, b);
}

indexed_triangle_set sphere(double  rho,
                            Portion portion = make_portion(0., 2. * PI),
                            double  fa      = (2. * PI / 360.));

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: Height
// ssteps: how many edges will create the base circle
// sp: starting point
indexed_triangle_set cylinder(double       r,
                              double       h,
                              size_t       steps = 45,
                              const Vec3d &sp    = Vec3d::Zero());

indexed_triangle_set pinhead(double r_pin,
                             double r_back,
                             double length,
                             size_t steps = 45);

// Simplified pinhead for manual-support editor preview (no back-sphere bulge).
indexed_triangle_set pinhead_preview(double r_pin,
                                     double r_back,
                                     double length,
                                     size_t steps = 45);

indexed_triangle_set halfcone(double       baseheight,
                              double       r_bottom,
                              double       r_top,
                              const Vec3d &pt    = Vec3d::Zero(),
                              size_t       steps = 45);

// Pinhead geometry in its local frame: anchor at the origin, axis along -Z.
// Depends only on the size fields of `h` (r_pin_mm / r_back_mm / width_mm /
// penetration_mm / r_contact_mm), never on pos / dir. Callers that draw many
// heads sharing the same size parameters can therefore build this once and
// place each instance with a model matrix instead of baking the placement
// into the vertices (see GLGizmoSlaSupports::render_points()).
inline indexed_triangle_set head_mesh_body(const Head &h, size_t steps, bool preview)
{
    // Preview: total axial length ~= width_mm (segment length), not width + extra sphere padding.
    const double segment_len = preview
        ? std::max(0.01, h.width_mm - 2.0 * h.r_pin_mm - 2.0 * h.r_back_mm)
        : h.width_mm;
    const double z_shift = preview
        ? (2.0 * h.r_pin_mm + segment_len + h.r_back_mm - h.penetration_mm)
        : (h.fullwidth() - h.r_back_mm);

    indexed_triangle_set mesh = preview
        ? pinhead_preview(h.r_pin_mm, h.r_back_mm, segment_len, steps)
        : pinhead(h.r_pin_mm, h.r_back_mm, h.width_mm, steps);

    for (auto& p : mesh.vertices) p.z() -= float(z_shift);

    if (h.r_contact_mm > h.r_pin_mm) {
        // Concentric with pin sphere: center at penetration_mm - r_pin_mm so the
        // contact sphere protrudes above the pin sphere by (r_contact - r_pin) mm.
        float center_z = float(h.penetration_mm - h.r_pin_mm);
        auto contact = sphere(h.r_contact_mm, make_portion(0, PI), 2 * PI / steps);
        for (auto &p : contact.vertices) p.z() += center_z;
        its_merge(mesh, contact);
    }

    return mesh;
}

inline indexed_triangle_set head_mesh_local(const Head &h, size_t steps, bool preview)
{
    indexed_triangle_set mesh = head_mesh_body(h, steps, preview);

    using Quaternion = Eigen::Quaternion<float>;

    auto quatern = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, -1.f},
                                              h.dir.cast<float>());

    Vec3f pos = h.pos.cast<float>();
    for (auto& p : mesh.vertices) p = quatern * p + pos;

    return mesh;
}

inline indexed_triangle_set get_mesh(const Head &h, size_t steps)
{
    return head_mesh_local(h, steps, /*preview=*/false);
}

inline indexed_triangle_set get_mesh_preview(const Head &h, size_t steps)
{
    return head_mesh_local(h, steps, /*preview=*/true);
}

inline indexed_triangle_set get_mesh(const Pillar &p, size_t steps)
{
    if(p.height > EPSILON) { // Endpoint is below the starting point
        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        return halfcone(p.height, p.r_end, p.r_start, p.endpt, steps);
    }

    return {};
}

inline indexed_triangle_set get_mesh(const Pedestal &p, size_t steps)
{
    return halfcone(p.height, p.r_bottom, p.r_top, p.pos, steps);
}

inline indexed_triangle_set get_mesh(const Junction &j, size_t steps)
{
    indexed_triangle_set mesh = sphere(j.r, make_portion(0, PI), 2 *PI / steps);
    Vec3f pos = j.pos.cast<float>();
    for(auto& p : mesh.vertices) p += pos;
    return mesh;
}

inline indexed_triangle_set get_mesh(const Bridge &br, size_t steps)
{
    using Quaternion = Eigen::Quaternion<float>;
    Vec3d v = (br.endp - br.startp);
    Vec3d dir = v.normalized();
    double d = v.norm();

    indexed_triangle_set mesh = cylinder(br.r, d, steps);

    auto quater = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, 1.f},
                                             dir.cast<float>());

    Vec3f startp = br.startp.cast<float>();
    for(auto& p : mesh.vertices) p = quater * p + startp;

    return mesh;
}

inline indexed_triangle_set get_mesh(const DiffBridge &br, size_t steps)
{
    double h = br.get_length();
    indexed_triangle_set mesh = halfcone(h, br.r, br.end_r, Vec3d::Zero(), steps);

    using Quaternion = Eigen::Quaternion<float>;

    // We rotate the head to the specified direction. The head's pointing
    // side is facing upwards so this means that it would hold a support
    // point with a normal pointing straight down. This is the reason of
    // the -1 z coordinate
    auto quatern = Quaternion::FromTwoVectors(Vec3f{0.f, 0.f, 1.f},
                                              br.get_dir().cast<float>());

    Vec3f startp = br.startp.cast<float>();
    for(auto& p : mesh.vertices) p = quatern * p + startp;

    return mesh;
}

}} // namespace Slic3r::sla

#endif // SUPPORTTREEMESHER_HPP
