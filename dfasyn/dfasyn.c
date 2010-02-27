/***************************************
  Main program for NFA to DFA table builder program.
  ***************************************/

/*
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2000-2003,2005,2006
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

#include "dfasyn.h"

FILE *report = NULL;
FILE *output = NULL;
FILE *header_output = NULL;

/* If non-null this gets prepended onto the names of the all the entities that
 * are generated in the output file. */
char *prefix = NULL;

extern int yyparse(void);

/* ================================================================= */
static char *entrystruct = NULL;
static char *entryvar = NULL;

void define_entrystruct(const char *s, const char *v)/*{{{*/
{
  if (!entrystruct) {
    entrystruct = new_string(s);
    entryvar = new_string(v);
  } else {
    fprintf(stderr, "Can't redefine entrystruct with <%s>\n", s);
    exit(1);
  }
}
/*}}}*/
/* ================================================================= */
static void print_token_table(void)/*{{{*/
{
  FILE *dest;
  int i;
  extern char *prefix;

  dest = header_output ? header_output : output;
  /* Not sure how it makes sense to write this to the C file : maybe if you're going
   * to include the C file into a bigger one it's reasonable?  Anyway, the intention
   * is that you're more likely to use this for real if you're writing a header file. */

  for (i=0; i<ntokens; i++) {
    fprintf(dest, "#define %s_%s %d\n",
        prefix ? prefix : "TOK_",
        toktable[i], i);
  }
}
/*}}}*/
static void print_attr_tables(struct DFA *dfa, const char *prefix_under)/*{{{*/
{
  int i, tab;

  for (tab=0; tab<n_evaluators; tab++) {
    char *defattr = get_defattr(tab);
    char *attrname = get_attr_name(tab);
    if (!attrname) attrname = "attr";
    fprintf(output, "%s %s%s[] = {\n", get_attr_type(tab), prefix_under, attrname);
    for (i=0; i<dfa->n; i++) {
      char *attr = dfa->s[i]->attrs[tab];
      fprintf(output, "  %s", attr ? attr : defattr);
      fputc ((i<(dfa->n - 1)) ? ',' : ' ', output);
      fprintf(output, " /* State %d */\n", i);
    }
    fprintf(output, "};\n\n");
    if (header_output) {
      fprintf(header_output, "extern %s %s%s[];\n", get_attr_type(tab), prefix_under, attrname);
    }
  }
}
/*}}}*/
static void check_default_attrs(void)/*{{{*/
{
  int tab;
  int fail = 0;

  for (tab=0; tab<n_evaluators; tab++) {
    char *defattr = get_defattr(tab);
    char *attrname = get_attr_name(tab);
    attrname = attrname ? attrname : "(DEFAULT)";
    if (!defattr) {
      fprintf(stderr, "ERROR: No defattr definition for %s\n", attrname);
      fail = 1;
    }
  }
  if (fail) {
    exit(1);
  }
}
/*}}}*/
static void write_next_state_function_uncompressed(int Nt, int do_inline, const char *prefix_under)/*{{{*/
{
  FILE *dest;

  dest = do_inline ? header_output : output;

  fprintf(dest, "%sint %snext_state(int current_state, int next_token) {\n",
      do_inline ? "static inline " : "",
      prefix_under);
  fprintf(dest, "  if (next_token < 0 || next_token >= %d) return -1;\n", Nt);
  fprintf(dest, "  return %strans[%d*current_state + next_token];\n",
      prefix_under, Nt);
  fprintf(dest, "}\n");
  if (!do_inline && header_output) {
    fprintf(header_output, "extern int %snext_state(int current_state, int next_token);\n",
        prefix_under);
  }
}
/*}}}*/
static void print_uncompressed_tables(struct DFA *dfa, int do_inline, const char *prefix_under)/*{{{*/
/* Print out the state/transition table uncompressed, i.e. every
   token has an array entry in every state.  This is fast to access
   but quite wasteful on memory with many states and many tokens. */
{
  int Nt = ntokens + n_charclasses;
  int n, i, j;

  n = 0;
  fprintf(output, "%sshort %strans[] = {",
      do_inline ? "" : "static ",
      prefix_under);

  if (do_inline) {
    fprintf(header_output, "extern short %strans[];\n",
        prefix_under);
  }

  for (i=0; i<dfa->n; i++) {
    for (j=0; j<Nt; j++) {
      if (n>0) fputc (',', output);
      if (n%8 == 0) {
        fprintf(output, "\n  ");
      } else {
        fputc(' ', output);
      }
      n++;
      fprintf(output, "%4d", dfa->s[i]->map[j]);
    }
  }

  fprintf(output, "\n};\n\n");

  write_next_state_function_uncompressed(Nt, do_inline, prefix_under);

}
/*}}}*/
static int check_include_char(struct DFA *dfa, int this_state, int token)/*{{{*/
{
  if (dfa->s[this_state]->defstate >= 0) {
    return (dfa->s[this_state]->map[token] !=
            dfa->s[dfa->s[this_state]->defstate]->map[token]);
  } else {
    return (dfa->s[this_state]->map[token] >= 0);
  }
}
/*}}}*/
static void write_next_state_function_compressed(int do_inline, const char *prefix_under)/*{{{*/
/* Write the next_state function for traversing compressed tables into the
   output file. */
{
  FILE *dest;
  dest = do_inline ? header_output : output;

  fprintf(dest, "%sint %snext_state(int current_state, int next_token) {\n",
      do_inline ? "static inline " : "",
      prefix_under);
  fprintf(dest, "  int h, l, m, xm;\n");
  fprintf(dest, "  while (current_state >= 0) {\n");
  fprintf(dest, "    l = %sbase[current_state], h = %sbase[current_state+1];\n", prefix_under, prefix_under);
  fprintf(dest, "    while (h > l) {\n");
  fprintf(dest, "      m = (h + l) >> 1; xm = %stoken[m];\n", prefix_under);
  fprintf(dest, "      if (xm == next_token) goto done;\n");
  fprintf(dest, "      if (m == l) break;\n");
  fprintf(dest, "      if (xm > next_token) h = m;\n");
  fprintf(dest, "      else                 l = m;\n");
  fprintf(dest, "    }\n");
  fprintf(dest, "    current_state = %sdefstate[current_state];\n", prefix_under);
  fprintf(dest, "  }\n");
  fprintf(dest, "  return -1;\n");
  fprintf(dest, "  done:\n");
  fprintf(dest, "  return %snextstate[m];\n", prefix_under);
  fprintf(dest, "}\n");
  if (!do_inline && header_output) {
    fprintf(header_output, "extern int %snext_state(int current_state, int next_token);\n",
        prefix_under);
  }

}
/*}}}*/
static void print_compressed_tables(struct DFA *dfa, int do_inline, const char *prefix_under)/*{{{*/
/* Print state/transition table in compressed form.  This is more
   economical on storage, but requires a bisection search to find
   the next state for a given current state & token */
{
  int *basetab = new_array(int, dfa->n + 1);
  int Nt = ntokens + n_charclasses;
  int n, i, j;

  n = 0;
  fprintf(output, "%sunsigned char %stoken[] = {",
      do_inline ? "" : "static ",
      prefix_under);
  for (i=0; i<dfa->n; i++) {
    for (j=0; j<Nt; j++) {
      if (check_include_char(dfa, i, j)) {
        if (n>0) fputc (',', output);
        if (n%8 == 0) {
          fprintf(output, "\n  ");
        } else {
          fputc(' ', output);
        }
        n++;
        fprintf(output, "%3d", j);
      }
    }
  }
  fprintf(output, "\n};\n\n");

  n = 0;
  fprintf(output, "%sshort %snextstate[] = {",
      do_inline ? "" : "static ",
      prefix_under);
  for (i=0; i<dfa->n; i++) {
    basetab[i] = n;
    for (j=0; j<Nt; j++) {
      if (check_include_char(dfa, i, j)) {
        if (n>0) fputc (',', output);
        if (n%8 == 0) {
          fprintf(output, "\n  ");
        } else {
          fputc(' ', output);
        }
        n++;
        fprintf(output, "%5d", dfa->s[i]->map[j]);
      }
    }
  }
  fprintf(output, "\n};\n\n");
  basetab[dfa->n] = n;

  n = 0;
  fprintf(output, "%sunsigned short %sbase[] = {",
      do_inline ? "" : "static ",
      prefix_under);
  for (i=0; i<=dfa->n; i++) {
    if (n>0) fputc (',', output);
    if (n%8 == 0) {
      fprintf(output, "\n  ");
    } else {
      fputc(' ', output);
    }
    n++;
    fprintf(output, "%5d", basetab[i]);
  }
  fprintf(output, "\n};\n\n");

  n = 0;
  fprintf(output, "%sshort %sdefstate[] = {",
      do_inline ? "" : "static ",
      prefix_under);
  for (i=0; i<dfa->n; i++) {
    if (n>0) fputc (',', output);
    if (n%8 == 0) {
      fprintf(output, "\n  ");
    } else {
      fputc(' ', output);
    }
    n++;
    fprintf(output, "%5d", dfa->s[i]->defstate);
  }
  fprintf(output, "\n};\n\n");

  if (do_inline) {
    fprintf(header_output, "extern unsigned char %stoken[];\n", prefix_under);
    fprintf(header_output, "extern short %snextstate[];\n", prefix_under);
    fprintf(header_output, "extern unsigned short %sbase[];\n", prefix_under);
    fprintf(header_output, "extern short %sdefstate[];\n", prefix_under);
  }
  free(basetab);

  write_next_state_function_compressed(do_inline, prefix_under);
}
/*}}}*/
static void print_entries_table(const char *prefix_under)/*{{{*/
{
  int i;
  if (entrystruct) {
    int first;
    /* If we write the struct defn to the header file, we ought not to emit the
     * full struct defn again in the main output.  This is tricky unless we can
     * guarantee the header will get included, though. */
    fprintf(output, "struct %s {\n", entrystruct);
    if (header_output) {
      fprintf(header_output, "extern struct %s {\n", entrystruct);
    }
    for (i=0; i<n_dfa_entries; i++) {
      fprintf(output, "  int %s;\n", dfa_entries[i].entry_name);
      if (header_output) {
        fprintf(header_output, "  int %s;\n", dfa_entries[i].entry_name);
      }
    }
    fprintf(output, "} %s = {\n", entryvar);
    if (header_output) {
      fprintf(header_output, "} %s;\n", entryvar);
    }
    for (i=0, first=1; i<n_dfa_entries; i++, first=0) {
      if (!first) {
        fputs(",\n", output);
      }
      fprintf(output, "  %d", dfa_entries[i].state_number);
    }
    fputs("\n};\n", output);
  } else {
    for (i=0; i<n_dfa_entries; i++) {
      fprintf(output, "int %s%s = %d;\n",
          prefix_under,
          dfa_entries[i].entry_name, dfa_entries[i].state_number);
      if (header_output) {
        fprintf(header_output, "extern int %s%s;\n",
            prefix_under,
            dfa_entries[i].entry_name);
      }
    }
  }
}
/*}}}*/
/* ================================================================= */
static void deal_with_multiple_entries(Block **blk, struct DFA **dfa)/*{{{*/
{
  /* Get the list of blocks that are to be combined to form a union of all their states. */
  struct Entrylist *e;
  int Ne;
  Block **blocks;
  Block *jumbo;
  int bi, Nb, Ns, si, ei;

  for (Ne=0, e=entries; e; e=e->next) Ne++;
  if (report) {
    fprintf(report, "Processing %d separate entry points\n", Ne);
  }
  blocks = new_array(Block*, Ne);
  for (Nb=0, e=entries; e; e=e->next) {
    int matched = 0;
    for (bi=0; bi<Nb; bi++) {
      if (e->state->parent == blocks[bi]) {
        matched = 1;
        break;
      }
    }
    if (!matched) {
      blocks[Nb++] = e->state->parent;
    }
  }
  for (Ns=0, bi=0; bi<Nb; bi++) {
    Ns += blocks[bi]->nstates;
  }

  if (report) {
    fprintf(report, "Entries in %d blocks, total of %d states\n",
        Nb, Ns);
  }

  jumbo = new(Block);
  jumbo->name = "(UNION OF MULTIPLE BLOCKS)";
  jumbo->nstates = jumbo->maxstates = Ns;
  jumbo->states = new_array(State *, Ns);
  jumbo->eclo = NULL;

  for (bi=0, si=0; bi<Nb; bi++) {
    int ns = blocks[bi]->nstates;
    int i;
    int block_name_len;
    memcpy(jumbo->states + si, blocks[bi]->states, sizeof(State *) * ns);
    block_name_len = strlen(blocks[bi]->name);
    for (i=0; i<ns; i++) {
      int len;
      char *new_name;
      State *s = jumbo->states[si + i];
      len = block_name_len + strlen(s->name) + 2;
      new_name = new_array(char, len);
      strcpy(new_name, blocks[bi]->name);
      strcat(new_name, ".");
      strcat(new_name, s->name);
      free(s->name);
      s->name = new_name;
    }
    si += ns;
  }

  /* Reindex all the states */
  for (si=0; si<Ns; si++) {
    jumbo->states[si]->index = si;
  }

  split_charclasses(jumbo);
  expand_charclass_transitions(jumbo);

  if (verbose) fprintf(stderr, "Computing epsilon closure...\n");
  generate_epsilon_closure(jumbo);
  print_nfa(jumbo);
  build_transmap(jumbo);

  if (verbose) fprintf(stderr, "Building DFA...\n");
  n_dfa_entries = Ne;
  dfa_entries = new_array(struct DFAEntry, Ne);
  for (e=entries, ei=0; e; e=e->next, ei++) {
    dfa_entries[ei].entry_name = new_string(e->entry_name);
    dfa_entries[ei].state_number = e->state->index;
  }
  *dfa = build_dfa(jumbo);
  *blk = jumbo;

}
/*}}}*/
/* ================================================================= */
static void usage(void)/*{{{*/
{
  fprintf(stderr,
    "dfasyn, Copyright (C) 2001-2003,2005,2006 Richard P. Curnow\n"
    "\n"
    "dfasyn comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; see the GNU General Public License for details.\n"
    "\n"
    "Usage: dfasyn [OPTION]... FILE\n"
    "Read state-machine description from FILE and generate a deterministic automaton.\n"
    "Write results to stdout unless options dictate otherwise.\n"
    "\n"
    "Output files:\n"
    "  -o,  --output FILE          Define the name of the output file (e.g. foobar.c)\n"
    "  -ho, --header-output FILE   Define the name of the header output file (e.g. foobar.h)\n"
    "  -r,  --report FILE          Define the name where the full generator report goes (e.g. foobar.report)\n"
    "\n"
    "Generated automaton:\n"
    "  -p,  --prefix PREFIX        Specify a prefix for the variables and functions in the generated file(s)\n"
    "  -u,  --uncompressed-tables  Don't compress the generated transition tables\n"
    "  -ud, --uncompressed-dfa     Don't common-up identical states in the DFA\n"
    "  -I,  --inline-function      Make the next_state function inline (requires -ho)\n"
    "\n"
    "General:\n"
    "  -v,  --verbose              Be verbose\n"
    "  -h,  --help                 Display this help message\n"
    );

}
/*}}}*/
/* ================================================================= */
int main (int argc, char **argv)/*{{{*/
{
  int result;

  Block *main_block;
  char *input_name = NULL;
  char *output_name = NULL;
  char *header_output_name = NULL;
  char *report_name = NULL;
  int uncompressed_tables = 0;
  int uncompressed_dfa = 0; /* Useful for debug */
  int do_inline = 0;
  extern char *prefix;
  char *prefix_under;
  FILE *input = NULL;
  struct DFA *dfa;

  verbose = 0;
  report = NULL;

  /*{{{ Parse cmd line arguments */
  while (++argv, --argc) {
    if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
      usage();
      exit(0);
    } else if (!strcmp(*argv, "-v") || !strcmp(*argv, "--verbose")) {
      verbose = 1;
    } else if (!strcmp(*argv, "-o") || !strcmp(*argv, "--output")) {
      ++argv, --argc;
      output_name = *argv;
    } else if (!strcmp(*argv, "-ho") || !strcmp(*argv, "--header-output")) {
      ++argv, --argc;
      header_output_name = *argv;
    } else if (!strcmp(*argv, "-r") || !strcmp(*argv, "--report")) {
      ++argv, --argc;
      report_name = *argv;
    } else if (!strcmp(*argv, "-u") || !strcmp(*argv, "--uncompressed-tables")) {
      uncompressed_tables = 1;
    } else if (!strcmp(*argv, "-ud") || !strcmp(*argv, "--uncompressed-dfa")) {
      uncompressed_dfa = 1;
    } else if (!strcmp(*argv, "-I") || !strcmp(*argv, "--inline-function")) {
      do_inline = 1;
    } else if (!strcmp(*argv, "-p") || !strcmp(*argv, "--prefix")) {
      ++argv, --argc;
      prefix = *argv;
    } else if ((*argv)[0] == '-') {
      fprintf(stderr, "Unrecognized command line option %s\n", *argv);
    } else {
      input_name = *argv;
    }
  }
  /*}}}*/

  if (do_inline && !header_output_name) {/*{{{*/
    fprintf(stderr,
        "--------------------------------------------------------------\n"
        "It doesn't make sense to try inlining if you're not generating\n"
        "a separate header file.\n"
        "Not inlining the transition function.\n"
        "--------------------------------------------------------------\n"
        );
    do_inline = 0;
  }
/*}}}*/
  if (input_name) {/*{{{*/
    input = fopen(input_name, "r");
    if (!input) {
      fprintf(stderr, "Can't open %s for input, exiting\n", input_name);
      exit(1);
    }
  } else {
    input = stdin;
  }
  /*}}}*/
  if (output_name) {/*{{{*/
    output = fopen(output_name, "w");
    if (!output) {
      fprintf(stderr, "Can't open %s for writing, exiting\n", output_name);
      exit(1);
    }
  } else {
    output = stdout;
  }
/*}}}*/
  if (header_output_name) {/*{{{*/
    header_output = fopen(header_output_name, "w");
    if (!header_output) {
      fprintf(stderr, "Can't open %s for writing, exiting\n", header_output_name);
      exit(1);
    }
  }
  /* otherwise the header stuff just goes to the same fd as the main output. */

/*}}}*/
  if (report_name) {/*{{{*/
    report = fopen(report_name, "w");
    if (!report) {
      fprintf(stderr, "Can't open %s for writing, no report will be created\n", report_name);
    }
  }
/*}}}*/

  if (verbose) {
    fprintf(stderr, "General-purpose automaton builder\n");
    fprintf(stderr, "Copyright (C) Richard P. Curnow  2000-2003,2005,2006\n");
  }

  eval_initialise();

  if (verbose) fprintf(stderr, "Parsing input...");
  yyin = input;

  /* Set yyout.  This means that if anything leaks from the scanner, or appears
     in a %{ .. %} block, it goes to the right place. */
  yyout = output;

  result = yyparse();
  if (result > 0) exit(1);
  if (verbose) fprintf(stderr, "\n");

  make_evaluator_array();
  check_default_attrs();

  if (!entries) {
    /* Support legacy method : the last state to be current in the input file
     * is the entry state of the NFA */
    State *start_state;
    start_state = get_curstate();
    main_block = start_state->parent;
    split_charclasses(main_block);
    expand_charclass_transitions(main_block);
    if (verbose) fprintf(stderr, "Computing epsilon closure...\n");
    generate_epsilon_closure(main_block);
    print_nfa(main_block);
    build_transmap(main_block);

    if (verbose) fprintf(stderr, "Building DFA...\n");
    {
      struct DFAEntry entry[1];
      n_dfa_entries = 1;
      dfa_entries = entry;
      entry[0].entry_name = "(ONLY ENTRY)";
      entry[0].state_number = start_state->index;
      dfa = build_dfa(main_block);
    }
  } else {
    /* Allow generation of multiple entry states, so you can use the same input file when
     * you need several automata that have a lot of logic in common. */
    deal_with_multiple_entries(&main_block, &dfa);
  }
  if (report) {
    fprintf(report, "--------------------------------\n"
                    "DFA structure before compression\n"
                    "--------------------------------\n");
  }
  print_dfa(dfa);

  if (had_ambiguous_result) {
    fprintf(stderr, "No output written, there were ambiguous attribute values for accepting states\n");
    exit(2);
  }

  if (!uncompressed_dfa) {
    if (verbose) fprintf(stderr, "\nCompressing DFA...\n");
    compress_dfa(dfa, ntokens + n_charclasses, n_dfa_entries, dfa_entries);
  }

  if (verbose) fprintf(stderr, "\nCompressing transition tables...\n");
  compress_transition_table(dfa, ntokens + n_charclasses);

  if (report) {
    fprintf(report, "-------------------------------\n"
                    "DFA structure after compression\n"
                    "-------------------------------\n");
  }
  if (verbose) fprintf(stderr, "Writing outputs...\n");
  print_dfa(dfa);

  if (prefix) {
    prefix_under = new_array(char, 2 + strlen(prefix));
    strcpy(prefix_under, prefix);
    strcat(prefix_under, "_");
  } else {
    prefix_under = "";
  }

  if (header_output) {
    fprintf(header_output, "#ifndef %sHEADER_H\n", prefix_under);
    fprintf(header_output, "#define %sHEADER_H\n", prefix_under);
  }

  print_token_table();
  print_charclass_mapping(output, header_output, prefix_under);
  print_attr_tables(dfa, prefix_under);

  if (uncompressed_tables) {
    print_uncompressed_tables(dfa, do_inline, prefix_under);
  } else {
    print_compressed_tables(dfa, do_inline, prefix_under);
  }

  if (entries) {
    /* Emit entry table */
    print_entries_table(prefix_under);
  } else {
    /* Legacy behaviour - DFA state 0 is implicitly the single entry state. */
  }

  if (report) {
    fclose(report);
    report = NULL;
  }

  report_unused_tags();

  if (header_output) {
    fprintf(header_output, "#endif\n");
  }

  return result;
}
/*}}}*/
