/*
 *  Copyright 2001-2007 Adrian Thurston <thurston@complang.org>
 */

/*  This file is part of Colm.
 *
 *  Colm is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  Colm is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with Colm; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sstream>

#include "colm.h"
#include "debug.h"
#include "lmscan.h"
#include "pcheck.h"
#include "vector.h"
#include "version.h"
#include "keyops.h"
#include "parsedata.h"
#include "vector.h"
#include "version.h"
#include "fsmcodegen.h"

using std::istream;
using std::ifstream;
using std::ostream;
using std::ios;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;

/* Graphviz dot file generation. */
bool genGraphviz = false;

using std::ostream;
using std::istream;
using std::ifstream;
using std::ofstream;
using std::ios;
using std::cout;
using std::cerr;
using std::cin;
using std::endl;

/* Io globals. */
istream *inStream = 0;
ostream *outStream = 0;
const char *inputFileName = 0;
const char *outputFileName = 0;

bool generateGraphviz = false;
bool verbose = false;
bool logging = false;
bool branchPointInfo = false;
bool addUniqueEmptyProductions = false;

ArgsVector includePaths;

/* Print version information. */
void version();

/* Total error count. */
int gblErrorCount = 0;

HostType hostTypesC[] =
{
	{ "char",     0,       true,   CHAR_MIN,  CHAR_MAX,   sizeof(char) },
};

HostLang hostLangC =    { hostTypesC,    8, hostTypesC+0,    true };

HostLang *hostLang = &hostLangC;
HostLangType hostLangType = CCode;

/* Print the opening to an error in the input, then return the error ostream. */
ostream &error( const InputLoc &loc )
{
	/* Keep the error count. */
	gblErrorCount += 1;

	cerr << "error: " << inputFileName << ":" << 
			loc.line << ":" << loc.col << ": ";
	return cerr;
}

/* Print the opening to a program error, then return the error stream. */
ostream &error()
{
	gblErrorCount += 1;
	cerr << "error: " PROGNAME ": ";
	return cerr;
}


/* Print the opening to a warning, then return the error ostream. */
ostream &warning( )
{
	cerr << "warning: " << inputFileName << ": ";
	return cerr;
}

/* Print the opening to a warning in the input, then return the error ostream. */
ostream &warning( const InputLoc &loc )
{
	assert( inputFileName != 0 );
	cerr << "warning: " << inputFileName << ":" << 
			loc.line << ":" << loc.col << ": ";
	return cerr;
}

void escapeLineDirectivePath( std::ostream &out, char *path )
{
	for ( char *pc = path; *pc != 0; pc++ ) {
		if ( *pc == '\\' )
			out << "\\\\";
		else
			out << *pc;
	}
}

void escapeLineDirectivePath( std::ostream &out, char *path );
void scan( char *fileName, istream &input );

bool printStatistics = false;

/* Print a summary of the options. */
void usage()
{
	cout <<
"usage: colm [options] file\n"
"general:\n"
"   -h, -H, -?, --help   print this usage and exit\n"
"   --version            print version information and exit\n"
"   -o <file>            write output to <file>\n"
"   -i                   show conflict information\n"
"   -v                   make colm verbose\n"
"   -l                   compile logging into the output executable\n"
	;	
}

/* Print version information. */
void version()
{
	cout << "Colm version " VERSION << " " PUBDATE << endl <<
			"Copyright (c) 2007, 2008 by Adrian Thurston" << endl;
}

/* Scans a string looking for the file extension. If there is a file
 * extension then pointer returned points to inside the string
 * passed in. Otherwise returns null. */
const char *findFileExtension( const char *stemFile )
{
	const char *ppos = stemFile + strlen(stemFile) - 1;

	/* Scan backwards from the end looking for the first dot.
	 * If we encounter a '/' before the first dot, then stop the scan. */
	while ( 1 ) {
		/* If we found a dot or got to the beginning of the string then
		 * we are done. */
		if ( ppos == stemFile || *ppos == '.' )
			break;

		/* If we hit a / then there is no extension. Done. */
		if ( *ppos == '/' ) {
			ppos = stemFile;
			break;
		}
		ppos--;
	} 

	/* If we got to the front of the string then bail we 
	 * did not find an extension  */
	if ( ppos == stemFile )
		ppos = 0;

	return ppos;
}

/* Make a file name from a stem. Removes the old filename suffix and
 * replaces it with a new one. Returns a newed up string. */
char *fileNameFromStem( const char *stemFile, const char *suffix )
{
	int len = strlen( stemFile );
	assert( len > 0 );

	/* Get the extension. */
	const char *ppos = findFileExtension( stemFile );

	/* If an extension was found, then shorten what we think the len is. */
	if ( ppos != 0 )
		len = ppos - stemFile;

	/* Make the return string from the stem and the suffix. */
	char *retVal = new char[ len + strlen( suffix ) + 1 ];
	strncpy( retVal, stemFile, len );
	strcpy( retVal + len, suffix );

	return retVal;
}


/* Invoked by the parser when the root element is opened. */
void openOutput( )
{
	/* If the output format is code and no output file name is given, then
	 * make a default. */
	if ( outputFileName == 0 ) {
		const char *ext = findFileExtension( inputFileName );
		if ( ext != 0 && strcmp( ext, ".rh" ) == 0 )
			outputFileName = fileNameFromStem( inputFileName, ".h" );
		else {
			const char *defExtension = ".c";
			outputFileName = fileNameFromStem( inputFileName, defExtension );
		}
	}

	if ( colm_log_compile ) {
		cerr << "opening output file: " << outputFileName << endl;
	}

	/* Make sure we are not writing to the same file as the input file. */
	if ( outputFileName != 0 && strcmp( inputFileName, outputFileName  ) == 0 ) {
		error() << "output file \"" << outputFileName  << 
				"\" is the same as the input file" << endl;
	}

	if ( outputFileName != 0 ) {
		/* Open the output stream, attaching it to the filter. */
		ofstream *outFStream = new ofstream( outputFileName );

		if ( !outFStream->is_open() ) {
			error() << "error opening " << outputFileName << " for writing" << endl;
			exit(1);
		}

		outStream = outFStream;
	}
	else {
		/* Writing out ot std out. */
		outStream = &cout;
	}
}

void compileOutputCommand( const char *command )
{
	if ( colm_log_compile )
		cout << "compiling with: " << command << endl;
	int res = system( command );
	if ( res != 0 )
		cout << "there was a problem compiling the output" << endl;
}

void compileOutputPath( const char *argv0 )
{
	/* Find the location of the colm program that is executing. */
	char *location = strdup( argv0 );
	char *last = location + strlen(location) - 1;
	while ( true ) {
		if ( last == location ) {
			last[0] = '.';
			last[1] = 0;
			break;
		}
		if ( *last == '/' ) {
			last[0] = 0;
			break;
		}
		last -= 1;
	}

	char *exec = fileNameFromStem( outputFileName, ".bin" );

	int length = 1024 + 3*strlen(location) + strlen(outputFileName) + strlen(exec);
	char command[length];
	sprintf( command, 
		"gcc -Wall -Wwrite-strings"
		" -I" PREFIX "/include"
		" -g"
		" -o %s"
		" %s"
		" -L" PREFIX "/lib"
		" -lcolm%c",
		exec, outputFileName, logging ? 'd' : 'p' );

	compileOutputCommand( command );
}

void compileOutputRelative( const char *argv0 )
{
	/* Find the location of the colm program that is executing. */
	char *location = strdup( argv0 );
	char *last = strrchr( location, '/' );
	assert( last != 0 );
	last[1] = 0;

	char *exec = fileNameFromStem( outputFileName, ".bin" );

	int length = 1024 + 3*strlen(location) + strlen(outputFileName) + strlen(exec);
	char command[length];
	sprintf( command, 
		"gcc -Wall -Wwrite-strings"
		" -I%s.."
		" -I%s../aapl"
		" -g"
		" -o %s"
		" %s"
		" -L%s"
		" -lcolm%c",
		location, location,
		exec, outputFileName, location, logging ? 'd' : 'p' );
	
	compileOutputCommand( command );
}

void compileOutput( const char *argv0 )
{
	if ( strchr( argv0, '/' ) == 0 )
		compileOutputPath( argv0 );
	else
		compileOutputRelative( argv0 );
}

void process_args( int argc, const char **argv )
{
	ParamCheck pc( "I:vlio:S:M:vHh?-:sV", argc, argv );

	while ( pc.check() ) {
		switch ( pc.state ) {
		case ParamCheck::match:
			switch ( pc.parameter ) {
			case 'I':
				includePaths.append( pc.parameterArg );
				break;
			case 'v':
				verbose = true;
				break;
			case 'l':
				logging = true;
				break;
			case 'i':
				branchPointInfo = true;
				break;
			/* Output. */
			case 'o':
				if ( *pc.parameterArg == 0 )
					error() << "a zero length output file name was given" << endl;
				else if ( outputFileName != 0 )
					error() << "more than one output file name was given" << endl;
				else {
					/* Ok, remember the output file name. */
					outputFileName = pc.parameterArg;
				}
				break;

			case 'H': case 'h': case '?':
				usage();
				exit(0);
			case 's':
				printStatistics = true;
				break;
			case 'V':
				generateGraphviz = true;
				break;
			case '-':
				if ( strcasecmp(pc.parameterArg, "help") == 0 ) {
					usage();
					exit(0);
				}
				else if ( strcasecmp(pc.parameterArg, "version") == 0 ) {
					version();
					exit(0);
				}
				else {
					error() << "--" << pc.parameterArg << 
							" is an invalid argument" << endl;
				}
			}
			break;

		case ParamCheck::invalid:
			error() << "-" << pc.parameter << " is an invalid argument" << endl;
			break;

		case ParamCheck::noparam:
			/* It is interpreted as an input file. */
			if ( *pc.curArg == 0 )
				error() << "a zero length input file name was given" << endl;
			else if ( inputFileName != 0 )
				error() << "more than one input file name was given" << endl;
			else {
				/* OK, Remember the filename. */
				inputFileName = pc.curArg;
			}
			break;
		}
	}
}

/* Main, process args and call yyparse to start scanning input. */
int main(int argc, const char **argv)
{
	process_args( argc, argv );

	if ( verbose ) {
		colm_log_bytecode = 1;
		colm_log_parse = 1;
		colm_log_match = 1;
		colm_log_compile = 1;
		colm_log_conds = 1;
		colmActiveRealm = 0xffffffff;
	}
	initInputFuncs();

	/* Bail on above errors. */
	if ( gblErrorCount > 0 )
		exit(1);

	/* Make sure we are not writing to the same file as the input file. */
	if ( inputFileName != 0 && outputFileName != 0 && 
			strcmp( inputFileName, outputFileName  ) == 0 )
	{
		error() << "output file \"" << outputFileName  << 
				"\" is the same as the input file" << endl;
	}

	/* Open the input file for reading. */
	istream *inStream;
	if ( inputFileName != 0 ) {
		/* Open the input file for reading. */
		ifstream *inFile = new ifstream( inputFileName );
		inStream = inFile;
		if ( ! inFile->is_open() )
			error() << "could not open " << inputFileName << " for reading" << endl;
	}
	else {
		inputFileName = "<stdin>";
		inStream = &cin;
	}

	/* Bail on above errors. */
	if ( gblErrorCount > 0 )
		exit(1);


	Scanner scanner( inputFileName, *inStream, cout, 0, 0 );
	scanner.scan();
	scanner.eof();

	/* Parsing complete, check for errors.. */
	if ( gblErrorCount > 0 )
		return 1;

	/* Initiate a compile following a parse. */
	scanner.parser->pd->semanticAnalysis();

	/*
	 * Write output.
	 */
	if ( generateGraphviz ) {
		outStream = &cout;
		scanner.parser->pd->writeDotFile();
	}
	else {
		openOutput();
		scanner.parser->pd->generateOutput();
	
		if ( outStream != 0 )
			delete outStream;

		compileOutput( argv[0] );
	}
	return 0;
}