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
#include <string.h>
#import <AppKit/AppKit.h>


string GetApplicationPath()
{
	NSBundle * bundle = [NSBundle mainBundle];
	NSString * path = [bundle bundlePath];
	
	return string([path UTF8String]);
}

string GetCacheFolder()
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];	
	NSFileManager * fmgr = [NSFileManager defaultManager];	
	NSURL * dir = [fmgr URLForDirectory:NSCachesDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];

	string ret([[dir path] UTF8String]);
	ret += DIR_STR;

	[pool release];
	
	return ret;


/*		FSRef	ref;
	OSErr	err = FSFindFolder(kOnAppropriateDisk,kCachedDataFolderType,TRUE, &ref);
	if (err != noErr) return false;

	char	buf[1024];
	err = FSRefMakePath(&ref, (UInt8*) buf, sizeof(buf));
	if (err != noErr) return false;
	strncpy(temp_path,buf,sz);
	return temp_path;
*/	
}

int		GetFilePathFromUserInternal(
					int					inType,
					const char * 		inPrompt,
					const char *		inAction,
					const char *		inDefaultFileName,
					int					inID,
					int					inMulti,
					vector<string>&		outFiles,
					const char *		initialPath)
{
    if(inType == getFile_Open || inType == getFile_PickFolder)
    {
        NSOpenPanel * panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:inMulti];
        [panel setCanChooseDirectories:inType == getFile_PickFolder];
        [panel setCanChooseFiles:inType != getFile_PickFolder];
        [panel setCanCreateDirectories:(inType == getFile_PickFolder || inType == getFile_Save)];
        [panel setPrompt:[NSString stringWithUTF8String:inAction]]; // NOT A TYPO: NSOpenPanel calls the button text the "prompt" and the text on the window the "message"
        [panel setMessage:[NSString stringWithUTF8String:inPrompt]]; // NOT A TYPO: NSOpenPanel calls the button text the "prompt" and the text on the window the "message"
		if(initialPath)
		{
			[panel setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:initialPath] isDirectory:true]];
		}
		if([panel runModal] == NSFileHandlingPanelOKButton)
        {
            NSArray * urls = [panel URLs];
            for(NSURL * url in urls)
            {
                outFiles.push_back(string([[url path] UTF8String]));
            }
            return 1;
        }
    }
    else // in_type == getFile_Save
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:inType == getFile_PickFolder || inType == getFile_Save];
        [panel setPrompt:[NSString stringWithUTF8String:inAction]]; // NOT A TYPO: NSOpenPanel calls the button text the "prompt" and the text on the window the "message"
        [panel setMessage:[NSString stringWithUTF8String:inPrompt]]; // NOT A TYPO: NSOpenPanel calls the button text the "prompt" and the text on the window the "message"
        if([panel runModal] == NSFileHandlingPanelOKButton)
        {
            NSURL * url = [panel URL];
            outFiles.push_back(string([[url path] UTF8String]));
            return 1;
        }
    }

	return 0;
}

int		GetFilePathFromUser(
					int					inType,
					const char * 		inPrompt,
					const char *		inAction,
					int					inID,
					char * 				outFileName,
					int					inBufSize,
					const char *		initialPath)
{
	vector<string> files;
	if(!GetFilePathFromUserInternal(inType,inPrompt,inAction, outFileName, inID, 0, files, initialPath))
		return 0;
	if(files.size() != 1)
		return 0;
	strncpy(outFileName,files[0].c_str(),inBufSize);
	return 1;
}

char *	GetMultiFilePathFromUser(
					const char * 		inPrompt,
					const char *		inAction,
					int					inID,
					const char *		initialPath)
{
	vector<string> files;
	if(!GetFilePathFromUserInternal(getFile_Open,inPrompt,inAction, "", inID, 1, files, initialPath))
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
	*p = 0;
	
	return ret;
}



void	DoUserAlert(const char * inMsg)
{
	LOG_MSG("I/Alert %s\n",inMsg);
	NSAlert *alert = [[NSAlert alloc] init];;
	[alert setMessageText:[NSString stringWithUTF8String:inMsg]];
	[alert runModal];
	[alert release];
}

int		ConfirmMessage(const char* inMsg, const char* proceedBtn, const char* cancelBtn, const char* optionBtn)
{
	NSAlert *alert = [[NSAlert alloc] init];;
	[alert setMessageText:[NSString stringWithUTF8String:inMsg]];
	[alert addButtonWithTitle:[NSString stringWithUTF8String:proceedBtn]];
	if (optionBtn)
		[alert addButtonWithTitle:[NSString stringWithUTF8String:optionBtn]];
	[alert addButtonWithTitle:[NSString stringWithUTF8String:cancelBtn]];
	int r = [alert runModal];
	[alert release];

	if (r == NSAlertFirstButtonReturn)
		return 1;
	if (optionBtn && r == NSAlertSecondButtonReturn)
		return 2;
	return 0;
}

int DoSaveDiscardDialog(const char * inMessage1, const char * inMessage2)
{
	NSAlert *alert = [[NSAlert alloc] init];;
	[alert setMessageText:[NSString stringWithUTF8String:inMessage1]];
	[alert setInformativeText:[NSString stringWithUTF8String:inMessage2]];
	[alert addButtonWithTitle:[NSString stringWithUTF8String:"Save"]];
	[alert addButtonWithTitle:[NSString stringWithUTF8String:"Cancel"]];
	[alert addButtonWithTitle:[NSString stringWithUTF8String:"Discard"]];
	int r = [alert runModal];
	[alert release];

	if (r == NSAlertFirstButtonReturn)
		return close_Save;
	if (r == NSAlertSecondButtonReturn)
		return close_Cancel;
	return close_Discard;
}

