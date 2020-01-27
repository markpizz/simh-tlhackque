/* makefile2cmake.c: makefile conversion into cmake configuration

   Copyright (c) 2020, Mark Pizzolato

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.
/* 

/*
    This program exists to transform the simulator build configuration 
    details specified in the simh makefile into equivalent details 
    needed to build with cmake.

    Two required inputs are specified by compile time defines:

        CMAKE_SOURCE_DIR    Specifies the path where to find the simh makefile
        CMAKE_BINARY_DIR    Specifies where the simh_makefile.cmake result goes

    Debug output can optionally be produced if an environment variable
    named MAKEFILE2CMAKE_DEBUG is defined.

    Since the actual contents of the simh makefile is completely controlled 
    by this project, as is this program, the details in the makefile input
    can be adjusted to accomodate this program or provide clues about 
    desired behavior.  Examples of this are:

       1) The requirement that the makefile use only ${} delimited variable 
          insertions rather than a potential mix of ${} and $().  Changing 
          the makefile to follow this, doesn't change its behavior.  
       2) Some makefile build targets may not be appropriate for building 
          via cmake.  If the first build step contains the string:
          #cmake:ignore-target that target will be ignored.

 */

#define _CRT_SECURE_NO_WARNINGS 1
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>


#if !defined (DEBUG_MODE)
#define DEBUG_MODE 0
#endif

int debug = DEBUG_MODE;

int Dprintf (const char* fmt, ...)
{
    va_list arglist;

    if (debug) {
        va_start (arglist, fmt);
        vfprintf (stderr, fmt, arglist);
        va_end (arglist);
    }
    return 0;
}

char *
paren_inserts_to_braces (char *string)
{
    char *c = string;

    while ((c = strstr (c, "$("))) {
        char *e = strchr (c, ')');

        c[1] = '{';
        if (e == NULL)
            break;
        *e = '}';
    }
    return string;
}

int line_number;

char *
get_makefile_line (FILE *f)
{
    char read_buf[1024];
    size_t buf_size = 1024;
    size_t buf_used = 0;
    char *buf = (char *)malloc (buf_size + 1);

    while (1) {
        if (NULL == fgets (read_buf, sizeof (read_buf), f)) {
            if (buf_used != 0)
                break;
            free (buf);
            return NULL;
        }
        ++line_number;
        while ((strlen (read_buf) > 0) && 
               ((read_buf[strlen (read_buf) - 1] == '\r') ||
                (read_buf[strlen (read_buf) - 1] == '\n')))
                read_buf[strlen (read_buf) - 1] = '\0';
        if (buf_size - buf_used < strlen (read_buf) + 1) {
            buf_size += strlen (read_buf) - (buf_size - buf_used);
            buf = (char *)realloc (buf, buf_size + 1);
            }
        memcpy (buf + buf_used, read_buf, 1 + strlen (read_buf));
        buf_used += strlen (read_buf);
        if (buf[buf_used - 1] != '\\')
            break;
        --buf_used;
    }
    while (isspace (*buf))
        memmove (buf, buf + 1, strlen (buf));
    return paren_inserts_to_braces (buf);
}

const char *get_buf_token (const char *buf, char *token, size_t tok_buf_size)
{
    const char *c = buf;

    if (tok_buf_size)
        *token = '\0';
    while (isspace (*c))
        ++c;
    while ((tok_buf_size > 1) && 
           (*c != '\0')       && 
           !isspace (*c)) {
        *token++ = *c++;
        --tok_buf_size;
    }
    *token = '\0';
    return c;
}

char *
x_strdup (const char *str)
{
    char *nstr = (char *)malloc (strlen (str) + 1);
    memcpy (nstr, str , strlen (str) + 1);
    return nstr;
}

char *
get_idname (const char *tok)
{
    char *tstr = x_strdup (tok);

    if ((tok[0] == '$') &&
        ((tok[1] == '{') || 
         (tok[1] == '('))) {
        memmove (tstr, tstr + 2, strlen (tstr + 2) + 1);
        tstr[strlen (tstr) - 1] = '\0';
    }
    return tstr;
}


struct symbol {
    char *name;
    char *value;
    } *symbols = NULL;

int symbols_used = 0;
int symbols_size = 0;

int
add_symbol_assignment (char *symbol_buf)
{
    char name[512];
    const char *cptr = get_buf_token (symbol_buf, name, sizeof (name));
    int i;

    for (i=0; i<symbols_used; i++) {
        if (0 == strcmp (name, symbols[i].name))
            break;
    }
    if (i == symbols_used) {
        char *c;

        if (symbols_used == symbols_size) {
            symbols_size += 100;
            symbols = (struct symbol *)realloc (symbols, symbols_size * sizeof (*symbols));
        }
        symbols[symbols_used].name = symbol_buf;
        c = &symbol_buf[strlen(name)];
        *c++ = '\0';
        while (isspace(*c))
            *c++ = '\0';
        *c++ =  '\0';       /* clear '=' */
        while (isspace(*c))
            *c++ = '\0';
        symbols[symbols_used].value = c;
        while (*c != '\0') {
            while ((*c != '\0') && (!isspace(*c)))
                ++c;
            while (isspace(*c)) {
                char *c1 = c + 1;
                *c = ' ';
                while (isspace(*c1))
                    ++c1;
                if (c1 != c + 1)
                    memmove (c + 1, c1, 1 + strlen (c1));
                c = c1;
            }
        }
        Dprintf ("Recording symbol %s as %s\n", name, symbols[symbols_used].value);
        ++symbols_used;
        return 0;
    }
    Dprintf ("Discarding additional definition for: %s - %s\n", name, symbol_buf);
    free (symbol_buf);
    return 1;
}

void
clear_symbols ()
{
    int i;

    for (i=0; i<symbols_used; i++)
        free (symbols[i].name);
    free (symbols);
    symbols = NULL;
    symbols_used = 0;
    symbols_size = 0;
}

const char *
lookup_symbol (const char *name)
{
    int i;
    char *isymbol = get_idname (name);

    for (i=0; i<symbols_used; i++)
        if (0 == strcmp (isymbol, symbols[i].name)) {
            Dprintf ("Symbol Lookup of %s returned: %s\n", isymbol, symbols[i].value);
            free (isymbol);
            return symbols[i].value;
        }
    Dprintf ("Symbol Lookup of %s not found.\n", isymbol);
    free (isymbol);
    return "";
}

typedef struct MFILE {
    char *buf;
    size_t pos;
    size_t size;
    } MFILE;

int Mprintf (MFILE *f, const char* fmt, ...)
{
    va_list arglist;
    int len;

    while (f) {
        size_t buf_space = (f->size - f->pos);

        va_start (arglist, fmt);
        len = vsnprintf (f->buf + f->pos, buf_space, fmt, arglist);
        va_end (arglist);

        if ((len < 0) || (len >= (int)buf_space)) {
            f->size *= 2;
            buf_space = (f->size - f->pos);
            if ((int)buf_space < len + 2)
                f->size += len + 2;
            f->buf = (char *)realloc (f->buf, f->size + 1);
            if (f->buf == NULL)            /* out of memory */
                return -1;
            f->buf[f->size-1] = '\0';
            continue;
            }
        f->pos += len;
        break;
    }
return 0;
}

void
MWriteSetSymbol (MFILE *f, const char *symbol)
{
    char *isymbol = get_idname (symbol);
    char tok[512];
    size_t offset, startoffset;
    const char *iptr = lookup_symbol (isymbol);

    Mprintf (f, "set(%s", isymbol);
    startoffset = offset = 4 + strlen (isymbol);
    while (1) {
        char *c;

        iptr = get_buf_token (iptr, tok, sizeof (tok));
        if (tok[0] == '\0')
            break;
        c = strstr (tok, "$(");
        if (c) {
            *(c + 1) = '{';
            c = strchr (tok, ')');
            if (c)
                *c = '}';
        }
        if (offset + strlen (tok) > 75) {
            Mprintf (f, "\n%*.*s", (int)startoffset, (int)startoffset, "");
            offset = startoffset;
        }
        Mprintf (f, " %s", tok);
        offset += 1 + strlen (tok);
    }
    Mprintf (f, ")\n");
}

MFILE *
MOpen ()
{
    return (MFILE *)calloc (1, sizeof (MFILE));
}

void
MFlush (MFILE *f)
{
    f->pos = 0;
}

int
FMwrite (FILE *fout, MFILE *fdata)
{
    int ret = fwrite (fdata->buf, 1, fdata->pos, fout);

    MFlush (fdata);
    return ret;
}

void
MClose (MFILE *f)
{
    free (f->buf);
    free (f);
}

void
add_string_to_list (char ***list, int *list_size, const char *string)
{
    (*list) = (char **)realloc ((*list), ((*list_size) + 1) * sizeof (**list));
    (*list)[(*list_size)++] = x_strdup (string);
}

void
add_unique_string_to_list (char ***list, int *list_size, const char *string)
{
    int i;

    for (i=0; i<*list_size; i++)
        if (0 == strcmp ((*list)[i], string))
            return;
    add_string_to_list (list, list_size, string);
}

int
remove_string_from_list (char ***list, int *list_size, const char *string)
{
    int i;
    int found = 0;

    for (i=0; i<*list_size; i++) {
        if (0 == strcmp ((*list)[i], string)) {
            found = 1;
            free ((*list)[i]);
            --(*list_size);
            memcpy (&(*list)[i], &(*list)[i + 1], sizeof (**list) * (*list_size - i));
        }
    }
    return found;
}

void
emit_list (MFILE *f, const char *name, char ***list, int *list_size)
{
    int i;

    if (*list_size) {
        if (name)
            Mprintf (f, "\n    %s", name);
        for (i=0; i<*list_size; i++) {
            Mprintf (f, "\n    %s%s", name ? "    " : "", (*list)[i]);
            free ((*list)[i]);
        }
    }
    free (*list);
    *list = NULL;
    *list_size = 0;
}

int 
variable_name_match (const char *string, const char *name)
{
    size_t string_len = strlen (string);

    if (*string != '$')
        return 0;
    if (((string_len - 3 == strlen (name)) &&
         (0 == memcmp (name, string + 2, string_len - 3))) &&
        (((string[1] == '(')                && 
          (string[string_len - 1] == ')')) ||
         ((string[1] == '{')                && 
          (string[string_len - 1] == '}'))))
        return 1;
    else
        return 0;
}

char *
expand_symbols (const char *string, char ***list, int *list_size)
{
    const char *start_string = string;
    const char *sym_start = strchr (string, '$');
    const char *sym_end;
    char *symname = NULL;
    const char *symvalue;
    char *exp_string = NULL;
    size_t exp_data = 0;
    char *return_string = NULL;

    if (sym_start == NULL)
        return x_strdup (string);
    while (sym_start) {
        exp_string = (char *)realloc (exp_string, 1 + exp_data + sym_start - string);
        exp_string[exp_data + sym_start - string] = '\0';
        memcpy (exp_string + exp_data, string, sym_start - string);
        exp_data += sym_start - string;
        symname = x_strdup (sym_start);
        sym_end = strchr (string, (*(sym_start + 1) == '{') ? '}' : ')');
        symname[1 + sym_end - sym_start] = '\0';
        symvalue = lookup_symbol (symname);
        if (list)
            add_unique_string_to_list (list, list_size, symname);
        Dprintf ("Lookup of '%s' returned: %s\n", symname, symvalue);
        free (symname);
        exp_string = (char *)realloc (exp_string, 1 + exp_data + strlen (symvalue));
        memcpy (exp_string + exp_data, symvalue, 1 + strlen (symvalue));
        exp_data += strlen (symvalue);
        string = sym_end + 1;
        sym_start = strchr (string, '$');
    }
    exp_string = (char *)realloc (exp_string, 1 + exp_data + strlen (string));
    memcpy (exp_string + exp_data, string, 1 + strlen (string));
    return_string = expand_symbols (exp_string, list, list_size);
    free (exp_string);
    Dprintf ("Expansion of: '%s' returned: '%s'\n", start_string, return_string);
    return return_string;
}

int
CheckTest (const char *directory, const char *file_prefix)
{
    char filename[512];
    char *dirstr = expand_symbols (directory, NULL, 0);
    FILE *f;

    sprintf (filename, "%s/tests/%s_test.ini", dirstr, file_prefix);
    free (dirstr);
    Dprintf ("Checking for simulator test: directory='%s', file_prefix='%s', filename='%s'\n", 
             directory, file_prefix, filename);
    f = fopen (filename, "r");
    if (f) {
        fclose (f);
        return 1;
    }
    return 0;
}

int 
main (int argc, char **argv)
{
    FILE *fin, *fout;
    char *makefile = "makefile";
    char *cmakefile = "simh_makefile.cmake";
    char *line;
    char tok1[512], tok2[512];
    int skipping = 1;

#define S_xstr(a) S_str(a)
#define S_str(a) #a
#if defined (CMAKE_SOURCE_DIR)
    makefile = S_xstr(CMAKE_SOURCE_DIR) "/makefile";
    add_symbol_assignment (x_strdup ("SIMHD = " S_xstr(CMAKE_SOURCE_DIR)));
#endif
#if defined (CMAKE_BINARY_DIR)
    cmakefile = S_xstr(CMAKE_BINARY_DIR) "/simh_makefile.cmake";
#endif
    debug = (NULL != getenv ("MAKEFILE2CMAKE_DEBUG"));
    fin = fopen (makefile, "r");
    if (fin == NULL) {
        fprintf (stderr, "Can't open input makefile: %s - %s\n", makefile, strerror(errno));
        exit (EXIT_FAILURE);
    }
    fout = fopen (cmakefile, "w");
    if (fout == NULL) {
        fprintf (stderr, "Can't open output cmake file: %s - %s\n", cmakefile, strerror(errno));
        exit (EXIT_FAILURE);
    }
#if defined (CMAKE_SOURCE_DIR)
    fprintf (fout, "# Built with SIMHD = %s\n", S_xstr(CMAKE_SOURCE_DIR));
#endif
#if defined (CMAKE_BINARY_DIR)
    fprintf (fout, "# Output Directory: %s\n", S_xstr(CMAKE_BINARY_DIR));
#endif
    add_symbol_assignment (x_strdup ("DISPLAYD = ${SIMHD}/display"));
    add_symbol_assignment (x_strdup ("DISPLAYL = ${DISPLAYD}/display.c ${DISPLAYD}/sim_ws.c"));
    add_symbol_assignment (x_strdup ("DISPLAYVT = ${DISPLAYD}/vt11.c"));
    add_symbol_assignment (x_strdup ("DISPLAY340 = ${DISPLAYD}/type340.c"));
    add_symbol_assignment (x_strdup ("DISPLAYNG = ${DISPLAYD}/ng.c"));
    add_symbol_assignment (x_strdup ("DISPLAY_OPT = -DUSE_DISPLAY -DUSE_SIM_VIDEO"));

    while (line = get_makefile_line (fin)) {
        const char *cptr = get_buf_token (line, tok1, sizeof (tok1));
        if (0 == strcmp ("# Common Libraries", line))
            skipping = 0;
        if (0 == memcmp ("#cmake-insert:", line, 14)) {
            fprintf (fout, "%s\n", &line[14]);
            continue;
        }
        if (skipping)   /* Skip until we reach the interesting stuff */
            continue;
        if (*line == '\0')
            continue;   /* Skip blank lines */
        if (*line == '#')
            continue;   /* Skip comment lines */
        if ((0 == strcmp (tok1, "$(info")) || (0 == strcmp (tok1, "$(error")))
            continue;
        cptr = get_buf_token (cptr, tok2, sizeof (tok2));
        if (0 == strcmp (tok2, "=")) {  /* Variable Assignment */
            add_symbol_assignment (line);
            continue;
        }
        if (0 == strcmp (tok2, ":")) {  /* Target Definition */
            char *target_line = line;
            const char *dptr = cptr;
            char simulator[32];
            char **dependencies = NULL;
            int dependency_count = 0;
            char **names = NULL;
            int name_count = 0;
            char **includes = NULL;
            int include_count = 0;
            char **defines = NULL;
            int define_count = 0;
            char **options = NULL;
            int option_count = 0;
            int cc_processed = 0;
            MFILE *fm = MOpen ();
            MFILE *fs = MOpen ();

            if ((0 != strncmp (tok1, "${BIN}", 6)) ||
                (0 != strcmp (tok1 + strlen(tok1) - 6, "${EXE}"))) {
                Dprintf ("Line %d: Ignoring Target: %s - %s\n", line_number, tok1, line);
                free (line);
                continue;
            }
            memcpy (simulator, tok1 + 6, strlen (tok1) - 12);
            simulator[strlen (tok1) - 12] = '\0';
            Dprintf ("Line %d: Target: %s\n", line_number, tok1);
            while (1) {
                cptr = get_buf_token (cptr, tok1, sizeof (tok1));
                if (tok1[0] == '\0')
                    break;
                if (variable_name_match (tok1, "SIM"))
                    continue;
                if (variable_name_match (tok1, "BUILD_ROMS")) {
                    add_string_to_list (&options, &option_count, "BUILDROMS");
                    continue;
                }
                free (expand_symbols (tok1, &names, &name_count));
            }
            while (line = get_makefile_line (fin)) {
                int i;

                if (strstr (line, "#cmake:ignore-target")) {
                    Dprintf ("Ignoring cmake marked skip target: %s\n", simulator);
                    emit_list (NULL, NULL, &options, &option_count);
                    emit_list (NULL, NULL, &names, &name_count);
                    MFlush (fs);
                    MFlush (fm);
                    break;
                }
                cptr = get_buf_token (line, tok1, sizeof (tok1));
                if ((cc_processed == 1) && ((0 == strncmp (line, "ifneq (,$(call find_test,", 25)) ||
                                            (0 == strncmp (line, "ifneq (,${call find_test,", 25)))) {
                    char *dirspec, *dirspecend;
                    char *test, *testend;
                    char testcmd[512];

                    dirspec = &line[25];
                    dirspecend = strchr (dirspec, ',');
                    *dirspecend = '\0';
                    test = dirspecend + 1;
                    testend = strchr (test, (line[9] == '(') ? ')' : '}');
                    *testend = '\0';
                    if (CheckTest (dirspec, test)) {
                        sprintf (testcmd, "TEST %s/tests/%s", dirspec, test);
                        add_string_to_list (&options, &option_count, testcmd);
                    }
                    free (line);
                    continue;
                }
                for (i = name_count - 1; i >= 0; i--)
                    if (strcmp (names[i], "${SIMHD}"))
                        MWriteSetSymbol (fs, names[i]);
                emit_list (NULL, NULL, &names, &name_count);
                if (0 == strcmp (tok1, "${CC}")) {
                    int use_int64 = 0, use_addr64 = 0, use_sim_video = 0;

                    cc_processed = 1;
                    Dprintf ("Found Target: %s, Dependencies: %s\nBuild Step: %s\n", tok1, dptr, line);
                    Mprintf (fm, "\nadd_simulator(%s", simulator);
                    Mprintf (fm, "\n    SOURCES");
                    while (1) {
                        dptr = get_buf_token (dptr, tok1, sizeof (tok1));
                        if (tok1[0] == '\0')
                            break;
                        if (strcmp (tok1, "${SIM}"))
                            Mprintf (fm, "\n        %s", tok1);
                    }
                    while (1) {
                        const char *vptr;
                        char *symbol;

                        cptr = get_buf_token (cptr, tok1, sizeof (tok1));
                        if (tok1[0] == '\0')
                            break;
                        if (0 == strcmp (tok1, "${LDFLAGS}"))
                            continue;
                        if (0 == strcmp (tok1, "${CC_OUTSPEC}"))
                            continue;
                        if (0 == strcmp (tok1, "-o"))
                            continue;
                        if (0 == strcmp (tok1, "$@"))
                            continue;
                        if (0 == strcmp (tok1, "${SIM}"))
                            continue;
                        symbol = get_idname (tok1);
                        vptr = lookup_symbol (symbol);
                        free (symbol);
                        if (0 == strcmp (tok1 + strlen (tok1) - 11, "_PANEL_OPT}")) {
                            char *tmp = (char *)malloc (strlen (tok1) + strlen ("-DFONTFILE="));

                            remove_string_from_list (&defines, &define_count, "FONTFILE=");
                            sprintf (tmp, "FONTFILE=%*.*s_FONT}", (int)(strlen (tok1) - 11), (int)(strlen (tok1) - 11), tok1);
                            add_string_to_list (&options, &option_count, "VIDEO");
                            add_string_to_list (&defines, &define_count, tmp);
                            free (tmp);
                            continue;
                        }
                        if (0 == strcmp (tok1 + strlen (tok1) - 5, "_OPT}")) {
                            char *expanded_opt = expand_symbols (tok1, NULL, 0);

                            vptr = expanded_opt;
                            while (1) {
                                vptr = get_buf_token (vptr, tok1, sizeof (tok1));
                                if (tok1[0] == '\0')
                                    break;
                                if (0 == strcmp (tok1, "-I")) {
                                    vptr = get_buf_token (vptr, tok1, sizeof (tok1));
                                    add_string_to_list (&includes, &include_count, tok1);
                                    if (0 == strncmp(tok1, "${", 2)) {
                                        MWriteSetSymbol (fs, tok1);
                                    }
                                    continue;
                                }
                                if (0 == memcmp (tok1, "-D", 2)) {
                                    add_string_to_list (&defines, &define_count, tok1+2);
                                    continue;
                                }
                            }
                            free (expanded_opt);
                            continue;
                        }
                    }
                    emit_list (fm, "INCLUDES", &includes, &include_count);
                    use_int64 = remove_string_from_list (&defines, &define_count, "USE_INT64");
                    use_addr64 = remove_string_from_list (&defines, &define_count, "USE_ADDR64");
                    use_sim_video = remove_string_from_list (&defines, &define_count, "USE_SIM_VIDEO");

                    if ((use_int64 == 1) && (use_addr64 == 1))
                        add_string_to_list (&options, &option_count, "FULL64");
                    if ((use_int64 == 1) && (use_addr64 == 0))
                        add_string_to_list (&options, &option_count, "INT64");
                    if (use_sim_video)
                        add_string_to_list (&options, &option_count, "VIDEO");
                    emit_list (fm, "DEFINES", &defines, &define_count);
                    continue;
                }
                if (0 == strcmp (tok1, "copy")) {
                    cptr = get_buf_token (cptr, tok1, sizeof (tok1)); /* copy source */
                    cptr = get_buf_token (cptr, tok1, sizeof (tok1)); /* copy destination */
                    if ((0 != memcmp (tok1, "${@D}\\", 6)) ||
                        (0 != strcmp (tok1 + strlen (tok1) - 6, "${EXE}"))) {
                        Dprintf ("Line %d: Unexpected copy target: %s\n", line_number, tok1);
                        continue;
                    }
                    tok1[strlen (tok1) - 6] = '\0';
                    memcpy (tok1, "COPY  ", 6);
                    add_string_to_list (&options, &option_count, tok1);
                    continue;
                }
                if (tok1[0] == '\0') { /* Empty line ends target rules */
                    emit_list (fm, NULL, &options, &option_count);
                    Mprintf (fm, ")\n\n");
                    break;
                }
                Dprintf ("Line %d: Ignoring Build Line: %s\n", line_number, line);
                free (line);
            }
        FMwrite (fout, fs);
        MClose (fs);
        FMwrite (fout, fm);
        MClose (fm);
        }
        Dprintf ("Line %d: %s\n\ttok1=%s\ttok2=%s\n", line_number, line, tok1, tok2);
        free (line);
        }
    clear_symbols ();
    fclose (fin);
    fclose (fout);
    exit (EXIT_SUCCESS);
}