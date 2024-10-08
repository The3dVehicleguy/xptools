/*
 * Copyright (c) 2018, Laminar Research.
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

#include "WED_NavaidLayer.h"
#include "GUI_Pane.h"

#include "GUI_DrawUtils.h"
#include "WED_DrawUtils.h"
#include "GUI_Fonts.h"
#include "GUI_GraphState.h"

#include "WED_Colors.h"
#include "WED_MapZoomerNew.h"
#include "WED_PackageMgr.h"
#include "MemFileUtils.h"
#include "PlatformUtils.h"
#include "GISUtils.h"

#if APL
	#include <OpenGL/gl.h>
#else
	#include <GL/gl.h>
#endif

#define SHOW_TOWERS           1
#define SHOW_LPV              0       // ILS-like GPS final approaches
#define SHOW_TRACON           1
#define MAX_ALT				 60       // ignore sectors starting above this (in x100ft)
#define INC_ALT				 15       // ignore sectors starting more than this above the lowest sector

#define SHOW_APTS_FROM_APTDAT 1
#define COMPARE_GW_TO_APTDAT  0       // loads list of all airports from gateway and comares it to local apt.dat data

#define NAVAID_EXTRA_RANGE  GLOBAL_WED_ART_ASSET_FUDGE_FACTOR  // degree's lon/lat, shows ILS beams even if ILS outside of map window

#if COMPARE_GW_TO_APTDAT

#include <json/json.h>
#include "RAII_Classes.h"
#include "WED_FileCache.h"
#include "WED_Url.h"
#include "FileUtils.h"
#include "GUI_Resources.h"

static void get_airports(map<string, navaid_t>& tAirports)
{
	WED_file_cache_request	mCacheRequest;
	
	mCacheRequest.in_domain = cache_domain_airports_json;
	mCacheRequest.in_folder_prefix = "scenery_packs" DIR_STR "GatewayImport";
	mCacheRequest.in_url = WED_get_GW_url() + "airports";

	WED_file_cache_response res = gFileCache.request_file(mCacheRequest);

	sleep(3);

	string json_string;
	
	if(res.out_status == cache_status_available)
	{
		//Attempt to open the file we just downloaded
		RAII_FileHandle file(res.out_path.c_str(),"r");
//		RAII_FileHandle file("/home/xplane/.cache/wed_file_cache/scenery_packs/GatewayImport/airports","r");

		if(FILE_read_file_to_string(file(), json_string) == 0)
		{
			file.close();
		}
		else
		  printf("cant read\n");
	}
	
	Json::Value root;
	Json::Reader reader;
	bool success = reader.parse(json_string, root);

	//Check for errors
	if(success == false)
	{
		printf("no airports\n");
		return;
	}

	Json::Value mAirportsGET = Json::Value(Json::arrayValue);
	mAirportsGET.swap(root["airports"]);

	for (int i = 0; i < mAirportsGET.size(); i++)
	{
		//Get the current scenery object
		Json::Value tmp(Json::objectValue);
		tmp = mAirportsGET.operator[](i);       //Yes, you need the verbose operator[] form. Yes it's dumb

		navaid_t n;
		
		n.icao =	tmp["AirportCode"].asString();
		n.name =	tmp["AirportName"].asString();
		n.heading = tmp["ApprovedSceneryCount"].asInt() > 0 ?  1 : 0;  // 3D or just 2D
		n.type = 10000 + 1; // + tmp["AirportClass"].asInt();    // rowcode 17= seaport, 1=airport, 16=heliport
		n.lonlat = Point2(tmp["Longitude"].asDouble(), tmp["Latitude"].asDouble());
		
		if(tmp["ApprovedSceneryCount"].asInt() > 0)
			n.rwy = "(GW)";
		else
			n.rwy = "(WEDbot)";
			
		tAirports[n.icao]= n;
	}
}
#endif

static void parse_apt_dat(MFMemFile * str, map<string, navaid_t>& tAirports, const string& source)
{
	MFScanner	s;
	MFS_init(&s, str);
	int versions[] = { 1000, 1021, 1050, 1100, 1130, 1200, 0 };
		
	if(MFS_xplane_header(&s,versions,NULL,NULL))
	{
		int apt_type = 0;
		Bbox2 apt_bounds;
		navaid_t n;

		while(!MFS_done(&s))
		{
			int rowcode = MFS_int(&s);
			if (rowcode == 1 || rowcode == 16 || rowcode == 17 || rowcode == 99)   // look only for accept only ILS component overrides
			{
				if(apt_type)
				{
					n.lonlat = apt_bounds.centroid();
					tAirports[n.icao]= n;
				}
				apt_type = rowcode;
				apt_bounds = Bbox2();
				n.type = 10000 + rowcode;
				n.heading = 0;  // going to store ATC tower frequency presence here.
				MFS_int(&s);	// skip elevation
				MFS_int(&s);
				MFS_int(&s);

				MFS_string(&s,&n.icao);
				MFS_string_eol(&s,&n.name);
				n.rwy = source;
			}
			else if(apt_type)
			{
				if((rowcode >=  111 && rowcode <=  116) ||
					rowcode == 1201 || rowcode == 1300  ||
					(rowcode >=   18 && rowcode <=   21))
				{
					double lat = MFS_double(&s);
					double lon = MFS_double(&s);
					apt_bounds += Point2(lon,lat);
				}
				else if (rowcode == 100) // runways
				{
					MFS_double(&s);  // width
					MFS_double(&s); MFS_double(&s); MFS_double(&s);
					MFS_double(&s); MFS_double(&s); MFS_double(&s);
					MFS_string(&s, NULL);
					double lat = MFS_double(&s);
					double lon = MFS_double(&s);
					MFS_double(&s); MFS_double(&s); MFS_double(&s);
					MFS_double(&s); MFS_double(&s); MFS_double(&s);
					apt_bounds += Point2(lon,lat);
					MFS_string(&s, NULL);
					lat = MFS_double(&s);
					lon = MFS_double(&s);
					apt_bounds += Point2(lon,lat);
				}
				else if (rowcode == 101) // sealanes
				{
					MFS_double(&s);
					MFS_double(&s);
					MFS_string(&s, NULL);
					double lat = MFS_double(&s);
					double lon = MFS_double(&s);
					apt_bounds += Point2(lon,lat);
					MFS_string(&s, NULL);
					lat = MFS_double(&s);
					lon = MFS_double(&s);
					apt_bounds += Point2(lon,lat);
				}
				else if (rowcode == 102) // helipads
				{
					MFS_string(&s, NULL);
					double lat = MFS_double(&s);
					double lon = MFS_double(&s);
					apt_bounds += Point2(lon,lat);
				}
				else if (rowcode == 54 || rowcode == 1054) // ATC frequency
				{
					n.heading = 1;  // ATC tower frequency presence
				}
			}
			MFS_string_eol(&s,NULL);
		}
	MemFile_Close(str);
	}
}

void WED_NavaidLayer::parse_nav_dat(MFMemFile * str, bool merge)
{
	MFScanner	s;
	MFS_init(&s, str);
	int versions[] = { 810, 1050, 1100, 1150, 1200, 0 };
		
	if(MFS_xplane_header(&s,versions,NULL,NULL))
	{
		while(!MFS_done(&s))
		{
			int type = MFS_int(&s);
			int first_type = merge ? 4 : 2;         // accept only ILS component overrides when merging
			if ((type >= first_type && type <= 9)   // NDB, VOR and ILS components
				|| (SHOW_LPV && (type == 14)) )     // GPS final approaches
			{
				navaid_t n;
				n.type = type;
				double lat = MFS_double(&s);
				double lon = MFS_double(&s);
				n.lonlat = Point2(lon,lat);
				MFS_int(&s);   // skip elevation
				n.freq  = MFS_int(&s);
				MFS_double(&s);                   // skip range, can be floats in XP12
				double d = MFS_double(&s);
				if (type == 6)
					n.heading = fmod(d, 1000.0);  // zero out the topmost 3 digits - its the glideslope
				else
					n.heading = fmod(d, 360.0);   // zero out multiples of 360 - its magnetic bearing stuff
				MFS_string(&s, &n.name);
				MFS_string(&s, &n.icao);
				if (type >= 4 && type <= 9)   // ILS components
				{
					MFS_string(&s, NULL);  // skip region
					MFS_string(&s, &n.rwy);
				}
				if(merge)
				{
					// check for duplicates before adding this new one
					auto i = mNavaids.cbegin();
					float closest_d = 9999.0;
					auto closest_i = i;
					while(i != mNavaids.cend())
					{
						if(n.type == i->type && n.icao == i->icao)
						{
							float d = LonLatDistMeters(n.lonlat, i->lonlat);
							if (n.name == i->name)
							{
//								printf("Replacing exact type %d, icao %s & name %s match. d=%5.1lfm\n", n.type, n.icao.c_str(), n.name.c_str(), d);
								mNavaids.replace(i,n);
								break;
							}
							if(d < closest_d)
							{
								closest_d = d;
								closest_i = i;
//								printf("Name mismatch, keeping type %d, icao %s at d=%5.1lfm in mind, name=%s,%s\n", n.type, n.icao.c_str(), d, i->name.c_str(), n.name.c_str());
							}
						}
						++i;
					}

					if (i == mNavaids.cend())
					{
						if (closest_d < 20.0)
						{
//							printf("Replacing despite name %s,%s", closest_i->name.c_str(), n.name.c_str());
//							if (closest_i->freq != n.freq) printf(" and frequency %d,%d", closest_i->freq, n.freq );
//							printf(" mismatch, type %d, icao %s d=%5.1lfm\n", n.type, n.icao.c_str(), closest_d);
							mNavaids.replace(closest_i, n);
						}
						else
						{
//							printf("Adding new %d %s %s\n", n.type, n.name.c_str(), n.icao.c_str());
							mNavaids.insert(n);
						}
					}

				}
				else // not merging, take them all
					mNavaids.insert(n);
			}
			MFS_string_eol(&s,NULL);
		}

		auto i = mNavaids.cbegin();

	}
	MemFile_Close(str);
}

void WED_NavaidLayer::parse_atc_dat(MFMemFile * str)
{
	MFScanner	s;
	MFS_init(&s, str);
	int versions[] = { 1000, 1100, 0 };
	struct airspace {
		int bottom, top;
		Polygon2  shape;
	};
	vector<airspace> all_air;

	if(MFS_xplane_header(&s,versions,"ATCFILE",NULL))
	{
		navaid_t n;
		while(!MFS_done(&s))
		{
			if(MFS_string_match(&s, "CONTROLLER", 1))
			{
				n.type = 0;
				n.shape.clear();
				n.lonlat = Point2(180.0,0.0);
				n.rwy.clear();
				all_air.clear();
			}
			else if(MFS_string_match(&s, "ROLE", 0))
			{
				string role;
				MFS_string(&s, &role);
#if SHOW_TRACON
				if(role == "tracon") n.type = 9999;   // navaid pseudo code for TRACON areas
#endif
#if SHOW_TOWERS
				if(role == "twr") n.type = 9998;      // navaid pseudo code for TOWER areas
#endif
			}
			else if(MFS_string_match(&s, "NAME", 0))
			{
				MFS_string_eol(&s, &n.name);
			}
			else if(MFS_string_match(&s, "FACILITY_ID", 0))
			{
				MFS_string(&s, &n.icao);
			}
			else if(n.type)
			{
				if(MFS_string_match(&s, "POINT", 0))
				{
					if(all_air.size())
					{
						double lat = MFS_double(&s);
						double lon = MFS_double(&s);
						all_air.back().shape.push_back(Point2(lon, lat));
					}
				}
				else if (MFS_string_match(&s, "FREQ", 0))
				{
					string tmp;
					MFS_string(&s, &tmp);
					if (!n.rwy.empty()) n.rwy += ", ";
					n.rwy += tmp.substr(0, tmp.size() - 2) + "." + tmp.substr(tmp.size() - 2);
				}
				else if (MFS_string_match(&s, "CHAN", 0))
				{
					string tmp;
					MFS_string(&s, &tmp);
					if (!n.rwy.empty()) n.rwy += ", ";
					n.rwy += tmp.substr(0, tmp.size() - 3) + "." + tmp.substr(tmp.size() - 3);
				}
				else if(MFS_string_match(&s, "AIRSPACE_POLYGON_BEGIN", 0))
				{
					all_air.push_back(airspace());
					all_air.back().bottom = round(MFS_double(&s) / 100.0);
					all_air.back().top    = round(MFS_double(&s) / 100.0);
				}
				else if(MFS_string_match(&s, "CONTROLLER_END", 1) && n.type)
				{
					if (n.type == 9998)
						n.name += " TOWER";
					else if (n.type == 9999)
					{
						if (all_air.back().shape[0].x() < -32.0)  // western hemisphere, USA
							n.name += " APPROACH";
						else if(n.name.find("RADAR") == string::npos)
							n.name += " RADAR";
					}
					n.rwy += " MHz";

					int lowest = 999;
					for (auto& a : all_air)
						if (a.bottom < lowest) lowest = a.bottom;

					if (lowest <= MAX_ALT)
					{
						for (auto& a : all_air)
							if (a.bottom <= lowest + INC_ALT)
							{
								string tmp(to_string(a.bottom) + "-" + to_string(a.top));
								if (n.name.find(tmp) == string::npos)
									n.name += string("  ") + tmp;
								n.shape.push_back(a.shape);
								for (auto& p : n.shape.back())
									if (p.x() < n.lonlat.x())
										n.lonlat = p;             // place label on leftmost edge of the airspace
							}
						for (auto nav = mNavaids.cbegin(); nav != mNavaids.cend(); nav++)
							if (LonLatDistMeters(nav->lonlat, n.lonlat) < 2000.0)
							{
								n.lonlat.y_ += 0.02;     // avoid two labels right ontop of each other
								break;
							}
						mNavaids.insert(n);
					}
				}
			}
			MFS_string_eol(&s,NULL);
		}
	}
	MemFile_Close(str);
}


WED_NavaidLayer::WED_NavaidLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host,zoomer,resolver)
{
    SetVisible(false);
	// ToDo: when using the gateway JSON data, initiate asynchronous load/update here.
}

WED_NavaidLayer::~WED_NavaidLayer()
{
}

WED_NavaidLayer::navaid_list::navaid_list() : best_begin(-1)
{ 
	nav_list.reserve(60000); // about 6 MBytes, as of 2024 its some 59000 navaids including ATC areas
}

vector<navaid_t>::const_iterator WED_NavaidLayer::navaid_list::cbegin(double longitude)
{
	if (best_begin < 0)
	{
		std::sort(nav_list.begin(), nav_list.end(), [](const navaid_t& a, const navaid_t& b)
			{ return a.lonlat.x() < b.lonlat.x(); });
		best_begin = nav_list.size() / 2;
	}

	if (longitude > nav_list[best_begin].lonlat.x())
	{
		while (best_begin < nav_list.size()-1 && longitude > nav_list[best_begin + 1].lonlat.x())
			best_begin++;
	}
	else
	{
		while (best_begin > 0 && longitude < nav_list[best_begin].lonlat.x())
			best_begin--;
	}

	return nav_list.begin() + best_begin;
}

void WED_NavaidLayer::navaid_list::insert(const navaid_t& aid)
{
	nav_list.push_back(aid);
	best_begin = -1;  // forces (re-)sort after any new insertions
}

void WED_NavaidLayer::navaid_list::replace(vector<navaid_t>::const_iterator where, const navaid_t& aid)
{
	const_cast<navaid_t&>(*where) = aid;
}

void WED_NavaidLayer::LoadNavaids(void)
{
// ToDo: move this into PackageMgr, so its updated when XPlaneFolder changes and re-used when another scenery is opened
	string resourcePath;
	gPackageMgr->GetXPlaneFolder(resourcePath);

	// deliberately ignoring any Custom Data/earth_424.dat or Custom Data/earth_nav.dat files that a user may have ... to avoid confusion
	string defaultNavaids  = resourcePath + DIR_STR "Resources" DIR_STR "default data" DIR_STR "earth_nav.dat";
	string globalNavaids = DIR_STR "Global Airports" DIR_STR "Earth nav data" DIR_STR "earth_nav.dat";
	
	MFMemFile * str = MemFile_Open(defaultNavaids.c_str());
	if(str) parse_nav_dat(str, false);

	str = MemFile_Open((resourcePath + DIR_STR "Global Scenery" + globalNavaids).c_str());
	if (!str)
		str = MemFile_Open((resourcePath + DIR_STR "Custom Scenery" + globalNavaids).c_str());
	if(str)	parse_nav_dat(str, true);
	
	string defaultATC = resourcePath + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR "default atc dat" DIR_STR "Earth nav data" DIR_STR "atc.dat";
	str = MemFile_Open(defaultATC.c_str());
	// on the linux and OSX platforms this path was different before XP11.30 for some unknown reasons. So try that, too.
	if(!str)
	{
		defaultATC = resourcePath + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR "default atc" DIR_STR "Earth nav data" DIR_STR "atc.dat";
		str = MemFile_Open(defaultATC.c_str());
	}
	// in XP12 the file again changed to a different location
	if (!str)
	{
		defaultATC = resourcePath + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR "1200 atc data" DIR_STR "Earth nav data" DIR_STR "atc.dat";
		str = MemFile_Open(defaultATC.c_str());
	}
	if(str)	parse_atc_dat(str);

//	str = MemFile_Open(seattleATC.c_str());  // don't take local local ATC data any more
//	if(str)	parse_atc_dat(str, mNavaids);
	
#if SHOW_APTS_FROM_APTDAT
	map<string,navaid_t> tAirports;
	string defaultApts = resourcePath + DIR_STR "Resources" DIR_STR "default scenery" DIR_STR "default apt dat" DIR_STR "Earth nav data" DIR_STR "apt.dat";
	string globalApts  = DIR_STR "Global Airports" DIR_STR "Earth nav data" DIR_STR "apt.dat";

	str = MemFile_Open(defaultApts.c_str());
	if(str) parse_apt_dat(str, tAirports, "");
	str = MemFile_Open((resourcePath + DIR_STR "Global Scenery" + globalApts).c_str());
	if(!str)
		str = MemFile_Open((resourcePath + DIR_STR "Custom Scenery" + globalApts).c_str());
	if(str) parse_apt_dat(str, tAirports, "");

#if COMPARE_GW_TO_APTDAT
	map<string,navaid_t> tAirp;
	get_airports(tAirp);
	
	for(auto a : tAirp)
	{
		auto b = tAirports.find(a.first);
		if (b != tAirports.end())
		{
			double dist = LonLatDistMeters(a.second.lonlat, b->second.lonlat);
			if (dist < 150000)
				printf("  matched %7s ll=%8.3lf %7.3lf d=%5.1lf km %s\n", a.first.c_str(), a.second.lonlat.x(), a.second.lonlat.y(), dist/1000.0, dist < 1000.0 ? "Good !" : "");
			else
				printf("  matched %7s ll=%8.3lf %7.3lf d=%5.0lf km Wow ! apt.dat ll=%8.3lf %7.3lf\n", a.first.c_str(), a.second.lonlat.x(), a.second.lonlat.y(), dist/1000.0, b->second.lonlat.x(), b->second.lonlat.y());
			if(dist > 1000.0)
				printf("UPDATE airports SET Latitude=%.3lf, Longitude=%.3lf WHERE AirportCode=\"%s\";\n", b->second.lonlat.y(), b->second.lonlat.x(), a.first.c_str());
		}
		else
			printf("unmatched %7s ll=%8.3lf %7.3lf\n", a.first.c_str(), a.second.lonlat.x(), a.second.lonlat.y());
	}
#endif

	for(auto& i : tAirports)
		mNavaids.insert(i.second);

#endif

// Todo: speedup drawing by sorting mNavaids into longitude buckets, so the preview function only have to go through a smalller part of the overall list.
//       although for now Navaid map drawing is under 1 msec on a 3.6 GHz CPU at all times == good enough
}

void		WED_NavaidLayer::DrawVisualization		(bool inCurrent, GUI_GraphState * g)
{
	double ll,lb,lr,lt;	// logical boundary
	double vl,vb,vr,vt;	// visible boundry

	if(mNavaids.empty()) LoadNavaids();

	GetZoomer()->GetMapLogicalBounds(ll,lb,lr,lt);
	GetZoomer()->GetMapVisibleBounds(vl,vb,vr,vt);

	vl = max(vl,ll) - NAVAID_EXTRA_RANGE;
	vb = max(vb,lb) - NAVAID_EXTRA_RANGE;
	vr = min(vr,lr) + NAVAID_EXTRA_RANGE;
	vt = min(vt,lt) + NAVAID_EXTRA_RANGE;

	double PPM = GetZoomer()->GetPPM();
	double scale = GetAirportIconScale();
	double beam_len = 3300.0/scale * PPM;

	const float red[4]        = { 1.0, 0.4, 0.4, 0.66 };
	const float vfr_purple[4] = { 0.9, 0.4, 0.9, 0.8 };
	const float vfr_blue[4]   = { 0.4, 0.4, 1.0, 0.8 };

	g->SetState(false,0,false,false,true,false,false);
	glLineWidth(1.6);
	glLineStipple(1, 0xF0F0);
	glDisable(GL_LINE_STIPPLE);
	
	if (PPM > 0.0005)          // stop displaying navaids when zoomed out - gets too crowded
		for(auto i = mNavaids.cbegin(vl); i != mNavaids.cend(); ++i)  // vector is sorted by longitude, skip what does not need to be iterated over
		{
			if(i->lonlat.x() > vr)
				break;
			if(i->lonlat.y() > vb && i->lonlat.y() < vt)
			{
				glColor4fv(red);
				Point2 pt = GetZoomer()->LLToPixel(i->lonlat);
				
				// draw icons
				if(i->type == 2)
					GUI_PlotIcon(g,"nav_ndb.png", pt.x(), pt.y(), 0.0, scale);
				else if(i->type == 3)
				{
					GUI_PlotIcon(g,"nav_vor.png", pt.x(), pt.y(), i->heading, scale);
				}
				else if(i->type <= 5 || i->type == 14)
				{
					Vector2 beam_dir(0.0, beam_len);
					beam_dir.rotate_by_degrees(180.0-i->heading);
					Vector2 beam_perp(beam_dir.perpendicular_cw()*0.1);

					g->SetState(0, 0, 0, 0, 1, 0, 0);
					if (i->type == 14)
						glEnable(GL_LINE_STIPPLE);
					glBegin(GL_LINE_STRIP);
						glVertex2(pt);
						glVertex2(pt + beam_dir*1.1 + beam_perp);
						glVertex2(pt + beam_dir);
						glVertex2(pt + beam_dir*1.1 - beam_perp);
						glVertex2(pt);
						glVertex2(pt + beam_dir);
					glEnd();
					glDisable(GL_LINE_STIPPLE);
				}
				else if(i->type == 6)
				{
					if(PPM > 0.1)
						GUI_PlotIcon(g,"nav_gs.png", pt.x(), pt.y(), i->heading, scale);
				}
				else if(i->type < 100)
					GUI_PlotIcon(g,"nav_mark.png", pt.x(), pt.y(), i->heading, scale);
				else if(i->type <= 9999)
				{
#if SHOW_TOWERS
					if (i->type == 9998)
						glEnable(GL_LINE_STIPPLE);
#endif					
					g->SetState(0, 0, 0, 0, 1, 0, 0);
					for (auto& p : i->shape)
					{
						glColor4fv(vfr_blue);
						int pts = p.size();
						vector<Point2> c(pts);
						GetZoomer()->LLToPixelv(&(c[0]), p.data(), pts);
						glShape2v(GL_LINE_LOOP, &(c[0]), pts);
					}
#if SHOW_TOWERS
					glDisable(GL_LINE_STIPPLE);
#endif					
				}
				else     // some airport
				{
					if(PPM > 0.002)
					{
						glColor4fv(i->heading ? vfr_blue : vfr_purple);
						if (i->type == 10017)
						{
							if(PPM > 0.02) GUI_PlotIcon(g,"map_helipad.png", pt.x(), pt.y(), 0.0, scale);
						}
						else if (i->type == 10016)
							GUI_PlotIcon(g,"navmap_seaport.png", pt.x(), pt.y(), 0.0, scale);
						else
							GUI_PlotIcon(g,"navmap_airport.png", pt.x(), pt.y(), 0.0, scale);
					}
				}
				// draw text labels, be carefull not to clutter things
#if SHOW_TOWERS
				if((i->type == 9998 && PPM  > 0.01) || i->type == 9999)
#else
				if(i->type == 9999) // Airspace labels/frequencies
#endif					
				{
					if (PPM > 0.01)
					{
						const float* color = vfr_blue;
						GUI_FontDraw(g, font_UI_Basic, color, pt.x() + 8.0, pt.y() - 15.0, i->name.c_str());
						GUI_FontDraw(g, font_UI_Basic, color, pt.x() + 8.0, pt.y() - 30.0, i->rwy.c_str());
					}
				}
				else if (PPM > 0.05)// Navaid labels
				{
					if(i->type > 10000)
					{
						const float * color = i->heading ? vfr_blue : vfr_purple;
						GUI_FontDraw(g, font_UI_Basic, color, pt.x()+15.0,pt.y()-20.0, i->name.c_str());
						GUI_FontDraw(g, font_UI_Basic, color, pt.x()+15.0,pt.y()-35.0, (string("Airport ID") + i->rwy + ": " + i->icao).c_str());
					}
					else if(PPM > 0.5)
					{
						GUI_FontDraw(g, font_UI_Basic, red, pt.x()+20.0,pt.y()-25.0, i->name.c_str());
						GUI_FontDraw(g, font_UI_Basic, red, pt.x()+20.0,pt.y()-40.0, (i->icao + " " + i->rwy).c_str());
					}
				}
			}
		}
		glLineWidth(1.0);
}

void		WED_NavaidLayer::GetCaps(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks)
{
	draw_ent_v = draw_ent_s = cares_about_sel = wants_clicks = 0;
}
