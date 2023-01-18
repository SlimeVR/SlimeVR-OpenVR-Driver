// NOTE: the following is an amalgamation of vrpathregistry_public.cpp, strtools_public.cpp, envvartools_public.cpp, and pathtools_public.cpp
//       from openvr, to avoid the questions of "Can we already call this somehow?"
//       or "How can we directly integrate this in our build system?"
// TODO: Test on OSX/Linux, because our build system is probably not outputting the macros that this wants for them.
//       I already had to change WIN32 to _WIN32 for the initial include of windows.h
// TODO: This is licensed under BSD 3-Clause which is compatible with MIT,
//       but we should probably do something to ensure we comply with clause 2.

/* Copyright (c) 2015, Valve Corporation
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef __linux__
// set from openvr cmake
#define LINUX
#define POSIX
#endif

#if defined( _WIN32 )
#include <windows.h>
#include <shlobj.h>
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#undef GetEnvironmentVariable
#elif defined (OSX)
#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#elif defined(LINUX)
#include <dlfcn.h>
#include <stdio.h>
#endif

#include <algorithm>
#include <sstream>
#include <locale>
#include <codecvt>


#ifndef VRLog
	#if defined( __MINGW32__ )
		#define VRLog(args...)		fprintf(stderr, args)
	#elif defined( WIN32 )
		#define VRLog(fmt, ...)		fprintf(stderr, fmt, __VA_ARGS__)
	#else
		#define VRLog(args...)		fprintf(stderr, args)
	#endif
#endif

typedef std::codecvt_utf8< wchar_t > convert_type;

std::string UTF16to8(const wchar_t * in)
{
	static std::wstring_convert< convert_type, wchar_t > s_converter;  // construction of this can be expensive (or even serialized) depending on locale

	try
	{
		return s_converter.to_bytes( in );
	}
	catch ( ... )
	{
		return std::string();
	}
}

std::string UTF16to8( const std::wstring & in ) { return UTF16to8( in.c_str() ); }

std::string Path_Join( const std::string & first, const std::string & second, char slash = 0 );

/** Returns the root of the directory the system wants us to store user config data in */
static std::string GetAppSettingsPath()
{
#if defined( WIN32 )
	WCHAR rwchPath[MAX_PATH];

	if( !SUCCEEDED( SHGetFolderPathW( NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, rwchPath ) ) )
	{
		return "";
	}

	// Convert the path to UTF-8 and store in the output
	std::string sUserPath = UTF16to8( rwchPath );

	return sUserPath;
#elif defined( OSX )
	std::string sSettingsDir;
	@autoreleasepool {
		// Search for the path
		NSArray *paths = NSSearchPathForDirectoriesInDomains( NSApplicationSupportDirectory, NSUserDomainMask, YES );
		if ( [paths count] == 0 )
		{
			return "";
		}
		
		NSString *resolvedPath = [paths objectAtIndex:0];
		resolvedPath = [resolvedPath stringByAppendingPathComponent: @"OpenVR"];
		
		if ( ![[NSFileManager defaultManager] createDirectoryAtPath: resolvedPath withIntermediateDirectories:YES attributes:nil error:nil] )
		{
			return "";
		}
		
		sSettingsDir.assign( [resolvedPath UTF8String] );
	}
	return sSettingsDir;
#elif defined( LINUX )

	// As defined by XDG Base Directory Specification 
	// https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

	const char *pchHome = getenv("XDG_CONFIG_HOME");
	if ( ( pchHome != NULL) && ( pchHome[0] != '\0' ) )
	{
		return pchHome;
	}

	//
	// XDG_CONFIG_HOME is not defined, use ~/.config instead
	// 
	pchHome = getenv( "HOME" );
	if ( pchHome == NULL )
	{
		return "";
	}

	std::string sUserPath( pchHome );
	sUserPath = Path_Join( sUserPath, ".config" );
	return sUserPath;
#else
	#warning "Unsupported platform"
#endif
}

char Path_GetSlash()
{
#if defined(_WIN32)
	return '\\';
#else
	return '/';
#endif
}

/** Jams two paths together with the right kind of slash */
std::string Path_Join( const std::string & first, const std::string & second, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	// only insert a slash if we don't already have one
	std::string::size_type nLen = first.length();
	if( !nLen )
		return second;
#if defined(_WIN32)
	if( first.back() == '\\' || first.back() == '/' )
	    nLen--;
#else
	char last_char = first[first.length()-1];
	if (last_char == '\\' || last_char == '/')
	    nLen--;
#endif

	return first.substr( 0, nLen ) + std::string( 1, slash ) + second;
}

/** Fixes the directory separators for the current platform */
std::string Path_FixSlashes( const std::string & sPath, char slash = 0 )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	std::string sFixed = sPath;
	for( std::string::iterator i = sFixed.begin(); i != sFixed.end(); i++ )
	{
		if( *i == '/' || *i == '\\' )
			*i = slash;
	}

	return sFixed;
}

// ---------------------------------------------------------------------------
// Purpose: Computes the registry filename
// ---------------------------------------------------------------------------
std::string GetOpenVRConfigPath()
{
	std::string sConfigPath = GetAppSettingsPath();
	if( sConfigPath.empty() )
		return "";

#if defined( _WIN32 ) || defined( LINUX )
	sConfigPath = Path_Join( sConfigPath, "openvr" );
#elif defined ( OSX ) 
	sConfigPath = Path_Join( sConfigPath, ".openvr" );
#else
	#warning "Unsupported platform"
#endif
	sConfigPath = Path_FixSlashes( sConfigPath );
	return sConfigPath;
}

std::string GetEnvironmentVariable( const char *pchVarName )
{
#if defined(_WIN32)
	char rchValue[32767]; // max size for an env var on Windows
	DWORD cChars = GetEnvironmentVariableA( pchVarName, rchValue, sizeof( rchValue ) );
	if( cChars == 0 )
		return "";
	else
		return rchValue;
#elif defined(POSIX)
	char *pchValue = getenv( pchVarName );
	if( pchValue )
		return pchValue;
	else
		return "";
#else
#error "Unsupported Platform"
#endif
}

std::string GetVRPathRegistryFilename()
{
	std::string sOverridePath = GetEnvironmentVariable( "VR_PATHREG_OVERRIDE" );
	if ( !sOverridePath.empty() )
		return sOverridePath;

	std::string sPath = GetOpenVRConfigPath();
	if ( sPath.empty() )
		return "";

#if defined( _WIN32 )
	sPath = Path_Join( sPath, "openvrpaths.vrpath" );
#elif defined ( POSIX ) 
	sPath = Path_Join( sPath, "openvrpaths.vrpath" );
#else
	#error "Unsupported platform"
#endif
	sPath = Path_FixSlashes( sPath );
	return sPath;
}

std::string GetDefaultChaperoneFromConfigPath(std::string path)
{
	return Path_Join(path, "chaperone_info.vrchap");
}

// prevent from leaking
#ifdef __linux__
#undef LINUX
#undef POSIX
#endif