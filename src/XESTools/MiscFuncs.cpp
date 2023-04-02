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
#include "NetTables.h"
#include "ObjTables.h"
#include <stdio.h>
#include "EnumSystem.h"
#include "ObjConvert.h"
#include "XObjDefs.h"
#include "ObjUtils.h"
#include "XObjReadWrite.h"

enum {
	fake_build, fake_tree, fake_both };


void	BuildOneFakeObject(const char * dir, const char * fname, double width, double depth, double height, int faketype)
{
	width -= 2.0;
	depth -= 2.0;
	if (width < 2.0) width = 2.0;
	if (depth < 2.0) depth = 2.0;

	float h = max(20.0, height);
	float w = width * 0.5;
	float d = depth * 0.5;
	char path[1024];
	strcpy(path, dir);
	strcat(path, fname);
	XObj	obj;
	obj.texture = "buildings";
	XObjCmd lod;
	lod.cmdType = type_Attr;
	lod.cmdID = attr_LOD;
	lod.attributes.push_back(0);
	lod.attributes.push_back(15000);
	obj.cmds.push_back(lod);
	XObjCmd	cmd;
//	cmd.cmdType = type_Attr;
//	cmd.cmdID = attr_NoCull;
//	if (faketype != fake_build) obj.cmds.push_back(cmd);
	cmd.cmdType = type_Poly;
	cmd.cmdID = obj_Quad;
	cmd.st.resize(4);
	cmd.st[0].st[0] = 0.0;	cmd.st[0].st[1] = 0.0;
	cmd.st[1].st[0] = 0.0;	cmd.st[1].st[1] = 0.0;
	cmd.st[2].st[0] = 0.0;	cmd.st[2].st[1] = 0.0;
	cmd.st[3].st[0] = 0.0;	cmd.st[3].st[1] = 0.0;
	// FRONT
	cmd.st[0].v[0] = -w;	cmd.st[0].v[1] = 0; cmd.st[0].v[2] = d;
	cmd.st[1].v[0] = -w;	cmd.st[1].v[1] = h; cmd.st[1].v[2] = d;
	cmd.st[2].v[0] =  w;	cmd.st[2].v[1] = h; cmd.st[2].v[2] = d;
	cmd.st[3].v[0] =  w;	cmd.st[3].v[1] = 0; cmd.st[3].v[2] = d;
	obj.cmds.push_back(cmd);
	// BACK
	cmd.st[0].v[0] =  w;	cmd.st[0].v[1] = 0; cmd.st[0].v[2] = -d;
	cmd.st[1].v[0] =  w;	cmd.st[1].v[1] = h; cmd.st[1].v[2] = -d;
	cmd.st[2].v[0] = -w;	cmd.st[2].v[1] = h; cmd.st[2].v[2] = -d;
	cmd.st[3].v[0] = -w;	cmd.st[3].v[1] = 0; cmd.st[3].v[2] = -d;
	obj.cmds.push_back(cmd);
	// LEFT
	cmd.st[0].v[0] = -w;	cmd.st[0].v[1] = 0; cmd.st[0].v[2] = -d;
	cmd.st[1].v[0] = -w;	cmd.st[1].v[1] = h; cmd.st[1].v[2] = -d;
	cmd.st[2].v[0] = -w;	cmd.st[2].v[1] = h; cmd.st[2].v[2] =  d;
	cmd.st[3].v[0] = -w;	cmd.st[3].v[1] = 0; cmd.st[3].v[2] =  d;
	obj.cmds.push_back(cmd);
	// RIGHT
	cmd.st[0].v[0] =  w;	cmd.st[0].v[1] = 0; cmd.st[0].v[2] =  d;
	cmd.st[1].v[0] =  w;	cmd.st[1].v[1] = h; cmd.st[1].v[2] =  d;
	cmd.st[2].v[0] =  w;	cmd.st[2].v[1] = h; cmd.st[2].v[2] = -d;
	cmd.st[3].v[0] =  w;	cmd.st[3].v[1] = 0; cmd.st[3].v[2] = -d;
	obj.cmds.push_back(cmd);
	// TOP
	if (faketype == fake_both) h *= 0.5;
	cmd.st[0].v[0] = -w;	cmd.st[0].v[1] = h; cmd.st[0].v[2] =  d;
	cmd.st[1].v[0] = -w;	cmd.st[1].v[1] = h; cmd.st[1].v[2] = -d;
	cmd.st[2].v[0] =  w;	cmd.st[2].v[1] = h; cmd.st[2].v[2] = -d;
	cmd.st[3].v[0] =  w;	cmd.st[3].v[1] = h; cmd.st[3].v[2] =  d;
	if (faketype != fake_tree)
		obj.cmds.push_back(cmd);

	XObj8	obj8;
	
	Obj7ToObj8(obj,obj8);
	XObj8Write(path, obj8);
}

void	BuildOneFakeFacade(const char * dir, const char * fname)
{
	char path[1024];
	strcpy(path, dir);
	strcat(path, fname);
	FILE * fi = fopen(path, "w");
fprintf(fi,"A\n");
fprintf(fi,"800\n");
fprintf(fi,"FACADE\n");

fprintf(fi,"TEXTURE A_buildings_1.png\n");
fprintf(fi,"RING 1\n");
fprintf(fi,"TWO_SIDED 0\n");
fprintf(fi,"LOD 0.000000 15000.000000\n");
fprintf(fi,"  ROOF 0.485352 0.148438\n");
fprintf(fi,"  WALL 5.000000 300.000000\n");
fprintf(fi,"    SCALE 64.000000 64.000000\n");
fprintf(fi,"    ROOF_SLOPE 0.000000\n");
fprintf(fi,"    LEFT 0.000000 0.092529\n");
fprintf(fi,"    CENTER 0.092529 0.182373\n");
fprintf(fi,"    CENTER 0.182373 0.240967\n");
fprintf(fi,"    CENTER 0.240967 0.355225\n");
fprintf(fi,"    CENTER 0.355225 0.460205\n");
fprintf(fi,"    CENTER 0.460205 0.572266\n");
fprintf(fi,"    CENTER 0.572266 0.689209\n");
fprintf(fi,"    CENTER 0.689209 0.791992\n");
fprintf(fi,"    CENTER 0.791992 0.881836\n");
fprintf(fi,"    RIGHT 0.881836 1.000000\n");
fprintf(fi,"    BOTTOM 0.656250 0.714600\n");
fprintf(fi,"    BOTTOM 0.714600 0.747803\n");
fprintf(fi,"    MIDDLE 0.747803 0.779541\n");
fprintf(fi,"    MIDDLE 0.779541 0.811279\n");
fprintf(fi,"    TOP 0.811279 0.849365\n");

	fclose(fi);
}

void	BuildFakeLib(const char * dir)
{
	char	fbuf[1024];
	strcpy(fbuf, dir);
	strcat(fbuf, "library.txt");
	FILE * lib = fopen(fbuf, "w");
	if (lib == NULL) {
		printf("Could not write %s\n", fbuf);
		return;
	}
	fprintf(lib, "A\n800\nLIBRARY\n\n");

	for (int n = 0; n < gRepTable.size(); ++n)
	{
		char	lname[400], oname[400];
		if (gRepTable[n].obj_type == rep_Obj)
		{
			sprintf(lname, "%s%s.obj",gObjLibPrefix.c_str(), FetchTokenString(gRepTable[n].obj_name));
			sprintf(oname, "%s.obj",FetchTokenString(gRepTable[n].obj_name));
			char * a = oname;
			while (*a)
			{
				if (*a == '/') *a = '_';
				++a;
			}
			fprintf(lib, "EXPORT %s %s\n",lname, oname);
			BuildOneFakeObject(dir, oname, gRepTable[n].width_max,gRepTable[n].depth_max, gRepTable[n].height_max, (gRepTable[n].road && gRepTable[n].fill) ? fake_both : (gRepTable[n].road ? fake_build : fake_tree) );
		}
/*
		if (!gRepTable[n].fac_allow)
		{
			sprintf(lname, "%s.fac",FetchTokenString(gRepTable[n].obj_name));
			sprintf(oname, "%s.fac",FetchTokenString(gRepTable[n].obj_name));
			char * a = oname;
			while (*a)
			{
				if (*a == '/') *a = '_';
				++a;
			}
			fprintf(lib, "EXPORT %s %s\n",lname, oname);
			BuildOneFakeFacade(dir, oname);
		}
*/
	}

	fprintf(lib, "EXPORT lib/us/roads.net gen_roads.net\n");
	fclose(lib);

	strcpy(fbuf, dir);
	strcat(fbuf, "gen_roads.net");
	FILE * rds = fopen(fbuf, "w");
	fprintf(rds, "A\n800\nROADS\n\nTEXTURE 3 road.bmp\nTEXTURE_LIT road_LIT.bmp\n\n");
	for (NetRepInfoTable::iterator net = gNetReps.begin(); net != gNetReps.end(); ++net)
	{
//		bool bridge = false;
//		for (Road2NetInfoTable::iterator feat = gRoad2Net.begin(); feat != gRoad2Net.end(); ++feat)
//		{
//			if (feat->second.entity_type == net->first && feat->second.bridge)
//				bridge = true;
//		}

		fprintf(rds, "# %s\n", FetchTokenString(net->first));

		fprintf(rds, "ROAD_TYPE %d   %f %f 0   1.0 1.0 1.0 \n",
			net->second.export_type_draped, net->second.width(), net->second.width());
//		if (bridge)	fprintf(rds, "SEGMENT 0 20000     0 -3 0.0    0  0 0.0\n");
					fprintf(rds, "SEGMENT 0 20000     0  0 0.0    1  0 1.0\n");
//		if (bridge)	fprintf(rds, "SEGMENT 0 20000     1  0 1.0    1 -3 1.0\n");
		fprintf(rds, "\n");
	}
	fclose(rds);
}

void	CheckLib(const char * inDir)
{
	char	buf[1024];
	strcpy(buf, inDir);
	strcat(buf, "library.txt");
	map	<string, string>	lib;
	map <string, float>		width;
	map <string, float>		depth;
	map <string, float>		offx;
	map <string, float>		offz;
	FILE * libf = fopen(buf, "r");
	if (libf == NULL) { printf("Could not open %s\n", buf); return; }
	if (!fgets(buf, 1024, libf))
		throw "fgets error";
	if (!fgets(buf, 1024, libf))
		throw "fgets error";
	if (!fgets(buf, 1024, libf))
		throw "fgets error";
	while (fgets(buf, 1024, libf))
	{
		char * t = strtok(buf, " \t");
		if (!strcmp(t, "EXPORT"))
		{
			char * t = strtok(NULL, " \t\n\r");
			char * r = strtok(NULL, "\n\r");
			if (strstr(r,".obj"))
				lib[t] = r;
		}
	}
	for (map<string, string>::iterator i = lib.begin(); i != lib.end(); ++i)
	{
		XObj8	foo;
		strcpy(buf, inDir);
		strcat(buf, i->second.c_str());
		for (char * p = buf; *p; ++p)
			if (*p == ':') *p = '/';

		if (!XObj8Read(buf, foo)) {
			printf("Could not open %s\n", buf); return; }
		float mins[3], maxs[3];
		GetObjDimensions8(foo, mins, maxs);
		width[i->second] = maxs[0] - mins[0];
		depth[i->second] = maxs[2] - mins[2];
		offx [i->second] = (maxs[0] + mins[0]) * 0.5;
		offz [i->second] = (maxs[2] + mins[2]) * 0.5;
	}

	for (int n = 0; n < gRepTable.size(); ++n)
	{
//		if (!gRepTable[n].obj_name.empty())
//		if (gRepTable[n].obj_name != "-")
		{
			strcpy(buf,FetchTokenString(gRepTable[n].obj_name));
			strcat(buf, ".obj");
			if (lib.count(buf) == 0)
			{printf("Lib is missing %s\n", buf); return; }

			string key = lib[buf];

			double	w = width[key];
			double	d = depth[key];
			double	x = offx[key];
			double	y = offz[key];

			if (w > gRepTable[n].width_max ||
				d > gRepTable[n].depth_max)
			{
				printf("Object %30s %30s Desired: %4f,%4f, actual %4f,%4f\n",
					buf, key.c_str(), gRepTable[n].width_max,gRepTable[n].depth_max, w, d);
			}
		}
	}

}
