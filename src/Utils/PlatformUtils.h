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
#ifndef _PlatformUtils_h_
#define _PlatformUtils_h_

#include <string>

/*
 * PlatformUtils
 *
 * This file declares all of the platform specific code for the converter.
 *
 * PlatformUtils.mac.c contains the mac implementation; this code must be rewritten
 * for the PC.
 *
 */

/* The directory separator is a macro and should be cased by some kind of compiler
   #define or something. */
#if	IBM
	#define DIR_CHAR	'\\'
	#define DIR_STR		"\\"
	#define TEMP_FILES_DIR_LEN MAX_PATH
#elif APL || LIN
		#define	DIR_CHAR	'/'
		#define DIR_STR		"/"
		#define TEMP_FILES_DIR_LEN 255
#else
	#error PLATFORM NOT DEFINED
#endif

/*
* This routine returns a fully qualified path to the application.
*
*/
string GetApplicationPath();

/*
* The FQP to the OS' semantically correct folder for caching files
*/
string GetCacheFolder();

/*
* Returns the FQP to the OS' "Best practices" temporary files folder
*/
string GetTempFilesFolder();

/*
 * GetFilePathFromUser takes a prompting C-string and fills in the buffer with a path
 * to a picked file.  It returns 1 if a file was picked, 0 if the user canceled.
 *
 */
enum {
	getFile_Open,
	getFile_Save,
	getFile_PickFolder
};
int		GetFilePathFromUser(
					int					inType,
					const char * 		inPrompt,
					const char *		inAction,
					int					inID,
					char *				outFileName,
					int					inBufSize,
					const char*			initialPath = nullptr);

// 0-len-terminated list of paths, you must free!
char *	GetMultiFilePathFromUser(
					const char * 		inPrompt,
					const char *		inAction,
					int					inID,
					const char *		initialPath = nullptr);

/*
 * DoUserAlert puts up an alert dialog box with the message and an OK button.
 * WARNING: Do not call during the middle of an operation!
 */
void	DoUserAlert(const char * inMsg);

/* Dialog box with a message and 2 or 3 user definable buttons. Proceed button is default.
 * Returns 1 to proceed, 2 on the optional button and 0 if the user cancels.
 */
int ConfirmMessage(const char * inMsg, const char * proceedBtn, const char * cancelBtn, const char* optionBtn = nullptr);


/* Dialog box with messages and 3 fixed buttons: Save, Discard, Cancel.
*  Returns the enums.
 */

enum {
	close_Save,
	close_Discard,
	close_Cancel
};

int DoSaveDiscardDialog(const char * inMessage1, const char * inMessage2);

#endif
