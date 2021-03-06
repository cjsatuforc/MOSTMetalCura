#ifndef UTILS_POLYGON_H
#define UTILS_POLYGON_H

#include <vector>
#include <assert.h>
#include <float.h>
#include <clipper/clipper.hpp>

#include <algorithm>    // std::reverse
#include <cmath> // fabs

#include "intpoint.h"

//#define CHECK_POLY_ACCESS
#ifdef CHECK_POLY_ACCESS
#define POLY_ASSERT(e) assert(e)
#else
#define POLY_ASSERT(e) do {} while(0)
#endif

namespace cura {

enum PolygonType
{
    NoneType,
    Inset0Type,
    InsetXType,
    SkinType,
    SupportType,
    SkirtType,
    InfillType,
    SupportInfillType
};

const static int clipper_init = (0);
#define NO_INDEX (std::numeric_limits<unsigned int>::max())

class PolygonRef
{
    ClipperLib::Path* polygon;
    PolygonRef()
    : polygon(nullptr)
    {}
public:
    PolygonRef(ClipperLib::Path& polygon)
    : polygon(&polygon)
    {}

    unsigned int size() const
    {
        return polygon->size();
    }

    Point& operator[] (unsigned int index) const
    {
        POLY_ASSERT(index < size());
        return (*polygon)[index];
    }

    void* data()
    {
        return polygon->data();
    }

    void add(const Point p)
    {
        polygon->push_back(p);
    }
    
    template <typename... Args>
    void emplace_back(Args... args)
    {
        polygon->emplace_back(args...);
    }

    void remove(unsigned int index)
    {
        POLY_ASSERT(index < size());
        polygon->erase(polygon->begin() + index);
    }

    void clear()
    {
        polygon->clear();
    }

    bool orientation() const
    {
        return ClipperLib::Orientation(*polygon);
    }

    void reverse()
    {
        ClipperLib::ReversePath(*polygon);
    }

    int64_t polygonLength() const
    {
        int64_t length = 0;
        Point p0 = (*polygon)[polygon->size()-1];
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            Point p1 = (*polygon)[n];
            length += vSize(p0 - p1);
            p0 = p1;
        }
        return length;
    }
    
    Point min() const
    {
        Point ret = Point(POINT_MAX, POINT_MAX);
        for(Point p : *polygon)
        {
            ret.X = std::min(ret.X, p.X);
            ret.Y = std::min(ret.Y, p.Y);
        }
        return ret;
    }
    
    Point max() const
    {
        Point ret = Point(POINT_MIN, POINT_MIN);
        for(Point p : *polygon)
        {
            ret.X = std::max(ret.X, p.X);
            ret.Y = std::max(ret.Y, p.Y);
        }
        return ret;
    }


    double area() const
    {
        return ClipperLib::Area(*polygon);
    }
    
    /*!
     * Translate the whole polygon in some direction.
     * 
     * \param translation The direction in which to move the polygon
     */
    void translate(Point translation)
    {
        for (Point& p : *this)
        {
            p += translation;
        }
    }

    Point centerOfMass() const
    {
        double x = 0, y = 0;
        Point p0 = (*polygon)[polygon->size()-1];
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            Point p1 = (*polygon)[n];
            double second_factor = (p0.X * p1.Y) - (p1.X * p0.Y);

            x += double(p0.X + p1.X) * second_factor;
            y += double(p0.Y + p1.Y) * second_factor;
            p0 = p1;
        }

        double area = Area(*polygon);
        
        x = x / 6 / area;
        y = y / 6 / area;

        return Point(x, y);
    }

    Point closestPointTo(Point p)
    {
        Point ret = p;
        float bestDist = FLT_MAX;
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            float dist = vSize2f(p - (*polygon)[n]);
            if (dist < bestDist)
            {
                ret = (*polygon)[n];
                bestDist = dist;
            }
        }
        return ret;
    }
    
    /*!
     * Check if we are inside the polygon. We do this by tracing from the point towards the positive X direction,
     * every line we cross increments the crossings counter. If we have an even number of crossings then we are not inside the polygon.
     * Care needs to be taken, if p.Y exactly matches a vertex to the right of p, then we need to count 1 intersect if the
     * outline passes vertically past; and 0 (or 2) intersections if that point on the outline is a 'top' or 'bottom' vertex.
     * The easiest way to do this is to break out two cases for increasing and decreasing Y ( from p0 to p1 ).
     * A segment is tested if pa.Y <= p.Y < pb.Y, where pa and pb are the points (from p0,p1) with smallest & largest Y.
     * When both have the same Y, no intersections are counted but there is a special test to see if the point falls
     * exactly on the line.
     * 
     * Returns false if outside, true if inside; if the point lies exactly on the border, will return 'border_result'.
     * 
     * \param p The point for which to check if it is inside this polygon
     * \param border_result What to return when the point is exactly on the border
     * \return Whether the point \p p is inside this polygon (or \p border_result when it is on the border)
     */
    bool inside(Point p, bool border_result=false)
    {
        PolygonRef thiss = *this;
        if (size() < 1)
        {
            return false;
        }
        
        int crossings = 0;
        Point p0 = back();
        for(unsigned int n=0; n<size(); n++)
        {
            Point p1 = thiss[n];
            // no tests unless the segment p0-p1 is at least partly at, or to right of, p.X
            if ( std::max(p0.X, p1.X) >= p.X )
            {
                int64_t pdY = p1.Y-p0.Y;
                if (pdY < 0) // p0->p1 is 'falling'
                {
                    if ( p1.Y <= p.Y && p0.Y > p.Y ) // candidate
                    {
                        // dx > 0 if intersection is to right of p.X
                        int64_t dx = (p1.X - p0.X) * (p1.Y - p.Y) - (p1.X-p.X)*pdY;
                        if (dx == 0) // includes p == p1
                        {
                            return border_result;
                        }
                        if (dx > 0)
                        {
                            crossings ++;
                        }
                    }
                }
                else if (p.Y >= p0.Y)
                {
                    if (p.Y < p1.Y) // candidate for p0->p1 'rising' and includes p.Y
                    {
                        // dx > 0 if intersection is to right of p.X
                        int64_t dx = (p1.X - p0.X) * (p.Y - p0.Y) - (p.X-p0.X)*pdY;
                        if (dx == 0) // includes p == p0
                        {
                            return border_result;
                        }
                        if (dx > 0)
                        {
                            crossings ++;
                        }
                    }
                    else if (p.Y == p1.Y)
                    {
                        // some special cases here, points on border:
                        // - p1 exactly matches p (might otherwise be missed)
                        // - p0->p1 exactly horizontal, and includes p.
                        // (we already tested std::max(p0.X,p1.X) >= p.X )
                        if (p.X == p1.X ||
                            (pdY==0 && std::min(p0.X,p1.X) <= p.X) )
                        {
                            return border_result;
                            // otherwise, count no crossings
                        }
                    }
                }
            }
            p0 = p1;
        }
        return (crossings % 2) == 1;
    }
    
    void smooth(int remove_length, PolygonRef result)
    {
        PolygonRef& thiss = *this;
        ClipperLib::Path* poly = result.polygon;
        if (size() > 0)
            poly->push_back(thiss[0]);
        for (unsigned int poly_idx = 1; poly_idx < size(); poly_idx++)
        {
            if (shorterThen(thiss[poly_idx-1]-thiss[poly_idx], remove_length))
            {
                poly_idx++; // skip the next line piece (dont escalate the removal of edges)
                if (poly_idx < size())
                    poly->push_back(thiss[poly_idx]);
            } else poly->push_back(thiss[poly_idx]);
        }
    }

    void simplify(int allowed_error_distance_squared, PolygonRef result) //!< removes consecutive line segments with same orientation
    {
        PolygonRef& thiss = *this;
        ClipperLib::Path* poly = result.polygon;
        
        if (size() < 4)
        {
            for (unsigned int poly_idx = 0; poly_idx < size(); poly_idx++)
                poly->push_back(thiss[poly_idx]);
            return;
        }
        
        Point& last = thiss[0];
        result.add(last);
        for (unsigned int poly_idx = 1; poly_idx < size(); poly_idx++)
        {
            /*
             *    /|
             * c / | a
             *  /__|
             *  \ b|
             * e \ | d
             *    \|
             * 
             * b^2 = c^2 - a^2
             * b^2 = e^2 - d^2
             * 
             * approximately: (this is asymptotically true for d -> 0)
             * a/d = c/e
             * a/(a+d) = c/(c+e)
             * a^2 / (a+d)^2 = c^2 / (c+e)^2
             * a^2 = c^2 * (a+d)^2/ (c+e)^2
             * 
             */
            if ( vSize2(thiss[poly_idx]-last) < allowed_error_distance_squared )
            {
                continue;
            }
            Point& next = thiss[(poly_idx+1) % size()];
            auto square = [](double in) { return in*in; };
            int64_t a2 = vSize2(next-thiss[poly_idx]) * vSize2(next-last) /  static_cast<int64_t>(square(vSizeMM(next-last) + vSizeMM(thiss[poly_idx]-last))*1000*1000);
            
            int64_t error2 = vSize2(next-thiss[poly_idx]) - a2;
            if (error2 < allowed_error_distance_squared)
            {
                // don't add the point to the result
            } else 
            {
                poly->push_back(thiss[poly_idx]);
                last = thiss[poly_idx];
            }
        }
        
        if (result.size() < 3)
        {
            poly->clear();
        
            for (unsigned int poly_idx = 0; poly_idx < size(); poly_idx++)
                poly->push_back(thiss[poly_idx]);
            return;
        }
    }

    void pop_back()
    { 
        polygon->pop_back();
    }
    
    ClipperLib::Path::reference back() const
    {
        return polygon->back();
    }
    
    ClipperLib::Path::iterator begin()
    {
        return polygon->begin();
    }

    ClipperLib::Path::iterator end()
    {
        return polygon->end();
    }

    ClipperLib::Path::const_iterator begin() const
    {
        return polygon->begin();
    }

    ClipperLib::Path::const_iterator end() const
    {
        return polygon->end();
    }

    friend class Polygons;
    friend class Polygon;
};

class Polygon : public PolygonRef
{
    ClipperLib::Path poly;
public:
    Polygon()
    : PolygonRef(poly)
    {
    }

    Polygon(const PolygonRef& other)
    : PolygonRef(poly)
    {
        poly = *other.polygon;
    }
};

class Polygons
{
private:
    ClipperLib::Paths polygons;
public:
    unsigned int size() const
    {
        return polygons.size();
    }

    PolygonRef operator[] (unsigned int index)
    {
        POLY_ASSERT(index < size());
        return PolygonRef(polygons[index]);
    }
    ClipperLib::Paths::iterator begin()
    {
        return polygons.begin();
    }
    ClipperLib::Paths::iterator end()
    {
        return polygons.end();
    }
    void remove(unsigned int index)
    {
        POLY_ASSERT(index < size());
        polygons.erase(polygons.begin() + index);
    }
    void clear()
    {
        polygons.clear();
    }
    void add(const PolygonRef& poly)
    {
        polygons.push_back(*poly.polygon);
    }
    void add(const Polygons& other)
    {
        for(unsigned int n=0; n<other.polygons.size(); n++)
            polygons.push_back(other.polygons[n]);
    }
    PolygonRef newPoly()
    {
        polygons.push_back(ClipperLib::Path());
        return PolygonRef(polygons[polygons.size()-1]);
    }
    PolygonRef back()
    {
        return polygons[polygons.size()-1];
    }

    Polygons() {}
    Polygons(const Polygons& other) { polygons = other.polygons; }
    Polygons& operator=(const Polygons& other) { polygons = other.polygons; return *this; }
    Polygons difference(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, ret.polygons);
        return ret;
    }
    Polygons unionPolygons(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptSubject, true);
        clipper.Execute(ClipperLib::ctUnion, ret.polygons, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        return ret;
    }
    Polygons intersection(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctIntersection, ret.polygons);
        return ret;
    }
    Polygons xorPolygons(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctXor, ret.polygons);
        return ret;
    }
    Polygons offset(int distance, ClipperLib::JoinType joinType = ClipperLib::jtMiter) const
    {
        Polygons ret;
        double miterLimit = 1.2;
        ClipperLib::ClipperOffset clipper(miterLimit, 10.0);
        clipper.AddPaths(polygons, joinType, ClipperLib::etClosedPolygon);
        clipper.MiterLimit = miterLimit;
        clipper.Execute(ret.polygons, distance);
        return ret;
    }
    
    Polygons smooth(int remove_length, int min_area) //!< removes points connected to small lines
    {
        Polygons ret;
        for (unsigned int p = 0; p < size(); p++)
        {
            PolygonRef poly(polygons[p]);
            if (poly.area() < min_area || poly.size() <= 5) // when optimally removing, a poly with 5 pieces results in a triangle. Smaller polys dont have area!
            {
                ret.add(poly);
                continue;
            }
            
            if (poly.size() == 0)
                continue;
            if (poly.size() < 4)
                ret.add(poly);
            else 
                poly.smooth(remove_length, ret.newPoly());
            

        }
        return ret;
    }
    
    Polygons simplify(int allowed_error_distance) //!< removes points connected to similarly oriented lines
    {
        int allowed_error_distance_squared = allowed_error_distance * allowed_error_distance;
        Polygons ret;
        Polygons& thiss = *this;
        for (unsigned int p = 0; p < size(); p++)
        {
            thiss[p].simplify(allowed_error_distance_squared, ret.newPoly());
        }
        return ret;
    }
    /*!
     * Split up the polygons into groups according to the even-odd rule.
     * Each polygons in the result has an outline as first polygon, whereas the rest are holes.
     */
    std::vector<Polygons> splitIntoParts(bool unionAll = false) const
    {
        std::vector<Polygons> ret;
        ClipperLib::Clipper clipper(clipper_init);
        ClipperLib::PolyTree resultPolyTree;
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        if (unionAll)
            clipper.Execute(ClipperLib::ctUnion, resultPolyTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        else
            clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

        _processPolyTreeNode(&resultPolyTree, ret);
        return ret;
    }
    /*!
     * Removes polygons with area smaller than \p minAreaSize (note that minAreaSize is in mm^2, not in micron^2).
     */
    void removeSmallAreas(double minAreaSize)
    {               
        Polygons& thiss = *this;
        for(unsigned int i=0; i<size(); i++)
        {
            double area = INT2MM(INT2MM(fabs(thiss[i].area())));
            if (area < minAreaSize) // Only create an up/down skin if the area is large enough. So you do not create tiny blobs of "trying to fill"
            {
                remove(i);
                i -= 1;
            }
        }
    }
    /*!
     * Removes overlapping consecutive line segments which don't delimit a positive area.
     */
    Polygons removeDegenerateVerts()
    {
        Polygons ret;
        for (PolygonRef poly : *this)
        {
            Polygon result;
            
            auto isDegenerate = [](Point& last, Point& now, Point& next)
            {
                Point last_line = now - last;
                Point next_line = next - now;
                return dot(last_line, next_line) == -1 * vSize(last_line) * vSize(next_line);
            };
            
            for (unsigned int idx = 0; idx < poly.size(); idx++)
            {
                Point& last = (result.size() == 0) ? poly.back() : result.back();
                if (idx+1 == poly.size() && result.size() == 0) { break; }
                Point& next = (idx+1 == poly.size())? result[0] : poly[idx+1];
                if ( isDegenerate(last, poly[idx], next) )
                { // lines are in the opposite direction
                    // don't add vert to the result
                    while (result.size() > 1 && isDegenerate(result[result.size()-2], result.back(), next) )
                    {
                        result.pop_back();
                    }
                }
                else 
                {
                    result.add(poly[idx]);
                }
            }
            
            if (result.size() > 2) {  ret.add(result); }
        }
        
        return ret;
    }
    /*!
     * Removes the same polygons from this set (and also empty polygons).
     * Polygons are considered the same if all points lie within [same_distance] of their counterparts.
     */
    Polygons remove(Polygons& to_be_removed, int same_distance = 0)
    {
        Polygons result;
        for (unsigned int poly_keep_idx = 0; poly_keep_idx < size(); poly_keep_idx++)
        {
            PolygonRef poly_keep = (*this)[poly_keep_idx];
            bool should_be_removed = false;
            if (poly_keep.size() > 0) 
//             for (int hole_poly_idx = 0; hole_poly_idx < to_be_removed.size(); hole_poly_idx++)
            for (PolygonRef poly_rem : to_be_removed)
            {
//                 PolygonRef poly_rem = to_be_removed[hole_poly_idx];
                if (poly_rem.size() != poly_keep.size() || poly_rem.size() == 0) continue;
                
                // find closest point, supposing this point aligns the two shapes in the best way
                int closest_point_idx = 0;
                int smallestDist2 = -1;
                for (unsigned int point_rem_idx = 0; point_rem_idx < poly_rem.size(); point_rem_idx++)
                {
                    int dist2 = vSize2(poly_rem[point_rem_idx] - poly_keep[0]);
                    if (dist2 < smallestDist2 || smallestDist2 < 0)
                    {
                        smallestDist2 = dist2;
                        closest_point_idx = point_rem_idx;
                    }
                }
                bool poly_rem_is_poly_keep = true;
                // compare the two polygons on all points
                if (smallestDist2 > same_distance * same_distance)
                    continue;
                for (unsigned int point_idx = 0; point_idx < poly_rem.size(); point_idx++)
                {
                    int dist2 = vSize2(poly_rem[(closest_point_idx + point_idx) % poly_rem.size()] - poly_keep[point_idx]);
                    if (dist2 > same_distance * same_distance)
                    {
                        poly_rem_is_poly_keep = false;
                        break;
                    }
                }
                if (poly_rem_is_poly_keep)
                {
                    should_be_removed = true;
                    break;
                }
            }
            if (!should_be_removed)
                result.add(poly_keep);
            
        }
        return result;
    }
            
private:
    void _processPolyTreeNode(ClipperLib::PolyNode* node, std::vector<Polygons>& ret) const
    {
        for(int n=0; n<node->ChildCount(); n++)
        {
            ClipperLib::PolyNode* child = node->Childs[n];
            Polygons polygons;
            polygons.add(child->Contour);
            for(int i=0; i<child->ChildCount(); i++)
            {
                polygons.add(child->Childs[i]->Contour);
                _processPolyTreeNode(child->Childs[i], ret);
            }
            ret.push_back(polygons);
        }
    }
public:
    Polygons processEvenOdd() const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.Execute(ClipperLib::ctUnion, ret.polygons);
        return ret;
    }

    int64_t polygonLength() const
    {
        int64_t length = 0;
        for(unsigned int i=0; i<polygons.size(); i++)
        {
            Point p0 = polygons[i][polygons[i].size()-1];
            for(unsigned int n=0; n<polygons[i].size(); n++)
            {
                Point p1 = polygons[i][n];
                length += vSize(p0 - p1);
                p0 = p1;
            }
        }
        return length;
    }
    
    Point min() const
    {
        Point ret = Point(POINT_MAX, POINT_MAX);
        for(const ClipperLib::Path& polygon : polygons)
        {
            for(Point p : polygon)
            {
                ret.X = std::min(ret.X, p.X);
                ret.Y = std::min(ret.Y, p.Y);
            }
        }
        return ret;
    }
    
    Point max() const
    {
        Point ret = Point(POINT_MIN, POINT_MIN);
        for(const ClipperLib::Path& polygon : polygons)
        {
            for(Point p : polygon)
            {
                ret.X = std::max(ret.X, p.X);
                ret.Y = std::max(ret.Y, p.Y);
            }
        }
        return ret;
    }

    bool inside(Point p)
    {
        if (size() < 1)
            return false;
        if (!(*this)[0].inside(p))
            return false;
        for(unsigned int n=1; n<polygons.size(); n++)
        {
            if ((*this)[n].inside(p))
                return false;
        }
        return true;
    }

    void applyMatrix(const PointMatrix& matrix)
    {
        for(unsigned int i=0; i<polygons.size(); i++)
        {
            for(unsigned int j=0; j<polygons[i].size(); j++)
            {
                polygons[i][j] = matrix.apply(polygons[i][j]);
            }
        }
    }
        
    void debugOutputHTML(const char* filename, bool dotTheVertices = false)
    {
        FILE* out = fopen(filename, "w");
        fprintf(out, "<!DOCTYPE html><html><body>");
        Point modelSize = max() - min();
        modelSize.X = std::max(modelSize.X, modelSize.Y);
        modelSize.Y = std::max(modelSize.X, modelSize.Y);
        Point modelMin = min();

        fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" style=\"width: 500px; height:500px\">\n");
        for(Polygons& parts : splitIntoParts())
        {
            for(unsigned int j=0;j<parts.size();j++)
            {
                Polygon poly = parts[j];
                fprintf(out, "<polygon points=\"");
                for(Point& p : poly)
                {
                    fprintf(out, "%f,%f ", float(p.X - modelMin.X)/modelSize.X*500, float(p.Y - modelMin.Y)/modelSize.Y*500);
                }
                if (j == 0)
                    fprintf(out, "\" style=\"fill:gray; stroke:black;stroke-width:1\" />\n");
                else
                    fprintf(out, "\" style=\"fill:red; stroke:black;stroke-width:1\" />\n");
                
                if (dotTheVertices)
                    for(Point& p : poly)
                        fprintf(out, "<circle cx=\"%f\" cy=\"%f\" r=\"2\" stroke=\"black\" stroke-width=\"3\" fill=\"black\" />", float(p.X - modelMin.X)/modelSize.X*500, float(p.Y - modelMin.Y)/modelSize.Y*500);
            }
        }
        fprintf(out, "</svg>\n");
        fprintf(out, "</body></html>");
        fclose(out);
    }
};

/* Axis aligned boundary box */
class AABB
{
public:
    Point min, max;

    AABB()
    : min(POINT_MIN, POINT_MIN), max(POINT_MIN, POINT_MIN)
    {
    }
    AABB(Polygons polys)
    : min(POINT_MIN, POINT_MIN), max(POINT_MIN, POINT_MIN)
    {
        calculate(polys);
    }

    void calculate(Polygons polys)
    {
        min = Point(POINT_MAX, POINT_MAX);
        max = Point(POINT_MIN, POINT_MIN);
        for(unsigned int i=0; i<polys.size(); i++)
        {
            for(unsigned int j=0; j<polys[i].size(); j++)
            {
                if (min.X > polys[i][j].X) min.X = polys[i][j].X;
                if (min.Y > polys[i][j].Y) min.Y = polys[i][j].Y;
                if (max.X < polys[i][j].X) max.X = polys[i][j].X;
                if (max.Y < polys[i][j].Y) max.Y = polys[i][j].Y;
            }
        }
    }

    bool hit(const AABB& other) const
    {
        if (max.X < other.min.X) return false;
        if (min.X > other.max.X) return false;
        if (max.Y < other.min.Y) return false;
        if (min.Y > other.max.Y) return false;
        return true;
    }
};

}//namespace cura

#endif//UTILS_POLYGON_H
