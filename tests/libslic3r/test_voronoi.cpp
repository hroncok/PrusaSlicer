#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <stack>

#include <libslic3r/Polygon.hpp>
#include <libslic3r/Polyline.hpp>
#include <libslic3r/EdgeGrid.hpp>
#include <libslic3r/Geometry.hpp>

#define BOOST_VORONOI_USE_GMP 1
#include "boost/polygon/voronoi.hpp"

using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

using namespace Slic3r;

struct VD : public boost::polygon::voronoi_diagram<double> {
    typedef double                                          coord_type;
    typedef boost::polygon::point_data<coordinate_type>     point_type;
    typedef boost::polygon::segment_data<coordinate_type>   segment_type;
    typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
};

// #define VORONOI_DEBUG_OUT

#ifdef VORONOI_DEBUG_OUT
#include <libslic3r/SVG.hpp>
#endif

#ifdef VORONOI_DEBUG_OUT
namespace boost { namespace polygon {

// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_graphic_utils.hpp header file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
template <typename CT>
class voronoi_visual_utils {
 public:
  // Discretize parabolic Voronoi edge.
  // Parabolic Voronoi edges are always formed by one point and one segment
  // from the initial input set.
  //
  // Args:
  //   point: input point.
  //   segment: input segment.
  //   max_dist: maximum discretization distance.
  //   discretization: point discretization of the given Voronoi edge.
  //
  // Template arguments:
  //   InCT: coordinate type of the input geometries (usually integer).
  //   Point: point type, should model point concept.
  //   Segment: segment type, should model segment concept.
  //
  // Important:
  //   discretization should contain both edge endpoints initially.
  template <class InCT1, class InCT2,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<InCT1> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<InCT2> >::type
        >::type
      >::type
    >::type,
    void
  >::type discretize(
      const Point<InCT1>& point,
      const Segment<InCT2>& segment,
      const CT max_dist,
      std::vector< Point<CT> >* discretization) {
    // Apply the linear transformation to move start point of the segment to
    // the point with coordinates (0, 0) and the direction of the segment to
    // coincide the positive direction of the x-axis.
    CT segm_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segm_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT sqr_segment_length = segm_vec_x * segm_vec_x + segm_vec_y * segm_vec_y;

    // Compute x-coordinates of the endpoints of the edge
    // in the transformed space.
    CT projection_start = sqr_segment_length *
        get_point_projection((*discretization)[0], segment);
    CT projection_end = sqr_segment_length *
        get_point_projection((*discretization)[1], segment);

    // Compute parabola parameters in the transformed space.
    // Parabola has next representation:
    // f(x) = ((x-rot_x)^2 + rot_y^2) / (2.0*rot_y).
    CT point_vec_x = cast(x(point)) - cast(x(low(segment)));
    CT point_vec_y = cast(y(point)) - cast(y(low(segment)));
    CT rot_x = segm_vec_x * point_vec_x + segm_vec_y * point_vec_y;
    CT rot_y = segm_vec_x * point_vec_y - segm_vec_y * point_vec_x;

    // Save the last point.
    Point<CT> last_point = (*discretization)[1];
    discretization->pop_back();

    // Use stack to avoid recursion.
    std::stack<CT> point_stack;
    point_stack.push(projection_end);
    CT cur_x = projection_start;
    CT cur_y = parabola_y(cur_x, rot_x, rot_y);

    // Adjust max_dist parameter in the transformed space.
    const CT max_dist_transformed = max_dist * max_dist * sqr_segment_length;
    while (!point_stack.empty()) {
      CT new_x = point_stack.top();
      CT new_y = parabola_y(new_x, rot_x, rot_y);

      // Compute coordinates of the point of the parabola that is
      // furthest from the current line segment.
      CT mid_x = (new_y - cur_y) / (new_x - cur_x) * rot_y + rot_x;
      CT mid_y = parabola_y(mid_x, rot_x, rot_y);

      // Compute maximum distance between the given parabolic arc
      // and line segment that discretize it.
      CT dist = (new_y - cur_y) * (mid_x - cur_x) -
          (new_x - cur_x) * (mid_y - cur_y);
      dist = dist * dist / ((new_y - cur_y) * (new_y - cur_y) +
          (new_x - cur_x) * (new_x - cur_x));
      if (dist <= max_dist_transformed) {
        // Distance between parabola and line segment is less than max_dist.
        point_stack.pop();
        CT inter_x = (segm_vec_x * new_x - segm_vec_y * new_y) /
            sqr_segment_length + cast(x(low(segment)));
        CT inter_y = (segm_vec_x * new_y + segm_vec_y * new_x) /
            sqr_segment_length + cast(y(low(segment)));
        discretization->push_back(Point<CT>(inter_x, inter_y));
        cur_x = new_x;
        cur_y = new_y;
      } else {
        point_stack.push(mid_x);
      }
    }

    // Update last point.
    discretization->back() = last_point;
  }

 private:
  // Compute y(x) = ((x - a) * (x - a) + b * b) / (2 * b).
  static CT parabola_y(CT x, CT a, CT b) {
    return ((x - a) * (x - a) + b * b) / (b + b);
  }

  // Get normalized length of the distance between:
  //   1) point projection onto the segment
  //   2) start point of the segment
  // Return this length divided by the segment length. This is made to avoid
  // sqrt computation during transformation from the initial space to the
  // transformed one and vice versa. The assumption is made that projection of
  // the point lies between the start-point and endpoint of the segment.
  template <class InCT,
            template<class> class Point,
            template<class> class Segment>
  static
  typename enable_if<
    typename gtl_and<
      typename gtl_if<
        typename is_point_concept<
          typename geometry_concept< Point<int> >::type
        >::type
      >::type,
      typename gtl_if<
        typename is_segment_concept<
          typename geometry_concept< Segment<long> >::type
        >::type
      >::type
    >::type,
    CT
  >::type get_point_projection(
      const Point<CT>& point, const Segment<InCT>& segment) {
    CT segment_vec_x = cast(x(high(segment))) - cast(x(low(segment)));
    CT segment_vec_y = cast(y(high(segment))) - cast(y(low(segment)));
    CT point_vec_x = x(point) - cast(x(low(segment)));
    CT point_vec_y = y(point) - cast(y(low(segment)));
    CT sqr_segment_length =
        segment_vec_x * segment_vec_x + segment_vec_y * segment_vec_y;
    CT vec_dot = segment_vec_x * point_vec_x + segment_vec_y * point_vec_y;
    return vec_dot / sqr_segment_length;
  }

  template <typename InCT>
  static CT cast(const InCT& value) {
    return static_cast<CT>(value);
  }
};

} } // namespace boost::polygon

// The following code for the visualization of the boost Voronoi diagram is based on:
//
// Boost.Polygon library voronoi_visualizer.cpp file
//          Copyright Andrii Sydorchuk 2010-2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
namespace Voronoi { namespace Internal {

    typedef double coordinate_type;
    typedef boost::polygon::point_data<coordinate_type> point_type;
    typedef boost::polygon::segment_data<coordinate_type> segment_type;
    typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
    typedef boost::polygon::voronoi_diagram<coordinate_type> VD;
    typedef VD::cell_type cell_type;
    typedef VD::cell_type::source_index_type source_index_type;
    typedef VD::cell_type::source_category_type source_category_type;
    typedef VD::edge_type edge_type;
    typedef VD::cell_container_type cell_container_type;
    typedef VD::cell_container_type vertex_container_type;
    typedef VD::edge_container_type edge_container_type;
    typedef VD::const_cell_iterator const_cell_iterator;
    typedef VD::const_vertex_iterator const_vertex_iterator;
    typedef VD::const_edge_iterator const_edge_iterator;

    static const std::size_t EXTERNAL_COLOR = 1;

    inline void color_exterior(const VD::edge_type* edge)
    {
        if (edge->color() == EXTERNAL_COLOR)
            return;
        edge->color(EXTERNAL_COLOR);
        edge->twin()->color(EXTERNAL_COLOR);
        const VD::vertex_type* v = edge->vertex1();
        if (v == NULL || !edge->is_primary())
            return;
        v->color(EXTERNAL_COLOR);
        const VD::edge_type* e = v->incident_edge();
        do {
            color_exterior(e);
            e = e->rot_next();
        } while (e != v->incident_edge());
    }

    inline point_type retrieve_point(const Points &points, const std::vector<segment_type> &segments, const cell_type& cell)
    {
        assert(cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT || cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT ||
               cell.source_category() == boost::polygon::SOURCE_CATEGORY_SINGLE_POINT);
        return cell.source_category() == boost::polygon::SOURCE_CATEGORY_SINGLE_POINT ?
                    Voronoi::Internal::point_type(double(points[cell.source_index()].x()), double(points[cell.source_index()].y())) :
                    (cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ?
                        low(segments[cell.source_index()]) : high(segments[cell.source_index()]);
    }

    inline void clip_infinite_edge(const Points &points, const std::vector<segment_type> &segments, const edge_type& edge, coordinate_type bbox_max_size, std::vector<point_type>* clipped_edge)
    {
        const cell_type& cell1 = *edge.cell();
        const cell_type& cell2 = *edge.twin()->cell();
        point_type origin, direction;
        // Infinite edges could not be created by two segment sites.
        if (! cell1.contains_point() && ! cell2.contains_point()) {
            printf("Error! clip_infinite_edge - infinite edge separates two segment cells\n");
            return;
        }
        if (cell1.contains_point() && cell2.contains_point()) {
            point_type p1 = retrieve_point(points, segments, cell1);
            point_type p2 = retrieve_point(points, segments, cell2);
            origin.x((p1.x() + p2.x()) * 0.5);
            origin.y((p1.y() + p2.y()) * 0.5);
            direction.x(p1.y() - p2.y());
            direction.y(p2.x() - p1.x());
        } else {
            origin = cell1.contains_segment() ? retrieve_point(points, segments, cell2) : retrieve_point(points, segments, cell1);
            segment_type segment = cell1.contains_segment() ? segments[cell1.source_index()] : segments[cell2.source_index()];
            coordinate_type dx = high(segment).x() - low(segment).x();
            coordinate_type dy = high(segment).y() - low(segment).y();
            if ((low(segment) == origin) ^ cell1.contains_point()) {
                direction.x(dy);
                direction.y(-dx);
            } else {
                direction.x(-dy);
                direction.y(dx);
            }
        }
        coordinate_type koef = bbox_max_size / (std::max)(fabs(direction.x()), fabs(direction.y()));
        if (edge.vertex0() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() - direction.x() * koef,
                origin.y() - direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex0()->x(), edge.vertex0()->y()));
        }
        if (edge.vertex1() == NULL) {
            clipped_edge->push_back(point_type(
                origin.x() + direction.x() * koef,
                origin.y() + direction.y() * koef));
        } else {
            clipped_edge->push_back(
                point_type(edge.vertex1()->x(), edge.vertex1()->y()));
        }
    }

    inline void sample_curved_edge(const Points &points, const std::vector<segment_type> &segments, const edge_type& edge, std::vector<point_type> &sampled_edge, coordinate_type max_dist)
    {
        point_type point = edge.cell()->contains_point() ?
            retrieve_point(points, segments, *edge.cell()) :
            retrieve_point(points, segments, *edge.twin()->cell());
        segment_type segment = edge.cell()->contains_point() ?
            segments[edge.twin()->cell()->source_index()] :
            segments[edge.cell()->source_index()];
        ::boost::polygon::voronoi_visual_utils<coordinate_type>::discretize(point, segment, max_dist, &sampled_edge);
    }

} /* namespace Internal */ } // namespace Voronoi

static inline void dump_voronoi_to_svg(
    const char          *path,
    /* const */ VD      &vd,
    const Points        &points,
    const Lines         &lines,
    const double         scale = 0.7) // 0.2?
{
    const std::string   inputSegmentPointColor      = "lightseagreen";
    const coord_t       inputSegmentPointRadius     = coord_t(0.09 * scale / SCALING_FACTOR);
    const std::string   inputSegmentColor           = "lightseagreen";
    const coord_t       inputSegmentLineWidth       = coord_t(0.03 * scale / SCALING_FACTOR);

    const std::string   voronoiPointColor           = "black";
    const coord_t       voronoiPointRadius          = coord_t(0.06 * scale / SCALING_FACTOR);
    const std::string   voronoiLineColorPrimary     = "black";
    const std::string   voronoiLineColorSecondary   = "green";
    const std::string   voronoiArcColor             = "red";
    const coord_t       voronoiLineWidth            = coord_t(0.02 * scale / SCALING_FACTOR);

    const bool          internalEdgesOnly           = false;
    const bool          primaryEdgesOnly            = false;

    BoundingBox bbox;
    bbox.merge(get_extents(points));
    bbox.merge(get_extents(lines));
    bbox.min -= (0.01 * bbox.size().cast<double>()).cast<coord_t>();
    bbox.max += (0.01 * bbox.size().cast<double>()).cast<coord_t>();

    ::Slic3r::SVG svg(path, bbox);

//    bbox.scale(1.2);
    // For clipping of half-lines to some reasonable value.
    // The line will then be clipped by the SVG viewer anyway.
    const double bbox_dim_max = double(std::max(bbox.size().x(), bbox.size().y()));
    // For the discretization of the Voronoi parabolic segments.
    const double discretization_step = 0.05 * bbox_dim_max;

    // Make a copy of the input segments with the double type.
    std::vector<Voronoi::Internal::segment_type> segments;
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++ it)
        segments.push_back(Voronoi::Internal::segment_type(
            Voronoi::Internal::point_type(double(it->a(0)), double(it->a(1))),
            Voronoi::Internal::point_type(double(it->b(0)), double(it->b(1)))));

    // Color exterior edges.
    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it)
        if (!it->is_finite())
            Voronoi::Internal::color_exterior(&(*it));

    // Draw the end points of the input polygon.
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        svg.draw(it->a, inputSegmentPointColor, inputSegmentPointRadius);
        svg.draw(it->b, inputSegmentPointColor, inputSegmentPointRadius);
    }
    // Draw the input polygon.
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it)
        svg.draw(Line(Point(coord_t(it->a(0)), coord_t(it->a(1))), Point(coord_t(it->b(0)), coord_t(it->b(1)))), inputSegmentColor, inputSegmentLineWidth);

#if 1
    // Draw voronoi vertices.
    for (boost::polygon::voronoi_diagram<double>::const_vertex_iterator it = vd.vertices().begin(); it != vd.vertices().end(); ++it)
        if (! internalEdgesOnly || it->color() != Voronoi::Internal::EXTERNAL_COLOR)
            svg.draw(Point(coord_t(it->x()), coord_t(it->y())), voronoiPointColor, voronoiPointRadius);

    for (boost::polygon::voronoi_diagram<double>::const_edge_iterator it = vd.edges().begin(); it != vd.edges().end(); ++it) {
        if (primaryEdgesOnly && !it->is_primary())
            continue;
        if (internalEdgesOnly && (it->color() == Voronoi::Internal::EXTERNAL_COLOR))
            continue;
        std::vector<Voronoi::Internal::point_type> samples;
        std::string color = voronoiLineColorPrimary;
        if (!it->is_finite()) {
            Voronoi::Internal::clip_infinite_edge(points, segments, *it, bbox_dim_max, &samples);
            if (! it->is_primary())
                color = voronoiLineColorSecondary;
        } else {
            // Store both points of the segment into samples. sample_curved_edge will split the initial line
            // until the discretization_step is reached.
            samples.push_back(Voronoi::Internal::point_type(it->vertex0()->x(), it->vertex0()->y()));
            samples.push_back(Voronoi::Internal::point_type(it->vertex1()->x(), it->vertex1()->y()));
            if (it->is_curved()) {
                Voronoi::Internal::sample_curved_edge(points, segments, *it, samples, discretization_step);
                color = voronoiArcColor;
            } else if (! it->is_primary())
                color = voronoiLineColorSecondary;
        }
        for (std::size_t i = 0; i + 1 < samples.size(); ++i)
            svg.draw(Line(Point(coord_t(samples[i].x()), coord_t(samples[i].y())), Point(coord_t(samples[i+1].x()), coord_t(samples[i+1].y()))), color, voronoiLineWidth);
    }
#endif

    svg.Close();
}
#endif

// https://svn.boost.org/trac10/ticket/12067
// This bug seems to be confirmed.
// Vojtech supposes that there may be no Voronoi edges produced for
// the 1st and last sweep line positions.
TEST_CASE("Voronoi missing edges - points 12067", "[Voronoi]")
{
    Points pts {
        { -10, -20 },
        {  10, -20 },
        {   5,   0 },
        {  10,  20 },
        { -10,  20 },
        {  -5,   0 }
    };

    // Construction of the Voronoi Diagram.
    VD vd;
    construct_voronoi(pts.begin(), pts.end(), &vd);

#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-pts.svg").c_str(),
        vd, pts, Lines());
#endif

//    REQUIRE(closest_point.z() == Approx(1.));
}

// https://svn.boost.org/trac10/ticket/12707
// This issue is confirmed, there are no self intersections in the polygon.
// A minimal test case is created at the end of this test,
// a new issue opened with the minimal test case:
// https://github.com/boostorg/polygon/issues/43
TEST_CASE("Voronoi missing edges - Alessandro gapfill 12707", "[Voronoi]")
{
    Lines lines0 {
        { { 42127548,   699996}, { 42127548, 10135750 } },
        { { 42127548, 10135750}, { 50487352, 10135750 } },
        { { 50487352, 10135750}, { 50487352,   699995 } },
        { { 50487352,   699995}, { 51187348,        0 } },
        { { 51187348,        0}, { 64325952,        0 } },
        { { 64325952,        0}, { 64325952,   699996 } },
        { { 64325952,   699996}, { 51187348,   699996 } },
        { { 51187348,   699996}, { 51187348, 10835701 } },
        { { 51187348, 10835701}, { 41427552, 10835701 } },
        { { 41427552, 10835701}, { 41427552,   699996 } },
        { { 41427552,   699996}, { 28664848,   699996 } },
        { { 28664848,   699996}, { 28664848, 10835701 } },
        { { 28664848, 10835701}, { 19280052, 10835701 } },
        { { 19280052, 10835701}, { 27964852,   699996 } },
        { { 27964852,   699996}, { 28664848,        0 } },
        { { 28664848,        0}, { 41427551,        0 } },
        { { 41427551,        0}, { 42127548,   699996 } }
    };

    Lines lines1 {
        { { 42127548,   699996}, { 42127548, 10135750 } },
        { { 42127548, 10135750}, { 50487352, 10135750 } },
        { { 50487352, 10135750}, { 50487352,   699995 } },
        { { 50487352,   699995}, { 51187348,        0 } },
        { { 51187348,        0}, { 51187348, 10835701 } },
        { { 51187348, 10835701}, { 41427552, 10835701 } },
        { { 41427552, 10835701}, { 41427552,   699996 } },
        { { 41427552,   699996}, { 28664848,   699996 } },
        { { 28664848,   699996}, { 28664848, 10835701 } },
        { { 28664848, 10835701}, { 19280052, 10835701 } },
        { { 19280052, 10835701}, { 27964852,   699996 } },
        { { 27964852,   699996}, { 28664848,        0 } },
        { { 28664848,        0}, { 41427551,        0 } },
        { { 41427551,        0}, { 42127548,   699996 } }
    };

    Lines lines2 {
        { { 42127548,   699996}, { 42127548, 10135750 } },
        { { 42127548, 10135750}, { 50487352, 10135750 } },
        { { 50487352, 10135750}, { 50487352,   699995 } },
        { { 50487352,   699995}, { 51187348,        0 } },
        { { 51187348,        0}, { 51187348, 10835701 } },
        { { 51187348, 10835701}, { 41427552, 10835701 } },
        { { 41427552, 10835701}, { 41427552,   699996 } },
        { { 41427552,   699996}, { 28664848,   699996 } },
        { { 28664848,   699996}, { 28664848, 10835701 } },
        { { 28664848, 10835701}, { 19280052, 10835701 } },
        { { 19280052, 10835701}, { 28664848,        0 } },
        { { 28664848,        0}, { 41427551,        0 } },
        { { 41427551,        0}, { 42127548,   699996 } }
    };

    Lines lines3 {
        { { 42127548,   699996}, { 42127548, 10135750 } },
        { { 42127548, 10135750}, { 50487352, 10135750 } },
        { { 50487352, 10135750}, { 50487352,   699995 } },
        { { 50487352,   699995}, { 51187348,        0 } },
        { { 51187348,        0}, { 51187348, 10835701 } },
        { { 51187348, 10835701}, { 41427552, 10835701 } },
        { { 41427552, 10835701}, { 41427552,   699996 } },
        { { 41427552,   699996}, { 41427551,        0 } },
        { { 41427551,        0}, { 42127548,   699996 } }
    };

    Lines lines4 {
        { { 42127548,   699996}, { 42127548, 10135750 } },
        { { 42127548, 10135750}, { 50487352, 10135750 } },
        { { 50487352, 10135750}, { 50487352,   699995 } },
        { { 50487352,   699995}, { 51187348,        0 } },
        { { 51187348,        0}, { 51187348, 10835701 } },
        { { 51187348, 10835701}, { 41427552, 10835701 } },
        { { 41427552, 10835701}, { 41427551,        0 } },
        { { 41427551,        0}, { 42127548,   699996 } }
    };

    Lines lines = to_lines(Polygon {
        {       0, 10000000},
        {  700000,        1}, // it has to be 1, higher number, zero or -1 work.
        {  700000,  9000000},
        { 9100000,  9000000},
        { 9100000,        0},
        {10000000, 10000000}
        });

    Polygon poly;
    std::mt19937 gen;
    std::uniform_int_distribution<size_t> dist(-100, 100);
    for (size_t i = 0; i < lines.size(); ++ i) {
      Line &l1 = lines[i];
      Line &l2 = lines[(i + 1) % lines.size()];
      REQUIRE(l1.b.x() == l2.a.x());
      REQUIRE(l1.b.y() == l2.a.y());
#if 0
      // Wiggle the points a bit to find out whether this fixes the voronoi diagram for this particular polygon.
      l1.b.x() = (l2.a.x() += dist(gen));
      l1.b.y() = (l2.a.y() += dist(gen));
#endif
      poly.points.emplace_back(l1.a);
    }

    REQUIRE(intersecting_edges({ poly }).empty());

    VD vd;
    construct_voronoi(lines.begin(), lines.end(), &vd);

#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-lines.svg").c_str(),
        vd, Points(), lines);
#endif
}

// https://svn.boost.org/trac10/ticket/12903
// division by zero reported, but this issue is most likely a non-issue, as it produces an infinity for the interval of validity
// of the floating point calculation, therefore forcing a recalculation with extended accuracy.
TEST_CASE("Voronoi division by zero 12903", "[Voronoi]")
{
    Points pts { { 1, 1 }, { 3, 1 }, { 1, 3 }, { 3, 3 },
                 { -1, 1 }, { 1, -1 }, { 5, 1 }, { 3, -1 },
                 { -1, 3 }, { 1, 5 }, { 5, 3 }, { 3, 5 } };
    {
        auto pts2 { pts };
        std::sort(pts2.begin(), pts2.end(), [](auto &l, auto &r) { return (l.x() == r.x()) ? l.y() < r.y() : l.x() < r.x(); });
        // No point removed -> no duplicate.
        REQUIRE(std::unique(pts2.begin(), pts2.end()) == pts2.end());
    }

    VD vd;
    construct_voronoi(pts.begin(), pts.end(), &vd);

#ifdef VORONOI_DEBUG_OUT
    // Scale the voronoi vertices and input points, so that the dump_voronoi_to_svg will display them correctly.
    for (auto &pt : vd.vertices()) {
        const_cast<double&>(pt.x()) = scale_(pt.x());
        const_cast<double&>(pt.y()) = scale_(pt.y());
    }
    for (auto &pt : pts)
        pt = Point::new_scale(pt.x(), pt.y());
    dump_voronoi_to_svg(debug_out_path("voronoi-div-by-zero.svg").c_str(), vd, pts, Lines());
#endif
}

// https://svn.boost.org/trac10/ticket/12139
// Funny sample from a dental industry?
// Vojtech confirms this test fails and rightly so, because the input data contain self intersections.
// This test is suppressed.
TEST_CASE("Voronoi NaN coordinates 12139", "[Voronoi][!hide][!mayfail]")
{
    Lines lines = {
        { {  260500,1564400 }, { 261040,1562960 } },
        { {  261040,1562960 }, { 260840,1561780 } },
        { {  260840,1561780 }, { 262620,1561480 } },
        { {  262620,1561480 }, { 263160,1561220 } },
        { {  263160,1561220 }, { 264100,1563259 } },
        { {  264100,1563259 }, { 262380,1566980 } },
        { {  262380,1566980 }, { 260500,1564400 } },
        { {  137520,1851640 }, { 132160,1851100 } },
        { {  132160,1851100 }, { 126460,1848779 } },
        { {  126460,1848779 }, { 123960,1847320 } },
        { {  123960,1847320 }, { 120960,1844559 } },
        { {  120960,1844559 }, { 119640,1843040 } },
        { {  119640,1843040 }, { 118320,1840900 } },
        { {  118320,1840900 }, { 117920,1838120 } },
        { {  117920,1838120 }, { 118219,1833340 } },
        { {  118219,1833340 }, { 116180,1835000 } },
        { {  116180,1835000 }, { 115999,1834820 } },
        { {  115999,1834820 }, { 114240,1836340 } },
        { {  114240,1836340 }, { 112719,1837260 } },
        { {  112719,1837260 }, { 109460,1838239 } },
        { {  109460,1838239 }, { 103639,1837480 } },
        { {  103639,1837480 }, { 99819,1835460 } },
        { {  99819,1835460 }, { 96320,1834260 } },
        { {  96320,1834260 }, { 95339,1834260 } },
        { {  95339,1834260 }, { 93660,1833720 } },
        { {  93660,1833720 }, { 90719,1833300 } },
        { {  90719,1833300 }, { 87860,1831660 } },
        { {  87860,1831660 }, { 84580,1830499 } },
        { {  84580,1830499 }, { 79780,1827419 } },
        { {  79780,1827419 }, { 76020,1824280 } },
        { {  76020,1824280 }, { 73680,1821180 } },
        { {  73680,1821180 }, { 72560,1818960 } },
        { {  72560,1818960 }, { 71699,1817719 } },
        { {  71699,1817719 }, { 70280,1814260 } },
        { {  70280,1814260 }, { 69460,1811060 } },
        { {  69460,1811060 }, { 69659,1807320 } },
        { {  69659,1807320 }, { 69640,1803300 } },
        { {  69640,1803300 }, { 69360,1799780 } },
        { {  69360,1799780 }, { 69320,1796720 } },
        { {  69320,1796720 }, { 69640,1793980 } },
        { {  69640,1793980 }, { 70160,1791780 } },
        { {  70160,1791780 }, { 72460,1784879 } },
        { {  72460,1784879 }, { 74420,1780780 } },
        { {  74420,1780780 }, { 76500,1772899 } },
        { {  76500,1772899 }, { 76760,1769359 } },
        { {  76760,1769359 }, { 76480,1766259 } },
        { {  76480,1766259 }, { 76839,1760360 } },
        { {  76839,1760360 }, { 77539,1756680 } },
        { {  77539,1756680 }, { 80540,1748140 } },
        { {  80540,1748140 }, { 84200,1742619 } },
        { {  84200,1742619 }, { 90900,1735220 } },
        { {  90900,1735220 }, { 94159,1732679 } },
        { {  94159,1732679 }, { 101259,1729559 } },
        { {  101259,1729559 }, { 107299,1727939 } },
        { {  107299,1727939 }, { 110979,1727919 } },
        { {  110979,1727919 }, { 113499,1727240 } },
        { {  113499,1727240 }, { 113619,1727359 } },
        { {  113619,1727359 }, { 114280,1727280 } },
        { {  114280,1727280 }, { 131440,1732560 } },
        { {  131440,1732560 }, { 118140,1727119 } },
        { {  118140,1727119 }, { 117120,1723759 } },
        { {  117120,1723759 }, { 113840,1720660 } },
        { {  113840,1720660 }, { 111399,1716760 } },
        { {  111399,1716760 }, { 109700,1712979 } },
        { {  109700,1712979 }, { 108879,1708400 } },
        { {  108879,1708400 }, { 108060,1696360 } },
        { {  108060,1696360 }, { 110040,1687760 } },
        { {  110040,1687760 }, { 112140,1682480 } },
        { {  112140,1682480 }, { 112540,1681780 } },
        { {  112540,1681780 }, { 115260,1678320 } },
        { {  115260,1678320 }, { 118720,1675320 } },
        { {  118720,1675320 }, { 126100,1670980 } },
        { {  126100,1670980 }, { 132400,1668080 } },
        { {  132400,1668080 }, { 136700,1667440 } },
        { {  136700,1667440 }, { 142440,1667159 } },
        { {  142440,1667159 }, { 143340,1666720 } },
        { {  143340,1666720 }, { 138679,1661319 } },
        { {  138679,1661319 }, { 137240,1657480 } },
        { {  137240,1657480 }, { 136760,1650739 } },
        { {  136760,1650739 }, { 136780,1647339 } },
        { {  136780,1647339 }, { 135940,1644280 } },
        { {  135940,1644280 }, { 136000,1640820 } },
        { {  136000,1640820 }, { 135480,1638020 } },
        { {  135480,1638020 }, { 137060,1634220 } },
        { {  137060,1634220 }, { 136320,1631340 } },
        { {  136320,1631340 }, { 134620,1629700 } },
        { {  134620,1629700 }, { 132460,1628199 } },
        { {  132460,1628199 }, { 132299,1627860 } },
        { {  132299,1627860 }, { 138360,1618020 } },
        { {  138360,1618020 }, { 142440,1611859 } },
        { {  142440,1611859 }, { 143180,1611299 } },
        { {  143180,1611299 }, { 144000,1611259 } },
        { {  144000,1611259 }, { 145960,1612540 } },
        { {  145960,1612540 }, { 146720,1613700 } },
        { {  146720,1613700 }, { 147700,1613539 } },
        { {  147700,1613539 }, { 148520,1614039 } },
        { {  148520,1614039 }, { 149840,1613740 } },
        { {  149840,1613740 }, { 150620,1614079 } },
        { {  150620,1614079 }, { 154760,1612740 } },
        { {  154760,1612740 }, { 159000,1608420 } },
        { {  159000,1608420 }, { 161120,1606780 } },
        { {  161120,1606780 }, { 164060,1605139 } },
        { {  164060,1605139 }, { 168079,1603620 } },
        { {  168079,1603620 }, { 170240,1603400 } },
        { {  170240,1603400 }, { 172400,1603499 } },
        { {  172400,1603499 }, { 194440,1613740 } },
        { {  194440,1613740 }, { 195880,1616460 } },
        { {  195880,1616460 }, { 197060,1618140 } },
        { {  197060,1618140 }, { 198039,1617860 } },
        { {  198039,1617860 }, { 198739,1618900 } },
        { {  198739,1618900 }, { 200259,1619200 } },
        { {  200259,1619200 }, { 201940,1618920 } },
        { {  201940,1618920 }, { 201700,1617139 } },
        { {  201700,1617139 }, { 203860,1618179 } },
        { {  203860,1618179 }, { 203500,1617540 } },
        { {  203500,1617540 }, { 205000,1616579 } },
        { {  205000,1616579 }, { 206780,1615020 } },
        { {  206780,1615020 }, { 210159,1614059 } },
        { {  210159,1614059 }, { 217080,1611080 } },
        { {  217080,1611080 }, { 219200,1611579 } },
        { {  219200,1611579 }, { 223219,1610980 } },
        { {  223219,1610980 }, { 224580,1610540 } },
        { {  224580,1610540 }, { 227460,1611440 } },
        { {  227460,1611440 }, { 229359,1611859 } },
        { {  229359,1611859 }, { 230620,1612580 } },
        { {  230620,1612580 }, { 232340,1614460 } },
        { {  232340,1614460 }, { 232419,1617040 } },
        { {  232419,1617040 }, { 231740,1619480 } },
        { {  231740,1619480 }, { 231880,1624899 } },
        { {  231880,1624899 }, { 231540,1625820 } },
        { {  231540,1625820 }, { 231700,1627079 } },
        { {  231700,1627079 }, { 231320,1628239 } },
        { {  231320,1628239 }, { 231420,1636080 } },
        { {  231420,1636080 }, { 231099,1637200 } },
        { {  231099,1637200 }, { 228660,1643280 } },
        { {  228660,1643280 }, { 227699,1644960 } },
        { {  227699,1644960 }, { 226080,1651140 } },
        { {  226080,1651140 }, { 225259,1653420 } },
        { {  225259,1653420 }, { 225159,1655399 } },
        { {  225159,1655399 }, { 223760,1659260 } },
        { {  223760,1659260 }, { 219860,1666360 } },
        { {  219860,1666360 }, { 219180,1667220 } },
        { {  219180,1667220 }, { 212580,1673680 } },
        { {  212580,1673680 }, { 207880,1676460 } },
        { {  207880,1676460 }, { 205560,1677560 } },
        { {  205560,1677560 }, { 199700,1678920 } },
        { {  199700,1678920 }, { 195280,1679420 } },
        { {  195280,1679420 }, { 193939,1679879 } },
        { {  193939,1679879 }, { 188780,1679440 } },
        { {  188780,1679440 }, { 188100,1679639 } },
        { {  188100,1679639 }, { 186680,1679339 } },
        { {  186680,1679339 }, { 184760,1679619 } },
        { {  184760,1679619 }, { 183520,1681440 } },
        { {  183520,1681440 }, { 183860,1682200 } },
        { {  183860,1682200 }, { 186620,1686120 } },
        { {  186620,1686120 }, { 190380,1688380 } },
        { {  190380,1688380 }, { 192780,1690739 } },
        { {  192780,1690739 }, { 195860,1694839 } },
        { {  195860,1694839 }, { 196620,1696539 } },
        { {  196620,1696539 }, { 197540,1701819 } },
        { {  197540,1701819 }, { 198939,1705699 } },
        { {  198939,1705699 }, { 198979,1711819 } },
        { {  198979,1711819 }, { 198240,1716900 } },
        { {  198240,1716900 }, { 197440,1720139 } },
        { {  197440,1720139 }, { 195340,1724639 } },
        { {  195340,1724639 }, { 194040,1726140 } },
        { {  194040,1726140 }, { 192559,1728239 } },
        { {  192559,1728239 }, { 187780,1732339 } },
        { {  187780,1732339 }, { 182519,1735520 } },
        { {  182519,1735520 }, { 181239,1736140 } },
        { {  181239,1736140 }, { 177340,1737619 } },
        { {  177340,1737619 }, { 175439,1738140 } },
        { {  175439,1738140 }, { 171380,1738880 } },
        { {  171380,1738880 }, { 167860,1739059 } },
        { {  167860,1739059 }, { 166040,1738920 } },
        { {  166040,1738920 }, { 163680,1738539 } },
        { {  163680,1738539 }, { 157660,1736859 } },
        { {  157660,1736859 }, { 154900,1735460 } },
        { {  154900,1735460 }, { 151420,1735159 } },
        { {  151420,1735159 }, { 142100,1736160 } },
        { {  142100,1736160 }, { 140880,1735920 } },
        { {  140880,1735920 }, { 142820,1736859 } },
        { {  142820,1736859 }, { 144080,1737240 } },
        { {  144080,1737240 }, { 144280,1737460 } },
        { {  144280,1737460 }, { 144239,1738120 } },
        { {  144239,1738120 }, { 144980,1739420 } },
        { {  144980,1739420 }, { 146340,1741039 } },
        { {  146340,1741039 }, { 147160,1741720 } },
        { {  147160,1741720 }, { 154260,1745800 } },
        { {  154260,1745800 }, { 156560,1746879 } },
        { {  156560,1746879 }, { 165180,1752679 } },
        { {  165180,1752679 }, { 168240,1755860 } },
        { {  168240,1755860 }, { 170940,1759260 } },
        { {  170940,1759260 }, { 173440,1762079 } },
        { {  173440,1762079 }, { 174540,1764079 } },
        { {  174540,1764079 }, { 176479,1766640 } },
        { {  176479,1766640 }, { 178900,1768960 } },
        { {  178900,1768960 }, { 180819,1772780 } },
        { {  180819,1772780 }, { 181479,1776859 } },
        { {  181479,1776859 }, { 181660,1788499 } },
        { {  181660,1788499 }, { 181460,1791740 } },
        { {  181460,1791740 }, { 181160,1792840 } },
        { {  181160,1792840 }, { 179580,1797180 } },
        { {  179580,1797180 }, { 174620,1808960 } },
        { {  174620,1808960 }, { 174100,1809839 } },
        { {  174100,1809839 }, { 171660,1812419 } },
        { {  171660,1812419 }, { 169639,1813840 } },
        { {  169639,1813840 }, { 168880,1814720 } },
        { {  168880,1814720 }, { 168960,1815980 } },
        { {  168960,1815980 }, { 169979,1819160 } },
        { {  169979,1819160 }, { 170080,1820159 } },
        { {  170080,1820159 }, { 168280,1830540 } },
        { {  168280,1830540 }, { 167580,1832200 } },
        { {  167580,1832200 }, { 165679,1835720 } },
        { {  165679,1835720 }, { 164720,1836819 } },
        { {  164720,1836819 }, { 161840,1841740 } },
        { {  161840,1841740 }, { 159880,1843519 } },
        { {  159880,1843519 }, { 158959,1844120 } },
        { {  158959,1844120 }, { 154960,1847500 } },
        { {  154960,1847500 }, { 152140,1848580 } },
        { {  152140,1848580 }, { 150440,1849520 } },
        { {  150440,1849520 }, { 144940,1850980 } },
        { {  144940,1850980 }, { 138340,1851700 } },
        { {  138340,1851700 }, { 137520,1851640 } },
        { {  606940,1873860 }, { 602860,1872460 } },
        { {  602860,1872460 }, { 600680,1871539 } },
        { {  600680,1871539 }, { 599300,1870640 } },
        { {  599300,1870640 }, { 598120,1869579 } },
        { {  598120,1869579 }, { 594680,1867180 } },
        { {  594680,1867180 }, { 589680,1861460 } },
        { {  589680,1861460 }, { 586300,1855020 } },
        { {  586300,1855020 }, { 584700,1848060 } },
        { {  584700,1848060 }, { 585199,1843499 } },
        { {  585199,1843499 }, { 584000,1842079 } },
        { {  584000,1842079 }, { 582900,1841480 } },
        { {  582900,1841480 }, { 581020,1839899 } },
        { {  581020,1839899 }, { 579440,1838040 } },
        { {  579440,1838040 }, { 577840,1834299 } },
        { {  577840,1834299 }, { 576160,1831859 } },
        { {  576160,1831859 }, { 574540,1828499 } },
        { {  574540,1828499 }, { 572140,1822860 } },
        { {  572140,1822860 }, { 570180,1815219 } },
        { {  570180,1815219 }, { 570080,1812280 } },
        { {  570080,1812280 }, { 570340,1808300 } },
        { {  570340,1808300 }, { 570160,1807119 } },
        { {  570160,1807119 }, { 570140,1804039 } },
        { {  570140,1804039 }, { 571640,1796660 } },
        { {  571640,1796660 }, { 571740,1794680 } },
        { {  571740,1794680 }, { 572279,1794039 } },
        { {  572279,1794039 }, { 575480,1788300 } },
        { {  575480,1788300 }, { 576379,1787419 } },
        { {  576379,1787419 }, { 577020,1786120 } },
        { {  577020,1786120 }, { 578000,1785100 } },
        { {  578000,1785100 }, { 579960,1783720 } },
        { {  579960,1783720 }, { 581420,1782079 } },
        { {  581420,1782079 }, { 585480,1778440 } },
        { {  585480,1778440 }, { 586680,1777079 } },
        { {  586680,1777079 }, { 590520,1774639 } },
        { {  590520,1774639 }, { 592440,1773199 } },
        { {  592440,1773199 }, { 595160,1772260 } },
        { {  595160,1772260 }, { 598079,1770920 } },
        { {  598079,1770920 }, { 601420,1769019 } },
        { {  601420,1769019 }, { 606400,1767280 } },
        { {  606400,1767280 }, { 607320,1766620 } },
        { {  607320,1766620 }, { 605760,1766460 } },
        { {  605760,1766460 }, { 604420,1766780 } },
        { {  604420,1766780 }, { 601660,1766579 } },
        { {  601660,1766579 }, { 597160,1766980 } },
        { {  597160,1766980 }, { 591420,1766720 } },
        { {  591420,1766720 }, { 585360,1765460 } },
        { {  585360,1765460 }, { 578540,1763680 } },
        { {  578540,1763680 }, { 574020,1761599 } },
        { {  574020,1761599 }, { 572520,1760560 } },
        { {  572520,1760560 }, { 570959,1759000 } },
        { {  570959,1759000 }, { 566580,1755620 } },
        { {  566580,1755620 }, { 563820,1752000 } },
        { {  563820,1752000 }, { 563140,1751380 } },
        { {  563140,1751380 }, { 560800,1747899 } },
        { {  560800,1747899 }, { 558640,1742280 } },
        { {  558640,1742280 }, { 557860,1741620 } },
        { {  557860,1741620 }, { 555820,1739099 } },
        { {  555820,1739099 }, { 553920,1737540 } },
        { {  553920,1737540 }, { 551900,1735179 } },
        { {  551900,1735179 }, { 551180,1733880 } },
        { {  551180,1733880 }, { 549540,1729559 } },
        { {  549540,1729559 }, { 548860,1720720 } },
        { {  548860,1720720 }, { 549080,1719099 } },
        { {  549080,1719099 }, { 548200,1714700 } },
        { {  548200,1714700 }, { 547560,1713860 } },
        { {  547560,1713860 }, { 544500,1711259 } },
        { {  544500,1711259 }, { 543939,1709780 } },
        { {  543939,1709780 }, { 544520,1705439 } },
        { {  544520,1705439 }, { 543520,1701519 } },
        { {  543520,1701519 }, { 543920,1699319 } },
        { {  543920,1699319 }, { 546360,1697440 } },
        { {  546360,1697440 }, { 546680,1695419 } },
        { {  546680,1695419 }, { 545600,1694180 } },
        { {  545600,1694180 }, { 543220,1692000 } },
        { {  543220,1692000 }, { 538260,1685139 } },
        { {  538260,1685139 }, { 537540,1683000 } },
        { {  537540,1683000 }, { 537020,1682220 } },
        { {  537020,1682220 }, { 535560,1675940 } },
        { {  535560,1675940 }, { 535940,1671220 } },
        { {  535940,1671220 }, { 536320,1669379 } },
        { {  536320,1669379 }, { 535420,1666400 } },
        { {  535420,1666400 }, { 533540,1664460 } },
        { {  533540,1664460 }, { 530720,1662860 } },
        { {  530720,1662860 }, { 529240,1662260 } },
        { {  529240,1662260 }, { 528780,1659160 } },
        { {  528780,1659160 }, { 528820,1653560 } },
        { {  528820,1653560 }, { 529779,1650900 } },
        { {  529779,1650900 }, { 536760,1640840 } },
        { {  536760,1640840 }, { 540360,1636120 } },
        { {  540360,1636120 }, { 541160,1635380 } },
        { {  541160,1635380 }, { 544719,1629480 } },
        { {  544719,1629480 }, { 545319,1626140 } },
        { {  545319,1626140 }, { 543560,1623740 } },
        { {  543560,1623740 }, { 539880,1620739 } },
        { {  539880,1620739 }, { 533400,1617300 } },
        { {  533400,1617300 }, { 527840,1613020 } },
        { {  527840,1613020 }, { 525200,1611579 } },
        { {  525200,1611579 }, { 524360,1610800 } },
        { {  524360,1610800 }, { 517320,1605739 } },
        { {  517320,1605739 }, { 516240,1604240 } },
        { {  516240,1604240 }, { 515220,1602000 } },
        { {  515220,1602000 }, { 514079,1594240 } },
        { {  514079,1594240 }, { 513740,1581460 } },
        { {  513740,1581460 }, { 514660,1577359 } },
        { {  514660,1577359 }, { 514660,1576380 } },
        { {  514660,1576380 }, { 514199,1575380 } },
        { {  514199,1575380 }, { 514680,1572860 } },
        { {  514680,1572860 }, { 513440,1573940 } },
        { {  513440,1573940 }, { 512399,1575580 } },
        { {  512399,1575580 }, { 511620,1576220 } },
        { {  511620,1576220 }, { 507840,1581880 } },
        { {  507840,1581880 }, { 504600,1584579 } },
        { {  504600,1584579 }, { 502440,1584599 } },
        { {  502440,1584599 }, { 499060,1584059 } },
        { {  499060,1584059 }, { 498019,1581960 } },
        { {  498019,1581960 }, { 497819,1581240 } },
        { {  497819,1581240 }, { 498019,1576039 } },
        { {  498019,1576039 }, { 497539,1574740 } },
        { {  497539,1574740 }, { 495459,1574460 } },
        { {  495459,1574460 }, { 492320,1575600 } },
        { {  492320,1575600 }, { 491040,1576360 } },
        { {  491040,1576360 }, { 490080,1575640 } },
        { {  490080,1575640 }, { 490020,1575040 } },
        { {  490020,1575040 }, { 490220,1574400 } },
        { {  490220,1574400 }, { 490819,1573440 } },
        { {  490819,1573440 }, { 492680,1568259 } },
        { {  492680,1568259 }, { 492920,1566799 } },
        { {  492920,1566799 }, { 495760,1563660 } },
        { {  495760,1563660 }, { 496100,1562139 } },
        { {  496100,1562139 }, { 497879,1560240 } },
        { {  497879,1560240 }, { 497059,1558020 } },
        { {  497059,1558020 }, { 495620,1557399 } },
        { {  495620,1557399 }, { 494800,1556839 } },
        { {  494800,1556839 }, { 493500,1555479 } },
        { {  493500,1555479 }, { 491860,1554100 } },
        { {  491860,1554100 }, { 487840,1552139 } },
        { {  487840,1552139 }, { 485900,1551720 } },
        { {  485900,1551720 }, { 483639,1555439 } },
        { {  483639,1555439 }, { 482080,1556480 } },
        { {  482080,1556480 }, { 480200,1556259 } },
        { {  480200,1556259 }, { 478519,1556259 } },
        { {  478519,1556259 }, { 474020,1554019 } },
        { {  474020,1554019 }, { 472660,1551539 } },
        { {  472660,1551539 }, { 471260,1549899 } },
        { {  471260,1549899 }, { 470459,1548020 } },
        { {  470459,1548020 }, { 469920,1545479 } },
        { {  469920,1545479 }, { 469079,1542939 } },
        { {  469079,1542939 }, { 469120,1541799 } },
        { {  469120,1541799 }, { 465840,1537139 } },
        { {  465840,1537139 }, { 463360,1539059 } },
        { {  463360,1539059 }, { 459680,1546900 } },
        { {  459680,1546900 }, { 458439,1547160 } },
        { {  458439,1547160 }, { 456480,1549319 } },
        { {  456480,1549319 }, { 454160,1551400 } },
        { {  454160,1551400 }, { 452819,1550820 } },
        { {  452819,1550820 }, { 451699,1549839 } },
        { {  451699,1549839 }, { 449620,1548440 } },
        { {  449620,1548440 }, { 449419,1548080 } },
        { {  449419,1548080 }, { 447879,1547720 } },
        { {  447879,1547720 }, { 446540,1546819 } },
        { {  446540,1546819 }, { 445720,1545640 } },
        { {  445720,1545640 }, { 444800,1545100 } },
        { {  444800,1545100 }, { 443500,1542899 } },
        { {  443500,1542899 }, { 443320,1541799 } },
        { {  443320,1541799 }, { 443519,1540220 } },
        { {  443519,1540220 }, { 445060,1537099 } },
        { {  445060,1537099 }, { 445840,1533040 } },
        { {  445840,1533040 }, { 442720,1529079 } },
        { {  442720,1529079 }, { 442479,1528360 } },
        { {  442479,1528360 }, { 436820,1529240 } },
        { {  436820,1529240 }, { 436279,1529200 } },
        { {  436279,1529200 }, { 433280,1529859 } },
        { {  433280,1529859 }, { 420220,1529899 } },
        { {  420220,1529899 }, { 414740,1528539 } },
        { {  414740,1528539 }, { 411340,1527960 } },
        { {  411340,1527960 }, { 406860,1524660 } },
        { {  406860,1524660 }, { 405379,1523080 } },
        { {  405379,1523080 }, { 403639,1520320 } },
        { {  403639,1520320 }, { 402040,1517220 } },
        { {  402040,1517220 }, { 400519,1517059 } },
        { {  400519,1517059 }, { 399180,1516720 } },
        { {  399180,1516720 }, { 395300,1515179 } },
        { {  395300,1515179 }, { 394780,1515080 } },
        { {  394780,1515080 }, { 394759,1515900 } },
        { {  394759,1515900 }, { 394339,1516579 } },
        { {  394339,1516579 }, { 393200,1516640 } },
        { {  393200,1516640 }, { 392599,1521799 } },
        { {  392599,1521799 }, { 391699,1525200 } },
        { {  391699,1525200 }, { 391040,1525600 } },
        { {  391040,1525600 }, { 390540,1526500 } },
        { {  390540,1526500 }, { 388999,1527939 } },
        { {  388999,1527939 }, { 387059,1531100 } },
        { {  387059,1531100 }, { 386540,1531440 } },
        { {  386540,1531440 }, { 382140,1531839 } },
        { {  382140,1531839 }, { 377360,1532619 } },
        { {  377360,1532619 }, { 375640,1532220 } },
        { {  375640,1532220 }, { 372580,1531019 } },
        { {  372580,1531019 }, { 371079,1529019 } },
        { {  371079,1529019 }, { 367280,1526039 } },
        { {  367280,1526039 }, { 366460,1521900 } },
        { {  366460,1521900 }, { 364320,1516400 } },
        { {  364320,1516400 }, { 363779,1515780 } },
        { {  363779,1515780 }, { 362220,1515320 } },
        { {  362220,1515320 }, { 361979,1515060 } },
        { {  361979,1515060 }, { 360820,1515739 } },
        { {  360820,1515739 }, { 353360,1518620 } },
        { {  353360,1518620 }, { 347840,1520080 } },
        { {  347840,1520080 }, { 342399,1521140 } },
        { {  342399,1521140 }, { 334899,1523380 } },
        { {  334899,1523380 }, { 333220,1523400 } },
        { {  333220,1523400 }, { 332599,1522919 } },
        { {  332599,1522919 }, { 329780,1521640 } },
        { {  329780,1521640 }, { 325360,1521220 } },
        { {  325360,1521220 }, { 319000,1520999 } },
        { {  319000,1520999 }, { 316180,1520240 } },
        { {  316180,1520240 }, { 312700,1518960 } },
        { {  312700,1518960 }, { 310520,1517679 } },
        { {  310520,1517679 }, { 309280,1517260 } },
        { {  309280,1517260 }, { 306440,1515040 } },
        { {  306440,1515040 }, { 304140,1512780 } },
        { {  304140,1512780 }, { 301640,1509720 } },
        { {  301640,1509720 }, { 301500,1509879 } },
        { {  301500,1509879 }, { 300320,1509059 } },
        { {  300320,1509059 }, { 299140,1507339 } },
        { {  299140,1507339 }, { 297340,1502659 } },
        { {  297340,1502659 }, { 298960,1508280 } },
        { {  298960,1508280 }, { 299120,1509299 } },
        { {  299120,1509299 }, { 298720,1510100 } },
        { {  298720,1510100 }, { 298420,1512240 } },
        { {  298420,1512240 }, { 297420,1514540 } },
        { {  297420,1514540 }, { 296900,1515340 } },
        { {  296900,1515340 }, { 294780,1517500 } },
        { {  294780,1517500 }, { 293040,1518380 } },
        { {  293040,1518380 }, { 289140,1521360 } },
        { {  289140,1521360 }, { 283600,1523300 } },
        { {  283600,1523300 }, { 280140,1525220 } },
        { {  280140,1525220 }, { 279620,1525679 } },
        { {  279620,1525679 }, { 274960,1527379 } },
        { {  274960,1527379 }, { 273440,1528819 } },
        { {  273440,1528819 }, { 269840,1532840 } },
        { {  269840,1532840 }, { 264800,1536240 } },
        { {  264800,1536240 }, { 261199,1540419 } },
        { {  261199,1540419 }, { 257359,1541400 } },
        { {  257359,1541400 }, { 250460,1539299 } },
        { {  250460,1539299 }, { 250240,1539400 } },
        { {  250240,1539400 }, { 249840,1540460 } },
        { {  249840,1540460 }, { 249779,1541140 } },
        { {  249779,1541140 }, { 248482,1539783 } },
        { {  248482,1539783 }, { 251320,1544120 } },
        { {  251320,1544120 }, { 252500,1548320 } },
        { {  252500,1548320 }, { 252519,1549740 } },
        { {  252519,1549740 }, { 253000,1553140 } },
        { {  253000,1553140 }, { 252920,1556539 } },
        { {  252920,1556539 }, { 253160,1556700 } },
        { {  253160,1556700 }, { 254019,1558220 } },
        { {  254019,1558220 }, { 253039,1559339 } },
        { {  253039,1559339 }, { 252300,1561920 } },
        { {  252300,1561920 }, { 251080,1565260 } },
        { {  251080,1565260 }, { 251120,1566160 } },
        { {  251120,1566160 }, { 249979,1570240 } },
        { {  249979,1570240 }, { 248799,1575380 } },
        { {  248799,1575380 }, { 247180,1579520 } },
        { {  247180,1579520 }, { 243380,1588440 } },
        { {  243380,1588440 }, { 241700,1591780 } },
        { {  241700,1591780 }, { 240280,1593080 } },
        { {  240280,1593080 }, { 231859,1598380 } },
        { {  231859,1598380 }, { 228840,1600060 } },
        { {  228840,1600060 }, { 226420,1601080 } },
        { {  226420,1601080 }, { 223620,1601940 } },
        { {  223620,1601940 }, { 220919,1603819 } },
        { {  220919,1603819 }, { 219599,1604420 } },
        { {  219599,1604420 }, { 218380,1605200 } },
        { {  218380,1605200 }, { 213219,1607260 } },
        { {  213219,1607260 }, { 210040,1607740 } },
        { {  210040,1607740 }, { 186439,1596440 } },
        { {  186439,1596440 }, { 185159,1594559 } },
        { {  185159,1594559 }, { 182239,1588300 } },
        { {  182239,1588300 }, { 181040,1585380 } },
        { {  181040,1585380 }, { 180380,1578580 } },
        { {  180380,1578580 }, { 180679,1573220 } },
        { {  180679,1573220 }, { 181220,1568539 } },
        { {  181220,1568539 }, { 181859,1565020 } },
        { {  181859,1565020 }, { 184499,1555500 } },
        { {  184499,1555500 }, { 183480,1558160 } },
        { {  183480,1558160 }, { 182600,1561700 } },
        { {  182600,1561700 }, { 171700,1554359 } },
        { {  171700,1554359 }, { 176880,1545920 } },
        { {  176880,1545920 }, { 189940,1529000 } },
        { {  189940,1529000 }, { 200040,1535759 } },
        { {  200040,1535759 }, { 207559,1531660 } },
        { {  207559,1531660 }, { 218039,1527520 } },
        { {  218039,1527520 }, { 222360,1526640 } },
        { {  222360,1526640 }, { 225439,1526440 } },
        { {  225439,1526440 }, { 231160,1527079 } },
        { {  231160,1527079 }, { 232300,1527399 } },
        { {  232300,1527399 }, { 236579,1529140 } },
        { {  236579,1529140 }, { 238139,1529120 } },
        { {  238139,1529120 }, { 238799,1529319 } },
        { {  238799,1529319 }, { 240999,1531780 } },
        { {  240999,1531780 }, { 238280,1528799 } },
        { {  238280,1528799 }, { 236900,1523840 } },
        { {  236900,1523840 }, { 236800,1522700 } },
        { {  236800,1522700 }, { 235919,1518880 } },
        { {  235919,1518880 }, { 236080,1514299 } },
        { {  236080,1514299 }, { 238260,1508380 } },
        { {  238260,1508380 }, { 240119,1505159 } },
        { {  240119,1505159 }, { 233319,1496360 } },
        { {  233319,1496360 }, { 239140,1490759 } },
        { {  239140,1490759 }, { 258760,1478080 } },
        { {  258760,1478080 }, { 263940,1484760 } },
        { {  263940,1484760 }, { 263460,1485159 } },
        { {  263460,1485159 }, { 265960,1483519 } },
        { {  265960,1483519 }, { 270380,1482020 } },
        { {  270380,1482020 }, { 272880,1481420 } },
        { {  272880,1481420 }, { 275700,1481400 } },
        { {  275700,1481400 }, { 278380,1481740 } },
        { {  278380,1481740 }, { 281220,1482979 } },
        { {  281220,1482979 }, { 284680,1484859 } },
        { {  284680,1484859 }, { 285979,1486140 } },
        { {  285979,1486140 }, { 290220,1489100 } },
        { {  290220,1489100 }, { 292680,1489520 } },
        { {  292680,1489520 }, { 293280,1490240 } },
        { {  293280,1490240 }, { 293140,1489160 } },
        { {  293140,1489160 }, { 293280,1488580 } },
        { {  293280,1488580 }, { 294100,1486980 } },
        { {  294100,1486980 }, { 294580,1484960 } },
        { {  294580,1484960 }, { 295680,1481660 } },
        { {  295680,1481660 }, { 297840,1477339 } },
        { {  297840,1477339 }, { 302240,1472280 } },
        { {  302240,1472280 }, { 307120,1469000 } },
        { {  307120,1469000 }, { 314500,1466340 } },
        { {  314500,1466340 }, { 324979,1464740 } },
        { {  324979,1464740 }, { 338999,1462059 } },
        { {  338999,1462059 }, { 345599,1461579 } },
        { {  345599,1461579 }, { 349020,1461620 } },
        { {  349020,1461620 }, { 353420,1461160 } },
        { {  353420,1461160 }, { 357000,1461500 } },
        { {  357000,1461500 }, { 359860,1461579 } },
        { {  359860,1461579 }, { 364520,1462740 } },
        { {  364520,1462740 }, { 367280,1464000 } },
        { {  367280,1464000 }, { 372020,1467560 } },
        { {  372020,1467560 }, { 373999,1469980 } },
        { {  373999,1469980 }, { 375580,1472240 } },
        { {  375580,1472240 }, { 376680,1474460 } },
        { {  376680,1474460 }, { 377259,1478620 } },
        { {  377259,1478620 }, { 379279,1480880 } },
        { {  379279,1480880 }, { 379260,1481600 } },
        { {  379260,1481600 }, { 378760,1482000 } },
        { {  378760,1482000 }, { 379300,1482040 } },
        { {  379300,1482040 }, { 380220,1482460 } },
        { {  380220,1482460 }, { 380840,1483020 } },
        { {  380840,1483020 }, { 385519,1482600 } },
        { {  385519,1482600 }, { 386019,1482320 } },
        { {  386019,1482320 }, { 386499,1481600 } },
        { {  386499,1481600 }, { 386540,1480139 } },
        { {  386540,1480139 }, { 387500,1478220 } },
        { {  387500,1478220 }, { 388280,1476100 } },
        { {  388280,1476100 }, { 390060,1473000 } },
        { {  390060,1473000 }, { 393659,1469460 } },
        { {  393659,1469460 }, { 396540,1467860 } },
        { {  396540,1467860 }, { 401260,1466040 } },
        { {  401260,1466040 }, { 406200,1465100 } },
        { {  406200,1465100 }, { 410920,1465439 } },
        { {  410920,1465439 }, { 420659,1467399 } },
        { {  420659,1467399 }, { 433500,1471480 } },
        { {  433500,1471480 }, { 441340,1473540 } },
        { {  441340,1473540 }, { 448620,1475139 } },
        { {  448620,1475139 }, { 450720,1475880 } },
        { {  450720,1475880 }, { 453299,1477059 } },
        { {  453299,1477059 }, { 456620,1478940 } },
        { {  456620,1478940 }, { 458480,1480399 } },
        { {  458480,1480399 }, { 461100,1482780 } },
        { {  461100,1482780 }, { 463820,1486519 } },
        { {  463820,1486519 }, { 464780,1488199 } },
        { {  464780,1488199 }, { 466579,1493960 } },
        { {  466579,1493960 }, { 467120,1497700 } },
        { {  467120,1497700 }, { 466999,1500280 } },
        { {  466999,1500280 }, { 467300,1502580 } },
        { {  467300,1502580 }, { 467399,1505280 } },
        { {  467399,1505280 }, { 466979,1506920 } },
        { {  466979,1506920 }, { 467920,1504780 } },
        { {  467920,1504780 }, { 468159,1505040 } },
        { {  468159,1505040 }, { 469400,1504859 } },
        { {  469400,1504859 }, { 470300,1505540 } },
        { {  470300,1505540 }, { 471240,1505200 } },
        { {  471240,1505200 }, { 471579,1504280 } },
        { {  471579,1504280 }, { 473939,1502379 } },
        { {  473939,1502379 }, { 476860,1500200 } },
        { {  476860,1500200 }, { 479800,1498620 } },
        { {  479800,1498620 }, { 480840,1498120 } },
        { {  480840,1498120 }, { 485220,1497480 } },
        { {  485220,1497480 }, { 489979,1497460 } },
        { {  489979,1497460 }, { 494899,1498700 } },
        { {  494899,1498700 }, { 500099,1501320 } },
        { {  500099,1501320 }, { 501439,1501839 } },
        { {  501439,1501839 }, { 503400,1502939 } },
        { {  503400,1502939 }, { 510760,1508340 } },
        { {  510760,1508340 }, { 513640,1510920 } },
        { {  513640,1510920 }, { 518579,1514599 } },
        { {  518579,1514599 }, { 519020,1515260 } },
        { {  519020,1515260 }, { 520700,1516480 } },
        { {  520700,1516480 }, { 524960,1521480 } },
        { {  524960,1521480 }, { 526820,1524820 } },
        { {  526820,1524820 }, { 528280,1527820 } },
        { {  528280,1527820 }, { 529120,1533120 } },
        { {  529120,1533120 }, { 528820,1537139 } },
        { {  528820,1537139 }, { 527020,1543920 } },
        { {  527020,1543920 }, { 526959,1546780 } },
        { {  526959,1546780 }, { 526420,1548060 } },
        { {  526420,1548060 }, { 527020,1547919 } },
        { {  527020,1547919 }, { 527620,1548160 } },
        { {  527620,1548160 }, { 528980,1548020 } },
        { {  528980,1548020 }, { 535180,1544980 } },
        { {  535180,1544980 }, { 540860,1542979 } },
        { {  540860,1542979 }, { 546480,1542720 } },
        { {  546480,1542720 }, { 547420,1542860 } },
        { {  547420,1542860 }, { 551800,1544140 } },
        { {  551800,1544140 }, { 558740,1547939 } },
        { {  558740,1547939 }, { 569920,1556259 } },
        { {  569920,1556259 }, { 573660,1560220 } },
        { {  573660,1560220 }, { 573040,1559500 } },
        { {  573040,1559500 }, { 574740,1559220 } },
        { {  574740,1559220 }, { 588480,1562899 } },
        { {  588480,1562899 }, { 585180,1576019 } },
        { {  585180,1576019 }, { 583440,1577979 } },
        { {  583440,1577979 }, { 584280,1582399 } },
        { {  584280,1582399 }, { 584520,1588960 } },
        { {  584520,1588960 }, { 583420,1601620 } },
        { {  583420,1601620 }, { 582840,1603880 } },
        { {  582840,1603880 }, { 579860,1611400 } },
        { {  579860,1611400 }, { 577980,1614579 } },
        { {  577980,1614579 }, { 577380,1616080 } },
        { {  577380,1616080 }, { 563800,1621579 } },
        { {  563800,1621579 }, { 561320,1622320 } },
        { {  561320,1622320 }, { 565080,1621960 } },
        { {  565080,1621960 }, { 571680,1620780 } },
        { {  571680,1620780 }, { 583260,1628340 } },
        { {  583260,1628340 }, { 583100,1630399 } },
        { {  583100,1630399 }, { 582200,1632160 } },
        { {  582200,1632160 }, { 595380,1627020 } },
        { {  595380,1627020 }, { 597400,1627320 } },
        { {  597400,1627320 }, { 602240,1628459 } },
        { {  602240,1628459 }, { 605660,1630260 } },
        { {  605660,1630260 }, { 610319,1634140 } },
        { {  610319,1634140 }, { 612340,1636319 } },
        { {  612340,1636319 }, { 614820,1638020 } },
        { {  614820,1638020 }, { 616460,1638740 } },
        { {  616460,1638740 }, { 620420,1639500 } },
        { {  620420,1639500 }, { 623000,1639280 } },
        { {  623000,1639280 }, { 624459,1639359 } },
        { {  624459,1639359 }, { 626180,1640159 } },
        { {  626180,1640159 }, { 627279,1640940 } },
        { {  627279,1640940 }, { 629980,1643759 } },
        { {  629980,1643759 }, { 632380,1648000 } },
        { {  632380,1648000 }, { 635020,1654800 } },
        { {  635020,1654800 }, { 636320,1659140 } },
        { {  636320,1659140 }, { 636680,1663620 } },
        { {  636680,1663620 }, { 636180,1665780 } },
        { {  636180,1665780 }, { 630620,1669720 } },
        { {  630620,1669720 }, { 628760,1672979 } },
        { {  628760,1672979 }, { 627540,1676859 } },
        { {  627540,1676859 }, { 627040,1680699 } },
        { {  627040,1680699 }, { 624700,1686500 } },
        { {  624700,1686500 }, { 623260,1688799 } },
        { {  623260,1688799 }, { 619620,1693799 } },
        { {  619620,1693799 }, { 621720,1694859 } },
        { {  621720,1694859 }, { 624940,1694379 } },
        { {  624940,1694379 }, { 627120,1695600 } },
        { {  627120,1695600 }, { 627740,1696120 } },
        { {  627740,1696120 }, { 631120,1697460 } },
        { {  631120,1697460 }, { 633980,1698340 } },
        { {  633980,1698340 }, { 638380,1700460 } },
        { {  638380,1700460 }, { 642660,1703300 } },
        { {  642660,1703300 }, { 643620,1704140 } },
        { {  643620,1704140 }, { 646300,1707000 } },
        { {  646300,1707000 }, { 649060,1710880 } },
        { {  649060,1710880 }, { 651160,1714879 } },
        { {  651160,1714879 }, { 651740,1716559 } },
        { {  651740,1716559 }, { 653139,1722619 } },
        { {  653139,1722619 }, { 653020,1728320 } },
        { {  653020,1728320 }, { 652719,1731420 } },
        { {  652719,1731420 }, { 651619,1736360 } },
        { {  651619,1736360 }, { 649819,1743160 } },
        { {  649819,1743160 }, { 646440,1749059 } },
        { {  646440,1749059 }, { 645219,1750399 } },
        { {  645219,1750399 }, { 643959,1752679 } },
        { {  643959,1752679 }, { 643959,1753740 } },
        { {  643959,1753740 }, { 642140,1754240 } },
        { {  642140,1754240 }, { 643760,1754099 } },
        { {  643760,1754099 }, { 644320,1754280 } },
        { {  644320,1754280 }, { 645000,1754879 } },
        { {  645000,1754879 }, { 646940,1755620 } },
        { {  646940,1755620 }, { 654779,1757820 } },
        { {  654779,1757820 }, { 661100,1761559 } },
        { {  661100,1761559 }, { 664099,1763980 } },
        { {  664099,1763980 }, { 668220,1768480 } },
        { {  668220,1768480 }, { 671920,1773640 } },
        { {  671920,1773640 }, { 674939,1779540 } },
        { {  674939,1779540 }, { 677760,1782440 } },
        { {  677760,1782440 }, { 679080,1785739 } },
        { {  679080,1785739 }, { 678780,1788100 } },
        { {  678780,1788100 }, { 678020,1791500 } },
        { {  678020,1791500 }, { 677120,1793600 } },
        { {  677120,1793600 }, { 676860,1795800 } },
        { {  676860,1795800 }, { 676440,1797320 } },
        { {  676440,1797320 }, { 676459,1798519 } },
        { {  676459,1798519 }, { 675620,1800159 } },
        { {  675620,1800159 }, { 675520,1801019 } },
        { {  675520,1801019 }, { 673360,1804899 } },
        { {  673360,1804899 }, { 672740,1807079 } },
        { {  672740,1807079 }, { 673300,1809260 } },
        { {  673300,1809260 }, { 674539,1811019 } },
        { {  674539,1811019 }, { 675499,1812020 } },
        { {  675499,1812020 }, { 677660,1817240 } },
        { {  677660,1817240 }, { 679659,1824280 } },
        { {  679659,1824280 }, { 680380,1828779 } },
        { {  680380,1828779 }, { 679519,1837999 } },
        { {  679519,1837999 }, { 677940,1844379 } },
        { {  677940,1844379 }, { 676940,1846900 } },
        { {  676940,1846900 }, { 675479,1849379 } },
        { {  675479,1849379 }, { 674000,1851200 } },
        { {  674000,1851200 }, { 671380,1853480 } },
        { {  671380,1853480 }, { 667019,1855240 } },
        { {  667019,1855240 }, { 662540,1856060 } },
        { {  662540,1856060 }, { 660960,1856599 } },
        { {  660960,1856599 }, { 656240,1857020 } },
        { {  656240,1857020 }, { 655600,1856960 } },
        { {  655600,1856960 }, { 652839,1855880 } },
        { {  652839,1855880 }, { 652019,1855840 } },
        { {  652019,1855840 }, { 651459,1855060 } },
        { {  651459,1855060 }, { 652179,1854359 } },
        { {  652179,1854359 }, { 652019,1849919 } },
        { {  652019,1849919 }, { 650620,1846920 } },
        { {  650620,1846920 }, { 647299,1844540 } },
        { {  647299,1844540 }, { 644500,1843819 } },
        { {  644500,1843819 }, { 641860,1844859 } },
        { {  641860,1844859 }, { 641059,1845340 } },
        { {  641059,1845340 }, { 638860,1845820 } },
        { {  638860,1845820 }, { 638000,1845820 } },
        { {  638000,1845820 }, { 636340,1845479 } },
        { {  636340,1845479 }, { 634980,1844800 } },
        { {  634980,1844800 }, { 632660,1842979 } },
        { {  632660,1842979 }, { 631140,1841120 } },
        { {  631140,1841120 }, { 629140,1839520 } },
        { {  629140,1839520 }, { 626640,1839540 } },
        { {  626640,1839540 }, { 624159,1840739 } },
        { {  624159,1840739 }, { 623820,1841380 } },
        { {  623820,1841380 }, { 622440,1842719 } },
        { {  622440,1842719 }, { 622100,1843680 } },
        { {  622100,1843680 }, { 623780,1846100 } },
        { {  623780,1846100 }, { 624580,1846920 } },
        { {  624580,1846920 }, { 626120,1856720 } },
        { {  626120,1856720 }, { 627440,1860000 } },
        { {  627440,1860000 }, { 628000,1864299 } },
        { {  628000,1864299 }, { 627380,1865999 } },
        { {  627380,1865999 }, { 626260,1867580 } },
        { {  626260,1867580 }, { 623660,1869520 } },
        { {  623660,1869520 }, { 618680,1872780 } },
        { {  618680,1872780 }, { 617699,1873140 } },
        { {  617699,1873140 }, { 612000,1874160 } },
        { {  612000,1874160 }, { 609840,1874220 } },
        { {  609840,1874220 }, { 606940,1873860 } },
        { {  136680,1926960 }, { 135500,1926360 } },
        { {  135500,1926360 }, { 137360,1923060 } },
        { {  137360,1923060 }, { 139500,1918559 } },
        { {  139500,1918559 }, { 140780,1913239 } },
        { {  140780,1913239 }, { 139600,1913020 } },
        { {  139600,1913020 }, { 127380,1923600 } },
        { {  127380,1923600 }, { 122800,1926059 } },
        { {  122800,1926059 }, { 118879,1927719 } },
        { {  118879,1927719 }, { 114420,1928300 } },
        { {  114420,1928300 }, { 111480,1927020 } },
        { {  111480,1927020 }, { 110619,1925399 } },
        { {  110619,1925399 }, { 109620,1924200 } },
        { {  109620,1924200 }, { 108860,1922780 } },
        { {  108860,1922780 }, { 108479,1920999 } },
        { {  108479,1920999 }, { 106600,1918080 } },
        { {  106600,1918080 }, { 106220,1917740 } },
        { {  106220,1917740 }, { 105199,1916960 } },
        { {  105199,1916960 }, { 101460,1914859 } },
        { {  101460,1914859 }, { 99480,1914379 } },
        { {  99480,1914379 }, { 97179,1913499 } },
        { {  97179,1913499 }, { 94900,1911100 } },
        { {  94900,1911100 }, { 94100,1909639 } },
        { {  94100,1909639 }, { 93379,1907740 } },
        { {  93379,1907740 }, { 93960,1898259 } },
        { {  93960,1898259 }, { 93739,1896460 } },
        { {  93739,1896460 }, { 94299,1893080 } },
        { {  94299,1893080 }, { 97240,1883440 } },
        { {  97240,1883440 }, { 99799,1879780 } },
        { {  99799,1879780 }, { 100400,1878120 } },
        { {  100400,1878120 }, { 100199,1877200 } },
        { {  100199,1877200 }, { 98940,1877460 } },
        { {  98940,1877460 }, { 96320,1878480 } },
        { {  96320,1878480 }, { 86020,1881039 } },
        { {  86020,1881039 }, { 84340,1881080 } },
        { {  84340,1881080 }, { 76780,1882600 } },
        { {  76780,1882600 }, { 74380,1883580 } },
        { {  74380,1883580 }, { 72679,1884019 } },
        { {  72679,1884019 }, { 70900,1885940 } },
        { {  70900,1885940 }, { 71240,1888340 } },
        { {  71240,1888340 }, { 72720,1889940 } },
        { {  72720,1889940 }, { 74640,1891360 } },
        { {  74640,1891360 }, { 75620,1893179 } },
        { {  75620,1893179 }, { 77140,1895340 } },
        { {  77140,1895340 }, { 81040,1899500 } },
        { {  81040,1899500 }, { 82760,1900380 } },
        { {  82760,1900380 }, { 83720,1902300 } },
        { {  83720,1902300 }, { 85459,1903700 } },
        { {  85459,1903700 }, { 86960,1905940 } },
        { {  86960,1905940 }, { 88280,1913020 } },
        { {  88280,1913020 }, { 88160,1913539 } },
        { {  88160,1913539 }, { 88020,1913860 } },
        { {  88020,1913860 }, { 86080,1915200 } },
        { {  86080,1915200 }, { 85660,1916740 } },
        { {  85660,1916740 }, { 83899,1918799 } },
        { {  83899,1918799 }, { 79360,1921160 } },
        { {  79360,1921160 }, { 76400,1923140 } },
        { {  76400,1923140 }, { 70800,1926180 } },
        { {  70800,1926180 }, { 64460,1927659 } },
        { {  64460,1927659 }, { 60880,1927820 } },
        { {  60880,1927820 }, { 55780,1925580 } },
        { {  55780,1925580 }, { 54940,1925040 } },
        { {  54940,1925040 }, { 52199,1921700 } },
        { {  52199,1921700 }, { 49680,1916579 } },
        { {  49680,1916579 }, { 48719,1914180 } },
        { {  48719,1914180 }, { 48620,1913080 } },
        { {  48620,1913080 }, { 47640,1909120 } },
        { {  47640,1909120 }, { 48280,1899319 } },
        { {  48280,1899319 }, { 49140,1895600 } },
        { {  49140,1895600 }, { 50320,1892899 } },
        { {  50320,1892899 }, { 51559,1890640 } },
        { {  51559,1890640 }, { 52140,1889960 } },
        { {  52140,1889960 }, { 54640,1887999 } },
        { {  54640,1887999 }, { 55639,1886500 } },
        { {  55639,1886500 }, { 55720,1885080 } },
        { {  55720,1885080 }, { 55439,1883220 } },
        { {  55439,1883220 }, { 54640,1882159 } },
        { {  54640,1882159 }, { 54100,1880300 } },
        { {  54100,1880300 }, { 52479,1874079 } },
        { {  52479,1874079 }, { 51700,1869000 } },
        { {  51700,1869000 }, { 51600,1865419 } },
        { {  51600,1865419 }, { 51720,1859820 } },
        { {  51720,1859820 }, { 52160,1857260 } },
        { {  52160,1857260 }, { 52539,1856120 } },
        { {  52539,1856120 }, { 57240,1845720 } },
        { {  57240,1845720 }, { 58280,1844400 } },
        { {  58280,1844400 }, { 60639,1840820 } },
        { {  60639,1840820 }, { 65580,1835540 } },
        { {  65580,1835540 }, { 68340,1833340 } },
        { {  68340,1833340 }, { 71660,1831480 } },
        { {  71660,1831480 }, { 73460,1829960 } },
        { {  73460,1829960 }, { 75200,1829319 } },
        { {  75200,1829319 }, { 77200,1828960 } },
        { {  77200,1828960 }, { 78640,1828920 } },
        { {  78640,1828920 }, { 111780,1842700 } },
        { {  111780,1842700 }, { 112800,1843480 } },
        { {  112800,1843480 }, { 113879,1844879 } },
        { {  113879,1844879 }, { 116379,1847379 } },
        { {  116379,1847379 }, { 116360,1847580 } },
        { {  116360,1847580 }, { 117100,1848799 } },
        { {  117100,1848799 }, { 120160,1851799 } },
        { {  120160,1851799 }, { 121860,1852320 } },
        { {  121860,1852320 }, { 124280,1852679 } },
        { {  124280,1852679 }, { 128920,1854659 } },
        { {  128920,1854659 }, { 130840,1856360 } },
        { {  130840,1856360 }, { 133520,1859460 } },
        { {  133520,1859460 }, { 135079,1860860 } },
        { {  135079,1860860 }, { 137280,1864440 } },
        { {  137280,1864440 }, { 142980,1872899 } },
        { {  142980,1872899 }, { 144600,1875840 } },
        { {  144600,1875840 }, { 147240,1883480 } },
        { {  147240,1883480 }, { 147460,1886539 } },
        { {  147460,1886539 }, { 147660,1886920 } },
        { {  147660,1886920 }, { 148399,1891720 } },
        { {  148399,1891720 }, { 148820,1896799 } },
        { {  148820,1896799 }, { 148399,1898880 } },
        { {  148399,1898880 }, { 148799,1899420 } },
        { {  148799,1899420 }, { 150520,1898539 } },
        { {  150520,1898539 }, { 154760,1892760 } },
        { {  154760,1892760 }, { 156580,1889240 } },
        { {  156580,1889240 }, { 156940,1888900 } },
        { {  156940,1888900 }, { 157540,1889540 } },
        { {  157540,1889540 }, { 156860,1896819 } },
        { {  156860,1896819 }, { 155639,1903940 } },
        { {  155639,1903940 }, { 153679,1908100 } },
        { {  153679,1908100 }, { 152859,1909039 } },
        { {  152859,1909039 }, { 149660,1915580 } },
        { {  149660,1915580 }, { 148000,1918600 } },
        { {  148000,1918600 }, { 141640,1926980 } },
        { {  141640,1926980 }, { 140060,1927899 } },
        { {  140060,1927899 }, { 136960,1929019 } },
        { {  136960,1929019 }, { 136680,1926960 } },
        { {  627100,1941519 }, { 625120,1940060 } },
        { {  625120,1940060 }, { 614580,1934680 } },
        { {  614580,1934680 }, { 608780,1929319 } },
        { {  608780,1929319 }, { 607400,1927679 } },
        { {  607400,1927679 }, { 606160,1925920 } },
        { {  606160,1925920 }, { 604480,1922240 } },
        { {  604480,1922240 }, { 602420,1916819 } },
        { {  602420,1916819 }, { 602279,1915260 } },
        { {  602279,1915260 }, { 602880,1907960 } },
        { {  602880,1907960 }, { 604140,1902719 } },
        { {  604140,1902719 }, { 605880,1898539 } },
        { {  605880,1898539 }, { 606640,1897399 } },
        { {  606640,1897399 }, { 609680,1894420 } },
        { {  609680,1894420 }, { 611099,1893640 } },
        { {  611099,1893640 }, { 616099,1890340 } },
        { {  616099,1890340 }, { 617520,1889160 } },
        { {  617520,1889160 }, { 620220,1885540 } },
        { {  620220,1885540 }, { 624480,1882260 } },
        { {  624480,1882260 }, { 628660,1880280 } },
        { {  628660,1880280 }, { 632520,1879659 } },
        { {  632520,1879659 }, { 637760,1879859 } },
        { {  637760,1879859 }, { 640899,1881500 } },
        { {  640899,1881500 }, { 644220,1883980 } },
        { {  644220,1883980 }, { 643900,1890860 } },
        { {  643900,1890860 }, { 643060,1894160 } },
        { {  643060,1894160 }, { 642459,1900320 } },
        { {  642459,1900320 }, { 642400,1903120 } },
        { {  642400,1903120 }, { 643819,1908519 } },
        { {  643819,1908519 }, { 644700,1912560 } },
        { {  644700,1912560 }, { 644640,1916380 } },
        { {  644640,1916380 }, { 644959,1918600 } },
        { {  644959,1918600 }, { 642540,1925620 } },
        { {  642540,1925620 }, { 642439,1926640 } },
        { {  642439,1926640 }, { 641860,1928300 } },
        { {  641860,1928300 }, { 638700,1932939 } },
        { {  638700,1932939 }, { 634820,1934200 } },
        { {  634820,1934200 }, { 631980,1936539 } },
        { {  631980,1936539 }, { 630160,1940600 } },
        { {  630160,1940600 }, { 627740,1941640 } },
        { {  627740,1941640 }, { 627400,1941660 } },
        { {  627400,1941660 }, { 627100,1941519 } }
    };

#if 0
    // Verify whether two any two non-neighbor line segments intersect. They should not, otherwise the Voronoi builder
    // is not guaranteed to succeed.
    for (size_t i = 0; i < lines.size(); ++ i)
        for (size_t j = i + 1; j < lines.size(); ++ j) {
            Point &ip1 = lines[i].a;
            Point &ip2 = lines[i].b;
            Point &jp1 = lines[j].a;
            Point &jp2 = lines[j].b;
            if (&ip1 != &jp2 && &jp1 != &ip2) {
                REQUIRE(! Slic3r::Geometry::segments_intersect(ip1, ip2, jp1, jp2));
            }
        }
#endif

    VD vd;
    construct_voronoi(lines.begin(), lines.end(), &vd);

    for (const auto& edge : vd.edges())
        if (edge.is_finite()) {
            auto v0 = edge.vertex0();
            auto v1 = edge.vertex1();
            REQUIRE((v0->x() == 0 || std::isnormal(v0->x())));
            REQUIRE((v0->y() == 0 || std::isnormal(v0->y())));
            REQUIRE((v1->x() == 0 || std::isnormal(v1->x())));
            REQUIRE((v1->y() == 0 || std::isnormal(v1->y())));
        }

#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-NaNs.svg").c_str(),
        vd, Points(), lines, 0.015);
#endif
}
