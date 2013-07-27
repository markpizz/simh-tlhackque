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

/* Emulated device-independent interface to lpt2pdf for SimH
 *
 * Resources:
 *   UNIT up8 is used for PDF context. A printer that uses it will have
 *   to relocate to another spot.
 *   
 * This is (except for attach) a fairly thin wrapper around lpt2pdf's API.
 * It allows a printer simulator to output either text (as always) or 
 * PDF files.  In the latter case it gets the look and behavior of real paper.
 *
 * There is a lot of documentation.  But typical use (user and developer)
 * is actually quite simple.  Don't get intimidated by the detail.
 *
 * The choice is made by the user at ATTACH time.  The simulated device need
 * only replace its attach/detach and output functions with those provided here.
 *
 * The output functions replace fputc, fputs and fwrite - though the argument order
 * matches simh conventions.  The only special characters (at present) that cause
 * pdf processing are:
 *   o <FF> \f  - Advance to the next page
 *   o <LF> \n  - Print the current line.  (This also resets the active position to column 1.)
 *   o <CR> \r  - Resets the active position to column 1.  (Overstrike works.)
 * Future versions MAY interpret some ANSI escape sequences, so <ESC> \033
 * is also reserved.  (This might enable access to additional characters.)
 *
 * The device emulator (still) is responsible for emulating
 * vertical tab, carriage control tapes, and any other functions.
 *
 * For updating the file position, a function is provided that is analogous to
 * ftell - but it returns a page number, not a file position.  File position is
 * meaningless in a .pdf file, and is not exported.  The line number is also
 * available if the device wants to provide detailed progress.
 *
 * ATTACH can be as simple as ATTACH LPT0 file.pdf.  But if one wants non-default
 * forms, pitch, etc, it gets a bit longer.  ATTACH LPT0 FORM=BLUEBAR file.pdf.
 *
 * Most devices don't do much with errors, but there are also functions for 
 * getting the last PDF error, turning error codes into strings, etc.
 *
 * Unlike text files, .PDF files can not be "tail"ed while being written.  DETACH is the
 * only way to get a consistent, readable file.  ATTACH wil render it unreadable
 * (by PDF readers).  The file is automaticaly checkpointed a short time after the 
 * printer becomes idle.  However, (if you're lucky) your OS will have an update
 * lock on it to prevent the reader from getting confused.
 *
 * Devices using sim_pdflpt automatically seek to EOF on attach, and flush their
 * file buffers buffer flush when the device is inactive, even when writing text
 * files.  "Inactive" means that the device is at column 0 (has just output a <CR>,
 * <LF> or <FF> and an idle timer has expired.  The timer is defaulted to 10
 * seconds, but pdflpt_set_idle_timeout can be used as a SET command action
 * function if it is necessary to provide user control of the value.  Minimum is 1
 * second.
 * 
 */

/* Updating your device:
 *
 * There are two methods: The simple method and the advanced method.
 *
 * A) Simple method:
 *    If your device follows the standard template and is not creative, the upgrade
 *    requires 4 lines of code, two at the top of your module, AFTER the other #includes:
 *       #define PDFLPT_REPLACE_STDIO
 *       #include "sim_pdflpt.h"
 *    and two in the help function:  (What, no help function?  You're done!)
 *        fprintf (st, "If the disk file is named .pdf, the output will be in PDF format on greenbar paper.\n");
 *        fprintf (st, "The PDF output is customizable; see the general documentation.\n");
 *    You also need to add sim_pdflpt.c and lpt2pdf.c to your machine's Makefile (or equivalent).
 *
 *    This method requires that the only use of <stdio.h> functions is for writing to your device; that the
 *    device doesn't uses fprintf (note how HELP does), and that the unit pointer has the default name "uptr"
 *    and is in scope where the IO functions are used.  It also requires that the unit have attach AND detach
 *    functions that call attach_unit and detach_unit respectively.
 *
 * B) Advanced method:
 *    If you have the time, your code will be more maintainable if you use this method (redefining standard
 *    library functions is ugly, and potentially fattening.)  Or if you don't meet the requirements for the
 *    Simple method.  This will always work:
 *
 *    #include "sim_pdflpt.h"
 *
 *    Make sure that sim_pdflpt.c and lpt2pdf.c are in your Makefile or equivalent.
 *
 *    point your device ATTACH & DETACH functions to pdflpt_attach/pdflpt_detach.  (If they are more
 *    involved than attach_unit and detach_unit, replace the calls in the custom functions.)  The
 *    device MUST have BOTH attach and detach functions.
 *
 *    Find the functions that write to the output file, and replace them with the
 *    corresponding call(s) to pdflpt_putc, pdflpt_puts and/or pdflpt_write.  If there is an opportunity
 *    to output more than one character per call (pdflpt_puts or pdflpt_write), it is advisable
 *    to use it.  The PDF API is optimized for handling longer amounts of output.  It will internally
 *    buffer calls to pdflpt_fputc, but not to pdflpt_puts or pdflpt_write.
 *
 *    Replace any calls to update the file position (typically, unit.pos = ftell (unit.fileref)
 *    with a call to pdflpt_where.  Be aware that the result is NOT seekable in PDF output.
 *
 *    Replace (or remove) any other references to unit.fileref.  flush can be replaced by pdflpt_flush.
 *    fseek() is unnecessary - but if you need it for some reason, call pdflpt_getmode and skip it for
 *    PDF files.  pdflpt_attach will position non-PDF files at EOF for you.
 *
 *    If you call sim_cancel to deactivate a unit other than in the device reset function
 *    or attach function, (e.g. in a device init caused by a register write), you may need
 *    to call pdflpt_reset to stop the idle timer.
 *
 *    Update the device help.
 *
 *    If you don't do anything special with errors (e.g. you just reflect them as SCPE_IOERR),
 *    you're done.  If you do, look at the error functions.  The error codes are defined in
 *    lpt2pdf.h.
 *
 * - And, of course whichever method you choose, test!
 */


#ifndef SIM_PDFLPT_H_
#define SIM_PDFLPT_H_  0

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include "lpt2pdf.h"
#include "sim_defs.h"


/* Replacements for attach_unit and detach_unit that handle PDF magic.
 * These defer to the standard functions if pdf output is not selected.
 */

/* Attach function for LPTs
 *
 * If the supplied filename ends with .pdf, the data will, barring errors,
 * be converted to PDF.  Otherwise, it's treated as a standard attach, and
 * output will be whatever the device supplies - usually raw ascii.
 *
 * To provide the PDF parameters without adding set and show commands, they
 * are encoded in the filename string, before the filename.  The syntax is:
 *      attach -switches unit par=value par=value ... filespec.pdf
 * No parameters or switches are required.
 *
 * PDF mode switches:
 *   -N - A new file is created, or an existing one overwritten
 *   -E - File must exist (and be a .pdf file)
 *   -R - If the file exists, must be empty
 *   None - If the file exists, data will be appended.  If not, will be created.
 *
 * Note that a .pdf file must be properly closed; a simulator crash will
 * likely corrupt a file.
 *
 * Parameters:     Default
 *   Parameter      Value       Description
 *   FORM         GREENBAR      Form to be printed: PLAIN, GREENBAR, BLUEBAR, YELLOWBAR, GRAYBAR
 *   IMAGE                      .JPG file printed instead of FORM.
 *   TITLE    Lineprinter data  Title of PDF document
 *   FONT     Courier           Font for text; must be PDF standard, monospace.
 *   TOP-MARGIN     1.0in       Space above first bar; start of normal print area
 *   BOTTOM-MARGIN  0.5in       Space below last bar; end of normal print area
 *   SIDE-MARGIN    0.47in      Space on either side of print area (rarely changed)
 *   NUMBER-WIDTH   0.1in       Width of line numbers (rarely changed; 0 to suppress)
 *   CPI            10          Characters per inch (horizontal pitch)
 *   LPI             6          Lines per inch (6 or 8)
 *   WIDTH        14.875in      All-inclusive width of paper
 *   LENGTH       11.0in        All-inclusive length of each sheet.
 *   COLUMNS       132          Number of columns printed (Centered within margins)
 *   TOF-OFFSET     lines       Line number of first line printed after <FF>.
 *                              Default: print after TOP-MARGIN (varies with LPI)
 *                              This is where line 1 of a carriage control tape would be
 *                              positioned by an operator.
 *   BAR-HEIGHT     0.5in       Height of bars.  Other values: 0.16667 for 1 line bars at
 *                              6 LPI, 0.125 for 1 line bars at 8 LPI.  Min 1 line.
 *
 * All items specified as "in" can be entered in mm or cm by appending mm or cm to the value.
 */

t_stat pdflpt_attach (UNIT *uptr, char *cptr);
t_stat pdflpt_detach (UNIT *uptr);

/* Information */

/* pdflpt_getmode
 *
 * To determine if a device is writing text or pdf, call this function.
 *
 * Generally, this shouldn't be necessary, unless the device is using seeks on
 * text files or you want a SHOW command to display the PDF/non-PDF status.
 */

int pdflpt_getmode (UNIT *uptr);
#define PDFLPT_INACTIVE    (-1)
#define PDFLPT_IS_TEXT      (0)
#define PDFLPT_IS_PDF       (1)

/* pdflpt_get_formlist
 * Returns a NULL-terminated list of the supported form names, and optionally it's length.
 */

const char *const *pdflpt_get_formlist ( size_t *length );

/* pdflpt_get_formlist
 * Returns a NULL-terminated list of the supported font names, and optionally it's length.
 */

const char *const *pdflpt_get_fontlist ( size_t *length );

/* Replacements for fputc, fputs and fwrite that handle PDF magic
 * 
 * These have the same semantics as the <stdio.h> and <string.h> functions
 * that they are named after, except that you supply a UNIT pointer rather
 * than a FILE pointer.  Also, for consistency with the rest of simh, the
 * UNIT pointer is always the first argument.
 */

int pdflpt_putc (UNIT *uptr, int c);

int pdflpt_puts (UNIT *uptr, const char *s) ;

size_t pdflpt_write (UNIT *uptr, void *ptr, size_t size, size_t nmemb);

/* Replacement for ftell for setting file postion
 *
 * For an open text file, returns the file position (just like ftell).
 *
 * For a PDF file, the result is NOT seekable.  The return value is the
 * page number in the file.  PDF files are not seekable, but this gives
 * some idea of the file position.
 *
 * For more detail, include the line argument; it will return the next line
 * to be printed.
 */

t_addr pdflpt_where (UNIT *uptr, size_t *line);

/* Error library
 * 
 * These replace the corresponding standard functions, but handle PDF stream
 * error codes as well as the system error codes. (That strerror() supports.)
 */

int pdflpt_error (UNIT *uptr);

const char *pdflpt_strerror (int errnum) ;

void pdflpt_perror (UNIT *uptr, const char *s);

void pdflpt_clearerr (UNIT *uptr);

/* Flush / checkpoint
 *
 * For text files, does a fflush()
 *
 * For PDF files, writes the metadata that makes a PDF file readable as well.
 * The file will be in a consistent state until the next write, although the 
 * OS (hopefully) will have a lock on the file to prevent readers from seeing that.
 */

void pdflpt_flush (UNIT *uptr);

/* If a device can not be found from its unit pointer, the reset
 * function should call pdflpt_reset to stop the idle timer.
 */

void pdflpt_reset (UNIT *uptr);

/* Obtain a snapshot of an active file.  (Creates a consistent copy in a new file.)
 * The copy may not be complete as the last page may not have been rendered.
 */

int pdflpt_snapshot (UNIT *uptr, const char *filename);
#define PDFLPT_OK (0)

/* For the daringly lazy:
 *
 * These are the macros that implement the 'Simple' method of upgrading devices.
 */

#ifdef  PDFLPT_REPLACE_STDIO
/* Some of these are sometimes implmented as macros by some RTLs. */
#undef fputc
#undef fputs
#undef fwrite
#undef ftell
#undef ferror
#undef clearerr
#define attach_unit pdflpt_attach
#define detach_unit pdflpt_detach
#define fputc(c, stream) pdflpt_putc (uptr, c)
#define fputs(s, stream) pdflpt_puts (uptr, s)
#define fwrite(ptr, size, nmemb, stream) pdflpt_write (uptr, ptr, size, nmemb)
#define ftell(stream) pdflpt_where (uptr, NULL)
#define ferror(stream) pdflpt_error (uptr)
#define strerror pdflpt_strerror
#define perror(s) pdflpt_perror (uptr, s)
#define clearerr(stream) pdflpt_clearerr (uptr)
#endif

#endif
