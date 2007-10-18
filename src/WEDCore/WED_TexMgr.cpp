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

#include "WED_TexMgr.h"
#include "BitmapUtils.h"
#include "MemFileUtils.h"
#include "TexUtils.h"
#include "WED_PackageMgr.h"

#if APL
	#include <OpenGL/gl.h>
#else
	#include <gl/gl.h>
#endif

WED_TexMgr::WED_TexMgr(const string& package) : mPackage(package)
{
}


TexRef		WED_TexMgr::LookupTexture(const char * path)
{
	TexMap::iterator i = mTexes.find(path);
	if (i == mTexes.end())
	{
		return LoadTexture(path);
	}
	return i->second;
}

int			WED_TexMgr::GetTexID(TexRef ref)
{
	return ((TexInfo *) ref)->tex_id;
}

void		WED_TexMgr::GetTexInfo(
						TexRef	ref,
						int *	vis_x,
						int *	vis_y,
						int *	act_x,
						int *	act_y,
						int *	org_x,
						int *	org_y)
{
	TexInfo * i = (TexInfo *) ref;
	if (vis_x) *vis_x = i->vis_x;
	if (vis_y) *vis_y = i->vis_y;
	if (act_x) *act_x = i->act_x;
	if (act_y) *act_y = i->act_y;
	if (org_x) *org_x = i->org_x;
	if (org_y) *org_y = i->org_y;
}

WED_TexMgr::TexInfo *	WED_TexMgr::LoadTexture(const char * path)
{
	string fpath;
	
	fpath = gPackageMgr->ComputePath(mPackage, path);

	TexInfo * inf = new TexInfo;
	
	ImageInfo	im;
	
/*	
	MFMemFile * dds_file;
	dds_file = MemFile_Open(path);
	if(dds_file)
	{
		if (LoadTextureFromDDS((unsigned const char *) MemFile_GetBegin(dds_file),(unsigned const char *) MemFile_GetEnd(dds_file),tn,0,&inf->act_x, &inf->act_y))
		{
			inf->tex_id = tn;
			inf->org_x = inf->vis_x = inf->act_x;
			inf->org_y = inf->vis_y = inf->act_y;
			MemFile_Close(dds_file);
			return inf;
		}
		MemFile_Close(dds_file);
	}	
*/	
	if (CreateBitmapFromPNG(fpath.c_str(), &im, false) != 0)
	if (CreateBitmapFromDDS(fpath.c_str(), &im) != 0)
	if (CreateBitmapFromFile(fpath.c_str(), &im) != 0)
	if (CreateBitmapFromJPEG(fpath.c_str(), &im) != 0)
	if (CreateBitmapFromTIF(fpath.c_str(), &im) != 0)
	{
		return NULL;
	}

	GLuint tn;
	glGenTextures(1,&tn);

	inf->tex_id = tn;
	inf->org_x = im.width;
	inf->org_y = im.height;
	
	float s,t;
	if (!LoadTextureFromImage(im, tn, 0, &inf->act_x, &inf->act_y, &s,&t))
	{
		delete inf;
		return NULL;
	}
	
	inf->vis_x = (float) inf->act_x * s;
	inf->vis_y = (float) inf->act_y * t;
	
	mTexes[path] = inf;

	return inf;
}

