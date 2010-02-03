//This file is part of Glest Shared Library (www.glest.org)
//Copyright (C) 2005 Matthias Braun <matze@braunis.de>

//You can redistribute this code and/or modify it under
//the terms of the GNU General Public License as published by the Free Software
//Foundation; either version 2 of the License, or (at your option) any later
//version.
#include "platform_util.h"

#include <iostream>
#include <sstream>
#include <cassert>

#include <glob.h>
#include <errno.h>
#include <string.h>

#include <SDL.h>

#include "util.h"
#include "conversion.h"
#include "leak_dumper.h"
#include "sdl_private.h"
#include "window.h"
#include "noimpl.h"

#include "checksum.h"
#include "socket.h"
#include <algorithm>

using namespace Shared::Util;
using namespace std;

namespace Shared{ namespace Platform{

namespace Private{

bool shouldBeFullscreen = false;
int ScreenWidth;
int ScreenHeight;

}

// =====================================
//          PerformanceTimer
// =====================================

void PerformanceTimer::init(float fps, int maxTimes){
	times= 0;
	this->maxTimes= maxTimes;

	lastTicks= SDL_GetTicks();

	updateTicks= static_cast<int>(1000./fps);
}

bool PerformanceTimer::isTime(){
	Uint32 thisTicks = SDL_GetTicks();

	if((thisTicks-lastTicks)>=updateTicks && times<maxTimes){
		lastTicks+= updateTicks;
		times++;
		return true;
	}
	times= 0;
	return false;
}

void PerformanceTimer::reset(){
	lastTicks= SDL_GetTicks();
}

// =====================================
//         Chrono
// =====================================

Chrono::Chrono() {
	freq = 1000;
	stopped= true;
	accumCount= 0;
}

void Chrono::start() {
	stopped= false;
	startCount = SDL_GetTicks();
}

void Chrono::stop() {
	Uint32 endCount;
	endCount = SDL_GetTicks();
	accumCount += endCount-startCount;
	stopped= true;
}

int64 Chrono::getMicros() const {
	return queryCounter(1000000);
}

int64 Chrono::getMillis() const {
	return queryCounter(1000);
}

int64 Chrono::getSeconds() const {
	return queryCounter(1);
}

int64 Chrono::queryCounter(int multiplier) const {
	if(stopped) {
		return multiplier*accumCount/freq;
	} else {
		Uint32 endCount;
		endCount = SDL_GetTicks();
		return multiplier*(accumCount+endCount-startCount)/freq;
	}
}

// =====================================
//         Misc
// =====================================

//finds all filenames like path and stores them in resultys
void findAll(const string &path, vector<string> &results, bool cutExtension) {
	results.clear();

	std::string mypath = path;
	/** Stupid win32 is searching for all files without extension when *. is
	 * specified as wildcard
	 */
	if(mypath.compare(mypath.size() - 2, 2, "*.") == 0) {
		mypath = mypath.substr(0, mypath.size() - 2);
		mypath += "*";
	}

	glob_t globbuf;

	int res = glob(mypath.c_str(), 0, 0, &globbuf);
	if(res < 0) {
		std::stringstream msg;
		msg << "Couldn't scan directory '" << mypath << "': " << strerror(errno);
		throw runtime_error(msg.str());
	}

	for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
		const char* p = globbuf.gl_pathv[i];
		const char* begin = p;
		for( ; *p != 0; ++p) {
			// strip the path component
			if(*p == '/')
				begin = p+1;
		}
		results.push_back(begin);
	}

	globfree(&globbuf);

	if(results.size() == 0) {
		throw runtime_error("No files found in: " + mypath);
	}

	if(cutExtension) {
		for (size_t i=0; i<results.size(); ++i){
			results.at(i)=cutLastExt(results.at(i));
		}
	}
}

int isdir(const char *path)
{
  struct stat stats;

  return stat (path, &stats) == 0 && S_ISDIR (stats.st_mode);
}

bool EndsWith(const string &str, const string& key)
{
    size_t keylen = key.length();
    size_t strlen = str.length();

    if(keylen <= strlen)
        return string::npos != str.rfind(key.c_str(),strlen - keylen, keylen);
    else
        return false;
}

//finds all filenames like path and gets their checksum of all files combined
int32 getFolderTreeContentsCheckSumRecursively(const string &path, const string &filterFileExt, Checksum *recursiveChecksum) {

    Checksum checksum = (recursiveChecksum == NULL ? Checksum() : *recursiveChecksum);

    //if(Socket::enableDebugText) printf("In [%s::%s] scanning [%s]\n",__FILE__,__FUNCTION__,path.c_str());

	std::string mypath = path;
	/** Stupid win32 is searching for all files without extension when *. is
	 * specified as wildcard
	 */
	if(mypath.compare(mypath.size() - 2, 2, "*.") == 0) {
		mypath = mypath.substr(0, mypath.size() - 2);
		mypath += "*";
	}

	glob_t globbuf;

	int res = glob(mypath.c_str(), 0, 0, &globbuf);
	if(res < 0) {
		std::stringstream msg;
		msg << "Couldn't scan directory '" << mypath << "': " << strerror(errno);
		throw runtime_error(msg.str());
	}

	for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
		const char* p = globbuf.gl_pathv[i];
		/*
		const char* begin = p;
		for( ; *p != 0; ++p) {
			// strip the path component
			if(*p == '/')
				begin = p+1;
		}
		*/

		if(isdir(p) == 0)
		{
            bool addFile = true;
            if(filterFileExt != "")
            {
                addFile = EndsWith(p, filterFileExt);
            }

            if(addFile)
            {
                //if(Socket::enableDebugText) printf("In [%s::%s] adding file [%s]\n",__FILE__,__FUNCTION__,p);

                checksum.addFile(p);
            }
		}
	}

	globfree(&globbuf);

    // Look recursively for sub-folders
	res = glob(mypath.c_str(), GLOB_ONLYDIR, 0, &globbuf);
	if(res < 0) {
		std::stringstream msg;
		msg << "Couldn't scan directory '" << mypath << "': " << strerror(errno);
		throw runtime_error(msg.str());
	}

	for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
		const char* p = globbuf.gl_pathv[i];
		/*
		const char* begin = p;
		for( ; *p != 0; ++p) {
			// strip the path component
			if(*p == '/')
				begin = p+1;
		}
		*/

        getFolderTreeContentsCheckSumRecursively(string(p) + "/*", filterFileExt, &checksum);
	}

	globfree(&globbuf);

    return checksum.getSum();
}

//finds all filenames like path and gets the checksum of each file
vector<std::pair<string,int32> > getFolderTreeContentsCheckSumListRecursively(const string &path, const string &filterFileExt, vector<std::pair<string,int32> > *recursiveMap) {

    vector<std::pair<string,int32> > checksumFiles = (recursiveMap == NULL ? vector<std::pair<string,int32> >() : *recursiveMap);

    //if(Socket::enableDebugText) printf("In [%s::%s] scanning [%s]\n",__FILE__,__FUNCTION__,path.c_str());

	std::string mypath = path;
	/** Stupid win32 is searching for all files without extension when *. is
	 * specified as wildcard
	 */
	if(mypath.compare(mypath.size() - 2, 2, "*.") == 0) {
		mypath = mypath.substr(0, mypath.size() - 2);
		mypath += "*";
	}

	glob_t globbuf;

	int res = glob(mypath.c_str(), 0, 0, &globbuf);
	if(res < 0) {
		std::stringstream msg;
		msg << "Couldn't scan directory '" << mypath << "': " << strerror(errno);
		throw runtime_error(msg.str());
	}

	for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
		const char* p = globbuf.gl_pathv[i];
		/*
		const char* begin = p;
		for( ; *p != 0; ++p) {
			// strip the path component
			if(*p == '/')
				begin = p+1;
		}
		*/

		if(isdir(p) == 0)
		{
            bool addFile = true;
            if(filterFileExt != "")
            {
                addFile = EndsWith(p, filterFileExt);
            }

            if(addFile)
            {
                //if(Socket::enableDebugText) printf("In [%s::%s] adding file [%s]\n",__FILE__,__FUNCTION__,p);

                Checksum checksum;
                checksum.addFile(p);

                checksumFiles.push_back(std::pair<string,int32>(p,checksum.getSum()));
            }
		}
	}

	globfree(&globbuf);

    // Look recursively for sub-folders
	res = glob(mypath.c_str(), GLOB_ONLYDIR, 0, &globbuf);
	if(res < 0) {
		std::stringstream msg;
		msg << "Couldn't scan directory '" << mypath << "': " << strerror(errno);
		throw runtime_error(msg.str());
	}

	for(size_t i = 0; i < globbuf.gl_pathc; ++i) {
		const char* p = globbuf.gl_pathv[i];
		/*
		const char* begin = p;
		for( ; *p != 0; ++p) {
			// strip the path component
			if(*p == '/')
				begin = p+1;
		}
		*/

        checksumFiles = getFolderTreeContentsCheckSumListRecursively(string(p) + "/*", filterFileExt, &checksumFiles);
	}

	globfree(&globbuf);

    return checksumFiles;
}

string extractDirectoryPathFromFile(string filename)
{
    return filename.substr( 0, filename.rfind("/")+1 );
}

void createDirectoryPaths(string Path)
{
 char DirName[256]="";
 const char *path = Path.c_str();
 char *dirName = DirName;
 while(*path)
 {
   //if (('\\' == *path) || ('/' == *path))
   if ('/' == *path)
   {
     //if (':' != *(path-1))
     {
        mkdir(DirName, S_IRWXO);
     }
   }
   *dirName++ = *path++;
   *dirName = '\0';
 }
 mkdir(DirName, S_IRWXO);
}

bool changeVideoMode(int resW, int resH, int colorBits, int ) {
	Private::shouldBeFullscreen = true;
	return true;
}

void restoreVideoMode() {
}

void message(string message) {
	std::cerr << "******************************************************\n";
	std::cerr << "    " << message << "\n";
	std::cerr << "******************************************************\n";
}

bool ask(string message) {
	std::cerr << "Confirmation: " << message << "\n";
	int res;
	std::cin >> res;
	return res != 0;
}

void exceptionMessage(const exception &excp) {
	std::cerr << "Exception: " << excp.what() << std::endl;
}

int getScreenW() {
	return SDL_GetVideoSurface()->w;
}

int getScreenH() {
	return SDL_GetVideoSurface()->h;
}

void sleep(int millis) {
	SDL_Delay(millis);
}

void showCursor(bool b) {
	SDL_ShowCursor(b ? SDL_ENABLE : SDL_DISABLE);
}

bool isKeyDown(int virtualKey) {
	char key = static_cast<char> (virtualKey);
	const Uint8* keystate = SDL_GetKeyState(0);

	// kinda hack and wrong...
	if(key >= 0) {
		return keystate[key];
	}
	switch(key) {
		case vkAdd:
			return keystate[SDLK_PLUS] | keystate[SDLK_KP_PLUS];
		case vkSubtract:
			return keystate[SDLK_MINUS] | keystate[SDLK_KP_MINUS];
		case vkAlt:
			return keystate[SDLK_LALT] | keystate[SDLK_RALT];
		case vkControl:
			return keystate[SDLK_LCTRL] | keystate[SDLK_RCTRL];
		case vkShift:
			return keystate[SDLK_LSHIFT] | keystate[SDLK_RSHIFT];
		case vkEscape:
			return keystate[SDLK_ESCAPE];
		case vkUp:
			return keystate[SDLK_UP];
		case vkLeft:
			return keystate[SDLK_LEFT];
		case vkRight:
			return keystate[SDLK_RIGHT];
		case vkDown:
			return keystate[SDLK_DOWN];
		case vkReturn:
			return keystate[SDLK_RETURN] | keystate[SDLK_KP_ENTER];
		case vkBack:
			return keystate[SDLK_BACKSPACE];
		default:
			std::cerr << "isKeyDown called with unknown key.\n";
			break;
	}
	return false;
}

}}//end namespace
