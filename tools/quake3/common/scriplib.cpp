/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

// scriplib.c

#include "cmdlib.h"
#include "inout.h"
#include "scriplib.h"
#include "vfs.h"

/*
   =============================================================================

                        PARSING STUFF

   =============================================================================
 */

struct script_t
{
	char filename[1024];
	char    *buffer,*script_p,*end_p;
	int line;
};

#define MAX_INCLUDES    8
script_t scriptstack[MAX_INCLUDES];
script_t    *script;
int scriptline;

char token[MAXTOKEN];
bool endofscript;
bool tokenready;                     // only true if UnGetToken was just called

/*
   ==============
   AddScriptToStack
   ==============
 */
void AddScriptToStack( const char *filename, int index ){
	int size;
	void* buffer;

	script++;
	if ( script == &scriptstack[MAX_INCLUDES] ) {
		Error( "script file exceeded MAX_INCLUDES" );
	}
	strcpy( script->filename, ExpandPath( filename ) );

	size = vfsLoadFile( script->filename, &buffer, index );

	if ( size == -1 ) {
		Sys_FPrintf( SYS_WRN, "Script file %s was not found\n", script->filename );
		script--;
	}
	else
	{
		if ( index > 0 ) {
			Sys_Printf( "entering %s (%d)\n", script->filename, index + 1 );
		}
		else{
			Sys_Printf( "entering %s\n", script->filename );
		}

		script->buffer = void_ptr( buffer );
		script->line = 1;
		script->script_p = script->buffer;
		script->end_p = script->buffer + size;
	}
}


/*
   ==============
   LoadScriptFile
   ==============
 */
void LoadScriptFile( const char *filename, int index ){
	script = scriptstack;
	AddScriptToStack( filename, index );

	endofscript = false;
	tokenready = false;
}
/* &unload current; for autopacker */
void SilentLoadScriptFile( const char *filename, int index ){
	int size;

	if ( script->buffer != NULL && !endofscript ) {
		free( script->buffer );
		script->buffer = NULL;
	}

	script = scriptstack;

	script++;
	if ( script == &scriptstack[MAX_INCLUDES] ) {
		Error( "script file exceeded MAX_INCLUDES" );
	}
	strcpy( script->filename, ExpandPath( filename ) );

	size = vfsLoadFile( script->filename, (void **)&script->buffer, index );

	if ( size == -1 ) {
		Sys_FPrintf( SYS_WRN, "Script file %s was not found\n", script->filename );
	}
	script->line = 1;
	script->script_p = script->buffer;
	script->end_p = script->buffer + size;

	endofscript = false;
	tokenready = false;
}

/*
   ==============
   ParseFromMemory
   ==============
 */
void ParseFromMemory( char *buffer, int size ){
	script = scriptstack;
	script++;
	if ( script == &scriptstack[MAX_INCLUDES] ) {
		Error( "script file exceeded MAX_INCLUDES" );
	}
	strcpy( script->filename, "memory buffer" );

	script->buffer = buffer;
	script->line = 1;
	script->script_p = script->buffer;
	script->end_p = script->buffer + size;

	endofscript = false;
	tokenready = false;
}


/*
   ==============
   UnGetToken

   Signals that the current token was not used, and should be reported
   for the next GetToken.  Note that

   GetToken (true);
   UnGetToken ();
   GetToken (false);

   could cross a line boundary.
   ==============
 */
void UnGetToken( void ){
	tokenready = true;
}


bool EndOfScript( bool crossline ){
	if ( !crossline ) {
		Error( "Line %i is incomplete\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
	}

	if ( strEqual( script->filename, "memory buffer" ) ) {
		endofscript = true;
		return false;
	}

	if ( script->buffer == NULL ) {
		Sys_Warning( "Attempt to free already freed script buffer\n" );
	}
	else{
		free( script->buffer );
	}
	script->buffer = NULL;
	if ( script == scriptstack + 1 ) {
		endofscript = true;
		return false;
	}
	script--;
	scriptline = script->line;
	Sys_Printf( "returning to %s\n", script->filename );
	return GetToken( crossline );
}

/*
   ==============
   GetToken
   ==============
 */
bool GetToken( bool crossline ){
	char    *token_p;


	/* ydnar: dummy testing */
	if ( script == NULL || script->buffer == NULL ) {
		return false;
	}

	if ( tokenready ) {                       // is a token already waiting?
		tokenready = false;
		return true;
	}

	if ( ( script->script_p >= script->end_p ) || ( script->script_p == NULL ) ) {
		return EndOfScript( crossline );
	}

//
// skip space
//
skipspace:
	while ( script->script_p < script->end_p && *script->script_p <= 32 )
	{
		if ( *script->script_p++ == '\n' ) {
			if ( !crossline ) {
				Error( "Line %i is incomplete\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
			}
			script->line++;
			scriptline = script->line;
		}
	}

	if ( script->script_p >= script->end_p ) {
		return EndOfScript( crossline );
	}

	// ; # // comments
	if ( *script->script_p == ';' || *script->script_p == '#'
	     || ( script->script_p[0] == '/' && script->script_p[1] == '/' ) ) {
		if ( !crossline ) {
			Error( "Line %i is incomplete\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
		}
		while ( *script->script_p++ != '\n' )
			if ( script->script_p >= script->end_p ) {
				return EndOfScript( crossline );
			}
		script->line++;
		scriptline = script->line;
		goto skipspace;
	}

	// /* */ comments
	if ( script->script_p[0] == '/' && script->script_p[1] == '*' ) {
		if ( !crossline ) {
			Error( "Line %i is incomplete\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
		}
		script->script_p += 2;
		while ( script->script_p[0] != '*' || script->script_p[1] != '/' )
		{
			if ( *script->script_p == '\n' ) {
				script->line++;
				scriptline = script->line;
			}
			script->script_p++;
			if ( script->script_p >= script->end_p ) {
				return EndOfScript( crossline );
			}
		}
		script->script_p += 2;
		goto skipspace;
	}

//
// copy token
//
	token_p = token;

	if ( *script->script_p == '"' ) {
		// quoted token
		script->script_p++;
		while ( *script->script_p != '"' )
		{
			*token_p++ = *script->script_p++;
			if ( script->script_p == script->end_p ) {
				break;
			}
			if ( token_p == &token[MAXTOKEN] ) {
				Error( "Token too large on line %i\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
			}
		}
		script->script_p++;
	}
	else{   // regular token
		while ( *script->script_p > 32 && *script->script_p != ';' )
		{
			*token_p++ = *script->script_p++;
			if ( script->script_p == script->end_p ) {
				break;
			}
			if ( token_p == &token[MAXTOKEN] ) {
				Error( "Token too large on line %i\nFile location be: %s\n", scriptline, g_strLoadedFileLocation );
			}
		}
	}

	*token_p = 0;

	if ( strEqual( token, "$include" ) ) {
		GetToken( false );
		AddScriptToStack( token, 0 );
		return GetToken( crossline );
	}

	return true;
}


/*
   ==============
   TokenAvailable

   Returns true if there is another token on the line
   ==============
 */
bool TokenAvailable( void ) {
	int oldLine;

	/* save */
	oldLine = scriptline;

	/* test */
	if ( !GetToken( true ) ) {
		return false;
	}
	UnGetToken();
	if ( oldLine == scriptline ) {
		return true;
	}

	/* restore */
	//%	scriptline = oldLine;
	//%	script->line = oldScriptLine;

	return false;
}


//=====================================================================


void MatchToken( const char *match ) {
	GetToken( true );

	if ( !strEqual( token, match ) ) {
		Error( "MatchToken( \"%s\" ) failed at line %i in file %s", match, scriptline, script->filename );
	}
}


template<typename T>
void Parse1DMatrix( int x, T *m ) {
	int i;

	MatchToken( "(" );

	for ( i = 0 ; i < x ; i++ ) {
		GetToken( false );
		m[i] = atof( token );
	}

	MatchToken( ")" );
}
template void Parse1DMatrix<float>( int x, float *m );
template void Parse1DMatrix<double>( int x, double *m );

void Parse2DMatrix( int y, int x, float *m ) {
	int i;

	MatchToken( "(" );

	for ( i = 0 ; i < y ; i++ ) {
		Parse1DMatrix( x, m + i * x );
	}

	MatchToken( ")" );
}

void Parse3DMatrix( int z, int y, int x, float *m ) {
	int i;

	MatchToken( "(" );

	for ( i = 0 ; i < z ; i++ ) {
		Parse2DMatrix( y, x, m + i * x * y );
	}

	MatchToken( ")" );
}


void Write1DMatrix( FILE *f, int x, float *m ) {
	int i;

	fprintf( f, "( " );
	for ( i = 0 ; i < x ; i++ ) {
		if ( m[i] == (int)m[i] ) {
			fprintf( f, "%i ", (int)m[i] );
		}
		else {
			fprintf( f, "%f ", m[i] );
		}
	}
	fprintf( f, ")" );
}

void Write2DMatrix( FILE *f, int y, int x, float *m ) {
	int i;

	fprintf( f, "( " );
	for ( i = 0 ; i < y ; i++ ) {
		Write1DMatrix( f, x, m + i * x );
		fprintf( f, " " );
	}
	fprintf( f, ")\n" );
}


void Write3DMatrix( FILE *f, int z, int y, int x, float *m ) {
	int i;

	fprintf( f, "(\n" );
	for ( i = 0 ; i < z ; i++ ) {
		Write2DMatrix( f, y, x, m + i * ( x * y ) );
	}
	fprintf( f, ")\n" );
}
