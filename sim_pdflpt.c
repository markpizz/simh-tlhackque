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

/* Context beyond the UNIT */

typedef struct {
    PDF_HANDLE pdfh;
    uint32 flags;
    void  (*io_flush)(UNIT *up);
    size_t bc;
    char buffer[256];
} PCTX;

/* Establish PDF context, and if first use, hook the
 * io_flush function and save the unit flags.
 */

#define pdfctx ((PCTX *)uptr->up8)
#define LPTVOID
#define SETCTX(nomem) {                                 \
    if (!pdfctx) {                                      \
        uptr->up8 = (PCTX *) calloc (1, sizeof (PCTX)); \
        if (!pdfctx)                                    \
            return nomem;                               \
        pdfctx->flags = uptr->flags;                    \
        pdfctx->io_flush = uptr->io_flush;              \
        uptr->io_flush = &pdflpt_flush;                 \
    } }

#define pdf (pdfctx->pdfh)

/* attach_unit replacement
 * drop-in, except that the user may qualify the filename with parameters.
 * see the description in sim_pdflpt.h.
 */

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
        if (pdfctx->flags & UNIT_SEQ) {
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
                    printf ("%s: is not a PDF file\n");
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
        return SCPE_ARG;
    }

    if (reason != PDF_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, fn);
        }
        pdf_close (pdf);
        return SCPE_ARG;
    }

    /* Go back thru the attributes and apply any to the handle */

    while (cptr < fn ) {
        double arg;

        cptr = get_glyph (cptr, gbuf, '=');
        if (!strncmp (gbuf, "FONT", strlen (gbuf)) ||
            !strncmp (gbuf, "TITLE", strlen (gbuf)) ||
            !strncmp (gbuf, "IMAGE", strlen (gbuf))) {
            cptr = get_glyph_quoted (cptr, vbuf, 0);
        } else {
            cptr = get_glyph (cptr, vbuf, 0);
        }

        /* Special cases first.  Keywords: */
        if (!strcmp (gbuf, "FORM")) {
            reason = pdf_set (pdf, PDF_FORM_TYPE, vbuf);
            if (reason != PDF_OK) {
                pdf_close (pdf);
                pdf = NULL;
                if (!sim_quiet) {
                    pdf_perror(pdf, vbuf);
                }
                return SCPE_ARG;
            }
            continue;
        }
        /* Filename */
        if (!strcmp (gbuf, "IMAGE")) {
            reason = pdf_set (pdf, PDF_FORM_IMAGE, vbuf);
            continue;
        }
        /* Text font */
        if (!strcmp (gbuf, "FONT")) {
            reason = pdf_set (pdf, PDF_TEXT_FONT, vbuf);
            continue;
        }
         /* document title */
        if (!strcmp (gbuf, "TITLE")) {
            reason = pdf_set (pdf, PDF_TITLE, vbuf);
            continue;
        }
        /* The rest are numeric */
        arg = strtod (vbuf, &p);
        if (p == vbuf) {
            pdf_close (pdf);
            pdf = NULL;
            return SCPE_ARG;
        }
        if (!strcmp (p, "cm")) {
            arg /= 2.54;
        } else {
            if (!strcmp (p, "mm")) {
                arg /= 25.4;
            } else if (*p && strcmp (p, "in")) {
                pdf_close (pdf);
                pdf = NULL;
               return SCPE_ARG;
            }
        }
        if (!strncmp (gbuf, "TOP-MARGIN", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_TOP_MARGIN, arg);
        } else if (!strncmp (gbuf, "BOTTOM-MARGIN", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_BOTTOM_MARGIN, arg);
        } else if (!strncmp (gbuf, "SIDE-MARGIN", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_SIDE_MARGIN, arg);
        } else if (!strncmp (gbuf, "NUMBER-WIDTH", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_LNO_WIDTH, arg);
        } else if (!strncmp (gbuf, "CPI", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_CPI, arg);
        } else if (!strncmp (gbuf, "LPI", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_LPI, arg);
        } else if (!strncmp (gbuf, "WIDTH", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_PAGE_WIDTH, arg);
        } else if (!strncmp (gbuf, "LENGTH", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_PAGE_LENGTH, arg);
        } else if (!strncmp (gbuf, "COLUMNS", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_COLS, arg);
        } else if (!strncmp (gbuf, "TOF-OFFSET", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_TOF_OFFSET, arg);
        } else if (!strncmp (gbuf, "BAR-HEIGHT", strlen (gbuf))) {
            reason = pdf_set (pdf, PDF_BAR_HEIGHT, arg);
        } else {
            pdf_close (pdf);
            pdf = NULL;
            return SCPE_ARG;
        }
        if (reason != SCPE_OK) {
            if (!sim_quiet) {
                pdf_perror (pdf, gbuf);
            }
            pdf_close (pdf);
            return SCPE_ARG;
        }
        continue;
    }

    /* Check for composite errors */

    reason = pdf_print (pdf, "", 0);
    if (reason != SCPE_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, gbuf);
        }
        pdf_close (pdf);
        return SCPE_ARG;
    }

    /* Give I/O a clean start */

    pdf_clearerr (pdf);

    /* Why does attach_unit not use strlen(name) +1 rather than CBUFSIZE? */
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

    if (pdf == NULL ) {
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

    if (r != PDF_OK) {
        if (!sim_quiet) {
            pdf_perror (pdf, uptr->filename);
        }
        r = SCPE_NOATT;
    } else if (pdflpt_getmode (uptr) == PDFLPT_IS_PDF) {
        if (!sim_quiet) {
            printf ( "Closed %s, on page %u\n", uptr->filename, page );
        }
    }

    pdf = NULL;

    if (pdfctx->flags & UNIT_SEQ) {
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

    SETCTX (-1);
    
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

    SETCTX (0);

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
