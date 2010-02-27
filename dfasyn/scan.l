/**********************************************************************
  Lexical analyser definition for input files defining an NFA
 *********************************************************************/

/*
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2001-2003,2005,2006
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **********************************************************************
 */

%{
#include "dfasyn.h"
#include "parse.h"

/* yyunput() not used - define this to avoid compiler warnings */
#define YY_NO_UNPUT

int lineno = 1;
%}

%x PASSTHRU
%x STR
%x CHARCLASS

%%

STATE|State|state           { return STATE; }
ABBREV|Abbrev|abbrev        { return ABBREV; }
DEFINE|Define|define        { return DEFINE; }
TOKENS|Tokens|tokens        { return TOKENS; }
PREFIX|Prefix|prefix        { return PREFIX; }
BLOCK|Block|block           { return BLOCK; }
TYPE|Type|type              { return TYPE; }
ENTRY|Entry|entry           { return ENTRY; }
ENTRYSTRUCT                 { return ENTRYSTRUCT; }
EntryStruct                 { return ENTRYSTRUCT; }
Entrystruct                 { return ENTRYSTRUCT; }
entrystruct                 { return ENTRYSTRUCT; }
ATTR|Attr|attr              { return ATTR; }
EARLY|Early|early           { return EARLY; }
DEFATTR|DefAttr             { return DEFATTR; }
Defattr|defattr             { return DEFATTR; }
TAG|Tag|tag                 { return TAG; }
GROUP|Group|group           { return GROUP; }
[A-Za-z0-9_.]+              { yylval.s = new_string(yytext); return STRING; }
\#.*$                       { /* strip comments */ }
\-\>                        { return ARROW; }
=                           { return EQUAL; }
\|                          { return PIPE; /* OR */ }
\&                          { return AND; }
\~                          { return NOT; }
\!                          { return NOT; }
\^                          { return XOR; }
\?                          { return QUERY; }
\:                          { return COLON; }
\;                          { return SEMICOLON; }
\(                          { return LPAREN; }
\)                          { return RPAREN; }
\{                          { return LBRACE; }
\}                          { return RBRACE; }
\<                          { return LANGLE; }
\>                          { return RANGLE; }
\[                          { BEGIN CHARCLASS; return LSQUARE; }
\[\^                        { BEGIN CHARCLASS; return LSQUARE_CARET; }
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

<CHARCLASS>\]               { BEGIN INITIAL; return RSQUARE; }
<CHARCLASS>\-               { return HYPHEN; }
<CHARCLASS>\\-              { yylval.c = '-'; return CHAR; }
<CHARCLASS>\\]              { yylval.c = ']'; return CHAR; }
<CHARCLASS>\\^              { yylval.c = '^'; return CHAR; }
<CHARCLASS>\\n              { yylval.c = '\n'; return CHAR; }
<CHARCLASS>\\r              { yylval.c = '\r'; return CHAR; }
<CHARCLASS>\\f              { yylval.c = '\f'; return CHAR; }
<CHARCLASS>\\t              { yylval.c = '\t'; return CHAR; }
<CHARCLASS>\\\\             { yylval.c = '\\'; return CHAR; }
<CHARCLASS>\^[@A-Z]         { yylval.c = yytext[1] - '@'; return CHAR; }
<CHARCLASS>\\x[0-9a-fA-F][0-9a-fA-F] { unsigned int foo; sscanf(yytext+2,"%x",&foo); yylval.c = (char) foo; return CHAR; }
<CHARCLASS>\\[0-7][0-7][0-7] { unsigned int foo; sscanf(yytext+1,"%o",&foo); yylval.c = (char) foo; return CHAR; }
<CHARCLASS>.                { yylval.c = yytext[0]; return CHAR; }

%{
/* vim:et
*/
%}
