#ifndef slic3r_ContourZ_hpp_
#define slic3r_ContourZ_hpp_

#include "libslic3r.h"

namespace Slic3r {

class Layer;
namespace sla {
    class IndexedMesh;
}

// Make Z-contoured extrusion paths for a layer
// This implements the Z Anti-aliasing feature which adjusts Z-coordinates
// of extrusion points to better match the actual mesh surface, reducing
// the stair-stepping effect on sloped surfaces.
void make_contour_z(Layer *layer, const sla::IndexedMesh &mesh);

} // namespace Slic3r

#endif // slic3r_ContourZ_hpp_
