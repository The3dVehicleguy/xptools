
/*
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

//#include "GISTool_Globals.h"
#include "MapDefs.h"
#include "MeshAlgs.h"
#include "ParamDefs.h"
#include "CompGeomDefs2.h"
#include "CompGeomDefs3.h"
#include "CompGeomUtils.h"
#include "PolyRasterUtils.h"
#include <CGAL/Triangulation_conformer_2.h>
#include "MeshConformer.h"
#include "AssertUtils.h"
#include "PlatformUtils.h"
#include "PerfUtils.h"
#include "MapAlgs.h"
#include "DEMAlgs.h"
#include "DEMTables.h"
#include "GISUtils.h"
#include "GreedyMesh.h"
#if APL && !defined(__MACH__)
#define __DEBUGGING__
#include "XUtils.h"
#endif

typedef CGAL::Mesh_2::Is_locally_conforming_Delaunay<CDT>	LCP;

#if PHONE
#define LOW_RES_WATER_INTERVAL 50
#else
#define LOW_RES_WATER_INTERVAL 40
#endif

// Burn every road segment!
#define BURN_ROADS 0

// This adds more vertices to cliffs.
#define SPLIT_CLIFFS 0

// Don't do ANY borders - really only for debugging - when we want to see the mesh tri choice with NO borders (since wide borders can "swamp" a triangle).
#define NO_BORDERS_AT_ALL 0

// This disables borders from neighboring DSFs.  Generally you ALWAYS want border sharing on.  If you see weird borders along the edge of the DSF, turn
// this off to see if the neighboring DSF is causing them.
#define NO_BORDER_SHARING 0

// This causes the alg to print out timing of individual meshing steps.
#define PROFILE_PERFORMANCE 1

// This guarantees that we don't have "beached" triangles - that is, water trianglse where all 3 points are coastal, and thus the water depth is ZERO in the entire
// thing.
#if PHONE
	#define SPLIT_BEACHED_WATER 0
#else
	#define SPLIT_BEACHED_WATER 1
#endif

#define DEBUG_DROPPED_PTS 0

#if DEBUG_DROPPED_PTS
#include "GISTool_Globals.h"
#endif

#if PROFILE_PERFORMANCE
#define TIMER(x)	StElapsedTime	__PerfTimer##x(#x);
#else
#define TIMER(x)
#endif

#ifndef PHONE
#define PHONE 0
#endif

MeshPrefs_t gMeshPrefs = {		/*iphone*/
/* max_points		*/	PHONE ?		25000	: 78000,
/* max_error		*/	PHONE ?		15		: 5.0,
/* border_match		*/	PHONE ?		1		: 1,
/* optimize_borders	*/	PHONE ?		1		: 1,
/* max_tri_size_m	*/	PHONE ?		6000	: 1500,
/* rep_switch_m		*/	PHONE ?		50000	: 50000
};

/* Conststraint markers - here's the deal: the way we set the water body
 * triangles to water (precisely) is we remember the pairs of vertices that
 * make up their constrained edges.  These vertices are directed and form
 * a CCB, so we know that the left side of this pair is a triangle that is
 * wet.  This lets us seed the water-finding process. */

typedef pair<CDT::Vertex_handle, CDT::Vertex_handle>	ConstraintMarker_t;
typedef pair< Halfedge_handle,  Halfedge_handle>		LandusePair_t;			// "Left" and "right" side
typedef pair<ConstraintMarker_t, LandusePair_t>			LanduseConstraint_t;

struct FaceHandleVectorHack {
	vector<CDT::Face_handle>	v;
};

inline bool must_burn_he(Halfedge_handle he)
{
	Halfedge_handle tw = he->twin();
	Face_handle f1 = he->face();
	Face_handle f2 = tw->face();
	
	if(f1->is_unbounded() || f2->is_unbounded()) 
		return false;

#if BURN_ROADS
	if(!he->data().mSegments.empty() ||
		!he->twin()->data().mSegments.empty()) return true;
#endif
	
	return he->data().mParams.count(he_MustBurn) ||
		   tw->data().mParams.count(he_MustBurn) ||
		   f1->data().mTerrainType != f2->data().mTerrainType ||
		   f1->data().GetZoning() != f2->data().GetZoning();
}

inline bool collinear_he(Halfedge_handle he1, Halfedge_handle he2)
{
	DebugAssert(he1->target() == he2->source());
	if(!CGAL::collinear(he1->source()->point(), he1->target()->point(), he2->target()->point()))
		return false;
	if(!CGAL::collinear_are_ordered_along_line(he1->source()->point(), he1->target()->point(), he2->target()->point()))
		return false;
	return true;
}

bool collect_virtual_edge(CDT& mesh, CDT::Vertex_handle a, CDT::Vertex_handle b, vector<CDT::Vertex_handle>& out_pts);

inline bool	PersistentFindEdge(CDT& ioMesh, CDT::Vertex_handle a, CDT::Vertex_handle b, CDT::Face_handle& h, int& vnum)
{
	if (ioMesh.is_edge(a, b, h, vnum))
	{
		DebugAssert(ioMesh.is_constrained(CDT::Edge(h, vnum)));
		return true;
	}

	vector<CDT::Vertex_handle>	pts;
	if(!collect_virtual_edge(ioMesh, a,b,pts))
		AssertPrintf("Failed to collect edge.\n");

	Assert(pts.size() > 1);
	
	if (ioMesh.is_edge(pts[0],pts[1], h, vnum))
	{
		DebugAssert(ioMesh.is_constrained(CDT::Edge(h, vnum)));
		return true;
	}
	
	Assert(!"Failed persistent find edge.");
	return false;
}

inline bool IsEdgeVertex(CDT& inMesh, CDT::Vertex_handle v)
{
	CDT::Vertex_circulator circ, stop;
	circ = stop = inMesh.incident_vertices(v);
	do {
		if (inMesh.is_infinite(circ)) return true;
		circ++;
	} while (circ != stop);
	return false;
}

inline bool is_border(const CDT& inMesh, CDT::Face_handle f)
{
	for (int n = 0; n < 3; ++n)
	{
		if (f->neighbor(n)->has_vertex(inMesh.infinite_vertex()))
			return true;
	}
	return false;
}

inline void FindNextEast(CDT& ioMesh, CDT::Face_handle& ioFace, int& index, bool is_bot_edge)
{
	CDT::Vertex_handle sv = ioFace->vertex(index);
	CDT::Point p = sv->point();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	//printf("Starting with: %lf, %lf\n", CGAL::to_double(sv->point().x()), CGAL::to_double(sv->point().y()));

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		//printf("Checking: %lf, %lf\n", CGAL::to_double(now->point().x()), CGAL::to_double(now->point().y()));
		if (now != ioMesh.infinite_vertex())
		if (cy(now->point(), p) == CGAL::EQUAL)
		if (cx(now->point(), p) == CGAL::LARGER)
		{
			CDT::Face_handle	a_face;
			CDT::Vertex_circulator next = now;
			if(is_bot_edge)	++next;
			else			--next;
			Assert(ioMesh.is_face(sv, now, next, a_face));
			Assert(!ioMesh.is_infinite(a_face));
			ioFace = a_face;
			index = ioFace->index(now);
			return;
		}
		++now;
	} while (stop != now);
	AssertPrintf("Next mesh point not found.");
}

inline void FindNextNorth(CDT& ioMesh, CDT::Face_handle& ioFace, int& index, bool is_right_edge)
{
	CDT::Vertex_handle sv = ioFace->vertex(index);
	CDT::Point p = sv->point();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	//printf("Starting with: %lf, %lf\n", CGAL::to_double(sv->point().x()), CGAL::to_double(sv->point().y()));

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		//printf("Checking: %lf, %lf\n", CGAL::to_double(now->point().x()), CGAL::to_double(now->point().y()));
		if (now != ioMesh.infinite_vertex())
		if (cx(now->point(), p) == CGAL::EQUAL)
		if (cy(now->point(), p) == CGAL::LARGER)
		{
			CDT::Face_handle	a_face;
			CDT::Vertex_circulator next = now;
			if(is_right_edge)	++next;
			else				--next;
			Assert(ioMesh.is_face(sv, now, next, a_face));
			Assert(!ioMesh.is_infinite(a_face));
			ioFace = a_face;
			index = ioFace->index(now);
			return;
		}
		++now;
	} while (stop != now);
	Assert(!"Next pt not found.");
}

/*
// This builds the set of all continuous triangles that have the same variation of a terrain (a contiguous blob if you will)
void	FindAllCovariant(CDT& inMesh, CDT::Face_handle f, set<CDT::Face_handle>& all, Bbox_2& bounds)
{
	bounds = Point_2(f->vertex(0)->point().x(),f->vertex(0)->point().y()).bbox();
	bounds = bounds + Point_2(f->vertex(1)->point().x(),f->vertex(1)->point().y()).bbox();
	bounds = bounds + Point_2(f->vertex(2)->point().x(),f->vertex(2)->point().y()).bbox();

	all.clear();
	set<CDT::Face_handle>	working;
	working.insert(f);

	while (!working.empty())
	{
		CDT::Face_handle w = *working.begin();
		working.erase(working.begin());
		all.insert(w);

		for (int n = 0; n < 3; ++n)
		{
			bounds = bounds + Point_2(w->vertex(0)->point().x(),w->vertex(0)->point().y()).bbox();
			CDT::Face_handle t = w->neighbor(n);

			if (!inMesh.is_infinite(t))
			if (t->info().terrain != terrain_Water)
			if (AreVariants(t->info().terrain, w->info().terrain))
			if (all.count(t) == 0)
				working.insert(t);
		}
	}
}
*/

#pragma mark -
/************************************************************************************************************************
 * BORDER MATCHING
 ************************************************************************************************************************

	BORDER MATCHING - THEORY

	We cannot do proper blending and transitions across DSF borders because we write one DSF at a time - we have no way
	to go back and edit a previous DSF when we get to the next one and find a transition should have leaked across files.
	So instead we use a master slave system...the west and south files always dominate the north and east.

	The right and top borders of a DSF are MASTER borders and the left and bottom are SLAVES.

	When we write a DSF we write out the border info for the master borders into text files - this includes both vertex
	position along the border and texturing.

	When we write a new DSF we find our old master borders via text file and use it to conform our work.

	VERTEX MATCHING

	We write out all vertices on our master border.  For the slave border we add the MINIMUM number of points to the slave
	border - basically just mandatory water-body edges.  We then do a nearest-fit match from the master and add any non-
	matched master vertices to the slave.  This gives exact matchups except for mandatrory features which should be close.
	If the water bodies are not totally discontinuous this works.  X-plane can also resolve very slight vertex discrepancies.

	TRANSITION AND LANDUSE MATCHING

 	Each master edge vertex will contain some level of blending for each border that originates there as well as a set of
 	base transitions from each incident triangle.  (Each base layer can be thought of as being represented at 100%.)  When
 	sorted by priority this forms a total set of 'stuff' intruding from this vertex.

	To blend the border, we build overlays on the slave triangles incident to these borders that have the master's mix levels
	on the incident vertices and 0 levels on the interior.

	REBASING

	There is one problem with the above scheme - if the border from above is LOWER priority than the terrain it will cover,
	the border will not work.  Fundamentally we can't force a border to go left to right against priority!

	So we use a trick called "rebasing".  Basically given a slave tri with a high prio ("HIGH") and a master vertex with a
	low prio terrain ("LOW") we set the base of the slave tri to "LOW" and add a border of type "HIGH" to the slave with 0%
	blend on the edges and 100% in the interior.  We then also find all tris not touching the border incident to the 100% vertex
	and blend from 100% back to 0%.


 */


// This is one vertex from our master
struct	mesh_match_vertex_t {
	Point_2					loc;			// Location in master
	double					height;			// Height in master
	hash_map<int, float>	blending;		// List of borders and blends in master
	CDT::Vertex_handle		buddy;			// Vertex on slave that is matched to it
};

// This is one edge from our master
struct	mesh_match_edge_t {
	int						base;			// For debugging
	set<int>				borders;		// For debugging
	CDT::Face_handle		buddy;			// Tri in our mesh that corresponds
};

struct	mesh_match_t {
	vector<mesh_match_vertex_t>	vertices;
	vector<mesh_match_edge_t>	edges;
};

inline bool MATCH(const char * big, const char * msmall)
{
	return strncmp(big, msmall, strlen(msmall)) == 0;
}

static mesh_match_t gMatchBorders[4];


static void border_find_edge_tris(CDT& ioMesh, mesh_match_t& ioBorder)
{
//	printf("Finding edge tris for %d edgse.\n",ioBorder.edges.size());
	DebugAssert(ioBorder.vertices.size() == (ioBorder.edges.size()+1));
	for (int n = 0; n < ioBorder.edges.size(); ++n)
	{
#if DEV
		CDT::Point	p1 = ioBorder.vertices[n  ].buddy->point();
		CDT::Point	p2 = ioBorder.vertices[n+1].buddy->point();
#endif
		if (!(ioMesh.is_face(ioBorder.vertices[n].buddy, ioBorder.vertices[n+1].buddy, ioMesh.infinite_vertex(), ioBorder.edges[n].buddy)))
//		if (!(ioMesh.is_face(ioBorder.vertices[n+1].buddy, ioBorder.vertices[n].buddy, ioMesh.infinite_vertex(), ioBorder.edges[n].buddy)))
		{
/*
			CDT::Vertex_circulator	circ, stop;
			printf("    Vert 1 vert = %lf,%lf (0x%08X)\n", ioBorder.vertices[n].buddy->point().x(), ioBorder.vertices[n].buddy->point().y(), &*ioBorder.vertices[n].buddy);
			circ = stop = ioMesh.incident_vertices(ioBorder.vertices[n].buddy);
			do {
				printf("    Buddy 1 vert = %lf,%lf (0x%08X)\n", circ->point().x(), circ->point().y(), &*circ);
				++circ;
			} while (circ != stop);
			circ = stop = ioMesh.incident_vertices(ioBorder.vertices[n+ 1].buddy);
			printf("    Vert 2 vert = %lf,%lf (0x%08X)\n", ioBorder.vertices[n+ 1].buddy->point().x(), ioBorder.vertices[n+ 1].buddy->point().y(), &*ioBorder.vertices[n+ 1].buddy);
			do {
				printf("    Buddy 2 vert = %lf,%lf (0x%08X)\n", circ->point().x(), circ->point().y(), &*circ);
				++circ;
			} while (circ != stop);
			AssertPrintf("Border match failure: %lf,%lf to %lf,%lf\n",
				ioBorder.vertices[n  ].buddy->point().x(),
				ioBorder.vertices[n  ].buddy->point().y(),
				ioBorder.vertices[n+1].buddy->point().x(),
				ioBorder.vertices[n+1].buddy->point().y());
*/
		// BEN SEZ: this used to be an error but - there are cases where the SLAVE file has a lake ENDING at the edge...there is no way the MASTER
		// could have induced these pts, so we're screwed.  For now - we'll just blunder on.
			ioBorder.edges[n].buddy = CDT::Face_handle();
		} else {
			int idx = ioBorder.edges[n].buddy->index(ioMesh.infinite_vertex());
			ioBorder.edges[n].buddy = ioBorder.edges[n].buddy->neighbor(idx);
		}
	}
}

inline void AddZeroMixIfNeeded(CDT::Face_handle f, int layer)
{
	if (f->info().terrain == terrain_Water) return;
	DebugAssert(layer != -1);
	f->info().terrain_border.insert(layer);
	for (int i = 0; i < 3; ++i)
	{
		CDT::Vertex_handle vv = f->vertex(i);
		if (vv->info().border_blend.count(layer) == 0)
			vv->info().border_blend[layer] = 0.0;
	}
}

inline void ZapBorders(CDT::Vertex_handle v)
{
	for (hash_map<int, float>::iterator i = v->info().border_blend.begin(); i != v->info().border_blend.end(); ++i)
		i->second = 0.0;
}

static bool	load_match_file(const char * path, mesh_match_t& outLeft, mesh_match_t& outBottom, mesh_match_t& outRight, mesh_match_t& outTop)
{
	outTop.vertices.clear();
	outTop.edges.clear();
	outRight.vertices.clear();
	outRight.edges.clear();
	outBottom.vertices.clear();
	outBottom.edges.clear();
	outLeft.vertices.clear();
	outLeft.edges.clear();

	FILE * fi = fopen(path, "r");
	if (fi == NULL) return false;
	char buf[80];
	bool go = true;
	int count;
	float mix;
	char ter[80];
	double x, y;
	int token;

	for(int b = 0; b < 4; ++b)
	{
		go = true;
		mesh_match_t *	dest;
		switch(b) {
		case 0: dest = &outLeft;	break;
		case 1: dest = &outBottom;	break;
		case 2: dest = &outRight;	break;
		case 3: dest = &outTop;		break;
		}

		while (go)
		{
			if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
			if (MATCH(buf, "VT"))
			{
				dest->vertices.push_back(mesh_match_vertex_t());
				sscanf(buf, "VT %lf, %lf, %lf", &x, &y, &dest->vertices.back().height);
				dest->vertices.back().loc = Point_2(x,y);
				dest->vertices.back().buddy = NULL;
			}
			if (MATCH(buf, "VC"))
			{
				go = false;
				dest->vertices.push_back(mesh_match_vertex_t());
				sscanf(buf, "VT %lf, %lf, %lf", &x, &y, &dest->vertices.back().height);
				dest->vertices.back().loc = Point_2(x,y);
				dest->vertices.back().buddy = NULL;
			}
			if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
			sscanf(buf, "VBC %d", &count);
			while (count--)
			{
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "VB %f %s", &mix, ter);
				dest->vertices.back().blending[token=LookupToken(ter)] = mix;
				DebugAssert(token != -1);
			}
			if (go)
			{
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "TERRAIN %s", ter);
				dest->edges.push_back(mesh_match_edge_t());
				dest->edges.back().base = token=LookupToken(ter);
				DebugAssert(token != -1);
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "BORDER_C %d", &count);
				while (count--)
				{
					if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
					sscanf(buf, "BORDER_T %s", ter);
					dest->edges.back().borders.insert( token=LookupToken(ter));
					DebugAssert(token != -1);
				}
			}
		}
	}

	return true;

bail:
	outTop.vertices.clear();
	outTop.edges.clear();
	outRight.vertices.clear();
	outRight.edges.clear();
	outBottom.vertices.clear();
	outBottom.edges.clear();
	outLeft.vertices.clear();
	outLeft.edges.clear();
	fclose(fi);
	return false;
}

// Given a point on the left edge of the top border or top edge of the right border, this fetches all border
// points in order of distance from that origin.
void	fetch_border(CDT& ioMesh, const Point_2& origin, map<double, CDT::Vertex_handle>& outPts, int side_num)
{
	CDT::Vertex_handle sv = ioMesh.infinite_vertex();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	CDT::Point	pt(origin.x(), origin.y());

	outPts.clear();

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		double dist;
		if ((side_num == 0 || side_num == 2) && cx(now->point(), pt) == CGAL::EQUAL)
		{
			dist = CGAL::to_double(now->point().y() - origin.y());
			DebugAssert(outPts.count(dist)==0);
			outPts[dist] = now;
		}
		if ((side_num == 1 || side_num == 3) && cy(now->point(), pt) == CGAL::EQUAL)
		{
			dist = CGAL::to_double(now->point().x() - origin.x());
			DebugAssert(outPts.count(dist)==0);
			outPts[dist] = now;
		}

		++now;
	} while (stop != now);
}

// Border matching:
// We are going to go through a master edge from an old render and our slave render and try to correlate
// vertices.  This is a 3-part algorithm:
// 1. Find all of the slave edge points.
// 2. Match existing slave points with master points.
// 3. Induce any extra slave points as needed.

void	match_border(CDT& ioMesh, mesh_match_t& ioBorder, int side_num)
{
	map<double, CDT::Vertex_handle>	slaves;					// Slave map, from relative border offset to the handle.  Allows for fast slave location.
	Point_2	origin = ioBorder.vertices.front().loc;			// Origin of our tile.

	// Step 1.  Fetch the entire border from the mesh.
	fetch_border(ioMesh, origin, slaves, side_num);

	// Step 2. Until we have exhausted all of the slaves, we are going to try to find the neaerest master-slave pair and link them.

	while (!slaves.empty())
	{
		multimap<double, pair<double, mesh_match_vertex_t *> >	nearest;	// This is a slave/master pair - slave is IDed by its offset.

		// Go through each non-assigned vertex.
		for (vector<mesh_match_vertex_t>::iterator pts = ioBorder.vertices.begin(); pts != ioBorder.vertices.end(); ++pts)
		if (pts->buddy == NULL)
		{
			// Find the nearest slave for it by decreasing distance.
			for (map<double, CDT::Vertex_handle>::iterator sl = slaves.begin(); sl != slaves.end(); ++sl)
			{
				double myDist = (side_num == 0 || side_num == 2) ? (CGAL::to_double(pts->loc.y() - sl->second->point().y())) : (CGAL::to_double(pts->loc.x() - sl->second->point().x()));
				if (myDist < 0.0) myDist = -myDist;
				nearest.insert(multimap<double, pair<double, mesh_match_vertex_t *> >::value_type(myDist, pair<double, mesh_match_vertex_t *>(sl->first, &*pts)));
			}
		}

		// If we have not found a nearest pair, it means that we have assigned all masters to slaves and still have slaves left over!  This happens when we
		// cannot conform the border due to more water in the slave than master (or at least different water).  The most common case is the US-Canada border,
		// where the US is the master, Canada is the slave; because the US is not hydro-reconstructed it does not force the Canada border to water match.  We just
		// accept a discontinuity on the 49th parallel for now. :-(
		if (nearest.empty())
			break;

		// File off the match, and nuke the slave.
		pair<double, mesh_match_vertex_t *> best_match = nearest.begin()->second;
		DebugAssert(slaves.count(best_match.first) > 0);
		best_match.second->buddy = slaves[best_match.first];
		//printf("Matched: %lf,%lf to %lf,%lf\n",			CGAL::to_double(best_match.second->buddy->point().x()),
		//	   CGAL::to_double(best_match.second->buddy->point().y()),
		//	   CGAL::to_double(best_match.second->loc.x()),			CGAL::to_double(best_match.second->loc.y()));
		slaves.erase(best_match.first);

//		gMeshPoints.push_back(pair<Point2,Point3>(cgal2ben(best_match.second->buddy->point()	),Point3(1,1,0)));
//		gMeshPoints.push_back(pair<Point2,Point3>(cgal2ben(best_match.second->loc				),Point3(0,1,0)));
		
	}

	// Step 3.  Go through all unmatched masters and insert them diriectly into the mesh.
	CDT::Face_handle	nearf = NULL;
	for (vector<mesh_match_vertex_t>::iterator pts = ioBorder.vertices.begin(); pts != ioBorder.vertices.end(); ++pts)
	if (pts->buddy == NULL)
	{	
		//printf("Found no buddy for: %lf,%lf\n", CGAL::to_double(pts->loc.x()), CGAL::to_double(pts->loc.y()));
		pts->buddy = ioMesh.safe_insert(CDT::Point(CGAL::to_double(pts->loc.x()), CGAL::to_double(pts->loc.y())), nearf);
		nearf = pts->buddy->face();
		pts->buddy->info().height = pts->height;
//		gMeshPoints.push_back(pair<Point2,Point3>(cgal2ben(pts->loc),Point3(1,0,0)));
		
	}

	// At this point all masters have a slave, and some slaves may be connected to a master.
}

// RebaseTriangle -

inline bool has_no_xon(int tex1, int tex2)
{
	NaturalTerrainInfo_t& rec1(gNaturalTerrainInfo[tex1]);
	NaturalTerrainInfo_t& rec2(gNaturalTerrainInfo[tex2]);

	return rec1.xon_dist == 0.0 || rec2.xon_dist == 0.0;
}

static void RebaseTriangle(CDT& ioMesh, CDT::Face_handle tri, int new_base, CDT::Vertex_handle v1, CDT::Vertex_handle v2, set<CDT::Vertex_handle>& ioModVertices)
{
	int old_base = tri->info().terrain;

	if (old_base == terrain_Water || new_base == terrain_Water)
		return;
	if(has_no_xon(old_base,new_base))
		return;

	DebugAssert(new_base != terrain_Water);
	DebugAssert(tri->info().terrain != terrain_Water);
	tri->info().terrain = new_base;
	if (new_base != terrain_Water)
	{
		DebugAssert(old_base != -1);
		tri->info().terrain_border.insert(old_base);

		for (int i = 0; i < 3; ++i)
		{
			CDT::Vertex_handle v = tri->vertex(i);
			if (v == v1 || v == v2)
				v->info().border_blend[old_base] = max(v->info().border_blend[old_base], 0.0f);
			else {
				v->info().border_blend[old_base] = 1.0;
				ioModVertices.insert(v);
			}
		}
	}
}

// Safe-smear border: when we have a vertex involved in a border from a master file
// then we need to make sure all incident triangles can transition out1
void SafeSmearBorder(CDT& mesh, CDT::Vertex_handle vert, int layer)
{
	if (vert->info().border_blend[layer] > 0.0)
	{
		CDT::Face_circulator iter, stop;
		iter = stop = mesh.incident_faces(vert);
		do {
			if (!mesh.is_infinite(iter))
			if (iter->info().terrain != layer)
			if (iter->info().terrain != terrain_Water)
			{
				DebugAssert(layer != -1);
				iter->info().terrain_border.insert(layer);
				for (int n = 0; n < 3; ++n)
				{
					CDT::Vertex_handle v = iter->vertex(n);
					v->info().border_blend[layer] = max(0.0f, v->info().border_blend[layer]);
				}
			}
			++iter;
		} while (iter != stop);
	}
}

#pragma mark -
/************************************************************************************************************************
 * TRANSITIONS
 ************************************************************************************************************************/

inline int MAJORITY_RULES(int a, int b, int c, int d)
{
	int la = 1, lb = 1, lc = 1, ld = 1;
	if (a == b) ++la, ++lb;
	if (a == c) ++la, ++lc;
	if (a == d) ++la, ++ld;
	if (b == c) ++lb, ++lc;
	if (b == d) ++lb, ++ld;
	if (c == d) ++lc, ++ld;

	if (la >= lb && la >= lc && la >= ld) return a;
	if (lb >= la && lb >= lc && lb >= ld) return b;
	if (lc >= la && lc >= lb && lc >= ld) return c;
	if (ld >= la && ld >= lb && ld >= lc) return d;
	return a;
}

inline float SAFE_AVERAGE(float a, float b, float c)
{
	int i = 0;
	float t = 0.0;
	if (a != DEM_NO_DATA) t += a, ++i;
	if (b != DEM_NO_DATA) t += b, ++i;
	if (c != DEM_NO_DATA) t += c, ++i;
	if (i == 0) return DEM_NO_DATA;
	return t / i;
}

inline float SAFE_MAX(float a, float b, float c)
{
	return max(a, max(b, c));
}

inline double GetXonDist(int layer1, int layer2, double y_normal)
{
	NaturalTerrainInfo_t& rec1(gNaturalTerrainInfo[layer1]);
	NaturalTerrainInfo_t& rec2(gNaturalTerrainInfo[layer2]);

#if DEV
	const char * t1 = FetchTokenString(layer1);
	const char * t2 = FetchTokenString(layer2);
#endif

	double dist_1 = rec1.xon_dist;
	double dist_2 = rec2.xon_dist;

	double base_dist = min(dist_1, dist_2);

	return base_dist * y_normal;
}



inline double	DistPtToTri(CDT::Vertex_handle v, CDT::Face_handle f)
{
	// Find the closest a triangle comes to a point.  Inputs are in lat/lon, otuput is in meters!
	Point2		vp(cgal2ben(v->point()));
	Point2		tp1(cgal2ben(f->vertex(0)->point()));
	Point2		tp2(cgal2ben(f->vertex(1)->point()));
	Point2		tp3(cgal2ben(f->vertex(2)->point()));

	double	DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(vp.y()) * DEG_TO_RAD);
	tp1.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp1.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	tp2.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp2.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	tp3.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp3.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	 vp.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	 vp.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);

	Segment2	s1(tp1, tp2);
	Segment2	s2(tp2, tp3);
	Segment2	s3(tp3, tp1);

	// BEN SAYS: squared dist to line is the dist to the supporting line if we are withn the interval, or dist to vertices
	// otherwise.  So this is good enough to do the whole tri.  This is INCORRECT for pts in the INTERIOR of the tri, but 
	// we have to trust CGAL's mesh to never give us a point INSIDE the tri!
	double d1 = s1.squared_distance(vp);
	double d2 = s2.squared_distance(vp);
	double d3 = s3.squared_distance(vp);

	return sqrt(min(min(d1,d2),d3));
}







#pragma mark -
/***************************************************************************
 * ALGORITHMS TO FIND VALUABLE POINTS IN A DEM *****************************
 ***************************************************************************
 *
 * These routines take a fully populated DEM and copy points of interest into
 * an empty DEM to build up a small number of points we can use to triangulate.
 * 'orig' is always the main DEM and 'deriv' the sparse one.  The goal is to
 * get about 20,000-30,000 points that provide good coverage and capture the
 * terrain morphology.
 */

/*
 * InsertDEMPoint - insert one point from the DEM into the mesh.
 *
 */
CDT::Vertex_handle InsertDEMPoint(
				const DEMGeo&			in_orig,
					  DEMMask&			io_used,
					  CDT&				io_mesh,
					  int				x,
					  int				y,
					  CDT::Face_handle&	hint)
{
	float h = in_orig.get(x,y);
	DebugAssert(h != DEM_NO_DATA);
	
	CDT::Point	p(in_orig.x_to_lon(CGAL::to_double(x)),in_orig.y_to_lat(CGAL::to_double(y)));

	CDT::Vertex_handle np = io_mesh.insert(p, hint);
	np->info().height = h;
	hint = np->face();
	
	io_used.set(x,y,true);
	
	return np;
}

/*
 * InsertAnyPoint - insert a non-DEM-aligned point into the mesh with interpolation.
 *
 */
CDT::Vertex_handle InsertAnyPoint(
			const DEMGeo&			in_orig,
			CDT&					io_mesh,
			const Point_2&			p,
			CDT::Face_handle&		hint)
{
	float e;
	e = in_orig.value_linear(CGAL::to_double(p.x()), CGAL::to_double(p.y()));
	if (e == DEM_NO_DATA)
		e = in_orig.xy_nearest(CGAL::to_double(p.x()), CGAL::to_double(p.y()));

	DebugAssert(e != DEM_NO_DATA);

	CDT::Vertex_handle v = io_mesh.insert(p, hint);
	hint = v->face();
	v->info().height = e;
	
	return v;
}


/*
 * CopyWetPoints
 *
 * This routine inserts every Nth point that is inside water into the mesh.  This is used
 * to put a sparse mesh inside the water areas.
 *
 */
double CopyWetPoints(
				const DEMGeo&			in_orig,
					  DEMMask&			io_used,
					  CDT&				io_mesh,
					  int				in_skip,
				const Pmwx& 			map)		// The map we get the water bodies from
{
	// BEN NOTE ON CLAMPING: I think we do NOT care if an edge is microscopically outside the DEM
	// in this case...xy_nearest could care less...and the polygon rasterizer doesn't care much
	// either.  We do not generate any coastline edges here.

	PolyRasterizer	rasterizer;
	SetupWaterRasterizer(map, in_orig, rasterizer);

	CDT::Face_handle	hint;

	int total = in_orig.mWidth * in_orig.mHeight;
	int wet = 0;

	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for(int x = x1; x < x2; ++x)
			{
				if((x % in_skip == 0) && (y % in_skip == 0))
					InsertDEMPoint(in_orig, io_used,io_mesh,x,y,hint);
				++wet;
			}
		}
		// Yeah we could be more clever about modulus in the Y axis, but..the rasterizer might
		// be unhappy skipping scanlines with "events" on them.
		++y;
		if (y >= in_orig.mHeight) break;
		rasterizer.AdvanceScanline(y);
	}
	
	return (double) wet / (double) total;
}

/*
 * AddEdgePoints
 *
 * This function adds the edges to the DEMs, at the interval specified.
 *
 */
void AddEdgePoints(
			const DEMGeo& 		orig, 			// The original DEM
			DEMMask& 			deriv, 			// Edge points are added to this
			int 				interval,		// The interval - add an edge point once every N points.
			int					divisions,		// Number of divisions - 1 means 1 big, "2" means 4 parts, etc.
			bool				has_border[4],	// Useful in making sure our borders match up.
			CDT&				mesh)
{

	int	div_skip_x = (deriv.mWidth-1) / divisions;
	int	div_skip_y = (deriv.mHeight-1) / divisions;
	int x, y, dx, dy;
	bool has_left = has_border[0];
	bool has_bottom = has_border[1];
	bool has_right = has_border[2];
	bool has_top = has_border[3];

	CDT::Face_handle hint;

	for (x = (has_left ? div_skip_x : 0); x < (deriv.mWidth - (has_right ? div_skip_x : 0)); x += div_skip_x)
	for (dy = 0; dy < deriv.mHeight; dy += interval)
	{
		InsertDEMPoint(orig, deriv, mesh, x, dy, hint);
	}

	for (y = (has_bottom ? div_skip_y : 0); y < (deriv.mHeight - (has_top ? div_skip_y : 0)) ; y += div_skip_y)
	for (dx = 0; dx < deriv.mWidth; dx += interval)
	{
		InsertDEMPoint(orig, deriv, mesh, dx, y, hint);
	}
	
	if(has_left || has_right)
	for(y = 0; y < orig.mHeight; ++y)
	{
		if(has_left ) deriv.set(0			  ,y,true);
		if(has_right) deriv.set(deriv.mWidth-1,y,true);
	}

	if(has_bottom || has_top)
	for(x = 0; x < orig.mWidth; ++x)
	{
		if(has_bottom) deriv.set(x,0			  ,true);
		if(has_top   ) deriv.set(x,deriv.mHeight-1,true);
	}
}

// Given a halfedge of the map that needs to be "burned" into the mesh, this attempts to find the farthest "extension" 
// we can make by:
// - only going to halfedges in the same collinear direction
// - only going through connected vertices
// - only going through other need-burn halfedges
// - stopping when we hit an edge we already marked
// - stopping if there are crossing burn-in edges.
// The result is that given a single coastline edge from source data that was split by a crossing overlay that is not burned
// (e.g. a bridge crossing a coastline) we will consolidate the coastline back into one edge, reducing start mesh complexity.
static Halfedge_handle ExtendLanduseEdge(Halfedge_handle start)
{
	Pmwx::Halfedge_around_vertex_circulator	circ, stop;
	Halfedge_handle next;

	Halfedge_handle best = start;
	
	while(1)
	{
		best->data().mMark = true;
		Vertex_handle v = best->target();
		
		Pmwx::Halfedge_around_vertex_circulator circ, stop;
		circ = stop = v->incident_halfedges();
		
		Halfedge_handle new_best = Halfedge_handle();
		do {
			Halfedge_handle cand = circ->twin();
			
			if(must_burn_he(cand) && circ != best)						// For every outgoing edge that gets burned and isn't us
			{
				if(cand->data().mMark || cand->twin()->data().mMark)	// Already burned?  Can't extend, we're done.
					return best;
				else if (new_best != Halfedge_handle())					// Two best edges?  T junction, we're done.
					return best;
				else
					new_best = cand;
			}
			
		} while(++circ != stop);

		if(new_best == Halfedge_handle())			// Didn't hit ANY "next" edge?  We're done.
			return best;
			
		if(!collinear_he(best,new_best))			// Next edge is a turn?  Can't consolidate out this pt.
			return best;

		best = new_best;
		
//		debug_mesh_point(cgal2ben(best->source()->point()),1,0,1);
	}
}

/*
 * Given a start and end point, collect all mesh points along the straight edge of the mesh.  Return true
 * if the edge is made entirely of sub-edges (or false if we must cut through the middle of a triangle).  Since 
 * constraints can be cut, this gievs us a way to do a colinear test. */
bool collect_virtual_edge(CDT& mesh, CDT::Vertex_handle a, CDT::Vertex_handle b, vector<CDT::Vertex_handle>& out_pts)
{
	Face_handle h;
	int vnum;
	if (mesh.is_edge(a, b)) 
	{
		out_pts.push_back(a);
		out_pts.push_back(b);
		return true;
	}

	Point_2 pa = a->point();
	Point_2 pb = b->point();
	
	out_pts.push_back(a);
	
	CDT::Vertex_handle s = a;
	
	while(1)
	{
		CDT::Vertex_handle	n = CDT::Vertex_handle();
		CDT::Vertex_circulator circ,stop;
		circ = stop = s->incident_vertices();
		
		do {
			if(!mesh.is_infinite(circ))
			{
				Point_2 pc = circ->point();
				
				if(CGAL::collinear(pa,pc,pb) &&
					CGAL::collinear_are_ordered_along_line(s->point(),pc,pb))	// Require in-order from the current point or else we will take the halfedge going backward.
				{
					n = circ;
					break;
				}
			}
		} while(++circ != stop);
		
		if(n == CDT::Vertex_handle())
			return false;
		
		if(n == b) break;
		
		out_pts.push_back(n);
		s = n;		
	}
	
	out_pts.push_back(b);
	return true;
}

/* This routine atttempts to determine if a constraint needs to be split.  For now, split if the mesh err gets too high along the constraint.
 * But don't try to split long thin tris - that code does not work. */
static bool needs_split(CDT& mesh, const DEMGeo& elev, CDT::Vertex_handle a, CDT::Vertex_handle b, Point_2& candidate, float err)
{
	return false;
	Point_2 pa(a->point());
	Point_2 pb(b->point());
	
	candidate = CGAL::midpoint(pa,pb);
	
	float h1 = elev.value_linear(CGAL::to_double(pa.x()),CGAL::to_double(pa.y()));
	float h2 = elev.value_linear(CGAL::to_double(pb.x()),CGAL::to_double(pb.y()));
	float hc = elev.value_linear(CGAL::to_double(candidate.x()),CGAL::to_double(candidate.y()));
	
	float ha = (h1 + h2) * 0.5;
	if (fabs(ha - hc) > err)
		return true;
	
	return false;
	/*
	// This tries to split long thin tris but never worked well.
	CDT::Face_handle	f1,f2;
	int					n1,n2;
	if(!mesh.is_edge(a,b,f1,n1))
	{
		DebugAssert(!"Not an edge?");
	}
	f2 = f1->neighbor(n1);
	n2 = f2->index(f1);
	
	Line_2 s(pa,pb);
	NT ll(CGAL::squared_distance(pa,pb));
	
	NT dl = CGAL::min(
		CGAL::squared_distance(s, f1->vertex(n1)->point()),
		CGAL::squared_distance(s, f2->vertex(n2)->point()));

	double l = sqrt(CGAL::to_double(ll)) * DEG_TO_MTR_LAT;
	double d = sqrt(CGAL::to_double(dl)) * DEG_TO_MTR_LAT;

	if(d < err || l < err) return false;

	return (l / d) > 20.0;
	*/
}

/* This burns the constraints into the mesh, splitting them as needed to reduce error. */
void	SplitConstraints(
				CDT&								io_mesh,
				const DEMGeo&						elev,
				const vector<LanduseConstraint_t>&	cons,
				float								max_err)
{
	int total = 0;
	list<ConstraintMarker_t>	queue;
	
	for(vector<LanduseConstraint_t>::const_iterator c = cons.begin(); c != cons.end(); ++c)
		queue.push_back(c->first);
		
	while(!queue.empty())
	{
		ConstraintMarker_t m = queue.front();
		queue.pop_front();
		io_mesh.insert_constraint(m.first,m.second);
		
		vector<CDT::Vertex_handle> actual_pts;
	
		DebugAssert(m.first->point() != m.second->point());
		
		if(!collect_virtual_edge(io_mesh, m.first,m.second,actual_pts))
		{
			DebugAssert(!"Invalid constraint");
		}
		
		CDT::Face_handle hint;
		
		for(int n = 1; n < actual_pts.size(); ++n)
		{
			Point_2 candidate;
			if (needs_split(io_mesh, elev, actual_pts[n-1], actual_pts[n], candidate, max_err))
			{
				CDT::Vertex_handle v =  InsertAnyPoint(elev, io_mesh, candidate, hint);
//				debug_mesh_point(cgal2ben(candidate), 1,1,0);
				++total;
				queue.push_back(ConstraintMarker_t(actual_pts[n-1], v));
				queue.push_back(ConstraintMarker_t(v, actual_pts[n]));
			}
		}
	}
	printf("Added %d vertices to reduce error on constraints.\n",total);
}

/*
 * AddConstraintPoints
 *
 * This routine calculates the constraints we need based on burned-in edges from the map, water, etc.
 * It inserts the vertices of the constraints now (so that triangulation can take advantage of them) and returns
 * the pairs that become constraints.  We don't add constraints now as that would screw up the quality of triangulation.
 *
 */
void	AddConstraintPoints(
				Pmwx& 								inMap, 		// Vec Map of waterbodies
				const DEMGeo& 						master, 	// Master DEM with elevations
				CDT& 								outMesh, 	// Vertices and constraints added to this mesh
				vector<LanduseConstraint_t>&		outCons)	// The constraints we add for water are added here for later use
{
	/*******************************************************************************************
	 * FIND POLYGON GROUPS THAT CONTAIN LAND USE DIFFERENCES
	 *******************************************************************************************/

	// We are going to go through the whole map and find every halfedge that represents a real land use
	// change.

		CDT::Face_handle	locale = CDT::Face_handle();	// For cache coherency
		CDT::Vertex_handle	v1, v2;
		float				e1, e2;

		Pmwx::Halfedge_iterator he;

	for (he = inMap.halfedges_begin(); he != inMap.halfedges_end(); ++he)
		he->data().mMark = false;

	for (he = inMap.halfedges_begin(); he != inMap.halfedges_end(); ++he)
	if (!he->twin()->data().mMark)
	if (!he->data().mMark)
	{
		Pmwx::Face_const_handle	f1 = he->face();
		Pmwx::Face_const_handle	f2 = he->twin()->face();
		if (must_burn_he(he))
		{
			DebugAssert(!f1->is_unbounded());
			DebugAssert(!f2->is_unbounded());
			
			Halfedge_handle extended1 = ExtendLanduseEdge(he);
			Halfedge_handle extended2 = ExtendLanduseEdge(he->twin());

			v1 = InsertAnyPoint(master, outMesh, extended2->target()->point(), locale);
			v2 = InsertAnyPoint(master, outMesh, extended1->target()->point(), locale);

			outCons.push_back(LanduseConstraint_t(ConstraintMarker_t(v1,v2),LandusePair_t(he, he->twin())));
		}
	}
}

/* This routine sets the feature type for the mesh tris from the terrain that required burn-in for constraints.
 * This is how we know that our water tris should be wet.  We set every tri on the border of a constraint, then 
 * flood-fill.*/
void	SetTerrainForConstraints(CDT& ioMesh, vector<LanduseConstraint_t>& inCoastlines, const DEMGeo& allPts)
{
	set<CDT::Face_handle>		wet_faces;
	set<CDT::Face_handle>		visited;

	// Quick pass - set everyone to natural.   This is needed because if there are no polys,
	// then the outside of those polys won't make natural terrain.

	for (CDT::Finite_faces_iterator ffi = ioMesh.finite_faces_begin(); ffi != ioMesh.finite_faces_end(); ++ffi)
	{
		ffi->info().terrain = terrain_Natural;
		ffi->info().feature = NO_VALUE;
	}

	// Next mark every point on a tri that's just inside as hot unless it's also an edge point.
	// Also mark these tris as wet.
	for (vector<LanduseConstraint_t>::iterator c = inCoastlines.begin(); c != inCoastlines.end(); ++c)
	{
		CDT::Face_handle	face_h;
		int					vnum;
		// Dig up the face that includes our edge.  is_edge gives us the right-hand side triangle, but we want
		// the left since this is a counter clockwise boundary, so go backward on the constraint.

		if (!PersistentFindEdge(ioMesh, c->first.second, c->first.first, face_h, vnum))
		{
			AssertPrintf("ASSERTION FAILURE: constraint not an edge.\n");
		} else {
			face_h->info().terrain = c->second.first->face()->data().mTerrainType;
			face_h->info().feature = c->second.first->face()->data().mTerrainType;
			// BEN SEZ: we will get conflicts on origin faces!  imagine water tries separated by a bridge - WED thinks they're
			// al the same but they're not.
//			DebugAssert(face_h->info().orig_face == NULL || face_h->info().orig_face == c->second.first->face());
			if (face_h->info().orig_face == Face_handle())
				face_h->info().orig_face = c->second.first->face();
			wet_faces.insert(face_h);
		}

		if (!PersistentFindEdge(ioMesh, c->first.first, c->first.second, face_h, vnum))
		{
			AssertPrintf("ASSERTION FAILURE: constraint not an edge.\n");
		} else {
			face_h->info().terrain = c->second.second->face()->data().mTerrainType;
			face_h->info().feature = c->second.second->face()->data().mTerrainType;
//			DebugAssert(face_h->info().orig_face == NULL || face_h->info().orig_face == c->second.second->face());
			if (face_h->info().orig_face == Face_handle())
				face_h->info().orig_face = c->second.second->face();

			wet_faces.insert(face_h);
		}
	}


	while (!wet_faces.empty())
	{
		CDT::Face_handle f = *wet_faces.begin();
		wet_faces.erase(f);
		visited.insert(f);

		int tg = f->info().terrain;
		const Face_handle of = f->info().orig_face;
		f->info().flag = 0;
		CDT::Face_handle	fn;
		for (int vi = 0; vi < 3; ++ vi)
		if (!ioMesh.is_constrained(CDT::Edge(f,vi)))
		{
			fn = f->neighbor(vi);
			if (!ioMesh.is_infinite(fn))
			if (visited.find(fn) == visited.end())
			{
				if (fn->info().terrain != terrain_Natural && fn->info().terrain != tg) {
					printf("Error: conflicting terrain assignment between %s and %s, near %lf, %lf\n",
							FetchTokenString(fn->info().terrain), FetchTokenString(tg),
							CGAL::to_double(f->vertex(vi)->point().x()), CGAL::to_double(f->vertex(vi)->point().y()));
				} else {
				fn->info().terrain = tg;
				fn->info().feature = tg;
				}
				if (fn->info().orig_face == Face_handle()) fn->info().orig_face = of;
				wet_faces.insert(fn);
			}
		}
	}

	for (CDT::Finite_faces_iterator ffi = ioMesh.finite_faces_begin(); ffi != ioMesh.finite_faces_end(); ++ffi)
	if (ffi->info().terrain == terrain_Water)
	for (int vi = 0; vi < 3; ++vi)
	{
		int xw, yw;
		float e = allPts.xy_nearest(CGAL::to_double(ffi->vertex(vi)->point().x()),CGAL::to_double(ffi->vertex(vi)->point().y()), xw, yw);

		//e = allPts.get_lowest_heuristic(xw, yw, 5);
		if (e != DEM_NO_DATA)
			ffi->vertex(vi)->info().height = e;
	}
}

/* Calculate the normal of one face. */
inline Vector3 CalculateMeshNormal(CDT::Face_handle f)
{
	Point3	p1(CGAL::to_double(f->vertex(0)->point().x()), CGAL::to_double(f->vertex(0)->point().y()), f->vertex(0)->info().height);
	Point3	p2(CGAL::to_double(f->vertex(1)->point().x()), CGAL::to_double(f->vertex(1)->point().y()), f->vertex(1)->info().height);
	Point3	p3(CGAL::to_double(f->vertex(2)->point().x()), CGAL::to_double(f->vertex(2)->point().y()), f->vertex(2)->info().height);

	Vector3 v1(p1,p2);
	Vector3 v2(p1,p3);
	v1.dx *= (DEG_TO_MTR_LAT * cos(p1.y * DEG_TO_RAD));
	v2.dx *= (DEG_TO_MTR_LAT * cos(p1.y * DEG_TO_RAD));
	v1.dy *= (DEG_TO_MTR_LAT);
	v2.dy *= (DEG_TO_MTR_LAT);
				
	if((v1.dx == 0.0 && v1.dy == 0.0 && v1.dz == 0.0) ||
	   (v2.dx == 0.0 && v2.dy == 0.0 && v2.dz == 0.0))
	{
		return Vector3(0,0,1);
	}
	else 
	{
	
		v1.normalize();
		v2.normalize();
		Vector3 normal(v1.cross(v2));
		if(normal.dz <= 0.0)
		{
			return Vector3(0,0,1);
		} 
		else {
			normal.normalize();
			return normal;
		}
	}
}

/* Is this triangle a cliff?  For special hadnling. */
inline bool tri_is_cliff(CDT& io_mesh, CDT::Face_handle f)
{
	if(io_mesh.is_infinite(f)) return false;
	Vector3 n = CalculateMeshNormal(f);
	return n.dz < 0.7;
}

/*
 * CalculateMeshNormals
 *
 * This routine calcs the normals per vertex.
 *
 */
void CalculateMeshNormals(CDT& ioMesh)
{
	for (CDT::Finite_vertices_iterator i = ioMesh.finite_vertices_begin(); i != ioMesh.finite_vertices_end(); ++i)
	{
		Vector3	total(0.0, 0.0, 0.0);
		CDT::Vertex_circulator last = ioMesh.incident_vertices(i);
		CDT::Vertex_circulator nowi = last, stop = last;
		Point3	selfP(CGAL::to_double(i->point().x()), CGAL::to_double(i->point().y()), i->info().height);

		do {
			last = nowi;
			++nowi;
			if(!ioMesh.is_infinite(last) && !ioMesh.is_infinite(nowi))
			{

                Point3  lastP(CGAL::to_double(last->point().x()), CGAL::to_double(last->point().y()), last->info().height);
                Point3  nowiP(CGAL::to_double(nowi->point().x()), CGAL::to_double(nowi->point().y()), nowi->info().height);
                Vector3 v1(selfP, lastP);
                Vector3 v2(selfP, nowiP);
                v1.dx *= (DEG_TO_MTR_LAT * cos(selfP.y * DEG_TO_RAD));
                v2.dx *= (DEG_TO_MTR_LAT * cos(selfP.y * DEG_TO_RAD));
                v1.dy *= (DEG_TO_MTR_LAT);
                v2.dy *= (DEG_TO_MTR_LAT);
				
				Vector3 normal;
				
				if((v1.dx == 0.0 && v1.dy == 0.0 && v1.dz == 0.0) ||
				   (v2.dx == 0.0 && v2.dy == 0.0 && v2.dz == 0.0))
				{
					normal = Vector3(0,0,1);
				}
				else 
				{
				
					v1.normalize();
					v2.normalize();
					normal = v1.cross(v2);
					if(normal.dz <= 0.0)
					{
						normal = Vector3(0,0,1);
					} 
					else
						normal.normalize();

				}

				CDT::Face_handle	a_face;
				if (ioMesh.is_face(i, last, nowi, a_face))
				{
                    a_face->info().normal[0] = normal.dx;
                    a_face->info().normal[1] = normal.dy;
                    a_face->info().normal[2] = normal.dz;
				}
				total = total + normal;
			}
		} while (nowi != stop);
        DebugAssert(total.dx != 0.0 || total.dy != 0.0 || total.dz != 0.0);
        DebugAssert(total.dz > 0.0);
        total.normalize();
        i->info().normal[0] = total.dx;
        i->info().normal[1] = total.dy;
        i->info().normal[2] = total.dz;
	}
}

/*******************************************************************************************
 *******************************************************************************************
 ** GENERATION OF A MESH MASTER ROUTINE ****************************************************
 *******************************************************************************************
 *******************************************************************************************/







void	TriangulateMesh(Pmwx& inMap, CDT& outMesh, DEMGeoMap& inDEMs, const char * mesh_folder, ProgressFunc prog)
{
	TIMER(Total)
	outMesh.clear();

	int		x, y;
	DEMGeo&	orig(inDEMs[dem_Elevation]);

	Assert(orig.get(0			 ,0				) != DEM_NO_DATA);
	Assert(orig.get(orig.mWidth-1,orig.mHeight-1) != DEM_NO_DATA);
	Assert(orig.get(0			 ,orig.mHeight-1) != DEM_NO_DATA);
	Assert(orig.get(orig.mWidth-1,orig.mHeight-1) != DEM_NO_DATA);

	DEMMask	deriv(orig.mWidth, orig.mHeight,false);					// A mash-up of points we will add to the final mesh.
	deriv.copy_geo_from(orig);

	vector<LanduseConstraint_t>	coastlines_markers;

	/* LOAD SLAVED EDGES */

	bool	has_borders[4];
	
	{
		// This adds edge points to the DEM if we need to (e.g. no slaving) or loads slaves.

		TIMER(edges);

		char	fname_lef[512];
		char	fname_bot[512];
		char	fname_rgt[512];
		char	fname_top[512];

		string border_loc = mesh_folder;
#if APL && !defined(__MACH__)
		string	appP;
		AppPath(appP);
		string::size_type b = appP.rfind(':');
		appP.erase(b+1);
		border_loc = appP + border_loc;
#endif

		sprintf(fname_lef,"%s%s%+03d%+04d%s%+03d%+04d.border.txt", border_loc.c_str(), DIR_STR,latlon_bucket(deriv.mSouth),latlon_bucket(deriv.mWest - 1), DIR_STR, (int) (deriv.mSouth), (int) (deriv.mWest - 1));
		sprintf(fname_bot ,"%s%s%+03d%+04d%s%+03d%+04d.border.txt", border_loc.c_str(), DIR_STR,latlon_bucket(deriv.mSouth - 1),latlon_bucket(deriv.mWest), DIR_STR, (int) (deriv.mSouth - 1), (int) (deriv.mWest));
		sprintf(fname_rgt,"%s%s%+03d%+04d%s%+03d%+04d.border.txt", border_loc.c_str(), DIR_STR,latlon_bucket(deriv.mSouth),latlon_bucket(deriv.mWest + 1), DIR_STR, (int) (deriv.mSouth), (int) (deriv.mWest + 1));
		sprintf(fname_top ,"%s%s%+03d%+04d%s%+03d%+04d.border.txt", border_loc.c_str(), DIR_STR,latlon_bucket(deriv.mSouth + 1),latlon_bucket(deriv.mWest), DIR_STR, (int) (deriv.mSouth + 1), (int) (deriv.mWest));

		mesh_match_t junk1, junk2, junk3;
		has_borders[0] = gMeshPrefs.border_match ? load_match_file(fname_lef, junk1, junk2, gMatchBorders[0], junk3) : false;
		has_borders[1] = gMeshPrefs.border_match ? load_match_file(fname_bot, junk1, junk2, junk3, gMatchBorders[1]) : false;
		has_borders[2] = gMeshPrefs.border_match ? load_match_file(fname_rgt, gMatchBorders[2], junk1, junk2, junk3) : false;
		has_borders[3] = gMeshPrefs.border_match ? load_match_file(fname_top, junk1, gMatchBorders[3], junk2, junk3) : false;
	}
	
	/* TRIANGULATE CORNERS */
	
	CDT::Face_handle hint;
	InsertDEMPoint(orig, deriv, outMesh, 0, 0, hint);
	InsertDEMPoint(orig, deriv, outMesh, orig.mWidth-1, 0, hint);
	InsertDEMPoint(orig, deriv, outMesh, orig.mWidth-1, orig.mHeight-1, hint);
	InsertDEMPoint(orig, deriv, outMesh, 0, orig.mHeight-1, hint);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished corners");
	
	/* TRIANGULATE CONSTRAINTS */
	
	AddConstraintPoints(inMap, orig, outMesh, coastlines_markers);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished constraints");
	
	/* TRIANGULATE SLAVED BORDER */
	
	for(int b=0;b<4;++b)
	if (!gMatchBorders[b].vertices.empty())
		match_border(outMesh, gMatchBorders[b], b);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished borders");
	
	/* TRIANGULATE NON-SLAVED EDGES */

	AddEdgePoints(orig, deriv, 20, 1, has_borders, outMesh);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished edges");
	
	/* TRIANGULATE WATER INTERIOR */
	
	double wet_ratio = CopyWetPoints(orig, deriv, outMesh, LOW_RES_WATER_INTERVAL, inMap);
	double dry_ratio = 1.0 - wet_ratio;

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished water interior");
	
	/* TRINAGULATE GREEDILY */

	GreedyMeshBuild(outMesh, orig, deriv, gMeshPrefs.max_error, 0.0, (dry_ratio * 0.8 + 0.2) * gMeshPrefs.max_points, prog);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished greedy1");

	GreedyMeshBuild(outMesh, orig, deriv, 0.0, gMeshPrefs.max_tri_size_m * MTR_TO_NM * NM_TO_DEG_LAT, gMeshPrefs.max_points, prog);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Finished greedy2");

	SplitConstraints(outMesh, orig, coastlines_markers, gMeshPrefs.max_error);

//	RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL);
//	DoUserAlert("Split Contraints");

#if SPLIT_CLIFFS

	set<Point_2> splits_needed;
	for (CDT::Finite_faces_iterator f = outMesh.finite_faces_begin(); f != outMesh.finite_faces_end(); ++f)
	{
		if(tri_is_cliff(outMesh, f))
		{
			if(!tri_is_cliff(outMesh, f->neighbor(0)) ||
			   !tri_is_cliff(outMesh, f->neighbor(1)) ||
			   !tri_is_cliff(outMesh, f->neighbor(2)))
			{
				CDT::Triangle tr(outMesh.triangle(f));
//				splits_needed.insert(CGAL::centroid(tr));
				splits_needed.insert(CGAL::midpoint(f->vertex(0)->point(),f->vertex(1)->point()));
				splits_needed.insert(CGAL::midpoint(f->vertex(1)->point(),f->vertex(2)->point()));
				splits_needed.insert(CGAL::midpoint(f->vertex(2)->point(),f->vertex(0)->point()));
			}
		}
	}
	
	printf("Need %d splits.\n", splits_needed.size());
	hint = CDT::Face_handle();
	for(set<Point_2>::iterator n = splits_needed.begin(); n != splits_needed.end(); ++n)
	{
		InsertAnyPoint(orig, outMesh, *n, hint);
//		debug_mesh_point(cgal2ben(*n), 1, 0, 0);
	}
#endif

#if !PHONE
	int n_vert = outMesh.number_of_vertices();					// Ben says: typically the end() iterator for the triangulation is _not_ stable across inserts.
	CGAL::make_conforming_any_2<CDT,LCP>(outMesh);				// Because the finite iterator is a filtered wrapper around the triangulation, it too is not stable
																// across inserts.  To get around this, simply note how many vertices we inserted.  Note that we are assuming
	CDT::Vertex_iterator v1,v2,v;								// vertices to be inserted into the END of the iteration list!
	v1 = outMesh.vertices_begin();
	v2 = outMesh.vertices_end();	
	DebugAssert(outMesh.number_of_vertices() >= n_vert);
	std::advance(v1,n_vert);
	
	int n_added = outMesh.number_of_vertices() - n_vert;
		
	printf("Conformer built %d verts.\n", n_added);
	
	for(v=v1;v!=v2;++v)
	{
		v->info().height = orig.value_linear(CGAL::to_double(v->point().x()),CGAL::to_double(v->point().y()));
//		debug_mesh_point(cgal2ben(v->point()),1,1,0);
		#if DEV
		if(!gMatchBorders[0].vertices.empty())
			DebugAssert(v->point().x() != orig.mWest);
		if(!gMatchBorders[1].vertices.empty())
			DebugAssert(v->point().y() != orig.mSouth);
		if(!gMatchBorders[2].vertices.empty())
			DebugAssert(v->point().x() != orig.mEast);
		if(!gMatchBorders[3].vertices.empty())
			DebugAssert(v->point().y() != orig.mNorth);
		#endif	
	}
#endif
	
	/*********************************************************************************************************************
	 * LAND USE CALC (A LITTLE BIT)
	 *********************************************************************************************************************/


	if (prog) prog(2, 3, "Calculating Wet Areas", 0.2);
	{
		SetTerrainForConstraints(outMesh, coastlines_markers, orig);
	}

#if SPLIT_BEACHED_WATER
	set<Point_2> splits_needed;
	for (CDT::Finite_faces_iterator f = outMesh.finite_faces_begin(); f != outMesh.finite_faces_end(); ++f)
	{
		if( f->info().terrain == terrain_Water)
		{
			bool	c0 = outMesh.are_there_incident_constraints(f->vertex(0));
			bool	c1 = outMesh.are_there_incident_constraints(f->vertex(1));
			bool	c2 = outMesh.are_there_incident_constraints(f->vertex(2));
			if(c0 && c1 && !f->is_constrained(2)) splits_needed.insert(CGAL::midpoint(f->vertex(0)->point(),f->vertex(1)->point()));
			if(c1 && c2 && !f->is_constrained(0)) splits_needed.insert(CGAL::midpoint(f->vertex(1)->point(),f->vertex(2)->point()));
			if(c2 && c0 && !f->is_constrained(1)) splits_needed.insert(CGAL::midpoint(f->vertex(2)->point(),f->vertex(0)->point()));
		}
	}

	printf("Need %d splits for beaches.\n", splits_needed.size());
	hint = CDT::Face_handle();
	for(set<Point_2>::iterator n = splits_needed.begin(); n != splits_needed.end(); ++n)
	{
		InsertAnyPoint(orig, outMesh, *n, hint);
	}
	
		SetTerrainForConstraints(outMesh, coastlines_markers, orig);
	
#endif




	/*********************************************************************************************************************
	 * CLEANUP - CALC MESH NORMALS
	 *********************************************************************************************************************/

	if (prog) prog(2, 3, "Calculating Wet Areas", 0.5);
	CalculateMeshNormals(outMesh);

	if (prog) prog(2, 3, "Calculating Wet Areas", 1.0);

//	orig.swap(water);
}


#pragma mark -
/*******************************************************************************************
 *******************************************************************************************
 ** MESH LANDUSE ASSIGNMENT ****************************************************************
 *******************************************************************************************
 *******************************************************************************************/

/*
	NOTE ON TERRAIN TYPES:
		The vector map contains a terrain type like none or airport or water.
		From this we then get natural, airport, or water in the mesh.  We then substitute
		on all but water through the spreadsheet.
*/

void	AssignLandusesToMesh(	DEMGeoMap& inDEMs,
								CDT& ioMesh,
								const char * mesh_folder,
								ProgressFunc	inProg)
{


		CDT::Finite_faces_iterator tri;
		CDT::Finite_vertices_iterator vert;

		int	rock_enum = LookupToken("rock_gray.ter");

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.0);

	DEMGeo&	inClimate(inDEMs[dem_Climate]);
	DEMGeo&	inElevation(inDEMs[dem_Elevation]);
	DEMGeo&	inSlope(inDEMs[dem_Slope]);
	DEMGeo&	inSlopeHeading(inDEMs[dem_SlopeHeading]);
	DEMGeo&	inRelElev(inDEMs[dem_RelativeElevation]);
	DEMGeo&	inRelElevRange(inDEMs[dem_ElevationRange]);
	DEMGeo&	inTemp(inDEMs[dem_Temperature]);
	DEMGeo&	inTempRng(inDEMs[dem_TemperatureRange]);
	DEMGeo&	inRain(inDEMs[dem_Rainfall]);
	DEMGeo& inUrbanDensity(inDEMs[dem_UrbanDensity]);
	DEMGeo& inUrbanRadial(inDEMs[dem_UrbanRadial]);
	DEMGeo& inUrbanTransport(inDEMs[dem_UrbanTransport]);
	DEMGeo& usquare(inDEMs[dem_UrbanSquare]);

	DEMGeo	landuse(inDEMs[dem_LandUse]);

// BEN SEZ: do NOT overwrite interrupted and other such areas with nearest landuse - that causes problems.
	for (int y = 0; y < landuse.mHeight;++y)
	for (int x = 0; x < landuse.mWidth; ++x)
	{
		float e = landuse(x,y);
		if (e == NO_VALUE ||
//			e == lu_usgs_INTERRUPTED_AREAS ||
//			e == lu_usgs_URBAN_SQUARE ||
//			e == lu_usgs_URBAN_IRREGULAR ||
			e == lu_globcover_WATER)
//			e == lu_usgs_SEA_WATER)
//			e == lu_usgs_DEM_NO_DATA)
			landuse(x,y) = DEM_NO_DATA;
	}
	landuse.fill_nearest();

	/***********************************************************************************************
	 * ASSIGN BASIC LAND USES TO MESH
	 ***********************************************************************************************/

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.1);
	for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
	{
		// First assign a basic land use type.
		{
			tri->info().flag = 0;
			// Hires - take from DEM if we don't have one.
			if (tri->info().terrain != terrain_Water)
			{
				double x0 = CGAL::to_double(tri->vertex(0)->point().x());
				double y0 = CGAL::to_double(tri->vertex(0)->point().y());
				double x1 = CGAL::to_double(tri->vertex(1)->point().x());
				double y1 = CGAL::to_double(tri->vertex(1)->point().y());
				double x2 = CGAL::to_double(tri->vertex(2)->point().x());
				double y2 = CGAL::to_double(tri->vertex(2)->point().y());
				double	center_x = (x0 + x1 + x2) / 3.0;
				double	center_y = (y0 + y1 + y2) / 3.0;

				float lu0  = landuse.search_nearest(center_x, center_y);
				float lu1 = landuse.search_nearest(x0,y0);
				float lu2 = landuse.search_nearest(x1,y1);
				float lu3 = landuse.search_nearest(x2,y2);
				float lu = MAJORITY_RULES(lu0,lu1,lu2, lu3);

//				float cl  = inClimate.search_nearest(center_x, center_y);
//				float cl1 = inClimate.search_nearest(x0,y0);
//				float cl2 = inClimate.search_nearest(x1,y1);
//				float cl3 = inClimate.search_nearest(x2,y2);

				// Ben sez: tiny island in the middle of nowhere - do NOT expect LU.  That's okay - Sergio doesn't need it.
//				if (lu == DEM_NO_DATA)
//					fprintf(stderr, "NO data anywhere near %f, %f\n", center_x, center_y);
//				cl = MAJORITY_RULES(cl, cl1, cl2, cl3);

//				float	el1 = inElevation.value_linear(x0,y0);
//				float	el2 = inElevation.value_linear(x1,y1);
//				float	el3 = inElevation.value_linear(x2,y2);
//				float	el = SAFE_AVERAGE(el1, el2, el3);

				float	sl1 = inSlope.value_linear(x0,y0);
				float	sl2 = inSlope.value_linear(x1,y1);
				float	sl3 = inSlope.value_linear(x2,y2);
				float	sl = SAFE_MAX	 (sl1, sl2, sl3);	// Could be safe max.
				if (sl<0.0) sl=0.0;

				float	tm1 = inTemp.value_linear(x0,y0);
				float	tm2 = inTemp.value_linear(x1,y1);
				float	tm3 = inTemp.value_linear(x2,y2);
				float	tm = SAFE_AVERAGE(tm1, tm2, tm3);	// Could be safe max.

				float	tmr1 = inTempRng.value_linear(x0,y0);
				float	tmr2 = inTempRng.value_linear(x1,y1);
				float	tmr3 = inTempRng.value_linear(x2,y2);
				float	tmr = SAFE_AVERAGE(tmr1, tmr2, tmr3);	// Could be safe max.

				float	rn1 = inRain.value_linear(x0,y0);
				float	rn2 = inRain.value_linear(x1,y1);
				float	rn3 = inRain.value_linear(x2,y2);
				float	rn = SAFE_AVERAGE(rn1, rn2, rn3);	// Could be safe max.

//				float	sh1 = inSlopeHeading.value_linear(x0,y0);
//				float	sh2 = inSlopeHeading.value_linear(x1,y1);
///				float	sh3 = inSlopeHeading.value_linear(x2,y2);
//				float	sh = SAFE_AVERAGE(sh1, sh2, sh3);	// Could be safe max.

				float	re1 = inRelElev.value_linear(x0,y0);
				float	re2 = inRelElev.value_linear(x1,y1);
				float	re3 = inRelElev.value_linear(x2,y2);
				float	re = SAFE_AVERAGE(re1, re2, re3);	// Could be safe max.

				float	er1 = inRelElevRange.value_linear(x0,y0);
				float	er2 = inRelElevRange.value_linear(x1,y1);
				float	er3 = inRelElevRange.value_linear(x2,y2);
				float	er = SAFE_AVERAGE(er1, er2, er3);	// Could be safe max.

				int		near_water =(tri->neighbor(0)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(0))) ||
									(tri->neighbor(1)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(1))) ||
									(tri->neighbor(2)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(2)));

				float	uden1 = inUrbanDensity.value_linear(x0,y0);
				float	uden2 = inUrbanDensity.value_linear(x1,y1);
				float	uden3 = inUrbanDensity.value_linear(x2,y2);
				float	uden = SAFE_AVERAGE(uden1, uden2, uden3);	// Could be safe max.

				float	urad1 = inUrbanRadial.value_linear(x0,y0);
				float	urad2 = inUrbanRadial.value_linear(x1,y1);
				float	urad3 = inUrbanRadial.value_linear(x2,y2);
				float	urad = SAFE_AVERAGE(urad1, urad2, urad3);	// Could be safe max.

				float	utrn1 = inUrbanTransport.value_linear(x0,y0);
				float	utrn2 = inUrbanTransport.value_linear(x1,y1);
				float	utrn3 = inUrbanTransport.value_linear(x2,y2);
				float	utrn = SAFE_AVERAGE(utrn1, utrn2, utrn3);	// Could be safe max.

				float usq  = usquare.search_nearest(center_x, center_y);
				float usq1 = usquare.search_nearest(x0,y0);
				float usq2 = usquare.search_nearest(x1,y1);
				float usq3 = usquare.search_nearest(x2,y2);
				usq = MAJORITY_RULES(usq, usq1, usq2, usq3);

//				float	el1 = tri->vertex(0)->info().height;
//				float	el2 = tri->vertex(1)->info().height;
//				float	el3 = tri->vertex(2)->info().height;
//				float	el_tri = (el1 + el2 + el3) / 3.0;

				float	sl_tri = 1.0 - tri->info().normal[2];
				float	flat_len = sqrt(tri->info().normal[1] * tri->info().normal[1] + tri->info().normal[0] * tri->info().normal[0]);
				float	sh_tri = tri->info().normal[1];
				if (flat_len != 0.0)
				{
					sh_tri /= flat_len;
					sh_tri = max(-1.0f, min(sh_tri, 1.0f));
				}

				float	patches = (gMeshPrefs.rep_switch_m == 0.0) ? 100.0 : (60.0 * NM_TO_MTR / gMeshPrefs.rep_switch_m);
				int x_variant = fabs(center_x /*+ RandRange(-0.03, 0.03)*/) * patches; // 25.0;
				int y_variant = fabs(center_y /*+ RandRange(-0.03, 0.03)*/) * patches; // 25.0;
				int variant_blob = ((x_variant + y_variant * 2) % 4) + 1;
				int variant_head = (tri->info().normal[0] > 0.0) ? 6 : 8;

				if (sh_tri < -0.7)	variant_head = 7;
				if (sh_tri >  0.7)	variant_head = 5;

				//fprintf(stderr, " %d", tri->info().feature);
				int zoning = (tri->info().orig_face == Pmwx::Face_handle()) ? NO_VALUE : tri->info().orig_face->data().GetZoning();
				int terrain = FindNaturalTerrain(tri->info().feature, zoning, lu, /* cl, el, */ sl, sl_tri, tm, tmr, rn, near_water, sh_tri, re, er, uden, urad, utrn, usq, fabs((float) center_y), variant_blob, variant_head);
				if (terrain == -1)
					AssertPrintf("Cannot find terrain for: %s, %f\n", FetchTokenString(lu), /*FetchTokenString(cl), el, */ sl);

				tri->info().mesh_temp = tm;
				tri->info().mesh_rain = rn;
			#if OPENGL_MAP
				tri->info().debug_terrain_orig = terrain;
				tri->info().debug_slope_dem = sl;
				tri->info().debug_slope_tri = sl_tri;
				tri->info().debug_temp_range = tmr;
				tri->info().debug_heading = sh_tri;
				tri->info().debug_re = re;
				tri->info().debug_er = er;				
				tri->info().debug_lu[0] = lu0;
				tri->info().debug_lu[1] = lu1;
				tri->info().debug_lu[2] = lu2;
				tri->info().debug_lu[3] = lu3;
				tri->info().debug_lu[4] = lu ;
			#endif
				if (terrain == -1)
				{
					AssertPrintf("No rule. lu=%s, slope=%f, trislope=%f, temp=%f, temprange=%f, rain=%f, water=%d, heading=%f, lat=%f\n",
						FetchTokenString(lu), /*el,*/ acos(1-sl)*RAD_TO_DEG, acos(1-sl_tri)*RAD_TO_DEG, tm, tmr, rn, near_water, sh_tri, center_y);
				}
				//fprintf(stderr, "->%d", terrain);

				tri->info().terrain = terrain;

			}

		}
	}

	/***********************************************************************************************
	 * TRY TO CONSOLIDATE BLOBS
	 ***********************************************************************************************/
	// If a blob's total area is less than the blobbing distance, it's not really needed!  Simplify
	// it.

/*
	int tri_merged = 0;
	set<CDT::Face_handle>	all_variants;

	for (CDT::Finite_faces_iterator f = ioMesh.finite_faces_begin(); f != ioMesh.finite_faces_end(); ++f)
	if (f->info().terrain != terrain_Water)
	if (HasVariant(f->info().terrain))
		all_variants.insert(f);

	float max_rat = gMeshPrefs.rep_switch_m * MTR_TO_NM * NM_TO_DEG_LAT;

	while (!all_variants.empty())
	{
		CDT::Face_handle		w = *all_variants.begin();
		int						base = SpecificVariant(w->info().terrain,0);
		set<CDT::Face_handle>	tri_set;
		Bbox_2					bounds;
		FindAllCovariant(ioMesh, w, tri_set, bounds);

		bool devary = (bounds.ymax() - bounds.ymin() < max_rat) && (bounds.xmax() - bounds.xmin()) < max_rat;

		for (set<CDT::Face_handle>::iterator kill = tri_set.begin(); kill != tri_set.end(); ++kill)
		{
			if (devary)
			{
				(*kill)->info().terrain = base;
				++tri_merged;
			}
			all_variants.erase(*kill);
		}
	}
*/

	/***********************************************************************************************
	 * DEAL WITH INTRUSION FROM OUR MASTER SIDE
	 ***********************************************************************************************/

	// BEN SEZ - IS THIS COMMENT TRUE?
	// ??? This must be POST optmize - we can go OUT OF ORDER on the borders because must have left-master/right-slave.
	// ??? So the optmizer will NUKE this stuff. :-(

	// First build a correlation between our border info and some real tris in the mesh.
	int b;
	for(b=0;b<4;++b)
	if (!gMatchBorders[b].vertices.empty())
		border_find_edge_tris(ioMesh, gMatchBorders[b]);
	int lowest;
	int n;
#if !NO_BORDER_SHARING

	set<CDT::Vertex_handle>	vertices;
	// Now we have to "rebase" our edges.  Basically it is possible that we are getting intruded from the left
	// by a lower priority texture.  If we just use borders, that low prio tex will end up UNDER our base, and we'll
	// never see it.  So we need to take the tex on our right side and reduce it.
	for(b=0;b < 4; ++b)
	{
		for (n = 0; n < gMatchBorders[b].edges.size(); ++n)
		if (gMatchBorders[b].edges[n].buddy != CDT::Face_handle())
		{
			lowest = gMatchBorders[b].edges[n].buddy->info().terrain;
			if (LowerPriorityNaturalTerrain(gMatchBorders[b].edges[n].base, lowest))
				lowest = gMatchBorders[b].edges[n].base;
			for (set<int>::iterator bl = gMatchBorders[b].edges[n].borders.begin(); bl != gMatchBorders[b].edges[n].borders.end(); ++bl)
			{
				if (LowerPriorityNaturalTerrain(*bl, lowest))
					lowest = *bl;
			}

			if (lowest != gMatchBorders[b].edges[n].buddy->info().terrain)
				RebaseTriangle(ioMesh, gMatchBorders[b].edges[n].buddy, lowest, gMatchBorders[b].vertices[n].buddy, gMatchBorders[b].vertices[n+1].buddy, vertices);
		}

		for (n = 0; n < gMatchBorders[b].vertices.size(); ++n)
		{
			CDT::Face_circulator circ, stop;
			circ = stop = ioMesh.incident_faces(gMatchBorders[b].vertices[n].buddy);
			do {
				if (!ioMesh.is_infinite(circ))
				if (!is_border(ioMesh, circ))
				{
					lowest = circ->info().terrain;
					for (hash_map<int, float>::iterator bl = gMatchBorders[b].vertices[n].blending.begin(); bl != gMatchBorders[b].vertices[n].blending.end(); ++bl)
					if (bl->second > 0.0)
					if (LowerPriorityNaturalTerrain(bl->first, lowest))
						lowest = bl->first;

					if (lowest != circ->info().terrain)
						RebaseTriangle(ioMesh, circ, lowest, gMatchBorders[b].vertices[n].buddy, CDT::Vertex_handle(), vertices);
				}
				++circ;
			} while (circ != stop);
		}
	}

	// These vertices were given partial borders by rebasing - go in and make sure that all incident triangles match them.
	for (set<CDT::Vertex_handle>::iterator rebased_vert = vertices.begin(); rebased_vert != vertices.end(); ++rebased_vert)
	{
		CDT::Face_circulator circ, stop;
		circ = stop = ioMesh.incident_faces(*rebased_vert);
		do {
			if (!ioMesh.is_infinite(circ))
			for (hash_map<int, float>::iterator bl = (*rebased_vert)->info().border_blend.begin(); bl != (*rebased_vert)->info().border_blend.end(); ++bl)
			if (bl->second > 0.0)
				AddZeroMixIfNeeded(circ, bl->first);
			++circ;
		} while (circ != stop);
	}

#endif

	/***********************************************************************************************
	 * CALCULATE BORDERS
	 ***********************************************************************************************/

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.5);

#if !NO_BORDERS_AT_ALL

	/* 	Here's the idea:
		We are going to go through each triangle, which now has a land use, and figure ouet which
		ones have borders.  A triangle that has a border will get:
		(1) the land use of the border triangle in its set of "border landuses", so it
		 	can easily be identified in that mesh, and
		(2) for each of its vertices, a hash map entry with the alpha level for the border at that
			point, so we can figure out how  the border fades.

		To do this we say: for each triangle, we do a "spreading" type algorithm, e.g. we collect
		non-visited neighbors that meet our criteria in a set and go outward.  We only take neighbors
		that have a lower natural land use and haven't been visited.  We calc our distance to the
		corners to get the blend, and if we're not all faded out, keep going.
	*/

	int		visited = 0;	// flag value - by using a rolling flag, we don't have to reset
							// this all of the time.
	int		tri_total = 0, tri_border = 0, tri_check = 0, tri_opt = 0;
	for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
	if (tri->info().terrain != terrain_Water)
	{
		++visited;
		set<CDT::Face_handle>	to_visit;
		to_visit.insert(tri);
		bool					spread;
		int						layer = tri->info().terrain;
		tri->info().flag = visited;

		while (!to_visit.empty())
		{
			CDT::Face_handle	border = *to_visit.begin();
			to_visit.erase(border);
			spread = false;
			if (&*border != &*tri)
			{
				// Calculation phase - figure out alphas of
				// the corners.
				CDT::Vertex_handle v1 = border->vertex(0);
				CDT::Vertex_handle v2 = border->vertex(1);
				CDT::Vertex_handle v3 = border->vertex(2);
				double	dist1 = DistPtToTri(v1, tri);
				double	dist2 = DistPtToTri(v2, tri);
				double	dist3 = DistPtToTri(v3, tri);
				double	dist_max = GetXonDist(layer, border->info().terrain, border->info().normal[2]);

				if (dist_max > 0.0)
				{
					dist1 = max(0.0, min((dist_max-dist1)/dist_max,1.0));
					dist2 = max(0.0, min((dist_max-dist2)/dist_max,1.0));
					dist3 = max(0.0, min((dist_max-dist3)/dist_max,1.0));

					++tri_check;
					if (dist1 > 0.0 || dist2 > 0.0 || dist3 > 0.0)
					{
						double	odist1 = v1->info().border_blend[layer];
						double	odist2 = v2->info().border_blend[layer];
						double	odist3 = v3->info().border_blend[layer];

						// Border propagation - we only want to set the levels of this border if we are are adjacent to ourselves..this way we don't set the far-side distance
						// unless there will be another border tri to continue with.

						bool has_0 = false, has_1 = false, has_2 = false;
						if (border->neighbor(0)->info().terrain_border.count(layer) || border->neighbor(0)->info().terrain == layer) { has_1 = true; has_2 = true; }
						if (border->neighbor(1)->info().terrain_border.count(layer) || border->neighbor(1)->info().terrain == layer) { has_2 = true; has_0 = true; }
						if (border->neighbor(2)->info().terrain_border.count(layer) || border->neighbor(2)->info().terrain == layer) { has_0 = true; has_1 = true; }

						// BUT...if we're at the edge of the file, go across anyway, what the hell...
						// Ben sez: no- try to limit cross-border madness or we get projection mismatches.
//						if (!has_0 && IsEdgeVertex(ioMesh, v1))	has_0 = true;
//						if (!has_1 && IsEdgeVertex(ioMesh, v2))	has_1 = true;
//						if (!has_2 && IsEdgeVertex(ioMesh, v3))	has_2 = true;

						if (!has_0) dist1 = 0.0;
						if (!has_1) dist2 = 0.0;
						if (!has_2) dist3 = 0.0;

						// If we're not faded out totally, record an increase.  ONLY keep
						// searching if we are increasing one of the vertices.  Otherwise
						// someone else has been over this territory who is already closer
						// and we're just wasting our time.
						if (dist1 > odist1) { spread = true; v1->info().border_blend[layer] = dist1; }
						if (dist2 > odist2) { spread = true; v2->info().border_blend[layer] = dist2; }
						if (dist3 > odist3) { spread = true; v3->info().border_blend[layer] = dist3; }

						// HACK - does always extending the borders fix a bug?
						DebugAssert(layer != -1);
						border->info().terrain_border.insert(layer);
						spread = true;
					}
				}
			} else
				spread = true;

			border->info().flag = visited;

			// Spreading case: check our neighbors to make sure we haven't seen them and it makes
			// sense to check them.
			if (spread)
			{
				CDT::Face_handle b1 = border->neighbor(0);
				CDT::Face_handle b2 = border->neighbor(1);
				CDT::Face_handle b3 = border->neighbor(2);

				if (b1->info().flag != visited && !ioMesh.is_infinite(b1) && b1->info().terrain != terrain_Water && LowerPriorityNaturalTerrain(b1->info().terrain, layer))	to_visit.insert(b1);
				if (b2->info().flag != visited && !ioMesh.is_infinite(b2) && b2->info().terrain != terrain_Water && LowerPriorityNaturalTerrain(b2->info().terrain, layer))	to_visit.insert(b2);
				if (b3->info().flag != visited && !ioMesh.is_infinite(b3) && b3->info().terrain != terrain_Water && LowerPriorityNaturalTerrain(b3->info().terrain, layer))	to_visit.insert(b3);
			}
		}
	}

	/***********************************************************************************************
	 * DEAL WITH INTRUSION FROM OUR MASTER SIDE
	 ***********************************************************************************************/
#if !NO_BORDER_SHARING
	// First - force border blend of zero at the slaved edge, no matter how ridiculous.  We can't possibly propagate
	// this border into a previously rendered file, so a hard stop is better than a cutoff.
	for(b=0;b<4;++b)
	for (n = 0; n < gMatchBorders[b].vertices.size(); ++n)
	for (hash_map<int, float>::iterator blev = gMatchBorders[b].vertices[n].buddy->info().border_blend.begin(); blev != gMatchBorders[b].vertices[n].buddy->info().border_blend.end(); ++blev)
		blev->second = 0.0;

	// Now we are going to go in and add borders on our slave edges from junk coming in on the left.  We have ALREADY
	// "rebased" the terrain.  This means that the border on the slave side is guaranteed to be lower priority than the border
	// on the master, so that we can make this border-extension safely.  For the base and borders on the master we just add
	// a border on the slave - the edge blend levels are the master's blend and the interior poiont gets a blend of 0 or whatever
	// was already there.

	for(b=0;b<4;++b)
	for (n = 0; n < gMatchBorders[b].edges.size(); ++n)
	if (gMatchBorders[b].edges[n].buddy != CDT::Face_handle())
	if (gMatchBorders[b].edges[n].buddy->info().terrain != terrain_Water)
	{
		// Handle the base terrain
		if (gMatchBorders[b].edges[n].buddy->info().terrain != gMatchBorders[b].edges[n].base)
		{
			AddZeroMixIfNeeded(gMatchBorders[b].edges[n].buddy, gMatchBorders[b].edges[n].base);
			gMatchBorders[b].vertices[n].buddy->info().border_blend[gMatchBorders[b].edges[n].base] = 1.0;
			SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n].buddy, gMatchBorders[b].edges[n].base);
			gMatchBorders[b].vertices[n+1].buddy->info().border_blend[gMatchBorders[b].edges[n].base] = 1.0;
			SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n+1].buddy, gMatchBorders[b].edges[n].base);
		}

		// Handle any overlay layers...
		for (set<int>::iterator bl = gMatchBorders[b].edges[n].borders.begin(); bl != gMatchBorders[b].edges[n].borders.end(); ++bl)
		{
			if (gMatchBorders[b].edges[n].buddy->info().terrain != *bl)
			{
				AddZeroMixIfNeeded(gMatchBorders[b].edges[n].buddy, *bl);
				gMatchBorders[b].vertices[n].buddy->info().border_blend[*bl] = gMatchBorders[b].vertices[n].blending[*bl];
				SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n].buddy, *bl);
				gMatchBorders[b].vertices[n+1].buddy->info().border_blend[*bl] = gMatchBorders[b].vertices[n+1].blending[*bl];
				SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n+1].buddy, *bl);
			}
		}
	}

#endif

	/***********************************************************************************************
	 * OPTIMIZE BORDERS!
	 ***********************************************************************************************/
	if (inProg) inProg(0, 1, "Assigning Landuses", 0.75);

	if (gMeshPrefs.optimize_borders)
	{
		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
		if (tri->info().terrain != terrain_Water)
		{
			bool need_optimize = false;
			for (set<int>::iterator blayer = tri->info().terrain_border.begin();
				blayer != tri->info().terrain_border.end(); ++blayer)
			{
				if (tri->vertex(0)->info().border_blend[*blayer] == 1.0 &&
					tri->vertex(1)->info().border_blend[*blayer] == 1.0 &&
					tri->vertex(2)->info().border_blend[*blayer] == 1.0)
				{
					if (LowerPriorityNaturalTerrain(tri->info().terrain, *blayer))
					{
						tri->info().terrain = *blayer;
						need_optimize = true;
//						debug_mesh_point(cgal2ben(CGAL::centroid(ioMesh.triangle(tri))),1,0,1);
					}
				}
			}
			if (need_optimize)
			{
				set<int>	nuke;
				for (set<int>::iterator blayer = tri->info().terrain_border.begin();
					blayer != tri->info().terrain_border.end(); ++blayer)
				{
					if (!LowerPriorityNaturalTerrain(tri->info().terrain, *blayer))
						nuke.insert(*blayer);
				}
				for (set<int>::iterator nlayer = nuke.begin(); nlayer != nuke.end(); ++nlayer)
				{
					tri->info().terrain_border.erase(*nlayer);
					// DO NOT eliminate these - maybe our neighbor is using them!!
//					tri->vertex(0)->info().border_blend.erase(*nlayer);
//					tri->vertex(1)->info().border_blend.erase(*nlayer);
//					tri->vertex(2)->info().border_blend.erase(*nlayer);
					++tri_opt;
				}
			}
		}
	}

	{
		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
		if (tri->info().terrain != terrain_Water)
		{
			tri_total++;
			tri_border += (tri->info().terrain_border.size());
		} else if (!tri->info().terrain_border.empty())
			AssertPrintf("BORDER ON WATER LAND USE!  Terrain = %s", FetchTokenString(tri->info().terrain));
		printf("Total: %d - border: %d - check: %d - opt: %d\n", tri_total, tri_border, tri_check, tri_opt);
	}

#endif /* NO_BORDERS_AT_ALL */

	/***********************************************************************************************
	 * WRITE OUT MESH
	 ***********************************************************************************************/

	// We need to write out an edge file for our next guy in line.

	if (gMeshPrefs.border_match)
	{
		double	west = inElevation.mWest;
		double	east = inElevation.mEast;
		double	south = inElevation.mSouth;
		double	north = inElevation.mNorth;
		char	fname[512];

		string border_loc = mesh_folder;
#if APL && !defined(__MACH__)
		string	appP;
		AppPath(appP);
		string::size_type b = appP.rfind(':');
		appP.erase(b+1);
		border_loc = appP + border_loc;
#endif


		sprintf(fname, "%s%s%+03d%+04d%s%+03d%+04d.border.txt", border_loc.c_str(), DIR_STR, latlon_bucket (south), latlon_bucket (west), DIR_STR, (int) south, (int) west);

		FILE * border = fopen(fname, "w");
		if (border == NULL) AssertPrintf("Unable to open file %s for writing.", fname);

		CDT::Point cur,stop;
		for(int b = 0; b < 4; ++b)
		{
			switch(b) {
			case 0:	cur = CDT::Point(west,south);	stop = CDT::Point(west,north);	break;
			case 1:	cur = CDT::Point(west,south);	stop = CDT::Point(east,south);	break;
			case 2:	cur = CDT::Point(east,south);	stop = CDT::Point(east,north);	break;
			case 3:	cur = CDT::Point(west,north);	stop = CDT::Point(east,north);	break;
			}

			CDT::Face_handle	f;
			int					i;
			CDT::Locate_type	lt;
			f = ioMesh.locate(cur, lt, i);
			Assert(lt == CDT::VERTEX);

			CDT::Face_circulator circ, circstop;

			do {
				fprintf(border, "VT %.12lf, %.12lf, %lf\n",
					CGAL::to_double(f->vertex(i)->point().x()),
					CGAL::to_double(f->vertex(i)->point().y()),
					CGAL::to_double(f->vertex(i)->info().height));

				hash_map<int, float>	borders;
				for (hash_map<int, float>::iterator hfi = f->vertex(i)->info().border_blend.begin(); hfi != f->vertex(i)->info().border_blend.end(); ++hfi)
				if (hfi->second > 0.0)
					borders[hfi->first] = max(borders[hfi->first], hfi->second);
				circ = circstop = ioMesh.incident_faces(f->vertex(i));
				do {
					if (!ioMesh.is_infinite(circ))
					{
						borders[circ->info().terrain] = 1.0;
					}
					++circ;
				} while (circ != circstop);

				fprintf(border, "VBC %llu\n", (unsigned long long)borders.size());
				for (hash_map<int, float>::iterator hfi = borders.begin(); hfi != borders.end(); ++hfi)
					fprintf(border, "VB %f %s\n", hfi->second, FetchTokenString(hfi->first));

				if(b == 1 || b == 3)				FindNextEast(ioMesh, f, i, b==1);
				else								FindNextNorth(ioMesh, f, i, b==2);
				DebugAssert(!ioMesh.is_infinite(f));

				fprintf(border, "TERRAIN %s\n", FetchTokenString(f->info().terrain));
				fprintf(border, "BORDER_C %llu\n", (unsigned long long)f->info().terrain_border.size());
				for (set<int>::iterator si = f->info().terrain_border.begin(); si != f->info().terrain_border.end(); ++si)
					fprintf(border, "BORDER_T %s\n", FetchTokenString(*si));

			} while (f->vertex(i)->point() != stop);

			fprintf(border, "VC %.12lf, %.12lf, %lf\n",
					CGAL::to_double(f->vertex(i)->point().x()),
					CGAL::to_double(f->vertex(i)->point().y()),
					CGAL::to_double(f->vertex(i)->info().height));
			fprintf(border, "VBC %llu\n", (unsigned long long)f->vertex(i)->info().border_blend.size());
			for (hash_map<int, float>::iterator hfi = f->vertex(i)->info().border_blend.begin(); hfi != f->vertex(i)->info().border_blend.end(); ++hfi)
				fprintf(border, "VB %f %s\n", hfi->second, FetchTokenString(hfi->first));
		}

		fprintf(border, "END\n");
		fclose(border);

	}

	if (inProg) inProg(0, 1, "Assigning Landuses", 1.0);

}


#pragma mark -
/*******************************************************************************************
 *	UTILITY ROUTINES
 *******************************************************************************************/
void SetupWaterRasterizer(const Pmwx& map, const DEMGeo& orig, PolyRasterizer& rasterizer)
{
	for (Pmwx::Edge_const_iterator i = map.edges_begin(); i != map.edges_end(); ++i)
	{
		bool	iWet = i->face()->data().IsWater() && !i->face()->is_unbounded();
		bool	oWet = i->twin()->face()->data().IsWater() && !i->twin()->face()->is_unbounded();

		if (iWet != oWet)
		{
			double x1 = orig.lon_to_x(CGAL::to_double(i->source()->point().x()));
			double y1 = orig.lat_to_y(CGAL::to_double(i->source()->point().y()));
			double x2 = orig.lon_to_x(CGAL::to_double(i->target()->point().x()));
			double y2 = orig.lat_to_y(CGAL::to_double(i->target()->point().y()));

//				gMeshLines.push_back(i->source()->point());
//				gMeshLines.push_back(i->target()->point());

//				fprintf(fi,"%lf,%lf    %lf,%lf   %s\n", x1,y1,x2,y2, ((y1 == 0.0 || y2 == 0.0) && y1 != y2) ? "****" : "");

			if (y1 != y2)
			{
				if (y1 < y2)
					rasterizer.masters.push_back(PolyRasterSeg_t(x1,y1,x2,y2));
				else
					rasterizer.masters.push_back(PolyRasterSeg_t(x2,y2,x1,y1));
			}
		}
	}
	rasterizer.SortMasters();
}

void	Calc2ndDerivative(DEMGeo& deriv)
{
	int x, y;
	float 	h, ha, hr, hb, hl;
	for (y = 0; y < (deriv.mHeight-1); ++y)
	for (x = 0; x < (deriv.mWidth-1); ++x)
	{
		h  = deriv(x,y);
		ha = deriv(x,y+1);
		hr = deriv(x+1,y);

		if (h == DEM_NO_DATA || ha == DEM_NO_DATA || hr == DEM_NO_DATA)
			deriv(x,y) = DEM_NO_DATA;
		else
			deriv(x,y) = (ha - h) + (hr - h);
	}

	for (y = (deriv.mHeight-2); y >= 1; --y)
	for (x = (deriv.mWidth-2);  x >= 1; --x)
	{
		h  = deriv(x,y);
		hb = deriv(x,y-1);
		hl = deriv(x-1,y);

		if (h == DEM_NO_DATA || hb == DEM_NO_DATA || hl == DEM_NO_DATA)
			deriv(x,y) = DEM_NO_DATA;
		else
			deriv(x,y) = (h - hl) + (h - hb);
	}

	for (x = 0; x < deriv.mWidth; ++x)
	{
		deriv(x, 0) = DEM_NO_DATA;
		deriv(x, deriv.mHeight-1) = DEM_NO_DATA;
	}
	for (x = y; y < deriv.mHeight; ++y)
	{
		deriv(0, y) = DEM_NO_DATA;
		deriv(deriv.mWidth-1, y) = DEM_NO_DATA;
	}
}

double	HeightWithinTri(CDT& inMesh, CDT::Face_handle f, CDT::Point in)
{
	Assert(!inMesh.is_infinite(f));

	double	DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(in.y()) * DEG_TO_RAD);

	Point_3	p1((f->vertex(0)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(0)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(0)->info().height));

	Point_3	p2((f->vertex(1)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(1)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(1)->info().height));

	Point_3	p3((f->vertex(2)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(2)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(2)->info().height));

	Vector_3	s1(p2, p3);
	Vector_3	s2(p2, p1);
	Vector_3	n = cross_product(s1, s2);
	//Plane_3	plane = Plane_3(p1, n);
	//plane.n = n;
	//plane.ndotp = n * Vector_3(p1);
	double r = CGAL::to_double(p1.z() - ((n.x() * (in.x() * (DEG_TO_NM_LON * NM_TO_MTR) - p1.x()) + (n.y() * (in.y() * (DEG_TO_NM_LAT * NM_TO_MTR) - p1.y()))) / n.z()));

	//Plane_3 plane = Plane_3(p2, p1, p3);
	//double r = CGAL::to_double(plane.to_3d(Point_2(in.x() * (DEG_TO_NM_LON * NM_TO_MTR), in.y() * (DEG_TO_NM_LAT * NM_TO_MTR))).z());

	//printf("%lf, %lf, %lf. ", CGAL::to_double(in.x()), CGAL::to_double(in.y()), r);
	return r;

}


double	MeshHeightAtPoint(CDT& inMesh, double inLon, double inLat, int hint_id)
{
	if (inMesh.number_of_faces() < 1) return DEM_NO_DATA;
	CDT::Face_handle	f = NULL;
	int	n;
	CDT::Locate_type lt;
	f = inMesh.locate_cache(CDT::Point(inLon, inLat), lt, n, hint_id);
	if (lt == CDT::VERTEX)
	{
		return f->vertex(n)->info().height;
	}
	if (lt == CDT::EDGE && inMesh.is_infinite(f))
	{
		f = f->neighbor(n);
	}

	if (!inMesh.is_infinite(f))
	{
		return HeightWithinTri(inMesh, f, CDT::Point(inLon, inLat));
	} else {
		printf("Requested point was off mesh: %lf, %lf\n", inLon, inLat);
		return DEM_NO_DATA;
	}
}

int	CalcMeshError(CDT& mesh, DEMGeo& elev, float& out_min, float& out_max, float& out_ave, float& std_dev, ProgressFunc inFunc)
{
	if (inFunc) inFunc(0, 1, "Calculating Error", 0.0);
	int ctr = 0;
	
	out_max = 0.0;
	out_ave = 0.0;
	std_dev = 0.0;
	out_min = 9.9e9;
	
	CDT::Face_handle	last_tri;
	Plane3				last_plane;
	Point2				last_tri_loc[3];
	double				DEG_TO_NM_LON = DEG_TO_NM_LAT;

	float				worst_pos = 0.0;
	float				worst_neg = 0.0;
	Point2				worst_pos_p;
	Point2				worst_neg_p;
	
	if(mesh.number_of_faces() >= 1)
	for (int y = 0; y < elev.mHeight; ++y)
	{
		if (inFunc && (y % 20) == 0) inFunc(0, 1, "Calculating Error", (float) y / (float) elev.mHeight);

		for (int x = 0; x < elev.mWidth ; ++x)
		{
			float ideal = elev.get(x,y);
			if (ideal != DEM_NO_DATA)
			{
				Point2	ll(elev.x_to_lon(x), elev.y_to_lat(y));
				if(last_tri == CDT::Face_handle() ||
				   Segment2(last_tri_loc[0],last_tri_loc[1]).on_right_side(ll) ||
				   Segment2(last_tri_loc[1],last_tri_loc[2]).on_right_side(ll) ||
				   Segment2(last_tri_loc[2],last_tri_loc[0]).on_right_side(ll))
				{

					CDT::Face_handle	f = NULL;
					int	n;
					CDT::Locate_type lt;
					f = mesh.locate(CDT::Point(ll.x(), ll.y()), lt, n, last_tri);
					if (lt == CDT::EDGE && mesh.is_infinite(f))
					{
						f = f->neighbor(n);
					}
					
					if(!mesh.is_infinite(f))
					{
						last_tri = f;

						last_tri_loc[0] = cgal2ben(f->vertex(0)->point());
						last_tri_loc[1] = cgal2ben(f->vertex(1)->point());
						last_tri_loc[2] = cgal2ben(f->vertex(2)->point());
						

						DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(ll.y()) * DEG_TO_RAD);

						Point3	p1((last_tri_loc[0].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[0].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(0)->info().height));

						Point3	p2((last_tri_loc[1].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[1].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(1)->info().height));

						Point3	p3((last_tri_loc[2].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[2].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(2)->info().height));

						Vector3	s1(p2, p3);
						Vector3	s2(p2, p1);
						Vector3	n = s1.cross(s2);
						n.normalize();
						last_plane = Plane3(p1,n);
					}
				}
				
				if(last_tri != CDT::Face_handle())
				{
					double real = (last_plane.n.dx * ll.x() +
								   last_plane.n.dy * ll.y() -
								   last_plane.ndotp) / -last_plane.n.dz;
					
					double	close = last_plane.distance_denormaled(Point3(ll.x(),ll.y(),ideal));
					float derr = real - ideal;
					derr = close;

					Point2	me(elev.x_to_lon(x), elev.y_to_lat(y));
					if(derr > worst_pos)
					{
						worst_pos = derr;
						worst_pos_p = me;
					}
					if(derr < worst_neg)
					{
						worst_neg = derr;
						worst_neg_p = me;
					}
					
					out_min = min(out_min,derr);
					out_max = max(out_max,derr);
					out_ave += derr;
					std_dev += (derr*derr);
					++ctr;
				}
			}
		}
	}
	if(worst_pos > 0.0)
	{	
//		debug_mesh_point(worst_pos_p,1,0,0);
		printf("Worst positive error is %f meters at %+08.6lf, %+09.7lf\n", worst_pos, worst_pos_p.x(), worst_pos_p.y());
	}	
	if(worst_neg < 0.0)
	{
		printf("Worst negative error is %f meters at %+08.6lf, %+09.7lf\n", worst_neg, worst_neg_p.x(), worst_neg_p.y());	
//		debug_mesh_point(worst_neg_p,1,0,1);
	}
	
	if(ctr > 0)
	{
		out_ave /= (float) ctr;
		std_dev = sqrt(std_dev / float(ctr));
	}
	
	if (inFunc) inFunc(0, 1, "Calculating Error", 1.0);
	return ctr;
}

int	CalcMeshTextures(CDT& inMesh, map<int, int>& out_lus)
{
	out_lus.clear();
	int total = 0;
	for(CDT::Face_iterator  f = inMesh.finite_faces_begin(); f != inMesh.finite_faces_end(); ++f)
	{
		out_lus[f->info().terrain]++;
		for(set<int>::iterator b =  f->info().terrain_border.begin();
							   b != f->info().terrain_border.end(); ++b)
			out_lus[*b]++;
		total += (1 + f->info().terrain_border.size());
	}
	return total;
}

static bool RayInTri(CDT::Face_handle tri, CDT::Vertex_handle v, const CDT::Point& goal)
{
	CDT::Orientation_2 pred;

	CDT::Vertex_handle	v_cw =  tri->vertex(CDT::cw (tri->index(v)));
	CDT::Vertex_handle	v_ccw = tri->vertex(CDT::ccw(tri->index(v)));

	if (pred(v->point(),  v_cw->point(), goal) == CGAL::LEFT_TURN ) return false;
	if (pred(v->point(), v_ccw->point(), goal) == CGAL::RIGHT_TURN) return false;
																	 return true;
}

bool common_vertex(CDT::Face_handle t1, CDT::Face_handle t2, int& index)
{
	if (t2->has_vertex(t1->vertex(0))) { index = 0; return true; }
	if (t2->has_vertex(t1->vertex(1))) { index = 1; return true; }
	if (t2->has_vertex(t1->vertex(2))) { index = 2; return true; }
	return false;
}



CDT_MarchOverTerrain_t::CDT_MarchOverTerrain_t()
{
	locate_face = NULL;
}

void MarchHeightStart(CDT& inMesh, const CDT::Point& loc, CDT_MarchOverTerrain_t& info)
{
	CDT::Locate_type	locate_type;
	int					locate_index;

	info.locate_face = inMesh.locate(loc, locate_type, locate_index, info.locate_face);

	// Special case: under some conditions we'll get the infinite-face edge.  This actually depends
	// on what our seed locate was.  Either way it is unacceptable - passing in an infinite face
	// generally makes the locate algorithm a little bonkers.  Reverse it here.
	if (inMesh.is_infinite(info.locate_face) && (locate_type == CDT::EDGE || locate_type == CDT::VERTEX))
	{
		info.locate_face = info.locate_face->neighbor(info.locate_face->index(inMesh.infinite_vertex()));
	}
	info.locate_pt = loc;
	info.locate_height = HeightWithinTri(inMesh, info.locate_face, loc);
}

void  MarchHeightGo(CDT& inMesh, const CDT::Point& goal, CDT_MarchOverTerrain_t& march_info, vector<Point3>& intermediates)
{
	static int level = 0;
	Assert(level < 2);

	// Makse sure our input makes some sense!
	DebugAssert(!inMesh.is_infinite(march_info.locate_face));
	DebugAssert(inMesh.triangle(march_info.locate_face).bounded_side(march_info.locate_pt) != CGAL::ON_UNBOUNDED_SIDE);

	intermediates.clear();

	CDT::Line_face_circulator circ(inMesh.line_walk(march_info.locate_pt, goal, march_info.locate_face));
	CDT::Line_face_circulator stop(circ);

	// Ben says: CGAL allows this, believe it or not - see special "null-type" comparator.
	// The REAL handle comparator is zapped out of MSC for templating reasons.
	if (circ == NULL)
	{
		CDT::Locate_type	goal_type;
		int					goal_index;
		CDT::Face_handle	goal_face;
		CDT::Point			rev_goal = march_info.locate_pt;
		goal_face = inMesh.locate(goal, goal_type, goal_index, march_info.locate_face);
		if (inMesh.is_infinite(goal_face) && goal_type == CDT::EDGE)
			goal_face = goal_face->neighbor(goal_index);

		double				goal_height = HeightWithinTri(inMesh, goal_face, goal);

		march_info.locate_pt = goal;
		march_info.locate_face = goal_face;
		march_info.locate_height = goal_height;

		++level;
		MarchHeightGo(inMesh, rev_goal, march_info, intermediates);
		--level;

		march_info.locate_pt = goal;
		march_info.locate_face = goal_face;
		march_info.locate_height = goal_height;

		int s = intermediates.size() / 2;
		for (int n = 0; n < s; ++n)
		{
			swap(intermediates[n], intermediates[intermediates.size() - n - 1]);
		}
		DebugAssert(!inMesh.is_infinite(march_info.locate_face));
		DebugAssert(inMesh.triangle(march_info.locate_face).bounded_side(march_info.locate_pt) != CGAL::ON_UNBOUNDED_SIDE);
		return;
	}

	intermediates.push_back(Point3(CGAL::to_double(march_info.locate_pt.x()), CGAL::to_double(march_info.locate_pt.y()), march_info.locate_height));

	CDT::Segment	ray(march_info.locate_pt, goal);
	int				cross_side;

	CDT::Geom_traits::Orientation_2 pred;

	while (1)
	{
		CDT::Point	last_pt;
		double		last_ht;

		CDT::Face_handle now = circ;
		++circ;
		CDT::Face_handle next = circ;

		if (!inMesh.is_infinite(now) && inMesh.triangle(now).bounded_side(goal) != CGAL::ON_UNBOUNDED_SIDE)
		{
			march_info.locate_pt = last_pt = goal;
			march_info.locate_height = last_ht = HeightWithinTri(inMesh, now, goal);
			march_info.locate_face = now;
			intermediates.push_back(Point3(CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()), last_ht));
			DebugAssert(!inMesh.is_infinite(march_info.locate_face));
			DebugAssert(inMesh.triangle(march_info.locate_face).bounded_side(march_info.locate_pt) != CGAL::ON_UNBOUNDED_SIDE);
			break;
		}

		if (now->has_neighbor(next))
		{
			cross_side = now->index(next);
			CDT::Segment crossed_seg = inMesh.segment(CDT::Edge(now, cross_side));

			CGAL::Orientation o1 = pred(ray.source(), ray.target(), crossed_seg.source());
			CGAL::Orientation o2 = pred(ray.source(), ray.target(), crossed_seg.target());

			// We can't both be any one value - that means the common side is on both tris -
			// one tri shouldn't be in the iteration!
			DebugAssert(o1 != o2);

			if (o1 == CGAL::COLLINEAR)
			{
				last_pt = now->vertex(CDT::ccw(cross_side))->point();
				last_ht = now->vertex(CDT::ccw(cross_side))->info().height ;
				intermediates.push_back(Point3(CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()), last_ht));
			} else if (o2 == CGAL::COLLINEAR)
			{
				last_pt = now->vertex(CDT::cw(cross_side))->point();
				last_ht = now->vertex(CDT::cw(cross_side))->info().height ;
				intermediates.push_back(Point3(CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()), last_ht));

			} else {
				CGAL::Object o = CGAL::intersection(ray, crossed_seg);
				if (CGAL::assign(last_pt, o))
				{
					Bbox_2 lim_ray = CDT::Segment(march_info.locate_pt,goal).bbox();
					Bbox_2 lim_seg = crossed_seg.bbox();
					Point_2	result(last_pt.x(),last_pt.y());
					if(!CGAL::do_overlap(lim_ray, result.bbox()))
					{
						#if DEBUG_DROPPED_PTS
							printf("WARNING: failed intersection: %.10lf, %.10lf\n",last_pt.x(),last_pt.y());
							gMeshPoints.push_back(pair<Point_2,Point_3>(last_pt,Point_3(1,1,0)));
							gMeshLines.push_back(pair<Point_2,Point_3>(march_info,Point_3(0,1,0)));
							gMeshLines.push_back(pair<Point_2,Point_3>(goal,Point_3(0,1,0)));
							gMeshLines.push_back(pair<Point_2,Point_3>(crossed_seg.source(),Point_3(0,0,1)));
							gMeshLines.push_back(pair<Point_2,Point_3>(crossed_seg.target(),Point_3(0,0,1)));
						#endif
					} else {

						last_ht = HeightWithinTri(inMesh, now, last_pt) ;
						intermediates.push_back(Point3(CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()), last_ht));
					}

				} else {
#if DEV
					printf("Ray: %lf,%lf->%lf,%lf\nSide: %lf,%lf->%lf,%lf\n",
						CGAL::to_double(ray.source().x()), CGAL::to_double(ray.source().y()),
						CGAL::to_double(ray.target().x()), CGAL::to_double(ray.target().y()),
						CGAL::to_double(crossed_seg.source().x()), CGAL::to_double(crossed_seg.source().y()),
						CGAL::to_double(crossed_seg.target().x()), CGAL::to_double(crossed_seg.target().y()));
#endif
					AssertPrintf("Intersection failed.");
				}
			}
		}
		else if (common_vertex(now, next, cross_side))
		{
			last_pt = now->vertex(cross_side)->point();
			last_ht = now->vertex(cross_side)->info().height ;
			printf("On Vertex: %lf, %lf\n", CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()));
			intermediates.push_back(Point3(CGAL::to_double(last_pt.x()), CGAL::to_double(last_pt.y()), last_ht));
		} else
			AssertPrintf("Cannot determine relationship between triangles!");

		// If we hit our goal dead-on, great!
		if (last_pt == goal)
		{
			march_info.locate_pt = last_pt;
			march_info.locate_height = last_ht;
			march_info.locate_face = next;
			DebugAssert(!inMesh.is_infinite(march_info.locate_face));
			DebugAssert(inMesh.triangle(march_info.locate_face).bounded_side(march_info.locate_pt) != CGAL::ON_UNBOUNDED_SIDE);
			break;
		}

/*
		// VERY STRANGE: given a simple horizontal line case, collinear_has_on is returning CRAP results.
		if (!ray.collinear_has_on(last_pt))
		{
			intermediates.pop_back();
			march_info.locate_pt = last_pt = goal;
			march_info.locate_height = last_ht = HeightWithinTri(now, goal.x(), goal.y());
			march_info.locate_face = now;
			intermediates.push_back(Point_3(last_pt.x(), last_pt.y(), last_ht));
			DebugAssert(!inMesh.is_infinite(march_info.locate_face));
			DebugAssert(inMesh.triangle(march_info.locate_face).bounded_side(march_info.locate_pt) != CGAL::ON_UNBOUNDED_SIDE);
			break;
		}
*/
		DebugAssert(circ != stop);
	}
}


