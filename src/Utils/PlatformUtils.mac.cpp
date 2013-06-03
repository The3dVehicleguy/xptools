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
#include "PlatformUtils.h"


#if __MWERKS__
	#if defined(__MACH__)
		#define _STDINT_H_
	#endif
	#include <Carbon.h>
#else
	#define _STDINT_H_
	#include <Carbon/Carbon.h>
#endif
#include <string.h>

static	OSErr		FSSpecToPathName(const FSSpec * inFileSpec, char * outPathname, int in_buf_size);
static	OSErr		FSRefToPathName(const FSRef * inFileSpec, char * outPathname, int in_buf_size);


/* Get FilePathFromUser puts up a nav services dialog box and converts the results
   to a C string path. */

pascal void event_proc(NavEventCallbackMessage callBackSelector, NavCBRecPtr callBackParms, void *callBackUD)
{
}

const char * GetApplicationPath(char * pathBuf, int pathLen)
{
/*	ProcessInfoRec		pir;
	FSSpec				spec;
	Str255				pStr;
	ProcessSerialNumber	psn = { 0, kCurrentProcess };
	pir.processInfoLength 	= sizeof(pir);
	pir.processAppSpec 		= &spec;
	pir.processName			= pStr;
	GetProcessInformation(&psn, &pir);
	OSErr err = FSSpecToPathName(&spec, pathBuf, sizeof(pathBuf));
	if (err != noErr)
		return NULL;
*/
	CFURLRef	main_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
	CFStringRef	main_path = CFURLCopyFileSystemPath(main_url, kCFURLPOSIXPathStyle);
	CFStringGetCString(main_path,pathBuf,pathLen,kCFStringEncodingMacRoman);
	CFRelease(main_url);
	CFRelease(main_path);
	return pathBuf;
}


int		GetFilePathFromUserInternal(
					int					inType,
					const char * 		inPrompt,
					const char *		inAction,
					const char *		inDefaultFileName,
					int					inID,
					int					inMulti,
					vector<string>&		outFiles)
{
		OSErr						err;
		NavDialogCreationOptions	options;
		FSRef						fileSpec;
		NavDialogRef				dialog = NULL;
		NavUserAction				action;
		NavEventUPP					eventUPP = NULL;

	err = NavGetDefaultDialogCreationOptions(&options);
	if (err != noErr) goto bail;

	if (inType == getFile_Save)
		options.saveFileName = CFStringCreateWithCString(kCFAllocatorDefault,inDefaultFileName,kCFStringEncodingMacRoman);

	options.message = CFStringCreateWithCString(kCFAllocatorDefault,inPrompt,kCFStringEncodingMacRoman);
	options.actionButtonLabel = CFStringCreateWithCString(kCFAllocatorDefault,inAction,kCFStringEncodingMacRoman);
	if(inMulti)
		options.optionFlags |= kNavAllowMultipleFiles;
	else
		options.optionFlags &= ~kNavAllowMultipleFiles;
	options.optionFlags &= ~kNavAllowStationery	;
	options.optionFlags |=  kNavAllFilesInPopup	;
	options.preferenceKey = inID;

	eventUPP = NewNavEventUPP(event_proc);

	switch(inType) {
	case getFile_Open:
		err = NavCreateGetFileDialog(&options, NULL, eventUPP, NULL, NULL, NULL, &dialog);
		if (err != noErr) goto bail;
		break;
	case getFile_Save:
		err = NavCreatePutFileDialog(&options, 0, 0, eventUPP, NULL, &dialog);
		if (err != noErr) goto bail;
		break;
	case getFile_PickFolder:
		err = NavCreateChooseFolderDialog(&options, eventUPP, NULL, NULL, &dialog);
		if (err != noErr) goto bail;
	}

	err = NavDialogRun(dialog);
	if (err != noErr) goto bail;

	CFRelease(options.message);
	CFRelease(options.actionButtonLabel);
	if(options.saveFileName) CFRelease(options.saveFileName);

	action = NavDialogGetUserAction(dialog);
	if (action !=kNavUserActionCancel && action != kNavUserActionNone)
	{
		NavReplyRecord	reply;
		err = NavDialogGetReply(dialog, &reply);
		if (err != noErr) goto bail;

		SInt32		numDocs;
		err = ::AECountItems(&reply.selection, &numDocs);

		for(int i = 1; i <= numDocs; ++i)
		{
			err = AEGetNthPtr(&reply.selection, i, typeFSRef, NULL, NULL, &fileSpec, sizeof(fileSpec), NULL);
			if (err != noErr)
				goto bail;
				
			char buf[4096];

			err = FSRefToPathName(&fileSpec, buf, sizeof(buf));
			if (err != noErr)
				goto bail;
			
			outFiles.push_back(buf);
		}

		NavDisposeReply(&reply);

		if (inType == getFile_Save)
		{
			CFStringRef str = NavDialogGetSaveFileName(dialog);

			char buf[1024];
			int len = CFStringGetLength(str);
			int got = CFStringGetBytes(str, CFRangeMake(0, len), kCFStringEncodingMacRoman, 0, 0, (UInt8*)buf, len, NULL);
			buf[len] = 0;
			
			outFiles[0] += DIR_STR;
			outFiles[0] += buf;
		}

	}


	NavDialogDispose(dialog);
	dialog = NULL;

	DisposeNavEventUPP(eventUPP);
	return (action !=kNavUserActionCancel && action != kNavUserActionNone);

bail:
	if(eventUPP)	DisposeNavEventUPP(eventUPP);
	if(dialog)		NavDialogDispose(dialog);
	return 0;


}

int		GetFilePathFromUser(
					int					inType,
					const char * 		inPrompt,
					const char *		inAction,
					int					inID,
					char * 				outFileName,
					int					inBufSize)
{
	vector<string> files;
	int result = GetFilePathFromUserInternal(inType,inPrompt,inAction, outFileName, inID, 0, files);
	if(!result)
		return 0;
	if(files.size() != 1)
		return 0;
	strncpy(outFileName,files[0].c_str(),inBufSize);
}

char *	GetMultiFilePathFromUser(
					const char * 		inPrompt,
					const char *		inAction,
					int					inID)
{
	vector<string> files;
	int result = GetFilePathFromUserInternal(getFile_Open,inPrompt,inAction, "", inID, 1, files);
	if(!result)
		return NULL;
	if(files.size() < 1)
		return NULL;

	for(int i = 0; i < files.size(); ++i)
		if(files[i].empty())
			return NULL;
		
	int buf_size = 1;
	for(int i = 0; i < files.size(); ++i)
		buf_size += (files[i].size() + 1);
	
	char * ret = (char *) malloc(buf_size);
	char * p = ret;

	for(int i = 0; i < files.size(); ++i)
	{
		strcpy(p, files[i].c_str());
		p += (files[i].size() + 1);
	}
	++p;
	
	return ret;
}



void	DoUserAlert(const char * inMsg)
{
	Str255	p1;
	size_t	sl;

	sl = strlen(inMsg);
	if (sl > 255)
		sl = 255;

	p1[0] = sl;
	memcpy(p1+1, inMsg, sl);

	StandardAlert(kAlertStopAlert, (const unsigned char*)p1, (const unsigned char*)"", NULL, NULL);
}

int		ConfirmMessage(const char * inMsg, const char * proceedBtn, const char * cancelBtn)
{
	Str255					pStr, proStr, clcStr;
	AlertStdAlertParamRec	params;
	short					itemHit;

	pStr[0] = strlen(inMsg);
	memcpy(pStr+1,inMsg,pStr[0]);
	proStr[0] = strlen(proceedBtn);
	memcpy(proStr+1, proceedBtn, proStr[0]);
	clcStr[0] = strlen(cancelBtn);
	memcpy(clcStr+1, cancelBtn, clcStr[0]);

	params.movable = false;
	params.helpButton = false;
	params.filterProc = NULL;
	params.defaultText = proStr;
	params.cancelText = clcStr;
	params.otherText = NULL;
	params.defaultButton = 1;
	params.cancelButton = 2;
	params.position = kWindowDefaultPosition;

	StandardAlert(kAlertCautionAlert, (const unsigned char*)pStr, (const unsigned char*)"", &params, &itemHit);

	return (itemHit == 1);
}

/*
 * FSSpecToPathName
 *
 * This routine builds a full path from a file spec by recursing up the directory
 * tree to the route, prepending each directory name.
 *
 */

OSErr	FSSpecToPathName(const FSSpec * inFileSpec, char * outPathname, int buf_size)
{
	FSRef ref;
	OSErr err = FSpMakeFSRef(inFileSpec, &ref);
	if (err != noErr) return err;
	return FSRefToPathName(&ref, outPathname, buf_size);
}

OSErr	FSRefToPathName(const FSRef * inFileRef, char * outPathname, int in_buf_size)
{
	CFURLRef url = CFURLCreateFromFSRef(kCFAllocatorDefault, inFileRef);
	if (url == NULL)	return -1;

	CFURLPathStyle st = kCFURLPOSIXPathStyle;
	#if defined(__MWERKS__)
	st = kCFURLHFSPathStyle;
	#endif
	CFStringRef	str = CFURLCopyFileSystemPath(url, st);
	CFRelease(url);
	if (str == NULL)	return -1;

	CFIndex		len = CFStringGetLength(str);
	CFIndex		got = CFStringGetBytes(str, CFRangeMake(0, len), kCFStringEncodingMacRoman, 0, 0, (UInt8*)outPathname, in_buf_size-1, NULL);
	outPathname[got] = 0;
	CFRelease(str);
	return noErr;
}


int DoSaveDiscardDialog(const char * inMessage1, const char * inMessage2)
{
	OSErr	err;
	Str255	pstr1, pstr2;
	SInt16	item;
	AlertStdAlertParamRec	rec;

	pstr1[0] = strlen(inMessage1);
	pstr2[0] = strlen(inMessage2);
	memcpy(pstr1+1,inMessage1,pstr1[0]);
	memcpy(pstr2+1,inMessage2,pstr2[0]);

	rec.movable = false;
	rec.helpButton = false;
	rec.filterProc = NULL;
	rec.defaultText = (ConstStringPtr) kAlertDefaultOKText;
	rec.cancelText = (ConstStringPtr) kAlertDefaultCancelText;
	rec.otherText = (ConstStringPtr)  kAlertDefaultOtherText;
	rec.defaultButton = kAlertStdAlertOKButton;
	rec.cancelButton = kAlertStdAlertCancelButton;
	rec.position = kWindowDefaultPosition;

	err = StandardAlert(
					kAlertCautionAlert,
					pstr1,
					pstr2,
					&rec,
					&item);

	if (err != noErr) return close_Cancel;
	switch(item) {
	case kAlertStdAlertOKButton: 		return close_Save;
	case kAlertStdAlertCancelButton: 	return close_Cancel;
	case kAlertStdAlertOtherButton: 	return close_Discard;
	default: return close_Cancel;
	}
}
