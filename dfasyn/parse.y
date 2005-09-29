/**********************************************************************
  $Header: /cvs/src/dfasyn/parse.y,v 1.3 2003/04/27 23:13:13 richard Exp $

  Grammar definition for input files defining an NFA

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

static Block *curblock = NULL; /* Current block being built */
static State *curstate = NULL; /* Current state being worked on */
static State *addtostate = NULL; /* Current state (incl ext) to which transitions are added */
static struct Abbrev *curabbrev = NULL; /* Current definition being worked on */
static Stringlist *curtranslist = NULL; /* Transition list prior to ARROW */

/* Prefix set by prefix command */
char *prefix = NULL;

State *get_curstate(void) { return curstate; }

%}

%union {
    char *s;
    int i;
    Stringlist *sl;
    InlineBlockList *ibl;
    InlineBlock *ib;
    Expr *e;
}

%token STRING STATE TOKENS PREFIX ARROW BLOCK ENDBLOCK COLON EQUAL SEMICOLON COMMA
%token ABBREV DEFINE 
%type<s> STRING option
%type<sl> option_seq transition_seq
%type<e> expr
%type<ibl> inline_block_seq
%type<ib> inline_block

%token RESULT SYMBOL SYMRESULT DEFRESULT
%token EARLYRESULT EARLYSYMRESULT
%token TYPE
%token ATTR DEFATTR
%token STAR
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

decl : block_decl | tokens_decl | prefix_decl | abbrev_decl | result_decl | attr_decl ;

/* Don't invalidate curstate at the end, this is the means of working out the
   starting state of the NFA */
block_decl : block1 block2 { fixup_state_refs(curblock); curblock = NULL; } ;

block1 : BLOCK STRING { curblock = lookup_block($2, CREATE_MUST_NOT_EXIST); addtostate = curstate = NULL; } ;

block2 : instance_decl_seq state_decl_seq ENDBLOCK ;

prefix_decl : PREFIX STRING { prefix = $2; };

tokens_decl : TOKENS token_seq ;

abbrev_decl : ABBREV STRING { curabbrev = create_abbrev($2); }
              EQUAL string_pipe_seq
            ;

token_seq : token_seq token | token ;

string_pipe_seq : string_pipe_seq PIPE STRING { add_tok_to_abbrev(curabbrev, $3); }
                |                      STRING { add_tok_to_abbrev(curabbrev, $1); }
                ;

token : STRING { (void) lookup_token($1, CREATE_MUST_NOT_EXIST); } ;

instance_decl_seq : /* empty */ | instance_decl_seq instance_decl ;

state_decl_seq : /* empty */ | state_decl_seq state_decl ;

state_decl : STATE STRING { addtostate = curstate = lookup_state(curblock, $2, CREATE_OR_USE_OLD); }
             opt_state_attribute
             sdecl_seq ;

opt_state_attribute : LPAREN STRING RPAREN
                      { set_state_attribute(curstate, $2); }
                    | /* empty */
                    ;

sdecl_seq : /* empty */ | sdecl_seq sdecl ;

sdecl : transition_decl ;

instance_decl : STRING COLON STRING { instantiate_block(curblock, $3 /* master_block_name */, $1 /* instance_name */ ); } ;

transition_decl : transition_seq ARROW { curtranslist = $1; } destination_seq { addtostate = curstate; }
                | transition_seq EQUAL STRING { addtostate = add_transitions_to_internal(curblock, addtostate, $1);
                                                add_exit_value(addtostate, $3);
                                                addtostate = curstate; }
                ;

destination_seq : STRING                       { add_transitions(addtostate, curtranslist, $1); }
                | destination_seq COMMA STRING { add_transitions(addtostate, curtranslist, $3); }
                ;

transition_seq : option_seq { $$ = $1; }
               | inline_block_seq 
                 { 
                   addtostate = add_inline_block_transitions(curblock, addtostate, $1);
                   $$ = add_token(NULL, NULL); /* epsilon transition afterwards */
                 }
               | transition_seq SEMICOLON option_seq 
                 {
                   addtostate = add_transitions_to_internal(curblock, addtostate, $1);
                   $$ = $3;
                 }
               | transition_seq SEMICOLON inline_block_seq
                 {
                   addtostate = add_transitions_to_internal(curblock, addtostate, $1);
                   addtostate = add_inline_block_transitions(curblock, addtostate, $3);
                   $$ = add_token(NULL, NULL); /* epsilon transition afterwards */
                 }
               ;

option_seq : option { $$ = add_token(NULL, $1); }
           | option_seq PIPE option { $$ = add_token($1, $3); } ;

option : STRING 
       | /* empty */ { $$ = NULL; }
       ;

inline_block_seq : inline_block
                   { $$ = add_inline_block(NULL, $1); }
                 | inline_block_seq PIPE inline_block
                   { $$ = add_inline_block($1, $3); }
                 | inline_block_seq PIPE /* + epsilon transitinon */
                   { $$ = add_inline_block($1, NULL); }
                 ;

inline_block : LANGLE STRING COLON STRING ARROW STRING RANGLE
               { $$ = create_inline_block($2, $4, $6); }
             ;

result_decl : RESULT STRING               { define_result(exit_evaluator, $2, NULL, 0); }
            | RESULT    expr ARROW STRING { define_result(exit_evaluator, $4, $2, 0); }
            | EARLYRESULT STRING            { define_result(exit_evaluator, $2, NULL, 1); }
            | EARLYRESULT expr ARROW STRING { define_result(exit_evaluator, $4, $2, 1); }
            | SYMRESULT expr ARROW STRING { define_symresult(exit_evaluator, $4, $2, 0); }
            | EARLYSYMRESULT expr ARROW STRING { define_symresult(exit_evaluator, $4, $2, 1); }
            | SYMBOL STRING EQUAL expr    { define_symbol(exit_evaluator, $2, $4); }
            | DEFRESULT STRING            { define_defresult(exit_evaluator, $2); }
            | TYPE STRING                 { define_type(exit_evaluator, $2); }
            ;

/* No 'early exit' form for attributes.  They are supposed to be actions that
   are done en-route to the final exit condition. */
attr_decl : ATTR RESULT STRING               { define_result(attr_evaluator, $3, NULL, 0); }
          | ATTR RESULT    expr ARROW STRING { define_result(attr_evaluator, $5, $3, 0); }
          | ATTR SYMRESULT expr ARROW STRING { define_symresult(attr_evaluator, $5, $3, 0); }
          | ATTR SYMBOL STRING EQUAL expr    { define_symbol(attr_evaluator, $3, $5); }
          | ATTR DEFRESULT STRING            { define_defresult(attr_evaluator, $3); }
          | DEFATTR STRING                   { define_defresult(attr_evaluator, $2); }
          | ATTR TYPE STRING                 { define_type(attr_evaluator, $3); }
          ;

expr : NOT expr { $$ = new_not_expr($2); }
     | expr AND expr { $$ = new_and_expr($1, $3); }
     | expr PIPE /* OR */ expr { $$ = new_or_expr($1, $3); }
     | expr XOR expr { $$ = new_xor_expr($1, $3); }
     | expr QUERY expr COLON expr { $$ = new_cond_expr($1, $3, $5); }
     | LPAREN expr RPAREN { $$ = $2; }
     | STRING { $$ = new_sym_expr($1); }
     | STAR { $$ = new_wild_expr(); }
     ;

