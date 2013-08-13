/*
 * Copyright (c) 2007, Laminar Research.
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

#include "WED_MapPane.h"
#include "WED_Archive.h"
#include "WED_Menus.h"
#include "GUI_ToolBar.h"
#include "XESConstants.h"
#include "IGIS.h"
#include "ISelection.h"
#include "WED_Thing.h"
#include "WED_Map.h"
#include "WED_MapBkgnd.h"
#include "WED_ToolUtils.h"
#include "WED_MarqueeTool.h"
#include "WED_CreateBoxTool.h"
#if AIRPORT_ROUTING
#include "WED_CreateEdgeTool.h"
#endif
#include "WED_CreatePolygonTool.h"
#include "WED_CreatePointTool.h"
#include "WED_CreateLineTool.h"
#include "WED_StructureLayer.h"
#include "WED_WorldMapLayer.h"
#include "WED_PreviewLayer.h"
#include "WED_DebugLayer.h"
#include "WED_VertexTool.h"
//#include "WED_TileServerLayer.h"
#include "WED_TerraserverLayer.h"
#include "GUI_Fonts.h"
#include "GUI_Table.h"
#include "GUI_TextTable.h"
#include "WED_Colors.h"
#include "GUI_Resources.h"
#include "WED_ToolInfoAdapter.h"
#include "WED_UIMeasurements.h"
#include "WED_GroupCommands.h"
#include "WED_LibraryListAdapter.h"
#include "WED_LibraryMgr.h"
#include "IDocPrefs.h"
#if WITHNWLINK
#include "WED_Document.h"
#include "WED_Server.h"
#include "WED_NWInfoLayer.h"
#endif
char	kToolKeys[] = {
	0, 0, 0, 0,
	0, 0, 0, 0,

	'b', 0, 'a', 'o',
	'e', 'w', 'f', 'g',

	'l', 'k', 'h', 't',
	'r', 's', 'v', 'm'
};


// A bit of a hack...zoom to selection sets the zoom so that the screen is filled with the sel.  If the sel size is 0 in both
// dimensions, our zoom is NaN, which is bad. But try telling that to users!
//
// So....IF the selected entity is a point AND it doesn't have an overloaded bounds that gives it some thickness, we apply this 
// extra padding (in meters) around it.  The result is that we always zoom out enough to show 50 meters around point objects.
// In practice this should be okay - you probably want to see SOME of what's happening, and padding a small distance around your 
// airport when you have perimeter objects isn't going to kill anything.  We can tune the dimensions as desired.
#define PAD_POINTS_FOR_ZOOM_MTR 50.0

static void GetExtentAll(Bbox2& box, IResolver * resolver)
{
	box = Bbox2();
	WED_Thing * wrl = WED_GetWorld(resolver);
	IGISEntity * ent = dynamic_cast<IGISEntity *>(wrl);
	if (ent) ent->GetBounds(gis_Geo,box);
}

static int accum_box(ISelectable * who, void * ref)
{
	Bbox2 * total = (Bbox2 *) ref;
	IGISEntity * ent = dynamic_cast<IGISEntity *>(who);
	if (ent)
	{
		Bbox2 ent_box;
		ent->GetBounds(gis_Geo,ent_box);
		GISClass_t gc = ent->GetGISClass();
		if(gc == gis_Point || gc == gis_Point_Bezier || gc == gis_Point_Heading)
		{
			if(ent_box.is_empty())
			{
				double lat = ent_box.ymin();
				double MTR_TO_DEG_LON = MTR_TO_DEG_LAT * cos(lat * DEG_TO_RAD);
				ent_box.expand(PAD_POINTS_FOR_ZOOM_MTR * MTR_TO_DEG_LON,PAD_POINTS_FOR_ZOOM_MTR * MTR_TO_DEG_LAT);
			}
		}

		if(!ent_box.is_null())
			*total += ent_box;
	}
	return 0;
}

static void GetExtentSel(Bbox2& box, IResolver * resolver)
{
	box = Bbox2();
	ISelection * sel = WED_GetSelect(resolver);
	sel->IterateSelectionOr(accum_box,&box);
}


WED_MapPane::WED_MapPane(GUI_Commander * cmdr, double map_bounds[4], IResolver * resolver, WED_Archive * archive, WED_LibraryListAdapter * library) : mResolver(resolver)
{
	this->SetBkgkndImage("gradient.png");

	mMap = new WED_Map(resolver);

	// Visualization layers
	mLayers.push_back(					new WED_MapBkgnd(mMap, mMap, resolver));
	mLayers.push_back(mWorldMap =		new WED_WorldMapLayer(mMap, mMap, resolver));
#if WANT_TERRASEVER	
	mLayers.push_back(mTerraserver = 	new WED_TerraserverLayer(mMap, mMap, resolver));
#endif	
	mLayers.push_back(mStructureLayer = new WED_StructureLayer(mMap, mMap, resolver));
	mLayers.push_back(mPreview =		new WED_PreviewLayer(mMap, mMap, resolver));
//	mLayers.push_back(mTileserver =		new WED_TileServerLayer(mMap, mMap, resolver));
	mLayers.push_back(					new WED_DebugLayer(mMap, mMap, resolver));
#if WITHNWLINK
	WED_NWLinkAdapter * nwlink = (dynamic_cast<WED_Document *>(resolver))->GetNWLink();
	if(nwlink)
	{
		mLayers.push_back(mNWInfoLayer = new WED_NWInfoLayer(mMap, mMap, resolver,nwlink));
		nwlink->AddListener(mNWInfoLayer);
	}
#endif
	// TOOLS

	mTools.push_back(					new WED_CreateBoxTool("Exclusions",mMap, mMap, resolver, archive, create_Exclusion));
#if ROAD_EDITING
	mTools.push_back(					new WED_CreateEdgeTool("Roads",mMap, mMap, resolver, archive, create_Road));
#else
	mTools.push_back(					NULL);
#endif

	mTools.push_back(mLinTool=			new WED_CreatePolygonTool("Lines",mMap, mMap, resolver, archive, create_Line));
	mTools.push_back(mPolTool=			new WED_CreatePolygonTool("Polygons",mMap, mMap, resolver, archive, create_Polygon));

	mTools.push_back(mFstTool=			new WED_CreatePolygonTool("Forests",mMap, mMap, resolver, archive, create_Forest));
	mTools.push_back(mStrTool=			new WED_CreatePolygonTool("Strings",mMap, mMap, resolver, archive, create_String));

	mTools.push_back(mObjTool=			new WED_CreatePointTool("Objects",mMap, mMap, resolver, archive, create_Object));
	mTools.push_back(mFacTool=			new WED_CreatePolygonTool("Facades",mMap, mMap, resolver, archive, create_Facade));


	mTools.push_back(					new WED_CreatePolygonTool("Boundary",mMap, mMap, resolver, archive, create_Boundary));
#if AIRPORT_ROUTING
	mTools.push_back(					new WED_CreateEdgeTool("Taxi Routes",mMap, mMap, resolver, archive, create_TaxiRoute));
#else
	mTools.push_back(					NULL);
#endif

	mTools.push_back(					new WED_CreatePointTool("Tower Viewpoint", mMap, mMap, resolver, archive, create_TowerViewpoint));
	mTools.push_back(					new WED_CreatePointTool("Ramp Start", mMap, mMap, resolver, archive, create_RampStart));

	mTools.push_back(					new WED_CreatePointTool("Airport Beacon", mMap, mMap, resolver, archive, create_Beacon));
	mTools.push_back(					new WED_CreatePointTool("Windsock", mMap, mMap, resolver, archive, create_Windsock));

	mTools.push_back(					new WED_CreatePointTool("Light Fixture", mMap, mMap, resolver, archive, create_Lights));
	mTools.push_back(					new WED_CreatePointTool("Sign", mMap, mMap, resolver, archive, create_Sign));

	mTools.push_back(					new WED_CreatePolygonTool("Taxilines",mMap, mMap, resolver, archive, create_Marks));
	mTools.push_back(					new WED_CreatePolygonTool("Hole",mMap, mMap, resolver, archive, create_Hole));

	mTools.push_back(					new WED_CreatePointTool("Helipad", mMap, mMap, resolver, archive, create_Helipad));
	mTools.push_back(					new WED_CreatePolygonTool("Taxiway",mMap, mMap, resolver, archive, create_Taxi));

	mTools.push_back(					new WED_CreateLineTool("Runway", mMap, mMap, resolver, archive, create_Runway));
	mTools.push_back(					new WED_CreateLineTool("Sealane", mMap, mMap, resolver, archive, create_Sealane));

	mTools.push_back(					new WED_VertexTool("Vertex",mMap, mMap, resolver, 1));
	mTools.push_back(					new WED_MarqueeTool("Marquee",mMap, mMap, resolver));

	mInfoAdapter = new WED_ToolInfoAdapter(GUI_GetImageResourceHeight("property_bar.png") / 2);
	mTextTable = new GUI_TextTable(cmdr,10,0);
	mTable = new GUI_Table(1);

	mTextTable->SetColors(
				WED_Color_RGBA(wed_Table_Gridlines),
				WED_Color_RGBA(wed_Table_Select),
				WED_Color_RGBA(wed_Table_Text),
				WED_Color_RGBA(wed_PropertyBar_Text),
				WED_Color_RGBA(wed_Table_Drag_Insert),
				WED_Color_RGBA(wed_Table_Drag_Into));
	mTextTable->SetFont(font_UI_Small);


	mTable->SetGeometry(mInfoAdapter);
	mTable->SetContent(mTextTable);
	mTextTable->SetProvider(mInfoAdapter);
//	mTable->SetParent(this);
//	mTable->Show();
	mTable->SizeShowAll();
	mTextTable->SetParentTable(mTable);
//	mTable->SetSticky(1,0,1,1);
//	this->PackPane(mTable, gui_Pack_Top);
	mTextTable->AddListener(mTable);
	mTextTable->SetImage("property_bar.png", 2);

	mInfoAdapter->AddListener(mTable);

	mToolbar = new GUI_ToolBar(2,12,"map_tools.png");
	mToolbar->SizeToBitmap();
	mToolbar->Show();
	mToolbar->SetParent(this);
	mToolbar->SetSticky(1,0,0,1);
	this->PackPane(mToolbar,gui_Pack_Left);
	mToolbar->SizeToBitmap();
	mToolbar->AddListener(this);
	vector<string>	tips;
	for (int n = 0; n < mTools.size(); ++n)
	{
		string tip(mTools[n] ? mTools[n]->GetToolName() : string());
		if (kToolKeys[n])
		{
			char buf[5] = { " [x]" };
			buf[2] = toupper(kToolKeys[n]);
			tip += buf;
		}
		tips.push_back(tip);

		if(mTools[n] == NULL)
			mToolbar->DisableTool(n);
	}
	mToolbar->SetToolTips(tips);

	GUI_ScrollerPane * map_scroller = new GUI_ScrollerPane(1,1);
	map_scroller->SetParent(this);
	map_scroller->Show();
	map_scroller->SetSticky(1,1,1,1);

	this->PackPane(map_scroller, gui_Pack_Center);

	mMap->SetParent(map_scroller);
	mMap->Show();
	map_scroller->PositionInContentArea(mMap);
	map_scroller->SetContent(mMap);

//	mMap->SetMapVisibleBounds(map_bounds[0], map_bounds[1], map_bounds[2], map_bounds[3]);
	mMap->SetMapLogicalBounds(map_bounds[0], map_bounds[1], map_bounds[2], map_bounds[3]);

	mMap->ZoomShowAll();

	for(vector<WED_MapLayer *>::iterator l = mLayers.begin(); l != mLayers.end(); ++l)
		mMap->AddLayer(*l);
	for(vector<WED_MapToolNew *>::iterator t = mTools.begin(); t != mTools.end(); ++t)
	if(*t)
		mMap->AddLayer(*t);

	mMap->SetTool(mTools[0]);
	mInfoAdapter->SetTool(mTools[0]);
	mToolbar->SetValue(mTools.size()-2);

	// This is a bit of a hack.  The archive provides whole-doc "changed" messages at the minimum global times:
	// 1. On the commit of any operation.
	// 2. On the undo or redo of any operation.
	// So ... for lack of a better idea right now, we simply broker a connection between the source opf these
	// messages (secretly it's our document's GetArchive() member) and anyone who needs it (our map).

	archive->AddListener(mMap);
}

GUI_Pane *	WED_MapPane::GetTopBar(void)
{
	return mTable;
}

WED_MapPane::~WED_MapPane()
{
	for(vector<WED_MapLayer *>::iterator l = mLayers.begin(); l != mLayers.end(); ++l)
		delete *l;
	for(vector<WED_MapToolNew *>::iterator t = mTools.begin(); t != mTools.end(); ++t)
	if(*t)
		delete *t;

	delete mTextTable;
	delete mInfoAdapter;

}

void WED_MapPane::SetResource(const string& r, int res_type)
{
	switch(res_type) {
	case res_Object:	mObjTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mObjTool)));	break;
	case res_Facade:	mFacTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mFacTool)));	break;
	case res_Forest:	mFstTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mFstTool)));	break;
	case res_String:	mStrTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mStrTool)));	break;
	case res_Line:		mLinTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mLinTool)));	break;
	case res_Polygon:	mPolTool->SetResource(r);	mToolbar->SetValue(distance(mTools.begin(),find(mTools.begin(),mTools.end(),mPolTool)));	break;
	}
}



void	WED_MapPane::ZoomShowAll(void)
{
//	double l,b,r,t;
//	mMap->GetMapLogicalBounds(l,b,r,t);
//	mMap->SetAspectRatio(1.0 / cos((b+t) * 0.5 * DEG_TO_RAD));
	mMap->ZoomShowAll();
}

void WED_MapPane::ZoomShowSel(void)
{
	Bbox2 box;
	GetExtentSel(box, mResolver); 
	if(!box.is_empty() && !box.is_null())
		mMap->ZoomShowArea(box.p1.x(),box.p1.y(),box.p2.x(),box.p2.y());	
	mMap->Refresh(); 
}

int		WED_MapPane::Map_KeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags)
{
	if (mMap->HandleKeyPress(inKey, inVK, inFlags)) return 1;
	if ((inFlags & (gui_ShiftFlag | gui_OptionAltFlag | gui_ControlFlag)) == 0)
	for (int n = 0; n < sizeof(kToolKeys) / sizeof(kToolKeys[0]); ++n)
	if (kToolKeys[n])
	if (kToolKeys[n]==inKey)
	{
		mToolbar->SetValue(n);
		return 1;
	}
	return 0;
}

int		WED_MapPane::Map_HandleCommand(int command, string* buf)
{
	Bbox2 box;
	
	switch(command) {
	case wed_ImportOrtho:	WED_DoMakeNewOverlay(mResolver, mMap, buf); return 1;
	//case wed_PickOverlay:	WED_DoMakeNewOverlay(mResolver, mMap,); return 1;
	case wed_ToggleWorldMap:mWorldMap->ToggleVisible(); return 1;
//	case wed_ToggleOverlay:	if (mImageOverlay->CanShow()) { mImageOverlay->ToggleVisible(); return 1; }
#if WANT_TERRASEVER
	case wed_ToggleTerraserver:	mTerraserver->ToggleVisible(); return 1;
#endif	
//	case wed_ToggleTileserver: mTileserver->ToggleVis(); return 1;
	case wed_TogglePreview:	mPreview->ToggleVisible(); return 1;

	case wed_Pavement0:		mPreview->SetPavementTransparency(0.0f);		return 1;
	case wed_Pavement25:	mPreview->SetPavementTransparency(0.25f);	return 1;
	case wed_Pavement50:	mPreview->SetPavementTransparency(0.5f);		return 1;
	case wed_Pavement75:	mPreview->SetPavementTransparency(0.75f);	return 1;
	case wed_Pavement100:	mPreview->SetPavementTransparency(1.0f);		return 1;

	case wed_ObjDensity1:	mPreview->SetObjDensity(1);	return 1;
	case wed_ObjDensity2:	mPreview->SetObjDensity(2);	return 1;
	case wed_ObjDensity3:	mPreview->SetObjDensity(3);	return 1;
	case wed_ObjDensity4:	mPreview->SetObjDensity(4);	return 1;
	case wed_ObjDensity5:	mPreview->SetObjDensity(5);	return 1;
	case wed_ObjDensity6:	mPreview->SetObjDensity(6);	return 1;

	case wed_ToggleLines:	mStructureLayer->SetRealLinesShowing(!mStructureLayer->GetRealLinesShowing());				return 1;
	case wed_ToggleVertices:mStructureLayer->SetVerticesShowing(!mStructureLayer->GetVerticesShowing());				return 1;

	case wed_ZoomWorld:		mMap->ZoomShowArea(-180,-90,180,90);	mMap->Refresh(); return 1;
	case wed_ZoomAll:		GetExtentAll(box, mResolver); mMap->ZoomShowArea(box.p1.x(),box.p1.y(),box.p2.x(),box.p2.y());	mMap->Refresh(); return 1;
	case wed_ZoomSelection:	GetExtentSel(box, mResolver); mMap->ZoomShowArea(box.p1.x(),box.p1.y(),box.p2.x(),box.p2.y());	mMap->Refresh(); return 1;

	default:		return 0;
	}
}

int		WED_MapPane::Map_CanHandleCommand(int command, string& ioName, int& ioCheck)
{
	Bbox2	box;

	switch(command) {
	case wed_PickOverlay:																	return 1;
	case wed_ToggleWorldMap:ioCheck = mWorldMap->IsVisible();								return 1;
//	case wed_ToggleOverlay:	if (mImageOverlay->CanShow()) { ioCheck = mImageOverlay->IsVisible(); return 1; }	break;
#if WANT_TERRASEVER
	case wed_ToggleTerraserver: ioCheck = mTerraserver->IsVisible();							return 1;
#endif	
//	case wed_ToggleTileserver: ioCheck = mTileserver->IsVis();								return 1;
	case wed_TogglePreview: ioCheck = mPreview->IsVisible();								return 1;
	case wed_Pavement0:		ioCheck = mPreview->GetPavementTransparency() == 0.0f;	return 1;
	case wed_Pavement25:	ioCheck = mPreview->GetPavementTransparency() == 0.25f;	return 1;
	case wed_Pavement50:	ioCheck = mPreview->GetPavementTransparency() == 0.5f;	return 1;
	case wed_Pavement75:	ioCheck = mPreview->GetPavementTransparency() == 0.75f;	return 1;
	case wed_Pavement100:	ioCheck = mPreview->GetPavementTransparency() == 1.0f;	return 1;

	case wed_ObjDensity1:	ioCheck = mPreview->GetObjDensity() == 1;	return 1;
	case wed_ObjDensity2:	ioCheck = mPreview->GetObjDensity() == 2;	return 1;
	case wed_ObjDensity3:	ioCheck = mPreview->GetObjDensity() == 3;	return 1;
	case wed_ObjDensity4:	ioCheck = mPreview->GetObjDensity() == 4;	return 1;
	case wed_ObjDensity5:	ioCheck = mPreview->GetObjDensity() == 5;	return 1;
	case wed_ObjDensity6:	ioCheck = mPreview->GetObjDensity() == 6;	return 1;

	case wed_ToggleLines:	ioCheck = mStructureLayer->GetRealLinesShowing();				return 1;
	case wed_ToggleVertices:ioCheck = mStructureLayer->GetVerticesShowing();				return 1;

	case wed_ZoomWorld:		return 1;
	case wed_ZoomAll:		GetExtentAll(box, mResolver); return !box.is_empty()  && !box.is_null();
	case wed_ZoomSelection:	GetExtentSel(box, mResolver); return !box.is_empty()  && !box.is_null();

	default:		return 0;
	}
	return 0;
}

void	WED_MapPane::ReceiveMessage(
							GUI_Broadcaster *		inSrc,
							intptr_t				inMsg,
							intptr_t				inParam)
{
	int i = mToolbar->GetValue();
	WED_MapToolNew * t = NULL;
	if (i >= 0 && i < mTools.size())
		t = mTools[i];
	mMap->SetTool(t);
	mInfoAdapter->SetTool(t);
}

void			WED_MapPane::FromPrefs(IDocPrefs * prefs)
{
	if ((mWorldMap->IsVisible()     ? 1 : 0) != prefs->ReadIntPref("map/world_map_vis",  mWorldMap->IsVisible()     ? 1 : 0))		mWorldMap->ToggleVisible();
#if WANT_TERRASEVER
	if ((mTerraserver->IsVisible () ? 1 : 0) != prefs->ReadIntPref("map/terraserver_vis",mTerraserver->IsVisible()  ? 1 : 0))		mTerraserver->ToggleVisible();
#endif	
	if ((mPreview->IsVisible ()     ? 1 : 0) != prefs->ReadIntPref("map/preview_vis"    ,mPreview->IsVisible()      ? 1 : 0))		mPreview->ToggleVisible();

	mPreview->SetPavementTransparency(prefs->ReadIntPref("map/pavement_alpha",mPreview->GetPavementTransparency()*4) * 0.25f);
	mPreview->SetObjDensity(prefs->ReadIntPref("map/obj_density",mPreview->GetObjDensity()));

	mStructureLayer->SetRealLinesShowing(	 prefs->ReadIntPref("map/real_lines_vis",mStructureLayer->GetRealLinesShowing() ? 1 : 0) != 0);
	mStructureLayer->SetVerticesShowing(	 prefs->ReadIntPref("map/vertices_vis",	 mStructureLayer->GetVerticesShowing() ? 1 : 0) != 0);

	double w,s,e,n;
	mMap->GetMapVisibleBounds(w,s,e,n);
	mMap->ZoomShowArea(
		prefs->ReadDoublePref("map/west" ,w),
		prefs->ReadDoublePref("map/south",s),
		prefs->ReadDoublePref("map/east", e),
		prefs->ReadDoublePref("map/north",n));


	for (int t = 0; t < mTools.size(); ++t)
	if(mTools[t])
	{
		int pc = mTools[t]->CountProperties();
		for (int p = 0; p < pc; ++p)
		{
			PropertyInfo_t	inf;
			PropertyVal_t	val;
			mTools[t]->GetNthPropertyInfo(p,inf);
			string key = "map_";
			key += mTools[t]->GetToolName();
			key += "_";
			key += inf.prop_name;
			string v;
			string::size_type s, e;

			v = prefs->ReadStringPref(key.c_str(),string());
			if (!v.empty())
			{
				val.prop_kind = inf.prop_kind;
				switch(inf.prop_kind) {
				case prop_Int:
				case prop_Bool:
				case prop_Enum:
					val.int_val = atoi(v.c_str());
					break;
				case prop_Double:
					val.double_val = atoi(v.c_str());
					break;
				case prop_String:
				case prop_FilePath:
					val.string_val = v;
					break;
				case prop_EnumSet:
					s = 0;
					do {
						e = v.find(',',s);
						val.set_val.insert(atoi(v.c_str() + s));
						if (e != v.npos)
							s = e + 1;
					} while (e != v.npos);
					break;
				}
				mTools[t]->SetNthProperty(p,val);
			}
		}
	}
}

void			WED_MapPane::ToPrefs(IDocPrefs * prefs)
{
	prefs->WriteIntPref("map/world_map_vis",mWorldMap->IsVisible() ? 1 : 0);
#if WANT_TERRASEVER
	prefs->WriteIntPref("map/terraserver_vis",mTerraserver->IsVisible() ? 1 : 0);
#endif	
	prefs->WriteIntPref("map/preview_vis",mPreview->IsVisible() ? 1 : 0);
	prefs->WriteIntPref("map/pavement_alpha",mPreview->GetPavementTransparency()*4);
	prefs->WriteIntPref("map/obj_density",mPreview->GetObjDensity());
	prefs->WriteIntPref("map/real_lines_vis",mStructureLayer->GetRealLinesShowing() ? 1 : 0);
	prefs->WriteIntPref("map/vertices_vis",mStructureLayer->GetVerticesShowing() ? 1 : 0);

	double w,s,e,n;
	mMap->GetMapVisibleBounds(w,s,e,n);
	prefs->WriteDoublePref("map/west" ,w);
	prefs->WriteDoublePref("map/south",s);
	prefs->WriteDoublePref("map/east", e);
	prefs->WriteDoublePref("map/north",n);

	for (int t = 0; t < mTools.size(); ++t)
	if(mTools[t])
	{
		int pc = mTools[t]->CountProperties();
		for (int p = 0; p < pc; ++p)
		{
			PropertyInfo_t	inf;
			PropertyVal_t	val;
			mTools[t]->GetNthPropertyInfo(p,inf);
			mTools[t]->GetNthProperty(p,val);

			string key = "map_";
			key += mTools[t]->GetToolName();
			key += "_";
			key += inf.prop_name;

			string v;
			char buf[256];
			switch(val.prop_kind) {
			case prop_Int:
			case prop_Bool:
			case prop_Enum:
				sprintf(buf,"%d",val.int_val);
				v = buf;
				break;
			case prop_Double:
				sprintf(buf,"%lf",val.double_val);
				v = buf;
				break;
			case prop_String:
			case prop_FilePath:
				v = val.string_val;
				break;
			case prop_EnumSet:
				for (set<int>::iterator it = val.set_val.begin(); it != val.set_val.end(); ++it)
				{
					if (!v.empty()) v += ",";
					sprintf(buf,"%d",*it);
					v += buf;
				}
				break;
			}

			prefs->WriteStringPref(key.c_str(),v);
		}
	}
}

