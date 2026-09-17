#include <catch2/catch_all.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "test_helpers.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// The layer at this Z is the last one of the base, so its top surface is the ledge.
const double ledge_z = 5.0;

// The first layer, at initial_layer_print_height.
const double first_layer_z = 0.2;

// TestMesh::step scaled 3x in X/Y: a 60x60x5 base carrying a 54x54 column up to z=10, leaving a 3mm
// top ledge around a feature that keeps rising. That is the geometry both only_one_wall_top and the
// top surface expansion act on. The ledge has to stay wider than the wall band plus two top-infill
// lines, or the expansion discards it as a sliver and the tests below assert nothing.
TriangleMesh step_with_ledge()
{
    TriangleMesh m = Slic3r::Test::mesh(TestMesh::step);
    m.scale(Vec3f(3.f, 3.f, 1.f));
    return m;
}

// Every setting the assertions depend on, so none of them rests on a default.
DynamicPrintConfig base_config(const char *wall_generator)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_generator",             wall_generator },
        { "layer_height",               0.2 },  // puts a layer boundary exactly on ledge_z
        { "initial_layer_print_height", 0.2 },
        { "wall_loops",                 3 },
        { "sparse_infill_density",      "15%" },
        { "top_shell_layers",           3 },
        { "bottom_shell_layers",        3 },
        { "top_surface_density",        "100%" },
        { "top_surface_expansion",      0.0 },
        { "only_one_wall_top",          false },
        { "only_one_wall_first_layer",  false },
        // Do not let the one-wall threshold discard the 3mm ledge before the feature sees it.
        { "min_width_top_surface",      0.0 },
    });
    return config;
}

DynamicPrintConfig staggered_config(bool enabled)
{
    DynamicPrintConfig config = base_config("arachne");
    config.set_deserialize_strict({
        { "staggered_perimeters",          enabled },
        { "staggered_perimeter_flow_ratio", 1.0 },
        { "wall_sequence",                 "inner wall/outer wall" },
        { "nozzle_diameter",               "0.4" },
        { "initial_layer_line_width",      "0.45" },
        { "inner_wall_line_width",         "0.45" },
        { "outer_wall_line_width",         "0.45" },
        { "top_surface_line_width",        "0.45" },
        { "elefant_foot_compensation",     0.0 },
    });
    return config;
}

double collection_length(const ExtrusionEntityCollection &coll)
{
    double len = 0.;
    for (const ExtrusionEntity *entity : coll.flatten().entities)
        if (! entity->is_collection())
            len += entity->length();
    return len;
}

// Extruded length per layer. Two slices are compared through this rather than through their G-code,
// because the G-code carries a config block that differs whenever any setting differs.
struct SliceLengths {
    std::vector<double> perimeters;
    std::vector<double> fills;
};

SliceLengths slice_lengths(const Print &print)
{
    SliceLengths out;
    for (const Layer *layer : print.objects().front()->layers()) {
        double perimeters = 0., fills = 0.;
        for (const LayerRegion *region : layer->regions()) {
            perimeters += collection_length(region->perimeters);
            fills      += collection_length(region->fills);
        }
        out.perimeters.push_back(perimeters);
        out.fills.push_back(fills);
    }
    return out;
}

double perimeter_length_at(const Print &print, double print_z)
{
    for (const Layer *layer : print.objects().front()->layers())
        if (std::abs(layer->print_z - print_z) < 1e-4) {
            double len = 0.;
            for (const LayerRegion *region : layer->regions())
                len += collection_length(region->perimeters);
            return len;
        }
    return 0.;
}

// Largest per-layer difference between two series; a negative result means they are not comparable.
double max_difference(const std::vector<double> &a, const std::vector<double> &b)
{
    if (a.size() != b.size() || a.empty())
        return -1.;
    double worst = 0.;
    for (size_t i = 0; i < a.size(); ++ i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

} // namespace

TEST_CASE("Staggered walls preserve layer gaps and flat object boundaries", "[Perimeters][Regression]")
{
    const int layer_count = GENERATE(1, 2, 3, 4, 10);
    const int raft_layers = GENERATE(0, 3);
    const bool enabled = GENERATE(false, true);
    CAPTURE(layer_count, raft_layers, enabled);

    DynamicPrintConfig config = staggered_config(enabled);
    config.set_deserialize_strict({{ "raft_layers", raft_layers }});
    const double height = config.opt_float("layer_height");
    Print print;
    Model model;
    init_print(std::vector<TriangleMesh>{make_cube(12., 12., layer_count * height)}, print, model, config, nullptr, false);
    REQUIRE(print.objects().size() == 1);
    print.objects().front()->make_perimeters();
    const auto &layers = print.objects().front()->layers();
    REQUIRE(layers.size() == size_t(layer_count));
    REQUIRE(layers.front()->id() == size_t(raft_layers));
    const double bottom_z = layers.front()->print_z - layers.front()->height;
    std::array<double, 3> previous_z{bottom_z, bottom_z, bottom_z};

    for (size_t i = 0; i < layers.size(); ++i) {
        CAPTURE(i);
        const Layer &layer = *layers[i];
        REQUIRE(layer.regions().size() == 1);
        const ExtrusionEntityCollection walls = layer.regions().front()->perimeters.flatten();
        REQUIRE(walls.size() == 3);
        std::vector<int> order;
        const bool raised = enabled && layer_count >= 4 && i > 0 && i + 2 < layers.size();
        for (const ExtrusionEntity *entity : walls.entities) {
            const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity);
            REQUIRE(loop != nullptr);
            const int inset = loop->inset_idx;
            REQUIRE(inset >= 0);
            REQUIRE(inset < 3);
            REQUIRE_FALSE(loop->paths.empty());
            order.push_back(inset);
            double wall_z = layer.print_z;
            for (const ExtrusionPath &path : loop->paths) {
                CAPTURE(inset);
                const double offset = raised && inset == 1 ? 0.5 : 0.0;
                CHECK_THAT(path.z_offset, Catch::Matchers::WithinAbs(offset, 1e-6));
                CHECK_THAT(path.height, Catch::Matchers::WithinAbs(height, 1e-6));
                wall_z = layer.print_z + path.z_offset * path.height;
                // Transition flow must fill the actual gap to this wall's previous Z.
                CHECK_THAT(path.extrusion_multiplier * path.height,
                           Catch::Matchers::WithinAbs(wall_z - previous_z[inset], 1e-6));
                CHECK(wall_z <= layers.back()->print_z + 1e-6);
            }
            previous_z[inset] = wall_z;
        }
        const std::vector<int> expected_order = raised ? std::vector<int>{2, 0, 1} : std::vector<int>{2, 1, 0};
        CHECK(order == expected_order);
    }
    for (double z : previous_z)
        CHECK_THAT(z, Catch::Matchers::WithinAbs(layers.back()->print_z, 1e-6));
}

TEST_CASE("Unshifted brick layers retain the configured wall order", "[Perimeters][Regression]")
{
    const char *sequence = GENERATE("inner wall/outer wall", "outer wall/inner wall", "inner-outer-inner wall");
    const int layer_count = GENERATE(3, 10);
    CAPTURE(sequence, layer_count);

    auto orders_for = [&](bool enabled) {
        DynamicPrintConfig config = staggered_config(enabled);
        config.set_deserialize_strict({{ "wall_sequence", sequence }});
        Print print;
        Model model;
        init_print(std::vector<TriangleMesh>{make_cube(12., 12., layer_count * config.opt_float("layer_height"))},
                   print, model, config, nullptr, false);
        REQUIRE(print.objects().size() == 1);
        print.objects().front()->make_perimeters();
        std::vector<std::vector<int>> orders;
        for (const Layer *layer : print.objects().front()->layers()) {
            REQUIRE(layer->regions().size() == 1);
            std::vector<int> order;
            for (const ExtrusionEntity *entity : layer->regions().front()->perimeters.flatten().entities)
                order.push_back(entity->inset_idx);
            REQUIRE(order.size() == 3);
            orders.push_back(std::move(order));
        }
        return orders;
    };
    const auto regular = orders_for(false);
    const auto staggered = orders_for(true);
    REQUIRE(regular.size() == size_t(layer_count));
    REQUIRE(staggered.size() == regular.size());
    for (size_t i = 0; i < regular.size(); ++i)
        if (layer_count < 4 || i == 0 || i + 2 >= regular.size())
            CHECK(staggered[i] == regular[i]);
}

// The expansion only retypes area as top solid infill, so it can do nothing where there is no top
// fill to begin with: zero top shell layers retypes the top surfaces as internal, and a top surface
// density of 0% leaves the top layer with walls only. The last section is the control - the same
// expansion on the same model does change the slice once a top fill exists - without which the two
// equality checks above it would hold for an unrelated reason.
TEST_CASE("Top surface expansion only acts where there is a top fill", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto lengths_for = [wall_generator](int top_shell_layers, const char *top_surface_density, double expansion) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "top_shell_layers",      top_shell_layers },
            { "top_surface_density",   top_surface_density },
            { "top_surface_expansion", expansion },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return slice_lengths(print);
    };

    SECTION("no top shell layers") {
        const SliceLengths off = lengths_for(0, "100%", 0.0);
        const SliceLengths on  = lengths_for(0, "100%", 2.0);
        REQUIRE(off.perimeters.size() == on.perimeters.size());
        CHECK_THAT(max_difference(off.perimeters, on.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
        CHECK_THAT(max_difference(off.fills,      on.fills),      Catch::Matchers::WithinAbs(0., 1.0));
    }

    SECTION("zero top surface density") {
        const SliceLengths off = lengths_for(3, "0%", 0.0);
        const SliceLengths on  = lengths_for(3, "0%", 2.0);
        REQUIRE(off.perimeters.size() == on.perimeters.size());
        CHECK_THAT(max_difference(off.perimeters, on.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
        CHECK_THAT(max_difference(off.fills,      on.fills),      Catch::Matchers::WithinAbs(0., 1.0));
    }

    SECTION("with a top fill the same expansion does change the slice") {
        const SliceLengths off = lengths_for(3, "100%", 0.0);
        const SliceLengths on  = lengths_for(3, "100%", 2.0);
        REQUIRE(off.fills.size() == on.fills.size());
        CHECK(max_difference(off.fills, on.fills) > scale_(0.5));
    }
}

// With no top shell the top surfaces are retyped as internal, so the top surface density has nothing
// left to control: there is no top fill, and only_one_wall_top - the one route from the density to the
// perimeters - is itself switched off for want of a top surface to act on.
TEST_CASE("Top surface density does not affect a slice without a top shell", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto lengths_for = [wall_generator](const char *top_surface_density) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "top_shell_layers",    0 },
            { "only_one_wall_top",   true },
            { "top_surface_density", top_surface_density },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return slice_lengths(print);
    };

    const SliceLengths solid = lengths_for("100%");
    const SliceLengths none  = lengths_for("0%");
    REQUIRE(solid.perimeters.size() == none.perimeters.size());
    CHECK_THAT(max_difference(solid.perimeters, none.perimeters), Catch::Matchers::WithinAbs(0., 1.0));
    CHECK_THAT(max_difference(solid.fills,      none.fills),      Catch::Matchers::WithinAbs(0., 1.0));
}

// On the ledge layer the inner walls are given up to the top fill, so that layer loses wall length.
// The handover needs a top fill that reaches the freed space: at a top surface density of 0% there is
// no top fill at all, and without top_surface_expansion the fill never grows over the walls. Either
// way the feature still runs, through the original generation, which keeps the inner walls up to the
// top boundary - putting that layer back between the plain and the one-wall slice.
TEST_CASE("Only one wall on top surfaces drops inner walls only where a top fill replaces them", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto ledge_perimeters_for = [wall_generator](bool only_one_wall_top, const char *top_surface_density, double expansion) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "only_one_wall_top",     only_one_wall_top },
            { "top_surface_density",   top_surface_density },
            { "top_surface_expansion", expansion },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return perimeter_length_at(print, ledge_z);
    };

    const double plain              = ledge_perimeters_for(false, "100%", 2.0);
    const double one_wall           = ledge_perimeters_for(true,  "100%", 2.0);
    const double one_wall_no_fill   = ledge_perimeters_for(true,  "0%",   2.0);
    const double one_wall_no_expand = ledge_perimeters_for(true,  "100%", 0.0);

    REQUIRE(plain > 0.);
    CHECK(one_wall < plain);
    // Both fall back to the original generation, which cuts the walls back to the top boundary but not past it.
    CHECK(one_wall_no_fill > one_wall);
    CHECK(one_wall_no_fill < plain);
    CHECK(one_wall_no_expand > one_wall);
    CHECK(one_wall_no_expand < plain);
}

// The bottom counterpart: the first layer is thinned to a single wall only where a bottom shell fills the
// space behind it. With no bottom shell layers the bottom surfaces are retyped as internal, so that wall
// would ring sparse infill on the bed - the option is switched off instead, and the GUI hides it in that
// state so a profile that left it enabled cannot act behind a hidden checkbox.
TEST_CASE("Only one wall on the first layer needs a bottom shell", "[Perimeters]")
{
    const char *wall_generator = GENERATE("classic", "arachne");
    CAPTURE(wall_generator);

    auto first_layer_perimeters_for = [wall_generator](bool only_one_wall_first_layer, int bottom_shell_layers) {
        DynamicPrintConfig config = base_config(wall_generator);
        config.set_deserialize_strict({
            { "only_one_wall_first_layer", only_one_wall_first_layer },
            { "bottom_shell_layers",       bottom_shell_layers },
        });
        Print print;
        init_and_process_print({ step_with_ledge() }, print, config);
        REQUIRE_FALSE(print.objects().empty());
        return perimeter_length_at(print, first_layer_z);
    };

    const double plain             = first_layer_perimeters_for(false, 3);
    const double one_wall          = first_layer_perimeters_for(true,  3);
    // Both at zero bottom shell layers, so everything else that setting changes cancels out between them.
    const double plain_no_shell    = first_layer_perimeters_for(false, 0);
    const double one_wall_no_shell = first_layer_perimeters_for(true,  0);

    REQUIRE(plain > 0.);
    CHECK(one_wall < plain);
    // No bottom shell: the option is inert, down to the same walls an unchecked box gives.
    CHECK_THAT(one_wall_no_shell, Catch::Matchers::WithinAbs(plain_no_shell, 1.0));
}
