/* 
 * Copyright (c) 2009, Laminar Research.
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

#ifndef WED_TaxiRoute_H
#define WED_TaxiRoute_H

#if AIRPORT_ROUTING

#include "WED_GISEdge.h"

struct AptRouteEdge_t;

class WED_TaxiRoute : public WED_GISEdge {

DECLARE_PERSISTENT(WED_TaxiRoute)

public:

	void		Import(const AptRouteEdge_t& info, void (* print_func)(void *, const char *, ...), void * ref);
	void		Export(		 AptRouteEdge_t& info) const;
	
	virtual		bool	IsOneway(void) const;
				bool	IsRunway(void) const;
				bool	HasHotArrival(void) const;
				bool	HasHotDepart(void) const;
				bool	HasHotILS(void) const;
	
				void		SetOneway(int p);
				void		SetRunway(int r);
				void		SetHotDepart(const set<int>& rwys);
				void		SetHotArrive(const set<int>& rwys);
				void		SetHotILS(const set<int>& rwys);
				
				bool		HasInvalidHotZones(const set<int>& legal_rwys) const;
				int			GetRunway(void) const;	// returns two-way enum!

	virtual		void	GetNthPropertyDict(int n, PropertyDict_t& dict);

	virtual void		GetNthPropertyInfo(int n, PropertyInfo_t& info);
	virtual void		GetNthProperty(int n, PropertyVal_t& val) const;

	virtual const char *	HumanReadableType(void) const { return "Taxi Route"; }
	
private:	

		WED_PropBoolText		oneway;
		WED_PropIntEnum			runway;
		WED_PropIntEnumSet		hot_depart;
		WED_PropIntEnumSet		hot_arrive;
		WED_PropIntEnumSet		hot_ils;		

};

#endif /* WED_TaxiRoute_H */

#endif /* AIRPORT_ROUTING*/
