/* pdp11_lp.c: PDP-11 line printer simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   lpt          LP11 line printer

   19-Jan-07    RMS     Added UNIT_TEXT flag
   07-Jul-05    RMS     Removed extraneous externs
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   29-Sep-02    RMS     Added vector change/display support
                        New data structures
   30-May-02    RMS     Widened POS to 32b
   06-Jan-02    RMS     Added enable/disable support
   09-Nov-01    RMS     Added VAX support
   07-Sep-01    RMS     Revised interrupt mechanism
   30-Oct-00    RMS     Standardized register naming
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "LP11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define VFU_SUPPORTED
#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#include "sim_pdflpt.h"

#define DIM(x) (sizeof (x) / sizeof (x[0]))

#define LPTCSR_IMP      (CSR_ERR + CSR_DONE + CSR_IE)   /* implemented */
#define LPTCSR_RW       (CSR_IE)                        /* read/write */

#define VFU_SIZE         (143)                          /* VFU size (max lines/form) */
#define VFU_DMASK        (077)                          /* data mask per byte */
#define VFU_TOF          (0)                            /* top of form channel */
#define VFU_BOF          (11)                           /* bottom of form channel */
#define VFU_MAX          (11)                           /* max channel number */
#define MIN_VFU_LEN      (2)                            /* minimum VFU length (in inches) */
#ifndef DEFAULT_LPI
#define DEFAULT_LPI      (6)
#endif
#ifndef LPT_WIDTH
#define LPT_WIDTH        (132)
#endif

extern int32 int_req[IPL_HLVL];

int32 lpt_csr = 0;                                      /* control/status */
int32 lpt_stopioe = 0;                                  /* stop on error */
static uint16 vfu[VFU_SIZE] = {0};                      /* VFU */
static uint32 vfu_length = 0;                           /* # lines in current vfu */
static uint32 last_vfu_length = 0;
static uint32 vfu_line = 0;                             /* Current line number */
#define VFU_NONE      0           /* No VFU installed */
#define VFU_READY     1           /* VFU is ready for a command, data prints */
#define VFU_LOAD_LOW  2           /* VFU loading, low 6 bits are next */
#define VFU_LOAD_HIGH 3           /* VFU loading, high 6 bits are next */
#define VFU_MOTION    4           /* VFU motion executing */
#define VFU_ERROR     5           /* VFU error: format, channel has no punch, etc. VFU load rquired. */
static uint32 vfu_state = VFU_NONE;                     /* VFU state machine */
static uint32 vfu_tab = 0;                              /* tab command for current line */
static uint32 lpi = DEFAULT_LPI;
#define LPI_SET         (0x8000)                        /* Set by OPR */
static uint32 last_lpi = 0;

#define VFU_LEN_VALID(lines, lpi) ((lines) >= ((lpi & ~LPI_SET) * MIN_VFU_LEN))
#define VFU_START_CODE (0356)
#define VFU_STOP_CODE  (0357)

/* TRUE if data is a vertical TAB command to VFU */
#define VFU_TAB_CODE(data) ((data) >= 0200 && (data) <= (0200 + VFU_MAX))
#define VFU_TAB_CHAN(data) ((data)- 0200)               /* Extract channel index (0-based) */

DEVICE lpt_dev;
t_stat lpt_rd (int32 *data, int32 PA, int32 access);
t_stat lpt_wr (int32 data, int32 PA, int32 access);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
static t_stat lpt_set_vfu (UNIT *uptr, int32 val, char *cptr, void *desc);
static void lpt_newform (UNIT *uptr);
static t_stat lpt_set_lpi (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat lpt_show_lpi (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lpt_show_vfu_state (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lpt_show_vfu (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lpt_set_tof (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat lpt_attach (UNIT *uptr, char *ptr);
t_stat lpt_detach (UNIT *uptr);
t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *lpt_description (DEVICE *dptr);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

#define IOLN_LPT        004

DIB lpt_dib = {
    IOBA_AUTO, IOLN_LPT, &lpt_rd, &lpt_wr,
    1, IVCL (LPT), VEC_AUTO, { NULL }
    };

#define UNIT_V_DUMMY (UNIT_V_UF+0)          /* For SET actions */
#define UNIT_DUMMY   (1u << UNIT_V_DUMMY)
#define UNIT_V_VFU   (UNIT_V_DUMMY+1)       /* VFU installed */
#define UNIT_VFU     (1u << UNIT_V_VFU)
#define UNIT_V_COL   (UNIT_V_VFU+1)         /* Off column zero */
#define UNIT_COL     (1u << UNIT_V_COL)

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

#ifdef VFU_SUPPORTED
#define VFU_REG 0
#else
#define VFU_REG REG_HRO
#endif
REG lpt_reg[] = {
    { GRDATAD (BUF, lpt_unit.buf, DEV_RDX, 8, 0, "LPDB data buffer") },
    { GRDATAD (CSR, lpt_csr, DEV_RDX, 16, 0, "LPCS control & status") },
    { FLDATAD (INT, IREQ (LPT), INT_V_LPT, "Interrupt pending") },
    { FLDATAD (ERR, lpt_csr, CSR_V_ERR, "Device error") },
    { FLDATAD (DONE, lpt_csr, CSR_V_DONE, "Device ready") },
    { FLDATAD (IE, lpt_csr, CSR_V_IE, "Interrupt enable") },
    { DRDATAD (POS, lpt_unit.pos, T_ADDR_W, "Output file position (bytes or page)"), PV_LEFT },
    { DRDATAD (TIME, lpt_unit.wait, 24, "Service time"), PV_LEFT },
    { FLDATAD (STOP_IOE, lpt_stopioe, 0, "Stop simulator on I/O error") },
    { DRDATAD (LPI, lpi, 8, "Vertical pitch lines-per-inch"), PV_LEFT },
    { DRDATA  (LASTLPI, last_lpi, 8), PV_LEFT| REG_HRO },
    { DRDATAD (VFUSTATE, vfu_state, 32, "VFU state"), PV_LEFT | VFU_REG },
    { GRDATAD (VFUTAB, vfu_tab, DEV_RDX, 12, 0, "VFU tab cmd"), VFU_REG },
    { GRDATAD (VFULINE, vfu_line, DEV_RDX, 8, 0, "Current VFU line"), VFU_REG },
    { GRDATAD (VFULEN, vfu_length, DEV_RDX, 8, 0, "VFU tape length (lines)"), VFU_REG },
    { BRDATAD (VFUDAT, vfu, DEV_RDX, 12, VFU_SIZE, "VFU tape CH 12..1" ), VFU_REG },
    { GRDATA (DEVADDR, lpt_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, lpt_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };
#undef VFU_REG

MTAB lpt_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL, "Interrupt vector" },
#ifdef VFU_SUPPORTED
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VFU", NULL, NULL, &lpt_show_vfu,
        NULL, "Display VFU contents" },
    { UNIT_VFU, 0, NULL, "NOVFU", &lpt_set_vfu, NULL, NULL, 
      "Standard printer" },
    { UNIT_VFU, UNIT_VFU, NULL, "VFU-INSTALLED", &lpt_set_vfu, NULL, NULL,
      "Electronic VFU installed (LP27/29 only)" },
    { MTAB_XTD|MTAB_VDV, 0, "VFU-STATUS", NULL, NULL, &lpt_show_vfu_state,
      NULL, "Display VFU status" },
    { UNIT_DUMMY, 0, NULL, "TOPOFFORM", &lpt_set_tof, NULL,
        NULL, "Align VFU to top-of-form" },
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_VALO, 0, "LPI", "LPI={6-LPI|8-LPI}  Printer lines per inch",
      &lpt_set_lpi, &lpt_show_lpi,
        NULL, "Display vertical pitch"  },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    &lpt_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS, 0,
    NULL, NULL, NULL, &lpt_help, &pdflpt_attach_help, NULL, 
    &lpt_description
    };

/* Line printer routines

   lpt_rd       I/O page read
   lpt_wr       I/O page write
   lpt_svc      process event (printer ready)
   lpt_reset    process reset
   lpt_attach   process attach
   lpt_detach   process detach
*/

t_stat lpt_rd (int32 *data, int32 PA, int32 access)
{
if ((PA & 02) == 0)                                     /* csr */
    *data = lpt_csr & LPTCSR_IMP;
else *data = 0;                                        /* buffer
                                                        * "Load only.  Data in this buffer
                                                        * can not be read.  Always reads as
                                                        * all 0s" */
return SCPE_OK;
}

t_stat lpt_wr (int32 data, int32 PA, int32 access)
{
UNIT *uptr = &lpt_unit;

if ((PA & 02) == 0) {                                   /* csr */
    if (PA & 1)
        return SCPE_OK;
    if ((data & CSR_IE) == 0)
        CLR_INT (LPT);
    else if ((lpt_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
        SET_INT (LPT);
    lpt_csr = (lpt_csr & ~LPTCSR_RW) | (data & LPTCSR_RW);
    }
else {                                                  /* buffer */
    t_bool vfuop = TRUE;

    if ((PA & 1) == 0)
        lpt_unit.buf = data & 0177;
    lpt_csr = lpt_csr & ~CSR_DONE;
    CLR_INT (LPT);

    data &= 0377;
    switch (vfu_state) {
    case VFU_NONE:
        vfuop = FALSE;
        break;

    case VFU_ERROR:
        if (data != VFU_START_CODE) {
            break;
            }
        /* Fall thru to process START */
    case VFU_READY:
        if (data < 0200) {
            vfuop = FALSE;
            break;
            }
        if (data == VFU_START_CODE) {
            vfu_state = VFU_LOAD_LOW;
            vfu_length = 0;
            break;
            }
        if (data == VFU_STOP_CODE) {
            vfu_line = 0;
            break;
            }
        if (VFU_TAB_CODE (data)) { /* If more than one code in a line, should this |=? */
            vfu_tab = 1 << VFU_TAB_CHAN (data); /* Hold for next LF */
            break;
            }
        /* 8-bit data that's not a VFU command?
         * Not clear what the HW does.  This will print the low 7 bits.
         */
        vfuop = FALSE;
        break;

    case VFU_LOAD_LOW:
        if (data == VFU_STOP_CODE) {
            if (vfu_length < 2 || !VFU_LEN_VALID (vfu_length, lpi) ) {
                vfu_state = VFU_ERROR;
                break;
                }
            vfu_state = VFU_READY;
            vfu_line = 0;
            lpt_newform (&lpt_unit);
            break;
            }
        if (vfu_length >= DIM (vfu)) {
            vfu_state = VFU_ERROR;
            break;
            }
        vfu[vfu_length] = data & 077;
        vfu_state = VFU_LOAD_HIGH;
        break;

    case VFU_LOAD_HIGH:
        vfu[vfu_length++] |= (data & 077) << 6;
        vfu_state = VFU_LOAD_LOW;
        break;

    default:
        break;
    } /* switch (vfustate) */ 

    if (vfuop) { /* Data consumed by VFU, same timing as buffering printable data */
        sim_activate_after (&lpt_unit, 1);
        return SCPE_OK;
        }

    /* Not a VFU load command */

    switch (lpt_unit.buf) { /* 7-bit data */
    case 014:               /* FF */
        if (vfu_state == VFU_READY) {
            vfu_tab = 1u << VFU_TOF; /* Treat as slew to TOF */
            }
        /* Fall into <LF> */
    case 012:
        if (vfu_tab) { /* A <VT> appeared in the line before the <LF> */
            size_t l, n;

            vfu_state = VFU_MOTION;

            if (lpt_unit.flags & UNIT_COL) {
                pdflpt_putc (&lpt_unit, '\r');
                lpt_unit.flags &= ~UNIT_COL;
                }

            /* Slew to next punch.  This can be a whole form length for a channel
             * with just one punch.  e.g. started at TOF, slew -> (next) TOF.
             */
            l = 
                n = 0;
            do {
                vfu_line = ++vfu_line % vfu_length;
                n++;
                if (vfu[vfu_line] & (1u << VFU_TOF)) {
                    pdflpt_putc (&lpt_unit, '\f'); /* Landed on TOF, FF will do */
                    n = 0;
                    }
                else {                             /* Not TOF, need another LF */
                    n++;
                    }
                } while (!(vfu[vfu_line] & vfu_tab) && (l++ < vfu_length));

            for (l = 0; l < n; l++) {
                pdflpt_putc (&lpt_unit, '\n');
            }

            if (!(vfu[vfu_line] & vfu_tab)) {
                vfu_state = VFU_ERROR;
                }
            vfu_tab = 0;
            }
        else if (vfu_length) { /* No VT or FF in line, advance VFU */
            vfu_line = ++vfu_line % vfu_length;
        }
        /* Fall into <CR> */
    case 015:
        /* State is NOVFU: <CR>, <LF>, or <FF> will be output.
         * State is VFU_MOTION: ouput done, service will delay and error check.
         * State is VFU_READY: No <VT> in line, handled as NOVFU.
         * State is VFU_ERROR: no output, ERR will be set.
         */
        sim_activate (&lpt_unit, lpt_unit.wait);
        break;

    default:
        /* Non-motion.  Simply buffer.  Spec says this takes 1 usec */
        sim_activate_after (&lpt_unit, 1);
        break;
        } /* switch (char) */
    } /* LPDB register */
return SCPE_OK;
}

/* Technically, data should go into a line buffer and be sent
 * to the printer only on paper motion or buffer full (which can be less
 * than a printed line) or at line width.  This emulation outputs
 * data as it arrives, which is wrong only in the cases that the
 * paper motion code never comes or the line is longer than the
 * printer supports.  This is a day 0 issue with this emulation.
 * As some users may have come to depend on it, for now it's a 'feature'.
 */

t_stat lpt_svc (UNIT *uptr)
{
lpt_csr = lpt_csr | CSR_ERR | CSR_DONE;
if (lpt_csr & CSR_IE)
    SET_INT (LPT);
if ((uptr->flags & UNIT_ATT) == 0)
    return IORETURN (lpt_stopioe, SCPE_UNATT);
if (vfu_state <= VFU_READY) { /* No VFU or VFU & !motion */
    switch (uptr->buf) {
    case 015:                 /* <CR> */
        if (uptr->flags & UNIT_COL) {
            pdflpt_putc (uptr, '\r');
            uptr->flags &= ~UNIT_COL;
            }
        break;

    case 012:                 /* <LF> */
    case 014:                 /* <FF> */
        if (uptr->flags & UNIT_COL) {
            pdflpt_putc (uptr, '\r');
            uptr->flags &= ~UNIT_COL;
            }
        pdflpt_putc (uptr, uptr->buf);
        break;

    default:
        pdflpt_putc (uptr, uptr->buf);
        uptr->flags |= UNIT_COL;
        break;
        }

    }
uptr->pos =  pdflpt_where (uptr, NULL);
if (pdflpt_error (uptr)) {
    pdflpt_perror (uptr, "LPT I/O error");
    pdflpt_clearerr (uptr);
    return SCPE_IOERR;
    }
if (vfu_state == VFU_MOTION) {
    vfu_state = VFU_READY;
    }
if (vfu_state != VFU_ERROR) { /* Leave error set if VFU problem */
    lpt_csr = lpt_csr & ~CSR_ERR;
    }
return SCPE_OK;
}

static t_stat lpt_set_vfu (UNIT *uptr, int32 val, char *cptr, void *desc) {
    if (cptr && *cptr) {
        return SCPE_ARG;
        }

    if (val) {
        vfu_state = VFU_ERROR;
    } else {
        vfu_state = VFU_NONE;
        vfu_length = 0;
        vfu_tab = 0;
    }
    return SCPE_OK;
}

static t_stat lpt_show_vfu_state (FILE *st, UNIT *uptr, int32 v, void *dp)
{
if (vfu_state == VFU_NONE)
    fprintf (st, "VFU not installed");
else if (vfu_state == VFU_READY)
    fprintf (st, "VFU loaded: %u lines, %.1f in", vfu_length, (((double)vfu_length)/(lpi & ~LPI_SET)));
else
    fprintf (st, "VFU not ready");

return SCPE_OK;
}

static t_stat lpt_set_lpi (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newlpi;

if (!cptr || !*cptr)
    newlpi = DEFAULT_LPI;
else if (!strcmp (cptr, "6") || !strcmp (cptr, "6-LPI"))
    newlpi = LPI_SET | 6;
else if (!strcmp (cptr, "8") || !strcmp (cptr, "8-LPI"))
    newlpi = LPI_SET | 8;
else
    return SCPE_ARG;

lpi = newlpi;
lpt_newform (uptr);

return SCPE_OK;
}

static t_stat lpt_show_lpi (FILE *st, UNIT *uptr, int32 v, void *dp)
{
fprintf (st, "%u LPI", lpi & ~ LPI_SET);

return SCPE_OK;
}

static t_stat lpt_set_tof (UNIT *uptr, int32 val, char *cptr, void *desc)
{
size_t i;

if (cptr && *cptr)
    return SCPE_ARG;

if (vfu_state == VFU_NONE || vfu_length == 0)
    return SCPE_INCOMP;

/* TOF should always be line zero.
 * In case of an unusual VFU, advance to TOF.
 */
vfu_line = 0;
for (i = 0; i < vfu_line; i++) {
    if (vfu[i] & (1u << VFU_TOF)) {
        vfu_line = i;
        break;
        }
    }

return SCPE_OK;
}

static void lpt_newform (UNIT *uptr) {
size_t i, tof;
char tbuf[sizeof ("columns=999 tof-offset=999 lpp=99999 lpi=99")];

/* Setup PDF defaults
 * Non-default columns
 * Set VFU tof-offset default to to match VFU.  The BOF
 * channel marks the end of data
 * Set lpp to match vfu length.
 * Set lpi if opr or VFU specified it.
 */

tof = 0;
for (i = 0; i < (size_t)vfu_length; i++) {
    if (vfu[i] & (1u << VFU_BOF)){
        tof = vfu_length - (i+1);
        break;
        }
    }
tbuf[0] = '\0';
if (lpi & LPI_SET) {
    sprintf (tbuf, "lpi=%u", (lpi & ~LPI_SET));
    }
if (LPT_WIDTH != 132) {
    sprintf (tbuf + strlen (tbuf), " columns=%u", LPT_WIDTH);
    }
if (tof != 0) {
    sprintf (tbuf + strlen (tbuf), " tof-offset=%d", (int)tof);
    }

i = vfu_length? vfu_length : last_vfu_length;
if (i) {
    sprintf (tbuf + strlen (tbuf), " lpp=%d", (int)i);
    
    }
pdflpt_set_defaults (uptr, tbuf);

if (pdflpt_getmode (uptr) == PDFLPT_IS_PDF) {
    tbuf[0] = '\0';
    if (vfu_length && vfu_length != last_vfu_length) {
        last_vfu_length = vfu_length;
        sprintf (tbuf, "lpp=%u", vfu_length);
        if (tof != 0) {
            sprintf (tbuf + strlen (tbuf), " tof-offset=%d", (int)tof);
            }
        }
    if ((lpi & LPI_SET) && lpi != last_lpi) {
        last_lpi = lpi;
        sprintf (tbuf + strlen (tbuf), " lpi=%u", (lpi & ~LPI_SET));
        }
    if (tbuf[0]) {
        pdflpt_newform (uptr, tbuf);
        }
    }

return;
}

t_stat lpt_reset (DEVICE *dptr)
{
UNIT *uptr = dptr->units;
if (sim_switches & SWMASK ('P')) {
    memset (vfu, 0, sizeof(vfu));
    vfu_state = VFU_NONE;
    uptr->flags &= ~UNIT_VFU;
    vfu_line = 0;
    lpi = DEFAULT_LPI;
    }
vfu_tab = 0;
uptr->buf = 0;
lpt_csr = CSR_DONE;
if ((uptr->flags & UNIT_ATT) == 0)
    lpt_csr = lpt_csr | CSR_ERR;
CLR_INT (LPT);
sim_cancel (uptr);                                 /* deactivate unit */
pdflpt_reset (uptr);
lpt_newform (uptr);
return SCPE_OK;
}

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

lpt_csr = lpt_csr & ~CSR_ERR;
reason = pdflpt_attach (uptr, cptr);
last_vfu_length = vfu_length;
last_lpi = lpi;
(void) lpt_set_tof (uptr, 0, NULL, NULL);
vfu_tab = 0;
if ((uptr->flags & UNIT_ATT) == 0)
    lpt_csr = lpt_csr | CSR_ERR;
return reason;
}

t_stat lpt_detach (UNIT *uptr)
{
lpt_csr = lpt_csr | CSR_ERR;
return pdflpt_detach (uptr);
}

static t_stat lpt_show_vfu (FILE *st, UNIT *uptr, int32 v, void *dp)
{
size_t l;
int c, sum;

if (vfu_state == VFU_NONE) {
    fprintf (st, "VFU is not installed\n");
    return SCPE_OK;
}

if (vfu_state != VFU_READY || vfu_length == 0) {
    fprintf (st, "VFU is not loaded\n");
    return SCPE_OK;
    }

fprintf (st, "VFU contains:\n"
                "     1 1 1\n"
                "line 2 1 0 9 8 7 6 5 4 3 2 1\n"
                "---- - - - - - - - - - - - -\n");
sum = 0;
for (l = 0; l < vfu_length; l++) {
    if (l && !(l % 5))
        fputc ('\n', st);
    fprintf (st, "%4d", (int)l);
    for (c = VFU_MAX; c >= 0; c--)
        fprintf (st, " %c", (vfu[l] & (1 << c))? ((c >= 9)? 'X': '1'+c) : ' ');
    fputc ('\n', st);
    sum |= vfu[l];
    }

if (!(sum & (1 << VFU_TOF))) {
    fprintf (st, "? No stop in channel %u (Top-of-Form)\n", VFU_TOF+1);
    }
if (!(sum & (1 << VFU_BOF))) {
    fprintf (st, "%% No stop in channel %u (Bottom-of-Form)\n", VFU_BOF+1);
    }

return SCPE_OK;
}

t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *defaults;
static const char helptext[] = {
" %D is emulating a %1s, a %2s device\n"
"\n"
" The LP11/LS11/LA11/LPV11 are software compatible interfaces to three\n"
" families of line printers.  They use programmed I/O; that is, each\n"
" character to be printed is moved to the device by software.\n"
" The printers respond to 7-bit ASCII\n graphics and the <CR> <LF> and\n"
" <FF> controls. On VAX, the LP27, LP29, LP37. LG01 and LG31 support\n"
" DAVFU with the VAX Vertical Forms Printing for VMS software.\n"
"\n"
" The LPV11 attaches to the Q-BUS; the others to the UNIBUS.\n"
" %3H\n"
"1 Hardware Description\n"
" The LP11 controller supports impact line printers at speeds of up to\n"
" 1250 lines per minute.  It was sold with Data Products corporation\n"
" line printers, including models 2230, 2310, 2410 and 2470 - which have\n"
" the DEC model numbers LP05 (132 columns 240/300 LPM), LP01 (80\n"
" columns, 356 LPM), LP02 (132 columns, 245 LPM) and LP04 (132 columns,\n"
" 925/1250 LPM).  The print speeds vary based on the number of 20 column\n"
" zones used, and whether the drum is 64 (upper case only) or 96 (upper\n"
" and lower case) characters.\n"
"\n"
" The LS11 supports Centronics line printers, including models 101\n"
" (5x7), 101A (9x7), 101D 102A and 303.  These are matrix printers\n"
" averaging 132 characters per second, 60 LPM for full lines.\n"
"\n"
" The LA11 supports the DEC LA180 (parallel interface) 132 columns at\n"
" 180 CPS at 70 LPM.\n"
"\n"
" For details, see the LP11/LS11/LA11 user's manual EK-LP11S-OP-001.\n"
"2 $Registers\n"
"\n"
" The POS register indicates the position in the ATTACHed file.\n"
"\n"
" For PDF files, this is the current page number.  DEPOSIT commands have\n"
" no effect.\n"
"\n"
" For other files, this is the byte position.  DEPOSIT commands may\n"
" backspace or advance the printer.\n"
"\n"
" Error handling is as follows:\n"
"\n"
"+error         STOP_IOE     processed as\n"
"+not attached    1          out of paper\n"
"+++++0          disk not ready\n"
"\n"
"+OS I/O error    x          report error and stop\n"
"2 Vertical Forms Unit\n"
" VAX Vertical Forms Printing for VMS software supports electronic\n"
" VFUs, only with the LP11/LPV11 attached to LP27 or LP29 printers.\n"
" The implementation is rather unusual.\n"
"\n"
" The processor loads characters one by one into the data buffer\n"
" register in the controller. The controller then transfers the\n"
" characters to the 132-character data buffer in the lineprinter. The\n"
" line length of 132 characters corresponds directly to this\n"
" 132-character data buffer.\n"
"\n"
" The current data buffer register (LXDB) contents are automatically\n"
" printed out whenever any of the three ASCII control codes (Carriage\n"
" Feed, Line Feed, Form Return) is recognized. The Carriage Return (CR)\n"
" code prints the line but does not advance the paper. The Line Feed\n"
" (LF) code advances the paper one line, while the Form Feed (FF) code\n"
" advances the paper to the top of the next page.\n"
"\n"
" Forms may be as long as 143 lines, and 12 different Vertical Tab Stops\n"
" (channels) are available.  Channel 1 is reserved for TopOfForm, and\n"
" channel 12 is reserved for BottomOfForm\n"
"\n"
" Start Load Command\n"
"\n"
" The Start Load command 356(8) initializes the PVFU and causes all\n"
" subsequent characters to be loaded into the PVFU buffer. The Stop Load\n"
" command 357(8) indicates the end of the characters to be loaded into\n"
" the PVFU buffer. The PVFU buffer allows storage of two characters for\n"
" each line of the form. The PVFU only uses the low-order six bits of\n"
" each character. The 12 bits (two characters) stored per line in the\n"
" PVFU correspond to the 12 Vertical Tab Stops.\n"
"\n"
" A 1 bit in a bit position in one of these two characters assigns a\n"
" Vertical Tab Stop to that line. Bits 0 through 5 of the first\n"
" characters correspond to Vertical Tab Stops 1 through 6 while bits 0\n"
" through 5 of the second character correspond to Vertical Tab Stops 7\n"
" through 12.\n"
"\n"
" Vertical Tab Stop Commands\n"
"\n"
" Vertical Tab Stops are commands 200(8) through 213(8). A Vertical Tab\n"
" Stop command sent anywhere in a line of characters causes the paper to\n"
" advance to the next PVFU line indicated by that Vertical Tab Stop\n"
" command at the next Line Feed code. Line 1 is assigned to Vertical Tab\n"
" Stop 1 and the last line of the form is assigned to Vertical Tab Stop\n"
" 12.\n"
"\n"
" Form Movement Commands\n"
"\n"
" Sending command 200(8) causes the paper to advance to the top of the\n"
" next form. Commands 201(8) through 212(8) correspond to Vertical Tab\n"
" Stops 2 through 11 and cause the paper to advance to the next line\n"
" that is loaded with that Vertical Tab Stop. Command 213(8) causes the\n"
" paper to advance to the bottom of the form.\n"
"1 Configuration\n"
"2 $Set commands\n"
" SETTOPOFFORM aligns the VFU with the top-of-form channel; usually line 1.\n"
" It does not send any data to the output file.\n"
"2 $Show commands\n"
};

if (!uptr) {
    uptr = dptr->units;
}
defaults = pdflpt_get_defaults (uptr);

return scp_help (st, dptr, uptr, flag, helptext, cptr,
                 ((UNIBUS)? "LP11": "LPV11"),
                 ((UNIBUS)? "UNIBUS": "Q-BUS"),
                  pdflpt_helptext,
                 (defaults? "T": "F"), defaults);
}

char *lpt_description (DEVICE *dptr)
{
return (UNIBUS) ? "LP11 line printer" :
                  "LPV11 line printer";
}
