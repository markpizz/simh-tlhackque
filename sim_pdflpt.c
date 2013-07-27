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

#include <sim_defs.h>
#include <sim_pdflpt.h>

#define DIM(x) (sizeof (x) / sizeof ((x)[0]))

/* Time (seconds) of idleness before data flushed to attached file. */

#ifndef PDFLPT_IDLE_TIME
#define PDFLPT_IDLE_TIME (10)
#endif

/* Context beyond the UNIT */

typedef struct {
    PDF_HANDLE pdfh;
    uint32 uflags;
    void  (*io_flush)(UNIT *up);
    t_stat (*reset) (DEVICE *dp);
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

    /* Hook io_flush function */

    ctx->io_flush = uptr->io_flush;
    uptr->io_flush = &pdflpt_flush;

    /* Hook device reset function */

    dptr = find_dev_from_unit (uptr);
    if (dptr != NULL) {
        ctx->reset = dptr->reset;
        dptr->reset = &reset;
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

/* attach_unit replacement
 * drop-in, except that the user may qualify the filename with parameters.
 * see the description in sim_pdflpt.h.
 */

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
} ARG;
static const ARG argtable[] = {
    SET (BAR-HEIGHT,    BAR_HEIGHT,     NUMBER)
    SET (BOTTOM-MARGIN, BOTTOM_MARGIN,  NUMBER)
    SET (COLUMNS,       COLS,           INTEGER)
    SET (CPI,           CPI,            NUMBER)
    SET (FONT,          TEXT_FONT,      QSTRING)
    SET (FORM,          FORM_TYPE,      STRING)
    SET (IMAGE,         FORM_IMAGE,     QSTRING)
    SET (LENGTH,        PAGE_LENGTH,    NUMBER)
   XSET (LFONT,         LABEL_FONT,     QSTRING)
    SET (NUMBER-WIDTH,  LNO_WIDTH,      NUMBER)
    SET (LPI,           LPI,            INTEGER)
   XSET (NFONT,         LNO_FONT,       QSTRING)
   XSET (REQUIRE,       FILE_REQUIRE,   STRING)
    SET (SIDE-MARGIN,   SIDE_MARGIN,    NUMBER)
    SET (TITLE,         TITLE,          QSTRING)
    SET (TOF-OFFSET,    TOF_OFFSET,     INTEGER)
    SET (TOP-MARGIN,    TOP_MARGIN,     NUMBER)
    SET (WIDTH,         PAGE_WIDTH,     NUMBER)
};

t_stat pdflpt_attach (UNIT *uptr, char *cptr) {
    DEVICE *dptr;
    t_stat reason;
    char *p, *fn;
    char gbuf[CBUFSIZE], vbuf[CBUFSIZE];
    size_t page, line;

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

    p = match_ext (cptr, "PDF");
    if (!p) {
        if (pdfctx->uflags & UNIT_SEQ) {
            uptr->flags |= UNIT_SEQ;
        }
        reason = attach_unit (uptr, cptr);
        if (reason == SCPE_OK) {
            sim_fseek (uptr->fileref, 0, SEEK_END);
            uptr->pos = (t_addr)sim_ftell (uptr->fileref);
        }
        return reason;
    }

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

    pdf = pdf_open (fn);
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

    while (cptr < fn ) {
        size_t k;

        reason = SCPE_ARG;

        cptr = get_glyph (cptr, gbuf, '=');

        for (k = 0; k < DIM (argtable); k++) {
            double arg;
            long iarg;
            char *ep;

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
            if (argtable[k].atype == AT_QSTRING) {
                cptr = get_glyph_quoted (cptr, vbuf, 0);
                if (*p == '"' || *p == '\'') {
                    if (p[strlen (p)-1] == *p) {
                        *p++;
                        p[strlen (p)-1] = '\0';
                    }
                }
            } else {
                cptr = get_glyph (cptr, vbuf, 0);
            }

            switch (argtable[k].atype) {
            case AT_QSTRING:
            case AT_STRING:
                reason = pdf_set (pdf, argtable[k].arg, p);
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
                reason = pdf_set (pdf, argtable[k].arg, arg);
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
                reason = pdf_set (pdf, argtable[k].arg, arg);
                break;

            default:
                return SCPE_ARG;
            } /* switch (argtype) */
            break;
        } /* for (argtable) */
        if (reason != SCPE_OK) {
            if (!sim_quiet) {
                if ( k < DIM (argtable)) {
                    if (pdf_error (pdf) != PDF_OK) {
                        pdf_perror (pdf, gbuf);
                    }
                } else {
                    printf ("Unknown parameter %s\n", gbuf);
                }
            }
            pdf_close (pdf);
            pdf = NULL;
            return SCPE_ARG;
        }
    } /* while (cptr < fn) */

    /* Check for composite errors */

    reason = pdf_print (pdf, "", 0);
    if (reason != SCPE_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, gbuf);
        }
        pdf_close (pdf);
        pdf = NULL;
        return SCPE_ARG;
    }

    /* Give I/O a clean start */

    pdf_clearerr (pdf);

    /* Why does attach_unit use CBUFSIZE rather than strlen(name) +1? */
    uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char));
    if (uptr->filename == NULL) {
        pdf_close (pdf);
        pdf = NULL;
        return SCPE_MEM;
    }
    strncpy (uptr->filename, cptr, CBUFSIZE);

    pdf_where (pdf, &page, &line);

    if (!sim_quiet) {
        if (dptr->numunits >1) {
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

    /* Declare this not a SEQUENTIAL unit so scp won't do random seeks. */

    uptr->flags &= ~UNIT_SEQ;
    uptr->flags = uptr->flags | UNIT_ATT;

    /* PDF files can't be written to randomly, expose page number
     * to show progress (but save/restore won't work.(
     */
    uptr->pos = page;

    return SCPE_OK;
}

/* detach_unit replacement
 * drop-in.
 */

t_stat pdflpt_detach (UNIT *uptr) {
    DEVICE *dptr;
    int r;
    size_t page;

    if (uptr == NULL)
        return SCPE_IERR;

    SETCTX (SCPE_MEM);

    pdflpt_reset (uptr);

    if (!pdf) {
        return detach_unit (uptr);
    }

    if (!(uptr->flags & UNIT_ATTABLE)) {
        return SCPE_NOATT;
    }

    if (sim_switches & SIM_SW_REST) {
        return SCPE_NOATT;
    }

    if (!(uptr->flags & UNIT_ATT)) {
        return SCPE_NOATT;
    }

    if ((dptr = find_dev_from_unit (uptr)) == NULL)
        return SCPE_OK;

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

    if (pdfctx->uflags & UNIT_SEQ) {
        uptr->flags |= UNIT_SEQ;
    }
    uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_RO);
    free (uptr->filename);
    uptr->filename = NULL;

    return SCPE_OK;
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
