/**********************************************************************
  Grammar definition for input files defining an NFA
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

static Block *curblock = NULL; /* Current block being built */
static State *curstate = NULL; /* Current state being worked on */
static State *addtostate = NULL; /* Current state (incl ext) to which transitions are added */
static StimulusList *curtranslist = NULL; /* Final option set of stimuli prior to ARROW */
static CharClass *curcharclass = NULL;
static Evaluator *current_evaluator = NULL;

State *get_curstate(void) { return curstate; }

%}

%union {
    char c;
    char *s;
    int i;
    Stringlist *sl;
    Stimulus *st;
    StimulusList *stl;
    InlineBlock *ib;
    CharClass *cc;
    Expr *e;
}

%token STRING STATE TOKENS PREFIX ARROW BLOCK ENDBLOCK COLON EQUAL SEMICOLON COMMA
%token ABBREV DEFINE
%type<s> STRING
%type<st> stimulus
%type<sl> tag_seq
%type<stl> stimulus_seq
%type<stl> transition_seq
%type<e> expr
%type<ib> inline_block
%type<c> CHAR
%type<cc> char_class simple_char_class negated_char_class char_class_diff

%token ATTR TAG
%token DEFATTR
%token EARLY
%token TYPE
%token ENTRY
%token ENTRYSTRUCT
%token GROUP
%token LBRACE RBRACE

%token LSQUARE RSQUARE
%token LSQUARE_CARET
%token CHAR HYPHEN

%right QUERY COLON
%left PIPE
%left XOR
%left AND
%left NOT
%left LPAREN RPAREN
%left LANGLE RANGLE

%%

all : decl_seq ;

decl_seq : /* empty */ | decl_seq decl ;

decl : block_decl
     | tokens_decl | abbrev_decl
     | attr_decl | group_decl | tag_decl
     | prefix_decl | entrystruct_decl ;

/* Don't invalidate curstate at the end, this is the means of working out the
   starting state of the NFA */
block_decl : block1 block2 { fixup_state_refs(curblock); curblock = NULL; } ;

block1 : BLOCK STRING LBRACE { curblock = lookup_block($2, CREATE_MUST_NOT_EXIST); addtostate = curstate = NULL; } ;

block2 : instance_decl_seq state_decl_seq RBRACE ;

prefix_decl : PREFIX STRING
              { if (!prefix) {
                  prefix = $2;
                } else {
                  fprintf(stderr, "\n\nWarning: prefix declaration ignored; already set on the command line\n");
                }
              };

tokens_decl : TOKENS token_seq ;

abbrev_decl : ABBREV STRING EQUAL stimulus_seq
              { create_abbrev($2, $4); }
            ;

token_seq : token_seq token | token ;

token : STRING { (void) lookup_token($1, CREATE_MUST_NOT_EXIST); } ;

instance_decl_seq : /* empty */ | instance_decl_seq instance_decl ;

state_decl_seq : /* empty */ | state_decl_seq state_decl ;

state_decl : STATE STRING { addtostate = curstate = lookup_state(curblock, $2, CREATE_OR_USE_OLD); }
             sdecl_seq
           | STATE STRING ENTRY STRING { addtostate = curstate = lookup_state(curblock, $2, CREATE_OR_USE_OLD);
	                                       add_entry_to_state(curstate, $4); }
             sdecl_seq
           ;

sdecl_seq : /* empty */ | sdecl_seq sdecl ;

sdecl : transition_decl ;

instance_decl : STRING COLON STRING { instantiate_block(curblock, $3 /* master_block_name */, $1 /* instance_name */ ); } ;

transition_decl : transition_seq ARROW { curtranslist = $1; } destination_seq { addtostate = curstate; }
                | transition_seq EQUAL tag_seq { addtostate = add_transitions_to_internal(curblock, addtostate, $1);
                                                    add_tags(addtostate, $3);
                                                    addtostate = curstate; }
                ;

destination_seq : STRING                       { add_transitions(curblock, addtostate, curtranslist, $1); }
                | destination_seq COMMA STRING { add_transitions(curblock, addtostate, curtranslist, $3); }
                ;

transition_seq : stimulus_seq { $$ = $1; }
               | transition_seq SEMICOLON stimulus_seq
                 {
                   addtostate = add_transitions_to_internal(curblock, addtostate, $1);
                   $$ = $3;
                 }
               ;

tag_seq : STRING { $$ = add_string_to_list(NULL, $1); }
        | tag_seq COMMA STRING { $$ = add_string_to_list($1, $3); }
        ;

stimulus_seq : stimulus
               { $$ = append_stimulus_to_list(NULL, $1); }
             | stimulus_seq PIPE stimulus
               { $$ = append_stimulus_to_list($1, $3); }
             ;

/* A 'thing' that will make the DFA move from one state to another */
stimulus : STRING
           { $$ = stimulus_from_string($1); }
         | inline_block
           { $$ = stimulus_from_inline_block($1); }
         | char_class
           { add_charclass_to_list($1); /* freeze it into the list. */
             $$ = stimulus_from_char_class($1); }
         | /* empty */
           { $$ = stimulus_from_epsilon(); }
         ;

inline_block : LANGLE STRING COLON STRING ARROW STRING RANGLE
               { $$ = create_inline_block($2, $4, $6); }
             ;

char_class : simple_char_class
           | negated_char_class
           | char_class_diff
           ;

negated_char_class : NOT simple_char_class
                     { invert_charclass($2); $$ = $2; }
                   ;

char_class_diff : simple_char_class NOT simple_char_class
                  { diff_charclasses($1, $3);
                    free_charclass($3);
                    $$ = $1;
                  }
                ;

simple_char_class : LSQUARE { curcharclass = new_charclass(); }
                    cc_body
                    RSQUARE { $$ = curcharclass;
                              curcharclass = NULL; }
                  | LSQUARE_CARET { curcharclass = new_charclass(); }
                    cc_body
                    RSQUARE { $$ = curcharclass;
                              invert_charclass($$);
                              curcharclass = NULL; }
                  ;

cc_body : CHAR { add_singleton_to_charclass(curcharclass, $1); }
        | CHAR HYPHEN CHAR { add_range_to_charclass(curcharclass, $1, $3); }
        | cc_body CHAR { add_singleton_to_charclass(curcharclass, $2); }
        | cc_body CHAR HYPHEN CHAR { add_range_to_charclass(curcharclass, $2, $4); }
        ;

attr_decl : ATTR simple_attr_seq
          | ATTR STRING COLON expr       {    define_attr(current_evaluator, $2, $4, 0); }
          | EARLY ATTR early_attr_seq
          | EARLY ATTR STRING COLON expr {    define_attr(current_evaluator, $3, $5, 1); }
          | DEFATTR STRING               { define_defattr(current_evaluator, $2); }
          | TYPE STRING                  {    define_type(current_evaluator, $2); }
          ;

simple_attr_seq : STRING
                  { define_attr(current_evaluator, $1, NULL, 0); }
                | simple_attr_seq COMMA STRING
                  { define_attr(current_evaluator, $3, NULL, 0); }
                ;

early_attr_seq : STRING
                  { define_attr(current_evaluator, $1, NULL, 1); }
                | early_attr_seq COMMA STRING
                  { define_attr(current_evaluator, $3, NULL, 1); }
                ;

group_decl : GROUP STRING LBRACE { current_evaluator = start_evaluator($2); }
             attr_decl_seq
             RBRACE { current_evaluator = NULL; }
           ;

attr_decl_seq : /* empty */
                | attr_decl_seq attr_decl
                ;

tag_decl : TAG STRING EQUAL expr { define_tag($2, $4); }
         ;

entrystruct_decl :
              ENTRYSTRUCT STRING STRING   { define_entrystruct($2, $3); }
            ;

expr : NOT expr { $$ = new_not_expr($2); }
     | expr AND expr { $$ = new_and_expr($1, $3); }
     | expr PIPE /* OR */ expr { $$ = new_or_expr($1, $3); }
     | expr XOR expr { $$ = new_xor_expr($1, $3); }
     | expr QUERY expr COLON expr { $$ = new_cond_expr($1, $3, $5); }
     | LPAREN expr RPAREN { $$ = $2; }
     | STRING { $$ = new_tag_expr($1); }
     ;

/* vim:et
*/

