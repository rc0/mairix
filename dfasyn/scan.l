/**********************************************************************
  $Header: /cvs/src/dfasyn/scan.l,v 1.2 2003/03/02 23:42:11 richard Exp $

  Lexical analyser definition for input files defining an NFA

 *********************************************************************/

/* 
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2001-2003
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 **********************************************************************
 */

%{
#include "n2d.h"
#include "parse.h"

/* yyunput() not used - define this to avoid compiler warnings */
#define YY_NO_UNPUT

int lineno = 1;
%}

%x PASSTHRU
%x STR

%%

STATE|State|state           { return STATE; }
ABBREV|Abbrev|abbrev        { return ABBREV; }
DEFINE|Define|define        { return DEFINE; }
TOKENS|Tokens|tokens        { return TOKENS; }
PREFIX|Prefix|prefix        { return PREFIX; }
BLOCK|Block|block           { return BLOCK; }
ENDBLOCK|EndBlock           { return ENDBLOCK; }
Endblock|endblock           { return ENDBLOCK; }
TYPE|Type|type              { return TYPE; }
RESULT|Result|result        { return RESULT; }
EARLYRESULT                 { return EARLYRESULT; }
EarlyResult                 { return EARLYRESULT; }
Earlyresult                 { return EARLYRESULT; }
earlyresult                 { return EARLYRESULT; }
ATTRIBUTE|ATTR              { return ATTR; }
Attribute|Attr              { return ATTR; }
attribute|attr              { return ATTR; }
DEFATTR|DefAttr             { return DEFATTR; }
Defattr|defattr             { return DEFATTR; }
DEFRESULT|DefResult         { return DEFRESULT; }
Defresult|defresult         { return DEFRESULT; }
SYMBOL|Symbol|symbol        { return SYMBOL; }
SYMRESULT|SymResult         { return SYMRESULT; }
Symresult|symresult         { return SYMRESULT; }
EARLYSYMRESULT              { return EARLYSYMRESULT; }
EarlySymResult              { return EARLYSYMRESULT; }
EarlySymresult              { return EARLYSYMRESULT; }
Earlysymresult              { return EARLYSYMRESULT; }
earlysymresult              { return EARLYSYMRESULT; }
[A-Za-z0-9_.]+              { yylval.s = new_string(yytext); return STRING; }
\#.*$                       { /* strip comments */ }
\-\>                        { return ARROW; }
=                           { return EQUAL; }
\|                          { return PIPE; /* OR */ }
\&                          { return AND; }
\~                          { return NOT; }
\!                          { return NOT; }
\^                          { return XOR; }
\*                          { return STAR; }
\?                          { return QUERY; }
\:                          { return COLON; }
\;                          { return SEMICOLON; }
\(                          { return LPAREN; }
\)                          { return RPAREN; }
\<                          { return LANGLE; }
\>                          { return RANGLE; }
\,                          { return COMMA; }
\n                          { lineno++; }
[ \t]+                      { /* ignore */ }
^\%\{[ \t]*\n               { BEGIN PASSTHRU; }
\"                          { BEGIN STR; }
.                           { printf("Unmatched input <%s> at line %d\n", yytext, lineno); exit (1); }

<PASSTHRU>^\%\}[ \t]*\n     { BEGIN INITIAL; }
<PASSTHRU>\n                { fputs(yytext, yyout); lineno++; }
<PASSTHRU>.+                { fputs(yytext, yyout); }

<STR>\"                     { BEGIN INITIAL; }
<STR>[^"]*                  { yylval.s = new_string(yytext); return STRING; }



