/* 
 * Copyright (c) 2014, Laminar Research.
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
 
#ifndef WED_ICAOTable_H
#define WED_ICAOTable_H 

#include "GUI_TextTable.h"
#include "GUI_Broadcaster.h"
#include "GUI_SimpleTableGeometry.h"
#include "AptDefs.h"

class	WED_ICAOTable :	public GUI_TextTableProvider, 
						public GUI_SimpleTableGeometry, 
						public GUI_TextTableHeaderProvider,
						public GUI_Broadcaster {
public:

	// Note: you must call set-filter once after the dust settles to get the 
	// table synced!
					 WED_ICAOTable(
						const AptVector *			apts);
	virtual			~WED_ICAOTable();
	
	
	// Call this to push a new filter string.
			void	SetFilter(
						const string&				new_filter);
			
	// Call this any time you change the contents of your apt vector - call
	// IMMEDIATELY, before letting the UI do anything!
			void	AptVectorChanged(void);
			
	// Call this to get the current selected indicies of your apt vector.
			void	GetSelection(
						set<int>&					out_selection);
	
	// GUI_TextTableHeaderProvider
	virtual	void	GetHeaderContent(
						int							cell_x,
						GUI_HeaderContent&			the_content);
	virtual	void	SelectHeaderCell(
						int							cell_x);
						

	// GUI_TextTableProvider
	virtual void	GetCellContent(
						int							cell_x,
						int							cell_y,
						GUI_CellContent&			the_content);
	virtual	void	GetEnumDictionary(
						int							cell_x,
						int							cell_y,
						GUI_EnumDictionary&			out_dictionary) { }
	virtual	void	AcceptEdit(
						int							cell_x,
						int							cell_y,
						const GUI_CellContent&		the_content,
						int							apply_all) { }
	virtual	void	ToggleDisclose(
						int							cell_x,
						int							cell_y) { }
	virtual void	DoDeleteCell(
						int							cell_x,
						int							cell_y) { }
	virtual	void	DoDrag(
						GUI_Pane *					drag_emitter,
						int							mouse_x,
						int							mouse_y,
						int							button,
						int							bounds[4]) { }
	virtual void	SelectionStart(
						int							clear);
	virtual	int		SelectGetExtent(
						int&						low_x,
						int&						low_y,
						int&						high_x,
						int&						high_y);
	virtual	int		SelectGetLimits(
						int&						low_x,
						int&						low_y,
						int&						high_x,
						int&						high_y);
	virtual	void	SelectRange(
						int							start_x,
						int							start_y,
						int							end_x,
						int							end_y,
						int							is_toggle);
	virtual	void	SelectionEnd(void);
	virtual	int		SelectDisclose(bool open_it, bool all, set<int>* did_open = nullptr);

	virtual	int		TabAdvance(
						int&						io_x,
						int&						io_y,
						int							reverse,
						GUI_CellContent&			the_content);
	virtual	int		DoubleClickCell(
						int							cell_x,
						int							cell_y);

	virtual	void					GetLegalDropOperations(
											int&						allow_between_col,
											int&						allow_between_row,
											int&						allow_into_cell) { allow_between_col = allow_between_row = allow_into_cell = 0; }
	virtual	GUI_DragOperation		CanDropIntoCell(
											int							cell_x,
											int							cell_y,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended,
											int&						whole_col,
											int&						whole_row) { return gui_Drag_None; }
	virtual	GUI_DragOperation		CanDropBetweenColumns(
											int							cell_x,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended) { return gui_Drag_None; }
	virtual	GUI_DragOperation		CanDropBetweenRows(
											int							cell_y,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended) { return gui_Drag_None; }


	virtual	GUI_DragOperation		DoDropIntoCell(
											int							cell_x,
											int							cell_y,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended) { return gui_Drag_None; }
	virtual	GUI_DragOperation		DoDropBetweenColumns(
											int							cell_x,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended) { return gui_Drag_None; }
	virtual	GUI_DragOperation		DoDropBetweenRows(
											int							cell_y,
											GUI_DragData *				drag,
											GUI_DragOperation			allowed,
											GUI_DragOperation			recommended) { return gui_Drag_None; }

	// GUI_[Simple]TableGeometry
	virtual	int						GetColCount(void);
	virtual	int						GetRowCount(void);
	
private:

			void					resort();

	const AptVector *	mApts;
	vector<int>			mSorted;
	set<int>			mSelected;
	int					mSortColumn;
	int					mInvertSort;
	string				mFilter;
		
};
	
#endif
