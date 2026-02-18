# Z Anti-aliasing (ZAA) Feature

## Overview

Z Anti-aliasing is a feature that dynamically adjusts the Z height of extrusion points during printing to better match the actual mesh surface geometry. This reduces the visible "staircase" effect on sloped surfaces and improves overall print quality, especially for top surfaces.

## How It Works

### 1. Configuration
Users can enable and configure ZAA through the following settings:
- **zaa_enabled**: Enable/disable Z anti-aliasing (default: false)
- **zaa_min_z**: Minimum Z height adjustment in mm (default: 0.05mm)
- **zaa_minimize_perimeter_height**: Angle threshold for perimeter height reduction (default: 35°)
- **zaa_region_disable**: Disable ZAA for specific regions (default: false)
- **zaa_dont_alternate_fill_direction**: Don't alternate fill direction with ZAA (default: false)

### 2. Processing Pipeline
1. **After Slicing**: Once layers are sliced, the mesh is converted to an IndexedMesh for efficient ray-casting
2. **Z-Contouring**: For each layer, extrusion paths are analyzed:
   - Sample points along paths at ~0.1mm resolution
   - Cast rays up and down from each point to find the mesh surface
   - Calculate optimal Z-offset based on distance to surface
   - Apply constraints based on configuration settings
3. **G-code Generation**: Output XYZ coordinates with adjusted extrusion flow rates

### 3. Affected Extrusion Types
- Top solid infill (erTopSolidInfill)
- Ironing (erIroning)
- External perimeters (erExternalPerimeter)
- Perimeters (erPerimeter)

## Algorithm Details

### Ray Casting
For each point along an extrusion path:
1. Cast two rays from the point: one upward (Z+) and one downward (Z-)
2. Find the closest intersection with the mesh surface
3. Calculate the distance to the surface
4. Determine the optimal Z-offset based on:
   - Distance to surface
   - Layer height
   - Minimum Z constraint (zaa_min_z)
   - Surface normal angle

### Z-Offset Constraints
- Maximum upward adjustment: zaa_min_z (e.g., 0.05mm)
- Maximum downward adjustment: -(layer_height - zaa_min_z)
- For external perimeters: only downward adjustments allowed (to avoid seam appearance)
- For ironing: extended range up to full layer height

### Extrusion Flow Adjustment
When Z position changes, extrusion amount is adjusted proportionally:
```
extrusion_ratio = (layer_height + z_offset) / layer_height
```
This ensures proper material deposition at different heights.

## Benefits

1. **Reduced Layer Lines**: Smoother appearance on sloped surfaces
2. **Better Top Surface Quality**: Fills gaps and reduces artifacts without ironing
3. **Sub-layer Detail**: Can capture fine surface textures from the 3D model
4. **Minimal Performance Impact**: Only affects top surfaces and perimeters
5. **Compatible**: Works with existing slicer features (supports, infill patterns, etc.)

## Limitations

1. **Collision Risk**: Nozzle may collide with previously printed material if Z adjustments are too aggressive
2. **Speed Constraints**: May require reduced print speeds (especially for outer walls and top surfaces) for best results
3. **Mesh Quality**: Requires clean, manifold meshes for accurate ray-casting
4. **Computational Cost**: Ray-casting adds processing time during slicing

## Usage Recommendations

### Getting Started
1. Enable ZAA: Set `zaa_enabled = true` in print settings
2. Start with conservative settings:
   - zaa_min_z = 0.05mm
   - zaa_minimize_perimeter_height = 35°
3. Reduce print speeds for affected features:
   - Outer wall speed: 20-30 mm/s
   - Inner wall speed: 30-40 mm/s
   - Top surface speed: 20-30 mm/s

### Advanced Tuning
- **For aggressive smoothing**: Increase zaa_min_z to 0.1mm
- **For shallow angles**: Adjust zaa_minimize_perimeter_height
- **For specific regions**: Use zaa_region_disable to disable ZAA selectively

### Troubleshooting
- **Stringing/artifacts**: Reduce print speeds or zaa_min_z
- **Nozzle collisions**: Reduce zaa_min_z or zaa_minimize_perimeter_height
- **Poor extrusion**: Check flow rate calibration

## Implementation Notes

### Files Modified
- `src/libslic3r/ContourZ.cpp/hpp`: Core algorithm implementation
- `src/libslic3r/ExtrusionEntity.hpp`: Added 3D polyline and z_contoured flag
- `src/libslic3r/PrintConfig.hpp/cpp`: Configuration options
- `src/libslic3r/Layer.hpp/cpp`: Layer processing integration
- `src/libslic3r/PrintObject.cpp`: Object-level ZAA application
- `src/libslic3r/Print.cpp`: Workflow integration
- `src/libslic3r/GCode.cpp`: G-code generation for 3D paths

### Key Data Structures
- **Polyline3**: 3D polyline with Vec3crd points
- **IndexedMesh**: Efficient mesh representation for ray-casting
- **ExtrusionPath**: Extended with polyline3 and z_contoured fields

## References

- Original research paper: [Anti-aliasing for fused filament deposition](https://arxiv.org/abs/1609.03032)
- BambuStudio-ZAA implementation: https://github.com/adob/BambuStudio-ZAA
- GCodeZAA post-processor: https://github.com/Theaninova/GCodeZAA

## Future Improvements

1. **Collision Detection**: Implement nozzle-path collision checking
2. **Performance Optimization**: Use spatial acceleration structures for ray-casting
3. **Flow Calibration**: Automatic flow rate adjustment based on Z-height changes
4. **Variable Resolution**: Adaptive sampling based on surface curvature
5. **Support Integration**: Extend ZAA to support material interfaces
