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
#ifndef WED_MAPZOOMERNEW_H
#define WED_MAPZOOMERNEW_H

#include "GUI_ScrollerPane.h"
#include "CompGeomDefs2.h"

/*

	The map zoomer maintains a relationship between logical and screen coordinates
	AND maintains a "windowing" system.  Thus it does scrolling and viewing at the same time.

	It does this with two rectangles:

	- Visible bounds - what we can see.
	- Total bounds - the entire image.

	It does this with two coordinate systems:

	- Pixels - screen drawing units.
	- Logical - whatever units our map is in (degrees lat/lon for WED).

	Thus we have the pixel visible bounds, logical visible bounds, and logical total bounds.

*/

#include "WED_Camera.h"

class	WED_MapZoomerNew : public GUI_ScrollerPaneContent {
public:
					 WED_MapZoomerNew(WED_Camera * c = nullptr);
	virtual			~WED_MapZoomerNew();
	// The map zoomer converts lat/lon coordinates to pixel coordinates.
	// This API is called by just about anything that needs to do coordinate
	// conversion.

			double	XPixelToLon(double) const;  // Warning: With Map projection - these fonversions only work for points near the center of the map.
			double	YPixelToLat(double) const;  // Use PixelToLL / LLToPixel whenever possible, as only these include GeographicProjection
			double	LonToXPixel(double) const;
			double	LatToYPixel(double) const;

			Point2	PixelToLL(const Point2& p) const;
			Point2	LLToPixel(const Point2& p) const;

			void	PixelToLLv(Point2 * dst, const Point2 * src, int n) const;
			void	LLToPixelv(Point2 * dst, const Point2 * src, int n) const;

			double	GetPPM(void) const;
			double	GetRotation(const Point2& p) const;

			double	GetClickRadius(double pixels) const;
			long long	CacheKey(void) { return mCacheKey; }

	// This API is called by the map class to set up and modify the zoomer

	// Overall setup

			void	SetMapLogicalBounds(			// Define the max scrollable map positions.
							double	inWest,
							double	inSouth,
							double	inEast,
							double	inNorth);

			void	GetPixelBounds(					// Get the area on the screen the user
							double& outLeft,			// can see.
							double&	outBottom,
							double&	outRight,
							double&	outTop);
			void	GetMapVisibleBounds(			// Get the amount of the map visible in
							double&	outWest,		// this screen area.
							double&	outSouth,
							double&	outEast,
							double&	outNorth);
			void	GetMapLogicalBounds(			// Defoute the max scrollable map positions.
							double&	outWest,
							double&	outSouth,
							double&	outEast,
							double&	outNorth);

	// Scrolling operations
			void	ZoomShowAll(void);				// Zoom out to reveal the whole map
			void	ZoomShowArea(
							double	inWest,
							double	inSouth,
							double	inEast,
							double	inNorth);
			void	PanPixels(						// Pan so that the logical pixel under p1
							double	x1,				// is now visible under p2
							double	y1,
							double	x2,
							double	y2);
			void	ZoomAround(						// Zoom in and out keeping one pixel constant
							double	zoomFactor,
							double	centerXPixel,
							double	centerYPixel);
			void	ScrollReveal(
							double	inLon,
							double	inLat);
			void	ScrollReveal(
							double	inWest,
							double	inSouth,
							double	inEast,
							double	inNorth);


	virtual	void	GetScrollBounds(float outTotalBounds[4], float outVisibleBounds[4]);
	virtual	void	ScrollH(float xOffset);
	virtual	void	ScrollV(float yOffset);

			// Note that unlike the corresponding WED_Camera methods, these take lat/lon coordinates.
			double	PixelSize(const Bbox2& bboxLL) const;
			double	PixelSize(const Bbox2& bboxLL, double featureSize) const;
			double	PixelSize(const Point2& positionLL, double diameter) const;

			void	PushMatrix(void);
			void	Rotatef(float r, float x, float y, float z);
			void	Translatef(float x, float y, float z);
			void	Scalef(float x, float y, float z);
			void	PopMatrix(void);

protected:
			void	SetPPM(double ppm);
	WED_Camera * 	cam;

			void	SetPixelBounds(					// Set the area on the screen the user
							double 	inLeft,			// can see.
							double	inBottom,
							double	inRight,
							double	inTop);
			void	SetPixelCenter(					// Set the position in pixel coordinates
							double x,				// that corresponds to the lat/lon center position.
							double y);

private:

			void	RecalcAspectRatio(void);
			double	wagner_proj_mult(double lat) const;

	double	mPixels[4];
	double	mLogicalBounds[4];

	// values below are dreived from the above, to provide
	// pre-computed values for faster pixel transformations

	double	mLatCenter;
	double	mLonCenter;
	double	mCenterX;
	double	mCenterY;
	double	mLatCenterCOS, mLatCenterSIN;
	double  mCenterCOS;
	long long mCacheKey;
	double  mMapSize;                          // width of visible map, normalized. 1.0 = whole world

	class mapScale {
	public:
		mapScale(void) : mPixel2DegLon(1.0), mPixel2DegLat(1.0), mPPM(1.0) {}

		void set(double PPM, double LatCenterDeg, double altitude_msl = 300.0);
		// why default 300m ? Well, WED doesn't let users set altitudes for now.
		// Advertised WYSIWYG accuracy is 'around 1 in 10000' and there are in fact multiple nitty
		// details on both sim and WED side that are in the way of reliably doing better than that.
		// So set map scale for everybody by half that amount higher, aka 'best on average for 
		// airports from sealevel to 2000 ft MSL'.

		double Pix2DegLat() const { return mPixel2DegLat; }
		double Pix2DegLon() const { return mPixel2DegLon; }
		double Deg2PixLat() const { return mDeg2PixelLat; }
		double Deg2PixLon() const { return mDeg2PixelLon; }
		double ppm(void) const { return mPPM; }
	private:
		double	mPixel2DegLat;
		double	mPixel2DegLon;
		double	mDeg2PixelLat;
		double	mDeg2PixelLon;
		double	mPPM;
	};
	mapScale	mScale;
};

#endif
