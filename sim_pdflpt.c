/* 
 *  Copyright (c) 2013, Timothe Litt

 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:

 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.

 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 *  Except as contained in this notice, the name of the author shall not be
 *  used in advertising or otherwise to promote the sale, use or other dealings
 *  in this Software without prior written authorization from the author.
 *
 */

/* Emulated device-independent interface to lpt2pdf
 *
 * The API and requirements are documented in sim_pdflpt.h
 *
 * You should not need to read this file to use the API.
*/

#include "sim_defs.h"
#include "sim_tmxr.h"
#include "sim_console.h"
#include "sim_pdflpt.h" 

#define DIM(x) (sizeof (x) / sizeof ((x)[0]))

/* Time (seconds) of idleness before data flushed to attached file. */

#ifndef PDFLPT_IDLE_TIME
#define PDFLPT_IDLE_TIME (10)
#endif

/* Template and limit for generating spool file names.
 * Names will be 1 - SPOOL_FN_MAX.
 * SPOOL_FN is inserted before the file extension,
 * and must be wide enough to accomodate SPOOL_FN_MAX.
 */
#ifdef VMS
#define SPOOL_FN "_%05u"
#else
#define SPOOL_FN ".%05u"
#endif
#define SPOOL_FN_MAX (99999)

/* Hard errors when generating a spool file name */

static const int noretry[] = {
    PDF_E_BAD_FILENAME,
#ifdef ENOMEM
    ENOMEM,
#endif
#ifdef EDQUOT
    EDQUOT,
#endif
#ifdef EFAULT
    EFAULT,
#endif
#ifdef EINVAL
    EINVAL,
#endif
#ifdef EIO
    EIO,
#endif
#ifdef EISDIR
    EISDIR,
#endif
#ifdef ELOOP
    ELOOP,
#endif
#ifdef EMEDIUMTYPE
    EMEDIUMTYPE,
#endif
#ifdef EMFILE
    EMFILE,
#endif
#ifdef EMLINK
    EMLINK,
#endif
#ifdef ENAMETOOLONG
    ENAMETOOLONG,
#endif
#ifdef ENFILE
    ENFILE,
#endif
#ifdef ENODEV
    ENODEV,
#endif
#ifdef ENOSPC
    ENOSPC,
#endif
#ifdef ENXIO
    ENXIO,
#endif
#ifdef EROFS
    EROFS,
#endif
};

/* Context beyond the UNIT */

typedef struct {
    PDF_HANDLE pdfh;
    uint32 uflags;
    uint32 udflags;
    void  (*io_flush)(UNIT *up);
    t_stat (*reset) (DEVICE *dp);
    char *fntemplate;
    char *defaults;
    uint32 fileseq;
    UNIT idle_unit;/* Used for idle detection and additional context
                    * Usage of device-specific context:
                    * u3 - requested idle timeout.
                    * u4 - time remaining on idle detection
                    * u5 - printer column number
                    * up7 - pointer to (real) UNIT
                    * up8 - pointer to PCTX
                    */
    size_t bc;
    char buffer[256];
} PCTX;

/* Internal functions */

static t_stat parse_params (PDF_HANDLE pdfh, char *cptr, size_t length);
static t_stat spool_file (UNIT *uptr, TMLN *lp);
static t_bool retryable_error (int error);
static t_bool setup_template (PCTX *ctx, const char *filename, const char *ext);
static void next_spoolname (PCTX *ctx, char *newname, size_t size);
static t_stat reset (DEVICE *dptr);
static t_stat idle_svc (UNIT *uptr);
static void set_idle_timer (UNIT *uptr);

/* Create and initialize pdf context
 * Only called by SETCTX macro, following.
 * Initialization modifies the SimH data structures for
 * the unit and the device to allow sim_pdflpt to manage it.
 *
 * Even in text mode, sim_pdflpt detects when a printer goes
 * idle, and checkpoints pdf or flushed buffers for other files.
 *
 * The UNIT's io_flush function is intercepted to ensure that this
 * also happens when the simulator pauses, and that the default
 * actions are not applied to pdf files.
 *
 * The DEVICE's reset function is intercepted to ensure that the
 * idle detection timer for each unit is stopped when the device
 * is reset.
 */

static void createctx (UNIT *uptr) {
    PCTX *ctx;
    DEVICE *dptr;

    if (uptr->up8) {
        return;                                 /* Should never be called if initialized */
    }

    uptr->up8 =
        ctx = (PCTX *) calloc (1, sizeof (PCTX));
    if (!ctx)
        return;

    /* Record non-pdf device flags */
    ctx->uflags = uptr->flags;
    ctx->udflags = uptr->dynflags;

    /* Hook io_flush function */

    ctx->io_flush = uptr->io_flush;
    uptr->io_flush = &pdflpt_flush;

    /* Hook device reset function */

    dptr = find_dev_from_unit (uptr);
    if (dptr != NULL) {
        ctx->reset = dptr->reset;
        dptr->reset = &reset;
        if (!dptr->help) {
            dptr->help = pdflpt_help;
        }
        if (!dptr->attach_help) {
            dptr->attach_help = pdflpt_attach_help;
        }
    }

    /* Initialize the idle unit */

    ctx->idle_unit.action = &idle_svc;
    ctx->idle_unit.flags = UNIT_DIS;
    ctx->idle_unit.wait = 1*1000*1000;
    ctx->idle_unit.u3 = PDFLPT_IDLE_TIME;
    ctx->idle_unit.up7 = uptr;
    ctx->idle_unit.up8 = ctx;

    return;
}

/* Establish PDF context, and if first use, initialize
 * the context.  Returns to caller with the specified
 * error code if a context can't be allocated.  If the
 * caller returns void, specify LPTVOID as the error code.
 */

#define pdfctx ((PCTX *)uptr->up8)
#define LPTVOID
#define SETCTX(nomem) {                                 \
    if (!pdfctx) {                                      \
        createctx (uptr);                               \
        if (!pdfctx)                                    \
            return nomem;                               \
    } }

#define pdf (pdfctx->pdfh)
#define idle_unit pdfctx->idle_unit

/* Parse parameter string */

/* Define all the parameter keywords and their types. */

#define SET(_key, _code, _type) { #_key, PDF_##_code, AT_##_type },
#define XSET(_key, _code, _type)
typedef struct {
    const char *const keyword;
    int arg;
    int atype;
#define AT_STRING  1
#define AT_QSTRING 2
#define AT_NUMBER  3
#define AT_INTEGER 4
#define AT_QSNULL  5
} ARG;
static const ARG argtable[] = {
    SET (BAR-HEIGHT,    BAR_HEIGHT,     NUMBER)
    SET (BOTTOM-MARGIN, BOTTOM_MARGIN,  NUMBER)
    SET (COLUMNS,       COLS,           INTEGER)
    SET (CPI,           CPI,            NUMBER)
    SET (FONT,          TEXT_FONT,      QSTRING)
    SET (FORM,          FORM_TYPE,      STRING)
    SET (IMAGE,         FORM_IMAGE,     QSNULL)
    SET (LENGTH,        PAGE_LENGTH,    NUMBER)
   XSET (LFONT,         LABEL_FONT,     QSTRING)
    SET (NUMBER-WIDTH,  LNO_WIDTH,      NUMBER)
    SET (LPI,           LPI,            INTEGER)
    SET (LPP,           LPP,            INTEGER)
   XSET (NFONT,         LNO_FONT,       QSTRING)
   XSET (REQUIRE,       FILE_REQUIRE,   STRING)
    SET (SIDE-MARGIN,   SIDE_MARGIN,    NUMBER)
    SET (TITLE,         TITLE,          QSTRING)
    SET (TOF-OFFSET,    TOF_OFFSET,     INTEGER)
    SET (TOP-MARGIN,    TOP_MARGIN,     NUMBER)
    SET (WIDTH,         PAGE_WIDTH,     NUMBER)
};

/* Parse a string & apply to pdf
 *
 */

static t_stat parse_params (PDF_HANDLE pdfh, char *cptr, size_t length) {
    char *fn = cptr + length;
    char gbuf[CBUFSIZE], vbuf[CBUFSIZE];
    t_stat reason;

    while (cptr < fn ) {
        size_t k;
        char *p;

        if (isspace (*cptr)) {
            cptr++;
            continue;
        }
        reason = SCPE_ARG;

        cptr = get_glyph (cptr, gbuf, '=');

        for (k = 0; k < DIM (argtable); k++) {
            double arg;
            long iarg;
            char *ep;
            int at;

            if (strncmp (gbuf, argtable[k].keyword, strlen (gbuf))) {
                continue;
            }
            reason = PDF_OK;
            if (strlen (argtable[k].keyword) != strlen (gbuf)) {
                size_t kk;
                for (kk = k+1; kk < DIM (argtable); kk++) {
                    if (!strncmp (gbuf, argtable[kk].keyword, strlen (gbuf))) {
                        if (!sim_quiet) {
                            printf ("Ambiguous keyword: %s\n", gbuf);
                        }
                        reason = SCPE_ARG;
                        break;
                    }
                }
                if (reason != PDF_OK) {
                    break;
                }
            }
            p = vbuf;
            at = argtable[k].atype;
            if (at == AT_QSTRING || at == AT_QSNULL) {
                cptr = get_glyph_quoted (cptr, vbuf, 0);
                if (*p == '"' || *p == '\'') {
                    if (p[strlen (p)-1] == *p) {
                        *p++;
                        p[strlen (p)-1] = '\0';
                    }
                }
                if (at == AT_QSNULL && !strlen (p)) {
                    p = NULL;
                }
            } else {
                cptr = get_glyph (cptr, vbuf, 0);
            }

            switch (at) {
            case AT_QSTRING:
            case AT_QSNULL:
            case AT_STRING:
                reason = pdf_set (pdfh, argtable[k].arg, p);
                break;

            case AT_INTEGER:
                iarg = strtol (vbuf, &ep, 10);
                if (!*vbuf || *ep || ep == vbuf) {
                    if (!sim_quiet) {
                        printf ("Not an integer for %s value: %s\n",
                                 argtable[k].keyword, ep);
                    }
                    reason= SCPE_ARG;
                    break;
                }
                arg = (double) iarg;
                reason = pdf_set (pdfh, argtable[k].arg, arg);
                break;

            case AT_NUMBER:
                arg = strtod (vbuf, &ep);
                if (*ep) {
                    if (!strcmp (ep, "CM")) {
                        arg /= 2.54;
                    } else {
                        if (!strcmp (ep, "MM")) {
                            arg /= 25.4;
                        } else if (strcmp (ep, "IN")) {
                            if (!sim_quiet) {
                                printf ("Unknown qualifier for %s value: %s\n",
                                                 argtable[k].keyword,  ep);
                                reason = SCPE_ARG;
                                break;
                            }
                        }
                    }
                }
                reason = pdf_set (pdfh, argtable[k].arg, arg);
                break;

            default:
                return SCPE_ARG;
            } /* switch (argtype) */
            break;
        } /* for (argtable) */
        if (reason != SCPE_OK) {
            if (!sim_quiet) {
                if ( k < DIM (argtable)) {
                    if (pdf_error (pdfh) != PDF_OK) {
                        pdf_perror (pdfh, gbuf);
                    }
                } else {
                    printf ("Unknown parameter %s\n", gbuf);
                }
            }
            pdf_close (pdfh);
            return SCPE_ARG;
        }
    } /* while (cptr < fn) */
    return SCPE_OK;
}

/* attach_unit replacement
 * drop-in, except that the user may qualify the filename with parameters.
 * see the description in sim_pdflpt.h.
 */

t_stat pdflpt_attach (UNIT *uptr, char *cptr) {
    DEVICE *dptr;
    t_stat reason;
    char *p, *fn, *ext;
    char gbuf[CBUFSIZE];
    size_t page, line, fnsize = CBUFSIZE;

    if (uptr->flags & UNIT_DIS)
        return SCPE_UDIS;
    if (!(uptr->flags & UNIT_ATTABLE))
        return SCPE_NOATT;

    if ((dptr = find_dev_from_unit (uptr)) == NULL)
        return SCPE_NOATT;

    if (dptr->flags & DEV_DIS) {
        return SCPE_NOATT;
    }

    SETCTX (SCPE_MEM);

    pdf = NULL;

    ext = match_ext (cptr, "PDF");
    if (!ext) {
        if (pdfctx->uflags & UNIT_SEQ) {
            uptr->flags |= UNIT_SEQ;
        }
        if (!(pdfctx->udflags & UNIT_NO_FIO)) {
            uptr->dynflags &= ~UNIT_NO_FIO;
        }
        if (sim_switches & SWMASK ('S') && strcmp (cptr, "-")) {
            ext = strrchr (cptr, '.');
            if (!ext) {
                ext = cptr + strlen (cptr);
            }
            if (!setup_template (pdfctx, cptr, ext)) {
                return SCPE_MEM;
            }
            reason = SCPE_OPENERR;

            do {
                uptr->fileref = pdf_open_exclusive (cptr, "rb+");
                if (uptr->fileref == NULL ) {
                    if (!retryable_error (errno)) {
                        if (!sim_quiet) {
                            perror (cptr);
                        }
                        break;
                    }
                } else {
                    fseek (uptr->fileref, 0, SEEK_END);
                    if (ftell (uptr->fileref) == 0) {
                        reason = SCPE_OK;
                        uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char)); 
                        strcpy (uptr->filename, cptr);
                        uptr->flags |= UNIT_ATT;
                        break;
                    }
                    fclose (uptr->fileref);
                }
                next_spoolname (pdfctx, cptr, fnsize);
            } while (pdfctx->fileseq < SPOOL_FN_MAX);
        } else {
            free (pdfctx->fntemplate);
            pdfctx->fntemplate = NULL;
            if (strcmp (cptr, "-")) {
                reason = attach_unit (uptr, cptr);
            } else {
                uptr->fileref = stdout;
                reason = SCPE_OK;
                uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char)); 
                strcpy (uptr->filename, "-");
                uptr->flags |= UNIT_ATT;
            }
        }

        if (reason == SCPE_OK) {
            sim_fseek (uptr->fileref, 0, SEEK_END);
            uptr->pos = (t_addr)sim_ftell (uptr->fileref);

            if (pdfctx->fntemplate) {
                reason = sim_con_register_printer (uptr, &spool_file);
            } else {
                reason = sim_con_register_printer (uptr, NULL);
            }
        }
        return reason;
    } /* Non-PDF setup */

    if (sim_switches & SIM_SW_REST) {
        return SCPE_NOATT;
    }

    /* Find end of pre-name attributes */

    fn = cptr;
    while ((p = strchr (fn, '=')) != NULL) {
        fn = get_glyph (fn, gbuf, '=');
        fn = get_glyph_quoted (fn, gbuf, 0);
    }
    if (!*fn) {
        return SCPE_OPENERR;
    }
    fnsize -= fn - cptr;

    /* Setup template for spooling if requested */

    if (sim_switches & SWMASK ('S')) {
        /* Only spool to existing files if they are empty */

        sim_switches |= SWMASK ('R');

        if (!setup_template (pdfctx, fn, ext)) {
            return SCPE_MEM;
        }
    } else {
        free (pdfctx->fntemplate);
        pdfctx->fntemplate = NULL;
    }

    if (sim_switches & SWMASK ('E')) {
        reason = pdf_file (cptr);
        if (reason != PDF_OK) {
            if (!sim_quiet) {
                if (reason == PDF_E_NOT_PDF) {
                    printf ("%s: is not a PDF file\n", cptr);
                }
            }
            return SCPE_OPENERR;
        }
    }

    /* Open file.
     * If spooling and an error, try the next sequential file in case
     * the specified base filename is in use.
     */

    do {
        pdf = pdf_open (fn);
        if (pdf || !pdfctx->fntemplate) {
            break;
        }
        if (!retryable_error (errno)) {
            if (!sim_quiet) {
                pdflpt_perror (NULL, fn);
            }
            return SCPE_OPENERR;
        }
        next_spoolname (pdfctx, fn, fnsize);
    } while (pdfctx->fileseq < SPOOL_FN_MAX);

    if (!pdf) {
        return SCPE_OPENERR;
    }
    
    switch (sim_switches & (SWMASK('E') | SWMASK('N') | SWMASK('R'))) {
    case SWMASK('N'):
        reason = pdf_set (pdf, PDF_FILE_REQUIRE, "REPLACE"); /* Overwrite existing */
        break;
    case SWMASK('E'):
        reason = pdf_set (pdf, PDF_FILE_REQUIRE, "APPEND");  /* Exists, APPEND OK */
        break;
    case SWMASK('R'):
        reason = pdf_set (pdf, PDF_FILE_REQUIRE, "NEW");     /* New, must be empty */
        break;
    case 0:
        reason = pdf_set (pdf, PDF_FILE_REQUIRE, "APPEND");  /* No switch, APPEND or create */
        break;
    default:
        pdf_close (pdf);
        pdf = NULL;
        if (!sim_quiet) {
            printf ("Invalid combination of switches\n");
        }
        return SCPE_ARG;
    }

    if (reason != PDF_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, fn);
        }
        pdf_close (pdf);
        pdf = NULL;
        return SCPE_ARG;
    }

    /* Go back thru the attributes and apply to the handle */

    if (pdfctx->defaults) {
        reason = parse_params (pdf, pdfctx->defaults, strlen (pdfctx->defaults));
        if (reason != SCPE_OK) {
            pdf = NULL;
            return reason;
        }
    }
    reason = parse_params (pdf, cptr, fn - cptr);
    if (reason != SCPE_OK) {
        pdf = NULL;
        return reason;
    }

    /* Check for composite errors */

    reason = pdf_print (pdf, "", 0);
    if (reason != SCPE_OK) {
        /* If setting up a spooled file, retry if possible */

        if (sim_switches & SWMASK ('S') && retryable_error (reason)) {
            size_t n;

            for (n = 1; n <= SPOOL_FN_MAX; n++) {
                PDF_HANDLE newpdf;

                next_spoolname (pdfctx, fn, fnsize);

                newpdf = pdf_newfile (pdf, fn);
                if (newpdf) {
                    reason = pdf_print (newpdf, "", 0);
                    if (reason == SCPE_OK) {
                        pdf_close (pdf);
                        pdf = newpdf;
                        break;
                    }
                    pdf_close (newpdf);
                } else {
                    reason = errno;
                }
                if (!retryable_error (reason)) {
                    break;
                }
            }
        }
        if (reason != SCPE_OK) {
            if (!sim_quiet) {
                pdf_perror (pdf, fn);
            }
            pdf_close (pdf);
            pdf = NULL;
            return SCPE_ARG;
        }
    }

    /* Give I/O a clean start */

    pdf_clearerr (pdf);

    uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char));
    if (uptr->filename == NULL) {
        pdf_close (pdf);
        pdf = NULL;
        return SCPE_MEM;
    }
    strncpy (uptr->filename, fn, CBUFSIZE);

    pdf_where (pdf, &page, &line);

    if (!sim_quiet) {
        if (dptr->numunits > 1) {
            printf ("%s%u Ready at page %u line %u of %s\n",
                    dptr->name, uptr - dptr->units, page, line,
                    uptr->filename);
        } else {
            printf ("%s Ready at page %u line %u of %s\n",
                    dptr->name, page, line,
                    uptr->filename);
        }
    }

    pdfctx->bc = 0;

    if (pdfctx->fntemplate) {
        reason = sim_con_register_printer (uptr, &spool_file);
    } else {
        reason = sim_con_register_printer (uptr, NULL);
    }

    /* Declare this not a SEQUENTIAL unit so scp won't do random seeks. */

    uptr->flags &= ~UNIT_SEQ;

    /* Prevent SCP interference with file position, contents */

    uptr->dynflags |= UNIT_NO_FIO;

    uptr->flags = uptr->flags | UNIT_ATT;

    /* PDF files can't be written to randomly. Expose page number
     * to show progress (but save/restore won't work.(
     */
    uptr->pos = page;

    return reason;
}

/* Setup default parameter string for attach.
 * Allows devices to override the PDF defaults,e.g. for column width.
 *
 * pstring is prefixed to ATTACH command.
 */

t_stat pdflpt_set_defaults (UNIT *uptr, const char *pstring) {
    char *p;
    size_t len = (pstring? strlen (pstring): 0);

    SETCTX (SCPE_MEM);

    if (len) {
        p = (char *) realloc (pdfctx->defaults, len + 1);
        if (p == NULL) {
            return SCPE_MEM;
        }
        pdfctx->defaults = p;
        strcpy (p, pstring);
        return PDFLPT_OK;
    }

    free (pdfctx->defaults);
    pdfctx->defaults = NULL;

    return PDFLPT_OK;
}

/* Returns default string.
 * Do not cache: not valid after defaults are changed.
 */

const char *pdflpt_get_defaults (UNIT *uptr) {
    SETCTX (NULL);

    return pdfctx->defaults;
}

/* Switch to a new form
 *
 */

t_stat pdflpt_newform (UNIT *uptr, const char *params) {
    int r;
    
    SETCTX (SCPE_MEM);

    if (!pdf || !(uptr->flags & UNIT_ATT)) {
        return SCPE_OK;
    }

    r = pdf_reopen (pdf);
    if (r != PDF_OK) {
        return SCPE_NOATT;
    }

    r = parse_params (pdf, (char *)params, strlen (params));
    if (r != SCPE_OK) {
        return SCPE_ARG;
    }
    r = pdf_print (pdf, "", 0);
    if (r != SCPE_OK) {
        return SCPE_ARG;
    }
    return SCPE_OK;
}

/* detach_unit replacement
 * drop-in.
 */

t_stat pdflpt_detach (UNIT *uptr) {
    DEVICE *dptr;
    int r;
    size_t page;

    if (uptr == NULL) {
        return SCPE_IERR;
    }

    SETCTX (SCPE_MEM);

    if (!(uptr->flags & UNIT_ATTABLE)) {
        return SCPE_NOATT;
    }

    if (!(uptr->flags & UNIT_ATT)) {
        if (sim_switches & SIM_SW_REST)
            return SCPE_OK;
        else
            return SCPE_NOATT;
    }

    if ((dptr = find_dev_from_unit (uptr)) == NULL) {
        return SCPE_OK;
    }

    pdflpt_reset (uptr);

    sim_con_register_printer (uptr, NULL);

    if (!pdf) {
        if (uptr->fileref == stdout) {
            uptr->flags &= ~UNIT_ATT;
            free (uptr->filename);
            uptr->filename = NULL;
            return SCPE_OK;
        }
        return detach_unit (uptr);
    }

    if (sim_switches & SIM_SW_REST) {
        return SCPE_NOATT;
    }

    if (pdfctx->bc) {
        pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
    }

    pdf_where (pdf, &page, NULL);

    r = pdf_close (pdf);
    pdf = NULL;

    if (r != PDF_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, uptr->filename);
        }
        r = SCPE_NOATT;
    } else if (!sim_quiet) {
        printf ( "Closed %s, on page %u\n", uptr->filename, page );
    }

    free (pdfctx->fntemplate);
    pdfctx->fntemplate = NULL;

    if (pdfctx->uflags & UNIT_SEQ) {
        uptr->flags |= UNIT_SEQ;
    }
    if (!(pdfctx->udflags & UNIT_NO_FIO)) {
        uptr->dynflags &= ~UNIT_NO_FIO;
    }
    uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_RO);
    free (uptr->filename);
    uptr->filename = NULL;

    return SCPE_OK;
}

/* Spool callback
 *  Called by the console print command to release the active file
 *  and replace it with a new one.
 *
 *  For PDF files, the old file must remain open until a new one replaces
 *  it.  This is because the old file's parameters are used to setup the new.
 *
 *  For other file types, the old file is closed and a new file is
 *  attached.  
 *
 * If the file is empty (in the sense of no printable data - metadata does
 * not count), the file is not closed and an error is returned to indicate
 * that nothing was released.
 */

static t_stat spool_file (UNIT *uptr, TMLN *lp) {
    DEVICE *dptr;
    PDF_HANDLE newpdf = NULL;
    size_t n, page, line;
    int r;
    char newname[CBUFSIZE];
    char devname[CBUFSIZE];
    t_bool pdf_mode;

    SETCTX (SCPE_MEM);

    if (!pdfctx->fntemplate || !(uptr->flags & UNIT_ATT)) {
        return SCPE_EOF;
    }

    /* Unit is setup for spooling.
     * Try to open a new file.
     */

    if ((dptr = find_dev_from_unit (uptr)) == NULL)
        return SCPE_ARG;

    if (dptr->numunits > 1) {
        sprintf (devname, "%s%u ", dptr->name, uptr - dptr->units);
    } else {
        sprintf (devname, "%s ", dptr->name);
    }

    pdf_mode = pdflpt_getmode (uptr) == PDFLPT_IS_PDF;
    if (pdf_mode) {
        if (!pdfctx->bc && pdf_is_empty (pdf)) {
            return SCPE_EOF;
        }
    } else {
        if (uptr->pos == 0) {
            return SCPE_EOF;
        }
        if (!sim_quiet) {
            if (lp)
                tmxr_linemsgf (lp, "%sClosing %s\n", devname, uptr->filename);
            printf ("%sClosing %s\n", devname, uptr->filename);
        }
        if (fclose (uptr->fileref) == -1) {
            r = SCPE_IOERR;
        } else {
            r = SCPE_OK;
        }
        uptr->fileref = NULL;

        if (r != SCPE_OK) {
            if (lp)
                tmxr_linemsgf (lp, "%s%s\n", devname, sim_error_text (r));
            printf ("%s%s\n", devname, sim_error_text (r));
        }
    }

    for (n = 1; n <= SPOOL_FN_MAX; n++) {
        next_spoolname (pdfctx, newname, sizeof (newname));

        if (pdf_mode) {
            newpdf = pdf_newfile (pdf, newname);
            if (newpdf) {
                break;
            }
        } else {
           uptr->fileref = pdf_open_exclusive (newname, "rb+");
           if (uptr->fileref != NULL) {
               fseek (uptr->fileref, 0, SEEK_END);
               if (ftell (uptr->fileref) == 0 ) {
                   r = SCPE_OK;
                   break;
               }
               fclose (uptr->fileref);
               uptr->fileref = NULL;
           }
           continue;
        }
        if (!retryable_error (errno)) {
            if (!sim_quiet) {
                if (lp)
                    tmxr_linemsgf (lp, "%s%s: %s\n", devname, newname, pdf_strerror (pdf_error(newpdf)));
                printf ("%s%s: %s\n", devname, newname, pdf_strerror (pdf_error(newpdf)));
            }
            break;
        }
    }

    if ((pdf_mode? !newpdf: (r != SCPE_OK))) {
        if (!sim_quiet) {
            if (lp)
                tmxr_linemsgf (lp, "%sUnable to open new file\n", devname);
            printf ("%sUnable to open new file\n", devname);
        }
        return SCPE_OPENERR;
    }

    if (!pdf_mode) {
        strcpy (uptr->filename, newname);
        sim_fseek (uptr->fileref, 0, SEEK_END);
        uptr->pos = (t_addr)sim_ftell (uptr->fileref);

        return SCPE_OK;
    }

    /* We have a new file open.  Close the old. */

    if (pdfctx->bc) {
        pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
    }

    pdf_where (pdf, &page, &line);

    r = pdf_close (pdf);
 
    if (r != PDF_OK) {
        if (!sim_quiet) {
            if (lp)
                tmxr_linemsgf (lp, "%s%s: %s\n", devname, uptr->filename,
                               pdf_strerror (pdf_error (pdf)));
            printf ("%s%s: %s\n", devname, uptr->filename,
                    pdf_strerror (pdf_error (pdf)));
        }
        r = SCPE_OPENERR;
    } else if (!sim_quiet) {
        if (lp)
            tmxr_linemsgf (lp, "%sClosed %s, on page %u\n", devname, 
                           uptr->filename, page );
        printf ("%sClosed %s, on page %u\n", devname, 
                uptr->filename, page );
    }

    pdf = newpdf;
    strcpy (uptr->filename, newname);

    if (!sim_quiet) {
        if (lp)
            tmxr_linemsgf (lp, "%sReady at page %u line %u of %s\n",
                           devname, page, line, uptr->filename);
        printf ("%sReady at page %u line %u of %s\n",
                devname, page, line, uptr->filename);
    }

    return r;
}

/* Check for retryable error finding a spool file.
 */

static t_bool retryable_error (int error) {
    size_t i;

    /* These errors aren't worth retrying */
    for (i = 0; i < DIM (noretry); i++) {
        if (error == noretry[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Setup template from filename.
 * Requires ext to be part of filename.
 */
static t_bool setup_template (PCTX *ctx, const char *filename, const char *ext) {
    char *p;

    p = (char *) realloc (ctx->fntemplate, (ext - filename) + sizeof (SPOOL_FN) + strlen (ext));
    if (!p) {
        return FALSE;
    }

    ctx->fntemplate = p;
    strncpy (p, filename, ext - filename);
    sprintf (p + (ext - filename), "%s%s",SPOOL_FN, ext);
    ctx->fileseq = 0;

    return TRUE;
}

/* Return the next spool file name to try
 */
static void next_spoolname (PCTX *ctx, char *newname, size_t size) {
    /* Next file number is 1 .. SPOOL_FN_MAX */

    ctx->fileseq %= SPOOL_FN_MAX;
    ctx->fileseq++;

#ifdef _MSC_VER
    _snprintf (newname, size, ctx->fntemplate, ctx->fileseq);
#else
    snprintf (newname, size, ctx->fntemplate, ctx->fileseq);
#endif
    newname[size-1] = '\0';
    return;
}

/* Query */

int pdflpt_getmode (UNIT *uptr) {
    SETCTX (SCPE_MEM);

    if (!uptr->flags & UNIT_ATT) {
        return PDFLPT_INACTIVE;
    }
    if (pdf) {
        return PDFLPT_IS_PDF;
    }

    return PDFLPT_IS_TEXT;
}

/* Equivalent of fputc
 *
 * For pdf files, buffer the output until a paper motion character or a string call.
 * The reasons are several:
 *   The pdf_print call is more efficient with long strings.
 *   The initial string when a file is opened can be <CR><FF>, which are easier to remove
 *   if they're in a string.  Devices smart enough to use the string writes should be
 *   smart enough to handle this themselves.
 *
 * If the file is a text file, no additional buffering is done.  The file system should do
 * this, and devices tend to call ftell() after each byte.  It would be necessary to flush
 * the buffer in pdflpt_where in that case (there might be line ending conversion) - and
 * there's no benefit.
 */

int pdflpt_putc (UNIT *uptr, int c) {
    int r;

    SETCTX (EOF);

    if (c == '\015' || c == '\012' || c == '\014') {
        idle_unit.u5 = 0;
        set_idle_timer (uptr);
    } else {
        idle_unit.u5++;
        sim_cancel (&idle_unit);
    }

    if (!pdf) {
        return fputc (c, uptr->fileref);
    }

    pdfctx->buffer[pdfctx->bc++] = c;

    if (c == '\n' || c == '\f' || pdfctx->bc >= sizeof (pdfctx->buffer)) {
        r = pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
        if (r != PDF_OK) {
            return EOF;
        }
    }
    return (int)((unsigned char)(c & 0xFF));
}

/* Equivalent of fputs */

int pdflpt_puts (UNIT *uptr, const char *s) {
    int r;
    const char *p = s;

    SETCTX (-1);

    while (*p) {
        char c = *p++;

        if (c == '\015' || c == '\012' || c == '\014') {
            idle_unit.u5 = 0;
        } else {
            idle_unit.u5++;
        }
    }
    if (idle_unit.u5 == 0) {
        set_idle_timer (&idle_unit);
    } else {
        sim_cancel (&idle_unit);
    }

    if (!pdf) {
        return fputs (s, uptr->fileref);
    }

    if (pdfctx->bc) {
        pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
    }
    r = pdf_print (pdf, s, PDF_USE_STRLEN);
    if (r != PDF_OK) {
        return EOF;
    }
    return 42;
}

/* Equivalent of write
 * It's not advisable to use size 1 with many members if size many
 * with one member will do.  Each member will cause a lot of work.
 */

size_t pdflpt_write (UNIT *uptr, void *ptr, size_t size, size_t nmemb) {
    size_t n;
    const char *p = (const char *)ptr;

    SETCTX (0);

    n = size * nmemb;

    while (n--) {
        char c = *p++;

        if (c == '\015' || c == '\012' || c == '\014') {
            idle_unit.u5 = 0;
        } else {
            idle_unit.u5++;
        }
    }
    if (idle_unit.u5 == 0) {
        set_idle_timer (&idle_unit);
    } else {
        sim_cancel (&idle_unit);
    }

    if (!pdf) {
        return fwrite (ptr, size, nmemb, uptr->fileref);
    }

    if (pdfctx->bc) {
        pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
    }

    for (n = 0; n < nmemb; n++) {
        int r;

        r = pdf_print (pdf, ((char *)ptr), size);
        if (r != PDF_OK) {
            return n;
        }
        ptr = (void *)(((char *)ptr) + size);
    }
    return n;
}

/* Flush
 *  Checkpoints PDF files (expensive)
 *  fflush for text files.
 */

void pdflpt_flush (UNIT *uptr) {

    SETCTX (LPTVOID);

    if (!pdf) {
        if (pdfctx->io_flush) {
            pdfctx->io_flush (uptr);
        } else {
            fflush (uptr->fileref);
        }
        return;
    }

    if (pdfctx->bc) {
        pdf_print (pdf, pdfctx->buffer, pdfctx->bc);
        pdfctx->bc = 0;
    }
    (void) pdf_checkpoint (pdf);
    return;
}

/* Obtain a snapshot of a PDF handle.
 * Atomic checkpoint, copy of data to a new file.
 * Output file will not contain last page if not completely written.
 */

int pdflpt_snapshot (UNIT *uptr, const char *filename) {

    SETCTX (SCPE_MEM);

    return pdf_snapshot (pdf, filename);
}


/* Roughly tell, but not quite.
 * Returns page for progress report.  If *line is non-NULL
 * also returns line.  These are where then NEXT write will
 * go; the page may not yet exist.
 */

t_addr pdflpt_where (UNIT *uptr, size_t *line) {
    size_t page;
    int r;

    SETCTX (SCPE_MEM);

    if (!pdf) {
        return ftell (uptr->fileref);
    }

    r = pdf_where (pdf, &page, line);
    if (r != PDF_OK) {
        return -1;
    }
    return page;
}

/* Get list of known form names */

const char *const *pdflpt_get_formlist ( size_t *length ) {
    return pdf_get_formlist ( length );
}

/* Get list of known font names */

const char *const *pdflpt_get_fontlist ( size_t *length ) {
    return pdf_get_fontlist ( length );
}

/* Get the error flag (last error for PDF) */

int pdflpt_error (UNIT *uptr) {
    SETCTX (SCPE_MEM);

    if (!pdf) {
        return ferror (uptr->fileref);
    }
    return pdf_error (pdf);
}

/* Turn error code into a string (works for errno too, at least
 * where strerror() works.
 */
const char *pdflpt_strerror (int errnum) {
    return pdf_strerror (errnum);
}

/* Print last error */

void pdflpt_perror (UNIT *uptr, const char *s) {
    SETCTX (LPTVOID);

    if (pdf) {
        pdf_perror (pdf, s);
    } else {
        perror (s);
    }
}

/* Clear last error
 */

void pdflpt_clearerr (UNIT *uptr) {
    SETCTX (LPTVOID);

    if (!pdf) {
        clearerr (uptr->fileref);
    } else {
        pdf_clearerr (pdf);
    }
    return;
}

/* Reset hook
 * Cancels idle timer.
 */

static t_stat reset (DEVICE *dptr) {
    t_stat (*reset) (DEVICE *dp) = NULL;
    UNIT *uptr;
    uint32 n;

    for (n = 0, uptr = dptr->units; n < dptr->numunits; n++, uptr++) {
        if (pdfctx) {
            pdflpt_reset (uptr);
            if (pdfctx->reset) {
                reset = pdfctx->reset;
            }
        }
    }
    if (reset) {
        return reset (dptr);
    }

    return SCPE_OK;
}

/* Reset function for any device that can't be hooked because
 * its DEVICE can't be found from its unit pointer.  This should
 * be rare.  Called internally from the reset hook.
 */

void pdflpt_reset (UNIT *uptr) {
    SETCTX (LPTVOID);

    if (sim_is_active (&idle_unit)) {
        pdflpt_flush (uptr);
        sim_cancel (&idle_unit);
    }

    return;
}

/*  pdflpt_set_idle_timeout
 *  Optional command action to modify idle timer.
 *  Not clear why one LPT might want a different value,
 *  but just in case.
 *
 */

t_stat pdflpt_set_idle_timeout (UNIT *uptr, int32 val, char *cptr, void *desc) {
    uint32 timeout;
    t_stat r;

    SETCTX (SCPE_MEM);

    if (cptr == NULL || !*cptr) {
        idle_unit.u3 = PDFLPT_IDLE_TIME;
        return SCPE_OK;
    }

    timeout = (uint32) get_uint (cptr, 10, UINT_MAX, &r);
    if (r != SCPE_OK) {
        return r;
    }
    if (r == 0) {
        return SCPE_ARG;
    }
    idle_unit.u3 = timeout;

    return SCPE_OK;
}

/* Set idle timer
 *
 * To allow for long idle times, the unit is scheduled once/sec, and
 * counts down the requested time (u3) in time remaining (u4).
 */

static void set_idle_timer (UNIT *uptr) {
    UNIT *iuptr;

    SETCTX (LPTVOID);

    iuptr = &idle_unit;

    iuptr->u4 = iuptr->u3;
    sim_cancel(iuptr);
    sim_activate_after (iuptr, iuptr->wait);

    return;
}

/* Service function for idle_unit.
 * Counts down time to idle, and flushes buffers if
 * it expires.
 */

static t_stat idle_svc (UNIT *uptr) {

    if (--uptr->u4 > 0) {
        sim_activate_after (uptr, uptr->wait);
        return SCPE_OK;
    }

    pdflpt_flush ((UNIT *)uptr->up7);
    return SCPE_OK;
}

/* Help for PDF-enabled printers */

t_stat pdflpt_attach_help (FILE *st, struct sim_device *dptr,
                                  struct sim_unit *uptr, int32 flag, char *cptr) {
    if (!uptr) {
        uptr = dptr->units;
    }
    SETCTX (SCPE_MEM);

    return scp_help (st, dptr, uptr, flag | SCP_HELP_ATTACH, pdflpt_attach_helptext, cptr,
                     pdflpt_helptext,                          /* P1 */
                     (pdfctx->defaults? "T": "F"),             /* P2 */
                     (pdfctx->defaults? pdfctx->defaults: ""), /* P3 */
                     (pdfctx->defaults? "T": "F"),             /* P1 */
                     (pdfctx->defaults? pdfctx->defaults: "")  /* P2 */
        );
}

t_stat pdflpt_help (FILE *st, struct sim_device *dptr,
                           struct sim_unit *uptr, int32 flag, char *cptr) {
    static const char helptext[] = {
" The %D controller does not have device-specific help.\n"
" However, the following generic topics may be of use."
"%1H\n"     /* Include PDF help after this string */
"\n"
" Note that the %D does support printing to PDF files, see the\n"
" \"PDF printing\" topic below.\n"
"\n"
" You are welcome to raise an issue on the SimH issue tracker\n"
" if you are having difficulty with this device.\n"
"\n"
" See \"How to Contribute\" for the location of the SimH team.\n"
"\n"
" If you are able, please feel free to contribute help for this device.\n"
"1 ?2Device-specific defaults\n"
" The %D defaults the parameters of ATTACH for PDF output to\n"
"+%3s\n"
" These defaults replace the standard PDF defaults listed in PDF Printing\n"
" for the %D, but not for any other PDF-capable printer.\n"
"1 $Set commands\n"
"1 $Show commands\n"
"1 $Registers\n"
"1 How to contribute\n"
" The SimH development team can be reached at https://github.com/simh/simh.\n"
" The file scp_help.h documents how to create help text in detail\n"
"\n"
" However, we would gratefully accept any knowledge that you have about\n"
" the history and capabilities of the %D.\n"
"\n"
" Thank you for considering a contribution.\n"
    };

    if (!uptr) {
        uptr = dptr->units;
    }
    SETCTX (SCPE_MEM);

    return scp_help (st, dptr, uptr, flag, helptext, cptr,
                     pdflpt_helptext,                          /* P1 */
                     (pdfctx->defaults? "T": "F"),             /* P2 */
                     (pdfctx->defaults? pdfctx->defaults: ""), /* P3 */
                     (pdfctx->defaults? "T": "F"),             /* P1 */
                     (pdfctx->defaults? pdfctx->defaults: "")  /* P2 */
        );
}

/* Please keep these strings last in the file to facilitate editing. */

const char pdflpt_attach_helptext[] = {
" ATTACH %D connects its output to a file.\n"
" %1H\n"
" For ASCII output, use:\n"
"+ATTACH -switches %D filename\n"
" -n creates a new file, or truncates an existing file\n"
" -e appends to an existing file, and generates an error if the file"
"+does not exist\n"
" The default is to append to a file if it exists, or create it if"
"+it does not\n"
"\n"
" For PDF (Adobe Portable Document Format) output, use:\n"
"+ATTACH %D filename.pdf\n"
" The switches and options are detailed below\n"
"\n"
" Both output formats support spooling, which allows output to be\n"
" released to an external process without stopping the simulation.\n"
" That process can print the file to a physical printer, move it\n"
" to a network location, e-mail it - or anything else.\n"
" For details, see the Spooling section.\n"
"1 ?2Device-specific defaults\n"
" The %D defaults the parameters of ATTACH for PDF output to\n"
"+%3s\n"
    };

const char pdflpt_helptext[] = 
"1 PDF printing\n"
" The %D is able to print to Adobe Portable Document Format (PDF) files on\n"
" simulated paper.\n"
"\n"
" The %D also supports writing files (PDF or ASCII) to a dedicated directory\n"
" for spooling to a physical printer.  \n"
"\n"
" These capabilities are described in detail in the following topics.\n"
"\n"
"2 About PDF files\n"
" Adobe's Portable Document Format - PDF - is a document format standardized\n"
" by the ISO as ISO 32000-1:2008.  PDF is supported on the platforms that\n"
" support SimH, and is widely used on the World-Wide Web.  It is also a\n"
" standard format for archiving electronic documents.  Thus, it should be\n"
" possible to render (view and/or print) PDF for the forseeable future.\n"
"\n"
" SimH generates a subset of PDF, and will only operate on files that it\n"
" generates.\n"
"\n"
" A PDF file is similar to a file system, in that it contains indexes and\n"
" other information about the file.  The file is not in a readable\n"
" state while it is being written, as the file structure places this\n"
" data at the end of the file.  If you abort SimH (or it crashes) while\n"
" a PDF file is being written, the file may not be readable.\n"
"\n"
" SimH watches for periods of inactivity, and puts the file into a\n"
" consistent state during these periods.\n"
"\n"
"2 Configuration\n"
" The paper simulated in SimH PDF files is configured by the ATTACH command.\n"
"\n"
" Although there are many options, none are required; the defaults produce\n"
" the U.S. standard 14.875 x 11 inch greenbar paper at 6 lines and 10 columns\n"
" per inch.  Other colors, sizes and spacing are available.\n"
"\n"
" To select PDF output, the filename specified on the ATTACH command must\n"
" end in .pdf (or .PDF).\n"
"\n"
" There are two kinds of options that may be specified.  Switches are\n"
" the usual single-letter qualifiers preceeded by hypen following the\n"
" word ATTACH.  Switches specify how the file to be attached.\n"
"\n"
" Parameters have the form keyword=value and precede the\n"
" filename.  Parameters specify the format of the paper to be simulated.\n"
"\n"
" Thus, the syntax for ATTACH is:\n"
"+ATTACH -switch -switch %D keyword=value keyword=\"value\" filename.pdf\n"
" The filename can include a device and/or directory specifier.\n"
"3 Switches\n"
" -S SimH will generate ouput for spooling to a physical printer. See\n"
"+the Spooling topic.\n"
" -E The file must exist.  If it is not empty, it must be in .PDF format, and\n"
"+new data will be appended.\n"
" -N A new file is created.  If the file already exists, its contents are\n"
"+replaced by new data.  Any old data is lost.\n"
" -R If the file exists, it must be empty. If it does not, it will be created.\n"
"\n"
" The ATTACH command will fail if contradictory switches are specified, if appending\n"
" to a non-PDF file is requested, if the constraints are not met, or if\n"
" the file can't be created for any reason.\n"
"\n"
" If no switch is specified:\n"
"+If the file exists, data wil be appended\n"
"+If the file does not exist, it will be created.\n"
"3 Parameters\n"
" Although there are many parameters available, to achieve any given effect\n"
" generally requires only a few.\n"
"\n"
" Parameters documented as a dimension in inches may be specified in cm or mm\n"
" by appending \"mm\" or \"cm\" to the value.  E.g. 38.1cm is equivalent to\n"
" 15.0in.  The \"in\" is optional.\n"
"\n"
" The default value listed with each parameter may be superseded by a\n"
" device-specific default.  Consult the \"Device-specific defaults\"\n"
" topic for specifics. (The topic is present only for these devices.)\n"
"\n"
" The parameters are described in groups.\n"
"4 ?1Device-specific defaults\n"
" The %D defaults the parameters of ATTACH for PDF output to\n"
"+%2s\n"
" These defaults replace the standard PDF defaults shown with each parameter.\n"
" for the %D, but not for any other PDF-capable printer.\n"
"4 Form style\n"
" All emulated paper is called a 'form'.  All forms are tractor-feed,\n"
" and include the tractor feed holes.\n"
"\n"
" Several form styles are available.  These are:\n"
"+Plain - Plain forms are a white page, optionally with line numbers.\n"
"+Bar   - Bar forms contain alternating white and colored bars in the\n"
"+++area of the form inside the margins.  The colors and height can be\n"
"+++selected.\n"
"+Image - Image forms can be any JPEG image, and thus can represent\n"
"+++custom-printed forms such as checks or letterhead.  Images are\n"
"+++usually printed on Plain forms, but can be printed over a bar form\n"
"+++(typically as a logo.)\n"
"\n"
" Bar forms are specified with the form keyword:\n"
" form=name\n"
"+name is one of:\n"
"++Greenbar\n"
"++Bluebar\n"
"++Graybar\n"
"++Yellowbar\n"
"++Plain\n"
" Default: Greenbar\n"
"\n"
" The height of the bars is specifed with the bar-height keyword:\n"
" bar-height=number\n"
"+Specifies the height of the bars printed on bar forms.\n"
"+Typical values are:\n"
"++0.5in     - 3 print lines per bar at 6lpi, 4 lines at 8lpi\n"
"++0.16667in - 1 line per bar at 6 lpi\n"
"++0.125in   - 1 line per bar at 8 lpi\n"
"++0.333in   - 2 lines per bar at 6 lpi\n"
"++0.25in    - 2 lines per bar at 8 lpi\n"
" Default: 0.5in\n"
"\n"
" Image forms are specifed with the image keyword:\n"
" image=filename\n"
"+If the filename contains spaces, uses quotes around the name.\n"
"+The file must be a JPEG (JPG, not JPEG) file that will be used as a\n"
"+custom background for each page.  This can be any .jpg image, but\n"
"+is intended for scanned images of custom forms.\n"
"\n"
"+The image should have the same aspect ratio as the area within the\n"
"+margins.  It will be stretched until it fits between the horizonta\n"
"+margins.  It will be centered vertically.  Nothing is done if the\n"
"+result doesn't fill or extends over the space between the top and\n"
"+bottom margins.  When an image is used, it is rendered over the\n"
"+form and under the text.  For just the image, specify the PLAIN\n"
"+form.\n"
" Default: none (no image)\n"
"4 Printing area\n"
" The format of the printing area is controlled by these parameters.\n"
" The printing area is strictly between the left and right margins.\n"
" It is normally between the top and bottom margins, but the software\n"
" on the emulated machine may print in the top and bottom margins.\n"
"\n"
" TOF-OFFSET\n"
"+Specifies how the form is positioned with respect to the top of page.\n"
"+<FF> (form-feed), or for some printers, VFU alignment, moves the\n"
"+print (head,drum,chain) the paper to a fixed line.  The TOF-OFFSET\n"
"+specifies the line on the paper on which the first line will be printed.\n"
"\n"
"+This is analogous to what happens with a real printer.  The operator\n"
"+presses Top-of-Form, and the printer goes to a fixed line.  Then the\n"
"+operator places the paper on the (usually tractor) feed mechanism so\n"
"+that output will be in the right place.  The TOF-OFFSET is the line\n"
"+on the form that the operator would place under the print head.\n"
" Default: The line after the top margin.  For 6LPI, this is line 7\n"
"++with the default top margin.\n"
"\n"
" columns=integer\n"
"+Specifies the number of columns per line.  This is used to center the\n"
"+print area on the form.  This is analogous to sliding the tractors\n"
"+left and right to center the form; however SimH does the math.\n"
" Default: 132\n"
"\n"
" cpi=number\n"
"+Specifies the character pitch in characters per inch.  \n"
" Default: 10\n"
"\n"
" lpi=integer\n"
"+Specifies the vertical pitch in lines per inch.  6 & 8\n"
"+are currently supported.\n"
" Default: 6\n"
"\n"
"4 Page Dimensions\n"
" A page has a width, length and margins.  Use these parameters to specify\n"
" any non-default values:\n"
"\n"
" width=number\n"
"+Specifies the width of the paper, inclusive of all margins.\n"
" Default: 14.875in\n"
"\n"
" length=number\n"
"+Specifies the length of the paper, inclusive of all margins.  Superseded\n"
"+by lpp if both are specified.\n"
" Default: 11in\n"
"\n"
" lpp=integer\n"
"+Specified the length of the page in lines.  Supersedes length if both are\n"
"+specified, as length is lpp * lpi.  VFUs may set this automatically to\n"
"+match the tape length.\n"
"\n"
" top-margin=number\n"
"+Specifies height of the top margin on the paper.  For 'bar' forms, this\n"
"+is where the first bar starts.  For image forms, this is the top of\n"
"+the image.\n"
" Default: 1.0in\n"
"\n"
" bottom-margin=number\n"
"+Specifies height of the bottom margin on the paper.  For 'bar' forms,\n"
"+this is where the last bar ends.  For image forms, this is the bottom\n"
"+of the image.\n"
" Default: 0.5in\n"
"\n"
" side-margin=number\n"
"+Specifies the width of the feed mechanism margin.  This is where the\n"
"+tractor-feed holes are placed.\n"
" Default: 0.47in\n"
"\n"
" number-width=number\n"
"+Specifies the width of the column where line numbers are printed.\n"
"+Specify 0 to omit the line numbers.\n"
" Default: 0.1in\n"
"4 Other parameters\n"
" title=string\n"
"+Specifies the title for the .PDF document.  This does NOT print on\n"
"+the page, rather it is the title for the window that a PDF viewer\n"
"+might present.\n"
" Default: \"Lineprinter data\"\n"
"\n"
" font=string\n"
"+Specifies the font used for lineprinter output on the page.  Only the\n"
"+PDF built-in fonts may be used, and of those, only the monospaced fonts\n"
"+make any sense.  There is little reason to change this.\n"
"\n"
"+The fonts are not embedded in the .PDF file (due to licensing concerns).\n"
"+More options may be available in the future.\n"
" Default: Courier\n"
"2 Spooling\n"
" SimH provides the infrastructure for spooling files to physical\n"
" printers or other processes.  To enable spooling, use the -S switch\n"
" on the ATTACH command.  -S implies -R.\n"
"\n"
" When spooling is enabled, the output files should be put into a\n"
" dedicated directory.  The filename specified on the ATTACH command is\n"
" used as a base to generate the names for each spooled session.\n"
"\n"
" A spooled session starts when you ATTACH a file to the %D device.  It\n"
" ends when you DETACH the device, or when you issue the PRINT command\n"
" to SIMH.  PRINT is also available as ^P on the remote console.  During\n"
" a session, the file is locked for exclusive use by the operating\n"
" system under which SimH runs.  This means that no other process can\n"
" access it.\n"
"\n"
" SimH does not know when a 'print job' begins or ends.  Thus the\n"
" operator of SimH must, use knowledge of the emulated system, commands\n"
" to the system and/or information from the system to decide when to end\n"
" a session.\n"
"\n"
" When a session ends, the current ouput file is closed, and a new one\n"
" is created.  The new one will contain a sequential number just before\n"
" the file extension.  (These numbers do not go on forever; the\n"
" eventually wrap.)\n"
"\n"
" When the file is closed, an external process can access the file. That\n"
" process can launch a viewer, send the file to a printer, or do\n"
" anything else.  One implementation is provided as an example, but is\n"
" not supported by SimH.  This watcher (PDFWatcher) uses the Windows OS\n"
" to monitor a directory and detect files that are closed.  It then\n"
" launches a command (typically a renderer/printer) and if the command\n"
" succeeds, deletes the file.\n"
"\n"
" Similar programs can be created for other Operating Systems.\n"
"3 PDF Watcher\n"
" The pdfwatcher.c sample, NOT supported by the SimH team is provided\n"
" as an example of how one can implement the system-specific parts of\n"
" print spooling.\n"
"\n"
" pdfwatcher --help is the definitive help, but this summary illustrates\n"
" the capabilities:\n"
"\n"
" Launch pdfwatcher in a command window - it can be minimized, as\n"
" pdfwatcher normally generates no output.\n"
"\n"
" The command is:\n"
"+pdfwatcher directory command\n"
" In the command, %%s indicates where the filename is to be placed.\n"
"\n"
" To have pdfwatcher print all files spooled to a physical printer,\n"
" the command might look like (this is one long line):\n"
"+pdfwatcher spool \n"
"++\"C:\\Program Files (x86)\\Adobe\\Reader 11.0\\Reader\\AcroRd32.exe\" \n"
"++/n /h /s /t %%s \"My Printer\" \"HPWJ65N3.GPD\" \"HP_printer_port\"\n"
" The second, third and fourth arguments to AcroRD32 are the windows\n"
" printer name, printer driver name, and printer port name.  These can\n"
" be obtained from the Control Panel Properties page for the printer.\n"
"\n"
" Other PDF readers, including Acrobat, ghostscript and Foxit, can be\n"
" substituted for AcroRd32.  Consult their documentation for the\n"
" required command syntax.\n"
;

/* Please leave the help as the last thing in this file, as this
 * makes editing it much easier.
 */
