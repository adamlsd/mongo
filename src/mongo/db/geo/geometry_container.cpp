/**
 *    Copyright (C) 2013 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/db/geo/geometry_container.h"

#include "mongo/db/geo/geoconstants.h"
#include "mongo/db/geo/geoparser.h"
#include "mongo/stdx/utility.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/transitional_tools_do_not_use/vector_spooling.h"

namespace mongo {

using mongoutils::str::equals;

bool GeometryContainer::isSimpleContainer() const {
    return NULL != _point || NULL != _line || NULL != _polygon;
}

bool GeometryContainer::isPoint() const {
    return nullptr != _point;
}

bool GeometryContainer::supportsContains() const {
    return NULL != _polygon || NULL != _box || NULL != _cap || NULL != _multiPolygon ||
        (NULL != _geometryCollection && (_geometryCollection->polygons.size() > 0 ||
                                         _geometryCollection->multiPolygons.size() > 0));
}

bool GeometryContainer::hasS2Region() const {
    return (NULL != _point && _point->crs == SPHERE) || NULL != _line ||
        (NULL != _polygon && (_polygon->crs == SPHERE || _polygon->crs == STRICT_SPHERE)) ||
        (NULL != _cap && _cap->crs == SPHERE) || NULL != _multiPoint || NULL != _multiLine ||
        NULL != _multiPolygon || NULL != _geometryCollection;
}

const S2Region& GeometryContainer::getS2Region() const {
    if (NULL != _point && SPHERE == _point->crs) {
        return _point->cell;
    } else if (NULL != _line) {
        return _line->line;
    } else if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return *_polygon->s2Polygon;
    } else if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return *_polygon->bigPolygon;
    } else if (NULL != _cap && SPHERE == _cap->crs) {
        return _cap->cap;
    } else if (NULL != _multiPoint) {
        return *_s2Region;
    } else if (NULL != _multiLine) {
        return *_s2Region;
    } else if (NULL != _multiPolygon) {
        return *_s2Region;
    } else {
        invariant(NULL != _geometryCollection);
        return *_s2Region;
    }
}

bool GeometryContainer::hasR2Region() const {
    return _cap || _box || _point || (_polygon && _polygon->crs == FLAT) ||
        (_multiPoint && FLAT == _multiPoint->crs);
}

class GeometryContainer::R2BoxRegion : public R2Region {
public:
    R2BoxRegion(const GeometryContainer* geometry);
    virtual ~R2BoxRegion();

    Box getR2Bounds() const;

    bool fastContains(const Box& other) const;

    bool fastDisjoint(const Box& other) const;

private:
    static Box buildBounds(const GeometryContainer& geometry);

    // Not owned here
    const GeometryContainer* _geometry;

    // TODO: For big complex shapes, may be better to use actual shape from above
    const Box _bounds;
};

GeometryContainer::R2BoxRegion::R2BoxRegion(const GeometryContainer* geometry)
    : _geometry(geometry), _bounds(buildBounds(*geometry)) {}

GeometryContainer::R2BoxRegion::~R2BoxRegion() {}

Box GeometryContainer::R2BoxRegion::getR2Bounds() const {
    return _bounds;
}

bool GeometryContainer::R2BoxRegion::fastContains(const Box& other) const {
    // TODO: Add more cases here to make coverings better
    if (_geometry->_box && FLAT == _geometry->_box->crs) {
        const Box& box = _geometry->_box->box;
        if (box.contains(other))
            return true;
    } else if (_geometry->_cap && FLAT == _geometry->_cap->crs) {
        const Circle& circle = _geometry->_cap->circle;
        // Exact test
        return circleContainsBox(circle, other);
    }

    if (_geometry->_polygon && FLAT == _geometry->_polygon->crs) {
        const Polygon& polygon = _geometry->_polygon->oldPolygon;
        // Exact test
        return polygonContainsBox(polygon, other);
    }

    // Not sure
    return false;
}

bool GeometryContainer::R2BoxRegion::fastDisjoint(const Box& other) const {
    if (!_bounds.intersects(other))
        return true;

    // Not sure
    return false;
}

static Point toLngLatPoint(const S2Point& s2Point) {
    Point point;
    S2LatLng latLng(s2Point);
    point.x = latLng.lng().degrees();
    point.y = latLng.lat().degrees();
    return point;
}

static void lineR2Bounds(const S2Polyline& flatLine, Box* flatBounds) {
    int numVertices = flatLine.num_vertices();
    verify(flatLine.num_vertices() > 0);

    flatBounds->init(toLngLatPoint(flatLine.vertex(0)), toLngLatPoint(flatLine.vertex(0)));

    for (int i = 1; i < numVertices; ++i) {
        flatBounds->expandToInclude(toLngLatPoint(flatLine.vertex(i)));
    }
}

static void circleR2Bounds(const Circle& circle, Box* flatBounds) {
    flatBounds->init(Point(circle.center.x - circle.radius, circle.center.y - circle.radius),
                     Point(circle.center.x + circle.radius, circle.center.y + circle.radius));
}

static void multiPointR2Bounds(const vector<S2Point>& points, Box* flatBounds) {
    verify(!points.empty());

    flatBounds->init(toLngLatPoint(points.front()), toLngLatPoint(points.front()));

    vector<S2Point>::const_iterator it = points.begin();
    for (++it; it != points.end(); ++it) {
        const S2Point& s2Point = *it;
        flatBounds->expandToInclude(toLngLatPoint(s2Point));
    }
}

static void polygonR2Bounds(const Polygon& polygon, Box* flatBounds) {
    *flatBounds = polygon.bounds();
}

static void s2RegionR2Bounds(const S2Region& region, Box* flatBounds) {
    S2LatLngRect s2Bounds = region.GetRectBound();
    flatBounds->init(Point(s2Bounds.lng_lo().degrees(), s2Bounds.lat_lo().degrees()),
                     Point(s2Bounds.lng_hi().degrees(), s2Bounds.lat_hi().degrees()));
}

Box GeometryContainer::R2BoxRegion::buildBounds(const GeometryContainer& geometry) {
    Box bounds;

    if (geometry._point && FLAT == geometry._point->crs) {
        bounds.init(geometry._point->oldPoint, geometry._point->oldPoint);
    } else if (geometry._line && FLAT == geometry._line->crs) {
        lineR2Bounds(geometry._line->line, &bounds);
    } else if (geometry._cap && FLAT == geometry._cap->crs) {
        circleR2Bounds(geometry._cap->circle, &bounds);
    } else if (geometry._box && FLAT == geometry._box->crs) {
        bounds = geometry._box->box;
    } else if (geometry._polygon && FLAT == geometry._polygon->crs) {
        polygonR2Bounds(geometry._polygon->oldPolygon, &bounds);
    } else if (geometry._multiPoint && FLAT == geometry._multiPoint->crs) {
        multiPointR2Bounds(geometry._multiPoint->points, &bounds);
    } else if (geometry._multiLine && FLAT == geometry._multiLine->crs) {
        verify(false);
    } else if (geometry._multiPolygon && FLAT == geometry._multiPolygon->crs) {
        verify(false);
    } else if (geometry._geometryCollection) {
        verify(false);
    } else if (geometry.hasS2Region()) {
        // For now, just support spherical cap for $centerSphere and GeoJSON points
        verify((geometry._cap && FLAT != geometry._cap->crs) ||
               (geometry._point && FLAT != geometry._point->crs));
        s2RegionR2Bounds(geometry.getS2Region(), &bounds);
    }

    return bounds;
}

const R2Region& GeometryContainer::getR2Region() const {
    return *_r2Region;
}

bool GeometryContainer::contains(const GeometryContainer& otherContainer) const {
    // First let's deal with the FLAT cases

    if (_point && FLAT == _point->crs) {
        return false;
    }

    if (NULL != _polygon && (FLAT == _polygon->crs)) {
        if (NULL == otherContainer._point) {
            return false;
        }
        return _polygon->oldPolygon.contains(otherContainer._point->oldPoint);
    }

    if (NULL != _box) {
        verify(FLAT == _box->crs);
        if (NULL == otherContainer._point) {
            return false;
        }
        return _box->box.inside(otherContainer._point->oldPoint);
    }

    if (NULL != _cap && (FLAT == _cap->crs)) {
        if (NULL == otherContainer._point) {
            return false;
        }
        // Let's be as consistent epsilon-wise as we can with the '2d' indextype.
        return distanceWithin(
            _cap->circle.center, otherContainer._point->oldPoint, _cap->circle.radius);
    }

    // Now we deal with all the SPHERE stuff.

    // Iterate over the other thing and see if we contain it all.
    if (NULL != otherContainer._point) {
        return contains(otherContainer._point->cell, otherContainer._point->point);
    }

    if (NULL != otherContainer._line) {
        return contains(otherContainer._line->line);
    }

    if (NULL != otherContainer._polygon) {
        invariant(NULL != otherContainer._polygon->s2Polygon);
        return contains(*otherContainer._polygon->s2Polygon);
    }

    if (NULL != otherContainer._multiPoint) {
        for (size_t i = 0; i < otherContainer._multiPoint->points.size(); ++i) {
            if (!contains(otherContainer._multiPoint->cells[i],
                          otherContainer._multiPoint->points[i])) {
                return false;
            }
        }
        return true;
    }

    if (NULL != otherContainer._multiLine) {
        const auto& lines = otherContainer._multiLine->lines;
        for (const auto& line : lines) {
            if (!contains(stdx::as_const(*line))) {
                return false;
            }
        }
        return true;
    }

    if (NULL != otherContainer._multiPolygon) {
        const auto& polys = otherContainer._multiPolygon->polygons;
        for (const auto& poly : polys) {
            if (!contains(stdx::as_const(*poly))) {
                return false;
            }
        }
        return true;
    }

    if (NULL != otherContainer._geometryCollection) {
        GeometryCollection& c = *otherContainer._geometryCollection;

        for (const auto& collection : c.points) {
            if (!contains(stdx::as_const(collection.cell), stdx::as_const(collection.point))) {
                return false;
            }
        }

        const auto& lines = c.lines;
        for (const auto& line : lines) {
            if (!contains(stdx::as_const(line->line))) {
                return false;
            }
        }

        const auto& polys = c.polygons;
        for (const auto& poly : polys) {
            if (!contains(stdx::as_const(*poly->s2Polygon))) {
                return false;
            }
        }

        const auto& multipoints = c.multiPoints;
        for (const auto& mp : multipoints) {
            for (size_t j = 0; j < mp->points.size(); ++j) {
                if (!contains(mp->cells[j], mp->points[j])) {
                    return false;
                }
            }
        }

        const auto& multilines = c.multiLines;
        for (size_t i = 0; i < multilines.size(); ++i) {
            const auto& lines = multilines[i]->lines;
            for (size_t j = 0; j < lines.size(); ++j) {
                if (!contains(*lines[j])) {
                    return false;
                }
            }
        }

        const auto& multipolys = c.multiPolygons;
        for (size_t i = 0; i < multipolys.size(); ++i) {
            const auto& polys = multipolys[i]->polygons;
            for (size_t j = 0; j < polys.size(); ++j) {
                if (!contains(*polys[j])) {
                    return false;
                }
            }
        }

        return true;
    }

    return false;
}

namespace {
bool containsPoint(const S2Polygon& poly, const S2Cell& otherCell, const S2Point& otherPoint) {
    // This is much faster for actual containment checking.
    if (poly.Contains(otherPoint)) {
        return true;
    }
    // This is slower but contains edges/vertices.
    return poly.MayIntersect(otherCell);
}
}  // namespace

bool GeometryContainer::contains(const S2Cell& otherCell, const S2Point& otherPoint) const {
    if (NULL != _polygon && (NULL != _polygon->s2Polygon)) {
        return containsPoint(*_polygon->s2Polygon, otherCell, otherPoint);
    }

    if (NULL != _polygon && (NULL != _polygon->bigPolygon)) {
        if (_polygon->bigPolygon->Contains(otherPoint))
            return true;
        return _polygon->bigPolygon->MayIntersect(otherCell);
    }

    if (NULL != _cap && (_cap->crs == SPHERE)) {
        return _cap->cap.MayIntersect(otherCell);
    }

    if (NULL != _multiPolygon) {
        const auto& polys = _multiPolygon->polygons;
        for (const auto& poly : polys) {
            if (containsPoint(stdx::as_const(*poly), otherCell, otherPoint)) {
                return true;
            }
        }
    }

    if (NULL != _geometryCollection) {
        const auto& polys = _geometryCollection->polygons;
        for (const auto& poly : polys) {
            if (containsPoint(stdx::as_const(*poly->s2Polygon), otherCell, otherPoint)) {
                return true;
            }
        }

        const auto& multipolys = _geometryCollection->multiPolygons;
        for (size_t i = 0; i < multipolys.size(); ++i) {
            const auto& innerpolys = multipolys[i]->polygons;
            for (size_t j = 0; j < innerpolys.size(); ++j) {
                if (containsPoint(*innerpolys[j], otherCell, otherPoint)) {
                    return true;
                }
            }
        }
    }

    return false;
}

namespace {
bool containsLine(const S2Polygon& poly, const S2Polyline& otherLine) {
    // Kind of a mess.  We get a function for clipping the line to the
    // polygon.  We do this and make sure the line is the same as the
    // line we're clipping against.
    std::vector<S2Polyline*> clipped;

    poly.IntersectWithPolyline(&otherLine, &clipped);
    const std::vector<std::unique_ptr<S2Polyline>> clippedOwned =
        transitional_tools_do_not_use::spool_vector(clipped);
    if (1 != clipped.size()) {
        return false;
    }

    // If the line is entirely contained within the polygon, we should be
    // getting it back verbatim, so really there should be no error.
    bool ret = clipped[0]->NearlyCoversPolyline(otherLine, S1Angle::Degrees(1e-10));

    return ret;
}
}  // namespace

bool GeometryContainer::contains(const S2Polyline& otherLine) const {
    if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return containsLine(*_polygon->s2Polygon, otherLine);
    }

    if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return _polygon->bigPolygon->Contains(otherLine);
    }

    if (NULL != _cap && (_cap->crs == SPHERE)) {
        // If the radian distance of a line to the centroid of the complement spherical cap is less
        // than the arc radian of the complement cap, then the line is not within the spherical cap.
        S2Cap complementSphere = _cap->cap.Complement();
        if (S2Distance::minDistanceRad(complementSphere.axis(), otherLine) <
            complementSphere.angle().radians()) {
            return false;
        }
        return true;
    }

    if (NULL != _multiPolygon) {
        const auto& polys = _multiPolygon->polygons;
        for (const auto& poly : polys) {
            if (containsLine(stdx::as_const(*poly), otherLine)) {
                return true;
            }
        }
    }

    if (NULL != _geometryCollection) {
        const auto& polys = _geometryCollection->polygons;
        for (const auto& poly : polys) {
            if (containsLine(stdx::as_const(*poly->s2Polygon), otherLine)) {
                return true;
            }
        }

        const auto& multipolys = _geometryCollection->multiPolygons;
        for (size_t i = 0; i < multipolys.size(); ++i) {
            const auto& innerpolys = multipolys[i]->polygons;
            for (const auto& innerpoly : innerpolys) {
                if (containsLine(stdx::as_const(*innerpoly), otherLine)) {
                    return true;
                }
            }
        }
    }

    return false;
}

namespace {
bool containsPolygon(const S2Polygon& poly, const S2Polygon& otherPoly) {
    return poly.Contains(&otherPoly);
}
}  // namespace

bool GeometryContainer::contains(const S2Polygon& otherPolygon) const {
    if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return containsPolygon(*_polygon->s2Polygon, otherPolygon);
    }

    if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return _polygon->bigPolygon->Contains(otherPolygon);
    }

    if (NULL != _cap && (_cap->crs == SPHERE)) {
        // If the radian distance of a polygon to the centroid of the complement spherical cap is
        // less than the arc radian of the complement cap, then the polygon is not within the
        // spherical cap.
        S2Cap complementSphere = _cap->cap.Complement();
        if (S2Distance::minDistanceRad(complementSphere.axis(), otherPolygon) <
            complementSphere.angle().radians()) {
            return false;
        }
        return true;
    }

    if (NULL != _multiPolygon) {
        const auto& polys = _multiPolygon->polygons;
        for (const auto& poly : polys) {
            if (containsPolygon(stdx::as_const(*poly), otherPolygon)) {
                return true;
            }
        }
    }

    if (NULL != _geometryCollection) {
        const auto& polys = _geometryCollection->polygons;
        for (const auto& poly : polys) {
            if (containsPolygon(stdx::as_const(*poly->s2Polygon), otherPolygon)) {
                return true;
            }
        }

        const auto& multipolys = _geometryCollection->multiPolygons;
        for (size_t i = 0; i < multipolys.size(); ++i) {
            const auto& innerpolys = multipolys[i]->polygons;
            for (const auto& innerpoly : innerpolys) {
                if (containsPolygon(stdx::as_const(*innerpoly), otherPolygon)) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool GeometryContainer::intersects(const GeometryContainer& otherContainer) const {
    if (NULL != otherContainer._point) {
        return intersects(otherContainer._point->cell);
    } else if (NULL != otherContainer._line) {
        return intersects(otherContainer._line->line);
    } else if (NULL != otherContainer._polygon) {
        if (NULL == otherContainer._polygon->s2Polygon) {
            return false;
        }
        return intersects(*otherContainer._polygon->s2Polygon);
    } else if (NULL != otherContainer._multiPoint) {
        return intersects(*otherContainer._multiPoint);
    } else if (NULL != otherContainer._multiLine) {
        return intersects(*otherContainer._multiLine);
    } else if (NULL != otherContainer._multiPolygon) {
        return intersects(*otherContainer._multiPolygon);
    } else if (NULL != otherContainer._geometryCollection) {
        const GeometryCollection& c = *otherContainer._geometryCollection;

        for (size_t i = 0; i < c.points.size(); ++i) {
            if (intersects(c.points[i].cell)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.polygons.size(); ++i) {
            if (intersects(*c.polygons[i]->s2Polygon)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.lines.size(); ++i) {
            if (intersects(c.lines[i]->line)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiPolygons.size(); ++i) {
            if (intersects(*c.multiPolygons[i])) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiLines.size(); ++i) {
            if (intersects(*c.multiLines[i])) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiPoints.size(); ++i) {
            if (intersects(*c.multiPoints[i])) {
                return true;
            }
        }
    }

    return false;
}

bool GeometryContainer::intersects(const MultiPointWithCRS& otherMultiPoint) const {
    for (const auto& cell : otherMultiPoint.cells) {
        if (intersects(stdx::as_const(cell))) {
            return true;
        }
    }
    return false;
}

bool GeometryContainer::intersects(const MultiLineWithCRS& otherMultiLine) const {
    for (const auto& line : otherMultiLine.lines) {
        if (intersects(stdx::as_const(*line))) {
            return true;
        }
    }
    return false;
}

bool GeometryContainer::intersects(const MultiPolygonWithCRS& otherMultiPolygon) const {
    for (const auto& poly : otherMultiPolygon.polygons) {
        if (intersects(stdx::as_const(*poly))) {
            return true;
        }
    }
    return false;
}

// Does this (GeometryContainer) intersect the provided data?
bool GeometryContainer::intersects(const S2Cell& otherPoint) const {
    if (NULL != _point) {
        return _point->cell.MayIntersect(otherPoint);
    } else if (NULL != _line) {
        return _line->line.MayIntersect(otherPoint);
    } else if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return _polygon->s2Polygon->MayIntersect(otherPoint);
    } else if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return _polygon->bigPolygon->MayIntersect(otherPoint);
    } else if (NULL != _multiPoint) {
        const vector<S2Cell>& cells = _multiPoint->cells;
        for (size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].MayIntersect(otherPoint)) {
                return true;
            }
        }
    } else if (NULL != _multiLine) {
        const auto& lines = _multiLine->lines;
        for (const auto& line : lines) {
            if (stdx::as_const(*line).MayIntersect(otherPoint)) {
                return true;
            }
        }
    } else if (NULL != _multiPolygon) {
        const auto& polys = _multiPolygon->polygons;
        for (size_t i = 0; i < polys.size(); ++i) {
            if (polys[i]->MayIntersect(otherPoint)) {
                return true;
            }
        }
    } else if (NULL != _geometryCollection) {
        const GeometryCollection& c = *_geometryCollection;

        for (size_t i = 0; i < c.points.size(); ++i) {
            if (c.points[i].cell.MayIntersect(otherPoint)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.polygons.size(); ++i) {
            if (c.polygons[i]->s2Polygon->MayIntersect(otherPoint)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.lines.size(); ++i) {
            if (c.lines[i]->line.MayIntersect(otherPoint)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiPolygons.size(); ++i) {
            const auto& innerPolys = c.multiPolygons[i]->polygons;
            for (size_t j = 0; j < innerPolys.size(); ++j) {
                if (innerPolys[j]->MayIntersect(otherPoint)) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiLines.size(); ++i) {
            const auto& innerLines = c.multiLines[i]->lines;
            for (size_t j = 0; j < innerLines.size(); ++j) {
                if (innerLines[j]->MayIntersect(otherPoint)) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiPoints.size(); ++i) {
            const auto& innerCells = c.multiPoints[i]->cells;
            for (size_t j = 0; j < innerCells.size(); ++j) {
                if (innerCells[j].MayIntersect(otherPoint)) {
                    return true;
                }
            }
        }
    }

    return false;
}

namespace {
bool polygonLineIntersection(const S2Polyline& line, const S2Polygon& poly) {
    // TODO(hk): modify s2 library to just let us know if it intersected
    // rather than returning all this.
    std::vector<S2Polyline*> clipped;
    poly.IntersectWithPolyline(&line, &clipped);
    const auto clippedOwned = transitional_tools_do_not_use::spool_vector(clipped);
    return !clipped.empty();
}
}  // namespace

bool GeometryContainer::intersects(const S2Polyline& otherLine) const {
    if (NULL != _point) {
        return otherLine.MayIntersect(_point->cell);
    } else if (NULL != _line) {
        return otherLine.Intersects(&_line->line);
    } else if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return polygonLineIntersection(otherLine, *_polygon->s2Polygon);
    } else if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return _polygon->bigPolygon->Intersects(otherLine);
    } else if (NULL != _multiPoint) {
        for (size_t i = 0; i < _multiPoint->cells.size(); ++i) {
            if (otherLine.MayIntersect(_multiPoint->cells[i])) {
                return true;
            }
        }
    } else if (NULL != _multiLine) {
        for (size_t i = 0; i < _multiLine->lines.size(); ++i) {
            if (otherLine.Intersects(_multiLine->lines[i].get())) {
                return true;
            }
        }
    } else if (NULL != _multiPolygon) {
        for (size_t i = 0; i < _multiPolygon->polygons.size(); ++i) {
            if (polygonLineIntersection(otherLine, *_multiPolygon->polygons[i])) {
                return true;
            }
        }
    } else if (NULL != _geometryCollection) {
        const GeometryCollection& c = *_geometryCollection;

        for (size_t i = 0; i < c.points.size(); ++i) {
            if (otherLine.MayIntersect(c.points[i].cell)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.polygons.size(); ++i) {
            if (polygonLineIntersection(otherLine, *c.polygons[i]->s2Polygon)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.lines.size(); ++i) {
            if (c.lines[i]->line.Intersects(&otherLine)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiPolygons.size(); ++i) {
            const auto& innerPolys = c.multiPolygons[i]->polygons;
            for (size_t j = 0; j < innerPolys.size(); ++j) {
                if (polygonLineIntersection(otherLine, *innerPolys[j])) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiLines.size(); ++i) {
            const auto& innerLines = c.multiLines[i]->lines;
            for (size_t j = 0; j < innerLines.size(); ++j) {
                if (innerLines[j]->Intersects(&otherLine)) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiPoints.size(); ++i) {
            const vector<S2Cell>& innerCells = c.multiPoints[i]->cells;
            for (size_t j = 0; j < innerCells.size(); ++j) {
                if (otherLine.MayIntersect(innerCells[j])) {
                    return true;
                }
            }
        }
    }

    return false;
}

// Does 'this' intersect with the provided polygon?
bool GeometryContainer::intersects(const S2Polygon& otherPolygon) const {
    if (NULL != _point) {
        return otherPolygon.MayIntersect(_point->cell);
    } else if (NULL != _line) {
        return polygonLineIntersection(_line->line, otherPolygon);
    } else if (NULL != _polygon && NULL != _polygon->s2Polygon) {
        return otherPolygon.Intersects(_polygon->s2Polygon.get());
    } else if (NULL != _polygon && NULL != _polygon->bigPolygon) {
        return _polygon->bigPolygon->Intersects(otherPolygon);
    } else if (NULL != _multiPoint) {
        for (size_t i = 0; i < _multiPoint->cells.size(); ++i) {
            if (otherPolygon.MayIntersect(_multiPoint->cells[i])) {
                return true;
            }
        }
    } else if (NULL != _multiLine) {
        for (size_t i = 0; i < _multiLine->lines.size(); ++i) {
            if (polygonLineIntersection(*_multiLine->lines[i], otherPolygon)) {
                return true;
            }
        }
    } else if (NULL != _multiPolygon) {
        for (size_t i = 0; i < _multiPolygon->polygons.size(); ++i) {
            if (otherPolygon.Intersects(_multiPolygon->polygons[i].get())) {
                return true;
            }
        }
    } else if (NULL != _geometryCollection) {
        const GeometryCollection& c = *_geometryCollection;

        for (size_t i = 0; i < c.points.size(); ++i) {
            if (otherPolygon.MayIntersect(c.points[i].cell)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.polygons.size(); ++i) {
            if (otherPolygon.Intersects(c.polygons[i]->s2Polygon.get())) {
                return true;
            }
        }

        for (size_t i = 0; i < c.lines.size(); ++i) {
            if (polygonLineIntersection(c.lines[i]->line, otherPolygon)) {
                return true;
            }
        }

        for (size_t i = 0; i < c.multiPolygons.size(); ++i) {
            const auto& innerPolys = c.multiPolygons[i]->polygons;
            for (const auto& poly : innerPolys) {
                if (otherPolygon.Intersects(poly.get())) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiLines.size(); ++i) {
            const auto& innerLines = c.multiLines[i]->lines;
            for (size_t j = 0; j < innerLines.size(); ++j) {
                if (polygonLineIntersection(*innerLines[j], otherPolygon)) {
                    return true;
                }
            }
        }

        for (size_t i = 0; i < c.multiPoints.size(); ++i) {
            const auto& innerCells = c.multiPoints[i]->cells;
            for (size_t j = 0; j < innerCells.size(); ++j) {
                if (otherPolygon.MayIntersect(innerCells[j])) {
                    return true;
                }
            }
        }
    }

    return false;
}

Status GeometryContainer::parseFromGeoJSON(const BSONObj& obj, bool skipValidation) {
    GeoParser::GeoJSONType type = GeoParser::parseGeoJSONType(obj);

    if (GeoParser::GEOJSON_UNKNOWN == type) {
        return Status(ErrorCodes::BadValue, str::stream() << "unknown GeoJSON type: " << obj);
    }

    Status status = Status::OK();
    std::vector<S2Region*> regions;

    if (GeoParser::GEOJSON_POINT == type) {
        _point = std::make_unique<PointWithCRS>();
        status = GeoParser::parseGeoJSONPoint(obj, _point.get());
    } else if (GeoParser::GEOJSON_LINESTRING == type) {
        _line = std::make_unique<LineWithCRS>();
        status = GeoParser::parseGeoJSONLine(obj, skipValidation, _line.get());
    } else if (GeoParser::GEOJSON_POLYGON == type) {
        _polygon = std::make_unique<PolygonWithCRS>();
        status = GeoParser::parseGeoJSONPolygon(obj, skipValidation, _polygon.get());
    } else if (GeoParser::GEOJSON_MULTI_POINT == type) {
        _multiPoint = std::make_unique<MultiPointWithCRS>();
        status = GeoParser::parseMultiPoint(obj, _multiPoint.get());
        for (size_t i = 0; i < _multiPoint->cells.size(); ++i) {
            regions.push_back(&_multiPoint->cells[i]);
        }
    } else if (GeoParser::GEOJSON_MULTI_LINESTRING == type) {
        _multiLine = std::make_unique<MultiLineWithCRS>();
        status = GeoParser::parseMultiLine(obj, skipValidation, _multiLine.get());
        for (size_t i = 0; i < _multiLine->lines.size(); ++i) {
            regions.push_back(_multiLine->lines[i].get());
        }
    } else if (GeoParser::GEOJSON_MULTI_POLYGON == type) {
        _multiPolygon = std::make_unique<MultiPolygonWithCRS>();
        status = GeoParser::parseMultiPolygon(obj, skipValidation, _multiPolygon.get());
        for (size_t i = 0; i < _multiPolygon->polygons.size(); ++i) {
            regions.push_back(_multiPolygon->polygons[i].get());
        }
    } else if (GeoParser::GEOJSON_GEOMETRY_COLLECTION == type) {
        _geometryCollection = std::make_unique<GeometryCollection>();
        status = GeoParser::parseGeometryCollection(obj, skipValidation, _geometryCollection.get());

        // Add regions
        for (size_t i = 0; i < _geometryCollection->points.size(); ++i) {
            regions.push_back(&_geometryCollection->points[i].cell);
        }
        for (size_t i = 0; i < _geometryCollection->lines.size(); ++i) {
            regions.push_back(&_geometryCollection->lines[i]->line);
        }
        for (size_t i = 0; i < _geometryCollection->polygons.size(); ++i) {
            regions.push_back(_geometryCollection->polygons[i]->s2Polygon.get());
        }
        for (size_t i = 0; i < _geometryCollection->multiPoints.size(); ++i) {
            const auto& multiPoint = _geometryCollection->multiPoints[i];
            for (size_t j = 0; j < multiPoint->cells.size(); ++j) {
                regions.push_back(&multiPoint->cells[j]);
            }
        }
        for (size_t i = 0; i < _geometryCollection->multiLines.size(); ++i) {
            const auto& multiLine = *_geometryCollection->multiLines[i];
            for (size_t j = 0; j < multiLine.lines.size(); ++j) {
                regions.push_back(multiLine.lines[j].get());
            }
        }
        for (size_t i = 0; i < _geometryCollection->multiPolygons.size(); ++i) {
            const auto& multiPolygon = *_geometryCollection->multiPolygons[i];
            for (size_t j = 0; j < multiPolygon.polygons.size(); ++j) {
                regions.push_back(multiPolygon.polygons[j].get());
            }
        }
    } else {
        // Should not reach here.
        MONGO_UNREACHABLE;
    }

    // Check parsing result.
    if (!status.isOK())
        return status;

    if (regions.size() > 0) {
        // S2RegionUnion doesn't take ownership of pointers.
        _s2Region = std::make_unique<S2RegionUnion>(&regions);
    }

    return Status::OK();
}

// Examples:
// { $geoWithin : { $geometry : <GeoJSON> } }
// { $geoIntersects : { $geometry : <GeoJSON> } }
// { $geoWithin : { $box : [[x1, y1], [x2, y2]] } }
// { $geoWithin : { $polygon : [[x1, y1], [x1, y2], [x2, y2], [x2, y1]] } }
// { $geoWithin : { $center : [[x1, y1], r], } }
// { $geoWithin : { $centerSphere : [[x, y], radius] } }
// { $geoIntersects : { $geometry : [1, 2] } }
//
// "elem" is the first element of the object after $geoWithin / $geoIntersects predicates.
// i.e. { $box: ... }, { $geometry: ... }
Status GeometryContainer::parseFromQuery(const BSONElement& elem) {
    // Check elem is an object and has geo specifier.
    GeoParser::GeoSpecifier specifier = GeoParser::parseGeoSpecifier(elem);

    if (GeoParser::UNKNOWN == specifier) {
        // Cannot parse geo specifier.
        return Status(ErrorCodes::BadValue, str::stream() << "unknown geo specifier: " << elem);
    }

    Status status = Status::OK();
    BSONObj obj = elem.Obj();
    if (GeoParser::BOX == specifier) {
        _box = std::make_unique<BoxWithCRS>();
        status = GeoParser::parseLegacyBox(obj, _box.get());
    } else if (GeoParser::CENTER == specifier) {
        _cap = std::make_unique<CapWithCRS>();
        status = GeoParser::parseLegacyCenter(obj, _cap.get());
    } else if (GeoParser::POLYGON == specifier) {
        _polygon = std::make_unique<PolygonWithCRS>();
        status = GeoParser::parseLegacyPolygon(obj, _polygon.get());
    } else if (GeoParser::CENTER_SPHERE == specifier) {
        _cap = std::make_unique<CapWithCRS>();
        status = GeoParser::parseCenterSphere(obj, _cap.get());
    } else if (GeoParser::GEOMETRY == specifier) {
        // GeoJSON geometry or legacy point
        if (Array == elem.type() || obj.firstElement().isNumber()) {
            // legacy point
            _point = std::make_unique<PointWithCRS>();
            status = GeoParser::parseQueryPoint(elem, _point.get());
        } else {
            // GeoJSON geometry
            status = parseFromGeoJSON(obj);
        }
    }
    if (!status.isOK())
        return status;

    // If we support R2 regions, build the region immediately
    if (hasR2Region()) {
        _r2Region = std::make_unique<R2BoxRegion>(this);
    }

    return status;
}

// Examples:
// { location: <GeoJSON> }
// { location: [1, 2] }
// { location: [1, 2, 3] }
// { location: {x: 1, y: 2} }
//
// "elem" is the element that contains geo data. e.g. "location": [1, 2]
// We need the type information to determine whether it's legacy point.
Status GeometryContainer::parseFromStorage(const BSONElement& elem, bool skipValidation) {
    if (!elem.isABSONObj()) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "geo element must be an array or object: " << elem);
    }

    BSONObj geoObj = elem.Obj();
    Status status = Status::OK();
    if (Array == elem.type() || geoObj.firstElement().isNumber()) {
        // Legacy point
        // { location: [1, 2] }
        // { location: [1, 2, 3] }
        // { location: {x: 1, y: 2} }
        // { location: {x: 1, y: 2, type: "Point" } }
        _point = std::make_unique<PointWithCRS>();
        // Allow more than two dimensions or extra fields, like [1, 2, 3]
        status = GeoParser::parseLegacyPoint(elem, _point.get(), true);
    } else {
        // GeoJSON
        // { location: { type: "Point", coordinates: [...] } }
        status = parseFromGeoJSON(elem.Obj(), skipValidation);
    }
    if (!status.isOK())
        return status;

    // If we support R2 regions, build the region immediately
    if (hasR2Region())
        _r2Region = std::make_unique<R2BoxRegion>(this);

    return Status::OK();
}

string GeometryContainer::getDebugType() const {
    if (NULL != _point) {
        return "pt";
    } else if (NULL != _line) {
        return "ln";
    } else if (NULL != _box) {
        return "bx";
    } else if (NULL != _polygon) {
        return "pl";
    } else if (NULL != _cap) {
        return "cc";
    } else if (NULL != _multiPoint) {
        return "mp";
    } else if (NULL != _multiLine) {
        return "ml";
    } else if (NULL != _multiPolygon) {
        return "my";
    } else if (NULL != _geometryCollection) {
        return "gc";
    } else {
        MONGO_UNREACHABLE;
        return "";
    }
}

CRS GeometryContainer::getNativeCRS() const {
    // TODO: Fix geometry collection reporting when/if we support multiple CRSes

    if (NULL != _point) {
        return _point->crs;
    } else if (NULL != _line) {
        return _line->crs;
    } else if (NULL != _box) {
        return _box->crs;
    } else if (NULL != _polygon) {
        return _polygon->crs;
    } else if (NULL != _cap) {
        return _cap->crs;
    } else if (NULL != _multiPoint) {
        return _multiPoint->crs;
    } else if (NULL != _multiLine) {
        return _multiLine->crs;
    } else if (NULL != _multiPolygon) {
        return _multiPolygon->crs;
    } else if (NULL != _geometryCollection) {
        return SPHERE;
    } else {
        MONGO_UNREACHABLE;
        return FLAT;
    }
}

bool GeometryContainer::supportsProject(CRS otherCRS) const {
    // TODO: Fix geometry collection reporting when/if we support more CRSes

    if (NULL != _point) {
        return ShapeProjection::supportsProject(*_point, otherCRS);
    } else if (NULL != _line) {
        return _line->crs == otherCRS;
    } else if (NULL != _box) {
        return _box->crs == otherCRS;
    } else if (NULL != _polygon) {
        return ShapeProjection::supportsProject(*_polygon, otherCRS);
    } else if (NULL != _cap) {
        return _cap->crs == otherCRS;
    } else if (NULL != _multiPoint) {
        return _multiPoint->crs == otherCRS;
    } else if (NULL != _multiLine) {
        return _multiLine->crs == otherCRS;
    } else if (NULL != _multiPolygon) {
        return _multiPolygon->crs == otherCRS;
    } else {
        invariant(NULL != _geometryCollection);
        return SPHERE == otherCRS;
    }
}

void GeometryContainer::projectInto(CRS otherCRS) {
    if (getNativeCRS() == otherCRS)
        return;

    if (NULL != _polygon) {
        ShapeProjection::projectInto(_polygon.get(), otherCRS);
        return;
    }

    invariant(NULL != _point);
    ShapeProjection::projectInto(_point.get(), otherCRS);
}

static double s2MinDistanceRad(const S2Point& s2Point, const MultiPointWithCRS& s2MultiPoint) {
    double minDistance = -1;
    for (const auto& point : s2MultiPoint.points) {
        double nextDistance = S2Distance::distanceRad(s2Point, point);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    return minDistance;
}

static double s2MinDistanceRad(const S2Point& s2Point, const MultiLineWithCRS& s2MultiLine) {
    double minDistance = -1;
    for (const auto& line : s2MultiLine.lines) {
        double nextDistance = S2Distance::minDistanceRad(s2Point, *line);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    return minDistance;
}

static double s2MinDistanceRad(const S2Point& s2Point, const MultiPolygonWithCRS& s2MultiPolygon) {
    double minDistance = -1;
    for (const auto& poly : s2MultiPolygon.polygons) {
        double nextDistance = S2Distance::minDistanceRad(s2Point, *poly);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    return minDistance;
}

static double s2MinDistanceRad(const S2Point& s2Point,
                               const GeometryCollection& geometryCollection) {
    double minDistance = -1;
    for (const auto& point : geometryCollection.points) {
        invariant(SPHERE == point.crs);
        double nextDistance = S2Distance::distanceRad(s2Point, point.point);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    for (const auto& line : geometryCollection.lines) {
        invariant(SPHERE == line->crs);
        double nextDistance = S2Distance::minDistanceRad(s2Point, line->line);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    for (const auto& poly : geometryCollection.polygons) {
        invariant(SPHERE == poly->crs);
        // We don't support distances for big polygons yet.
        invariant(nullptr != poly->s2Polygon);
        double nextDistance = S2Distance::minDistanceRad(s2Point, *poly->s2Polygon);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    for (const auto& multiPoint : geometryCollection.multiPoints) {
        double nextDistance = s2MinDistanceRad(s2Point, *multiPoint);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    for (const auto& multiLine : geometryCollection.multiLines) {
        double nextDistance = s2MinDistanceRad(s2Point, *multiLine);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    for (const auto& multiPoly : geometryCollection.multiPolygons) {
        double nextDistance = s2MinDistanceRad(s2Point, *multiPoly);
        if (minDistance < 0 || nextDistance < minDistance) {
            minDistance = nextDistance;
        }
    }

    return minDistance;
}

double GeometryContainer::minDistance(const PointWithCRS& otherPoint) const {
    const CRS crs = getNativeCRS();

    if (FLAT == crs) {
        invariant(NULL != _point);

        if (FLAT == otherPoint.crs) {
            return distance(_point->oldPoint, otherPoint.oldPoint);
        } else {
            S2LatLng latLng(otherPoint.point);
            return distance(_point->oldPoint,
                            Point(latLng.lng().degrees(), latLng.lat().degrees()));
        }
    } else {
        invariant(SPHERE == crs);

        double minDistance = -1;

        if (NULL != _point) {
            minDistance = S2Distance::distanceRad(otherPoint.point, _point->point);
        } else if (NULL != _line) {
            minDistance = S2Distance::minDistanceRad(otherPoint.point, _line->line);
        } else if (NULL != _polygon) {
            // We don't support distances for big polygons yet.
            invariant(NULL != _polygon->s2Polygon);
            minDistance = S2Distance::minDistanceRad(otherPoint.point, *_polygon->s2Polygon);
        } else if (NULL != _cap) {
            minDistance = S2Distance::minDistanceRad(otherPoint.point, _cap->cap);
        } else if (NULL != _multiPoint) {
            minDistance = s2MinDistanceRad(otherPoint.point, *_multiPoint);
        } else if (NULL != _multiLine) {
            minDistance = s2MinDistanceRad(otherPoint.point, *_multiLine);
        } else if (NULL != _multiPolygon) {
            minDistance = s2MinDistanceRad(otherPoint.point, *_multiPolygon);
        } else if (NULL != _geometryCollection) {
            minDistance = s2MinDistanceRad(otherPoint.point, *_geometryCollection);
        }

        invariant(minDistance != -1);
        return minDistance * kRadiusOfEarthInMeters;
    }
}

const CapWithCRS* GeometryContainer::getCapGeometryHack() const {
    return _cap.get();
}

}  // namespace mongo
