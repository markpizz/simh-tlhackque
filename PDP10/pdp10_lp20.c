/* pdp10_lp20.c: PDP-10 LP20 line printer simulator

   Copyright (c) 1993-2009, Robert M Supnik

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

   lp20         line printer

   23-Jun-13    TL      Add optical VFU support and fix some inconsistencies
                        with the hardware.  Add documentation.
   29-May-13    TL      Force append when an existing file is attached.
                        Previously over-wrote file from the top.
   19-Jan-07    RMS     Added UNIT_TEXT flag
   04-Sep-05    RMS     Fixed missing return (found by Peter Schorn)
   07-Jul-05    RMS     Removed extraneous externs
   18-Mar-05    RMS     Added attached test to detach routine
   29-Dec-03    RMS     Fixed bug in scheduling
   25-Apr-03    RMS     Revised for extended file support
   29-Sep-02    RMS     Added variable vector support
                        Modified to use common Unibus routines
                        New data structures
   30-May-02    RMS     Widened POS to 32b
   06-Jan-02    RMS     Added enable/disable support
   30-Nov-01    RMS     Added extended SET/SHOW support

   References:
   EK-LP20-TM-004 LP20 LINE PRINTER SYSTEM MANUAL
   B-TC-LP20-0-1 MP0006 LP20 Field Maintenance Print Set
   DpC255137D Dataproducts Corp Maintenance Guide Vol. I
              300LPM/600 LPM Line Printers.
   LP2SER.MAC TOPS-10 Device driver
   LPKSDV.MAC TOPS-20 Device driver
   LP20.MAC   TOPS-10/20 VFU/RAM utility
   LPTSPL/LPTSUB.MAC TOPS-10/20 GALAXY spoolre
*/

#include <ctype.h>
#include "pdp10_defs.h"
#include "sim_pdflpt.h"

#define DIM(x) (sizeof (x) / sizeof (x[0]))

extern UNIT cpu_unit;

/* The LP20 has the following CSR assignments:
 * Unit No. 1: 775400, Vector: 754
 * Unit No. 2: 775420, Vector: 750
 *
 * Note that the KS only supported one LP20.
 * Note also that the vector assigned to unit 2 is lower than unit 1's.
 */

/* Define printer characteristics
 * Some had slower speed options for better print quality.
 * Doesn't seem worth simulating that.
 */
typedef struct {
    const char *name;
    const char *desc;
    size_t columns;           /* Maximum number of print columns */
    size_t chars;             /* Number of distinct characters on drum/band */
    size_t lpm;               /* Rated lines/min.  Assumes 1 line advance */
    size_t rpm;               /* Drum rotation speed */
    double firstadv;          /* usec for first line moved */
    double slew;              /* usec/in for slew */
    t_bool davfu;
#define DAVFU TRUE
#define OVFU  FALSE
} LPT;
static const LPT printers[] = {
    /* LPT macro:
     * firstadv - time to advance first line in msec
     * ips      - slew rate in inches/sec - 2nd - nth line advance
     * davfu    - printer has davfu (vs. optical)
     */
#define LPT(name, width, chars, lpm, rpm, firstadv, ips, davfu, notes)        \
    { #name, #notes, (width), (chars), (lpm), (rpm), (1000.0 * (firstadv)), (1000000.0/(ips)), (davfu) },
    /*    model                     1st adv Slew
     *    name   cols char LPM   RPM   msec ips VFU */
    LPT ( LP05A, 132,  64,  300, 1000, 41.0, 20, DAVFU, Drum )
    LPT ( LP05B, 132,  96,  240,  660, 41.0, 20, DAVFU, Drum )

    LPT ( LP07A, 132,  64, 1220,    0, 12.5, 60, DAVFU, Drum )
    LPT ( LP07B, 132,  96,  905,    0, 12.5, 60, DAVFU, Drum )

    LPT ( LP10A, 132,  64,  300,  333, 12.0, 24, OVFU, Drum )
    LPT ( LP10B, 132,  64,  600,  750, 12.0, 24, OVFU, Drum )

    LPT ( LP10C, 132,  64, 1000, 1250, 12.0, 24, OVFU, Drum )
    LPT ( LP10D, 132,  96,  600,  750, 12.0, 24, OVFU, Drum )

    LPT ( LP10E, 132, 128,  500,  550, 12.0, 24, OVFU, Drum )

    LPT ( LP10F, 132,  64, 1250, 1800, 14.0, 35, OVFU, Drum )
    LPT ( LP10H, 132,  96,  925, 1200, 14.0, 35, OVFU, Drum )

    LPT ( LP14A, 132,  64,  890, 1280, 20.0, 30, DAVFU, Drum )
    LPT ( LP14B, 132,  96,  650,  857, 20.0, 30, DAVFU, Drum )

    LPT ( LP26A, 132,  64,  600,    0, 25.0, 15, DAVFU, Band ) /* Adv unknown, estimated */
    LPT ( LP26B, 132,  96,  445,    0, 25.0, 15, DAVFU, Band ) /* Adv unknown, estimated */

    LPT ( LP27A, 132,  64, 1200,    0, 18.0, 50, DAVFU, Band ) /* Adv unknown, estimated */
    LPT ( LP27B, 132,  96,  800,    0, 18.0, 50, DAVFU, Band ) /* Adv unknown, estimated */

    /* The LP29 has 2 64-char bands: 2000 LPM with a statistical band, 1650 std
     * Dataproducts BP-2000
     */
    LPT ( LP29A, 132,  64, 1650,    0, 12.0, 50, DAVFU, Band (standard) )
    LPT ( LP29S, 132,  64, 2000,    0, 12.0, 50, DAVFU, Band (statistical) )
    LPT ( LP29B, 132,  96, 1250,    0, 12.0, 50, DAVFU, Band )
#define DEFAULT_LPT "LP29B"

    LPT ( FAST,  132, 256,10000,10000,  0.1,999, DAVFU, Emulation-only )
#undef LPT
};
#undef DAVFU
#undef OVFU

/* The real timing is a function of where the drum is, what
 * characters are selected, how may columns share a hammer..
 * We'll approximate all that with LPM - which includes 1 line
 * advance + slew time.  Results are in usec of delay.
 */
#define pch ((LPT *)uptr->up7)
#define PRTTIME ((60000000.0 / pch->lpm) - pch->firstadv)
#define ADVTIME(n) ((n) * (pch->slew / (lpi & ~LPI_SET)))
#define GO_TIME (100)

#define UNIT_DUMMY      (1 << UNIT_V_UF)
#define LP_WIDTH        132                             /* printer width */
#define DEFAULT_LPI       6                             /* default lines-per-inch of LPT */
#define LPI_SET         0x8000                          /* Set by OPR or VFU */

/* DAVFU RAM */

#define DV_SIZE         143                             /* DAVFU size */
#define DV_DMASK        077                             /* data mask per byte */
#define DV_TOF          0                               /* top of form channel */
#define DV_BOF          11                              /* bottom of form channel */
#define DV_MAX          11                              /* max channel number */
#define MIN_VFU_LEN     2                               /* minimum VFU length (in inches) */
#define VFU_LEN_VALID(lines, lpi) ((lines) >= ((lpi & ~LPI_SET) * MIN_VFU_LEN))

/* Translation RAM */

#define TX_SIZE         256                             /* translation RAM */
#define TX_AMASK        (TX_SIZE - 1)
#define TX_DMASK        007777
#define TX_PARITY       010000                          /* Parity bit (emulated: 'valid'; unwritten has bad 'parity') */
#define TX_V_FL         8                               /* flags */
#define TX_M_FL         017
/* define TX_INTR       04000                         *//* interrupt */
#define TX_DELH         02000                           /* delimiter */
/* define TX_XLAT       01000                         *//* translate */
/* define TX_DVFU       00400                         *//* DAVFU */
#define TX_SLEW         00020                           /* chan vs slew */
#define TX_VMASK        00017                           /* spacing mask */
#define TX_CHR          0                               /* states: pr char */
#define TX_RAM          1                               /* pr translation */
#define TX_DVU          2                               /* DAVFU action */
#define TX_INT          3                               /* interrupt */
#define TX_GETFL(x)     (((x) >> TX_V_FL) & TX_M_FL)

/* LPCSRA (765400) */

#define CSA_GO          0000001                         /* go */
#define CSA_PAR         0000002                         /* parity enable NI */
#define CSA_V_FNC       2                               /* function */
#define CSA_M_FNC       03
#define  FNC_PR          0                              /* print */
#define  FNC_TST         1                              /* test */
#define  FNC_DVU         2                              /* load DAVFU */
#define  FNC_RAM         3                              /* load translation RAM */
#define  FNC_INTERNAL   1                               /* internal function */
#define CSA_FNC         (CSA_M_FNC << CSA_V_FNC)
#define CSA_V_UAE       4                               /* Unibus addr extension */
#define CSA_UAE         (03 << CSA_V_UAE)
#define CSA_IE          0000100                         /* interrupt enable */
#define CSA_DONE        0000200                         /* done */
#define CSA_INIT        0000400                         /* init */
#define CSA_ECLR        0001000                         /* clear errors */
#define CSA_DELH        0002000                         /* delimiter hold */
#define CSA_ONL         0004000                         /* online */
#define CSA_DVON        0010000                         /* DAVFU online */
#define CSA_UNDF        0020000                         /* undefined char */
#define CSA_PZRO        0040000                         /* page counter zero */
#define CSA_ERR         0100000                         /* error */
#define CSA_RW          (CSA_DELH | CSA_IE | CSA_UAE | CSA_FNC | CSA_PAR | CSA_GO)
#define CSA_MBZ         (CSA_ECLR | CSA_INIT)
#define CSA_GETUAE(x)   (((x) & CSA_UAE) << (16 - CSA_V_UAE))
#define CSA_GETFNC(x)   (((x) >> CSA_V_FNC) & CSA_M_FNC)

/* LPCSRB (765402) */

#define CSB_GOE         0000001                         /* go error */
#define CSB_DTE         0000002                         /* DEM timing error NI */
#define CSB_MTE         0000004                         /* MSYN error (Ubus timeout) */
#define CSB_RPE         0000010                         /* RAM parity error */
#define CSB_MPE         0000020                         /* MEM parity error NI */
#define CSB_LPE         0000040                         /* LPT parity error NI */
#define CSB_DVOF        0000100                         /* DAVFU not ready */
#define CSB_OFFL        0000200                         /* offline */
#define CSB_TEST        0003400                         /* test mode */
#define CSB_OVFU        0004000                         /* optical VFU */
#define CSB_PBIT        0010000                         /* data parity bit NI */
#define CSB_NRDY        0020000                         /* printer error NI */
#define CSB_LA180       0040000                         /* LA180 printer NI */
#define CSB_VLD         0100000                         /* valid data NI */
#define CSB_ECLR        (CSB_GOE | CSB_DTE | CSB_MTE | CSB_RPE | CSB_MPE | CSB_LPE)
#define CSB_ERR         (CSB_ECLR | CSB_DVOF | CSB_OFFL)
#define CSB_RW          CSB_TEST
#define CSB_MBZ         (CSB_DTE | CSB_RPE | CSB_MPE | CSB_LPE | \
                         CSB_PBIT | CSB_NRDY | CSB_LA180 | CSB_VLD)

/* LPBA (765404) */

/* LPBC (765506) */

#define BC_MASK         0007777                         /* <15:12> MBZ */

/* LPPAGC (765510) */

#define PAGC_MASK       0007777                         /* <15:12> MBZ */

/* LPRDAT (765512) */

#define RDAT_MASK       0007777                         /* <15:12> MBZ */

/* LPCOLC/LPCBUF (765514) */

/* LPCSUM/LPPDAT (765516) */

extern int32 int_req;

static int32 lpcsa = 0;                                 /* control/status A */
static int32 lpcsb = CSB_DVOF;                          /* control/status B */
static int32 lpba = 0;                                  /* bus address */
static int32 lpbc = 0;                                  /* byte count */
static int32 lppagc = 0;                                /* page count */
static int32 lprdat = 0;                                /* RAM data */
static int32 lpcbuf = 0;                                /* character buffer */
static int32 lpcolc = 0;                                /* column count */
static int32 lppdat = 0;                                /* printer data */
static int32 lpcsum = 0;                                /* checksum */
static int32 dvptr = 0;                                 /* davfu pointer */
static int32 dvlnt = 0;                                 /* davfu length */
static int32 last_dvlnt = 0;                            /* Last good davfu length */
static int32 lp20_irq = 0;                              /* int request */
static int32 dvld = 0;
static int32 dvld_hold = 0;
static int32 lpi = DEFAULT_LPI;                         /* Printer's LPI. */
static int32 last_lpi = 0;
static int16 txram[TX_SIZE] = { 0 };                    /* translation RAM */
static int16 davfu[DV_SIZE] = { 0 };                    /* DAVFU */

DEVICE lp20_dev;
static t_stat lp20_rd (int32 *data, int32 pa, int32 access);
static t_stat lp20_wr (int32 data, int32 pa, int32 access);
static int32 lp20_inta (void);
static t_stat lp20_svc (UNIT *uptr);
static t_stat lp20_done (UNIT *uptr);
static t_stat lp20_reset (DEVICE *dptr);
static t_stat lp20_init (DEVICE *dptr);
static t_stat lp20_attach (UNIT *uptr, char *ptr);
static t_stat lp20_detach (UNIT *uptr);
static t_stat lp20_set_lpi (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat lp20_show_lpi (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lp20_show_printers (FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat lp20_set_unit_type (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat lp20_show_unit_type (FILE *st, UNIT *uptr, int32 v, void *dp);
static void lp20_newform (UNIT *uptr);
static t_stat lp20_set_tape (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat lp20_show_vfu_state (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lp20_show_vfu (FILE *st, UNIT *uptr, int32 v, void *dp);
static t_stat lp20_set_tof (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat lp20_clear_vfu (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_bool lp20_print (UNIT *uptr, int32 c);
static t_bool lp20_adv (UNIT *uptr, int32 c, t_bool advdvu);
static t_bool lp20_davfu (UNIT *uptr, int32 c);
static void update_lpcs (int32 flg);
static void change_rdy (int32 setrdy, int32 clrrdy);
static int16 evenbits (int16 value);
static t_stat lp20_help (FILE *st, struct sim_device *dptr,
                            struct sim_unit *uptr, int32 flag, char *cptr); 
static char *lp20_description (DEVICE *dptr); 

/* DEC standard VFU tape for 'optical' VFU default.
 * Note that this must be <= DV_SIZE as it is copied into the DAVFU.
 */
static const int16 defaultvfu[] = { /* Generated by vfu.pl per DEC HRM */
    /* 66 line page with 6 line margin */
    00377,    /* Line   0     8  7  6  5  4  3  2  1 */
    00220,    /* Line   1     8        5             */
    00224,    /* Line   2     8        5     3       */
    00230,    /* Line   3     8        5  4          */
    00224,    /* Line   4     8        5     3       */
    00220,    /* Line   5     8        5             */
    00234,    /* Line   6     8        5  4  3       */
    00220,    /* Line   7     8        5             */
    00224,    /* Line   8     8        5     3       */
    00230,    /* Line   9     8        5  4          */
    00264,    /* Line  10     8     6  5     3       */
    00220,    /* Line  11     8        5             */
    00234,    /* Line  12     8        5  4  3       */
    00220,    /* Line  13     8        5             */
    00224,    /* Line  14     8        5     3       */
    00230,    /* Line  15     8        5  4          */
    00224,    /* Line  16     8        5     3       */
    00220,    /* Line  17     8        5             */
    00234,    /* Line  18     8        5  4  3       */
    00220,    /* Line  19     8        5             */
    00364,    /* Line  20     8  7  6  5     3       */
    00230,    /* Line  21     8        5  4          */
    00224,    /* Line  22     8        5     3       */
    00220,    /* Line  23     8        5             */
    00234,    /* Line  24     8        5  4  3       */
    00220,    /* Line  25     8        5             */
    00224,    /* Line  26     8        5     3       */
    00230,    /* Line  27     8        5  4          */
    00224,    /* Line  28     8        5     3       */
    00220,    /* Line  29     8        5             */
    00276,    /* Line  30     8     6  5  4  3  2    */
    00220,    /* Line  31     8        5             */
    00224,    /* Line  32     8        5     3       */
    00230,    /* Line  33     8        5  4          */
    00224,    /* Line  34     8        5     3       */
    00220,    /* Line  35     8        5             */
    00234,    /* Line  36     8        5  4  3       */
    00220,    /* Line  37     8        5             */
    00224,    /* Line  38     8        5     3       */
    00230,    /* Line  39     8        5  4          */
    00364,    /* Line  40     8  7  6  5     3       */
    00220,    /* Line  41     8        5             */
    00234,    /* Line  42     8        5  4  3       */
    00220,    /* Line  43     8        5             */
    00224,    /* Line  44     8        5     3       */
    00230,    /* Line  45     8        5  4          */
    00224,    /* Line  46     8        5     3       */
    00220,    /* Line  47     8        5             */
    00234,    /* Line  48     8        5  4  3       */
    00220,    /* Line  49     8        5             */
    00264,    /* Line  50     8     6  5     3       */
    00230,    /* Line  51     8        5  4          */
    00224,    /* Line  52     8        5     3       */
    00220,    /* Line  53     8        5             */
    00234,    /* Line  54     8        5  4  3       */
    00220,    /* Line  55     8        5             */
    00224,    /* Line  56     8        5     3       */
    00230,    /* Line  57     8        5  4          */
    00224,    /* Line  58     8        5     3       */
    04220,    /* Line  59 12  8        5             */
    00020,    /* Line  60              5             */
    00020,    /* Line  61              5             */
    00020,    /* Line  62              5             */
    00020,    /* Line  63              5             */
    00020,    /* Line  64              5             */
    00020,    /* Line  65              5             */
};

/* LP data structures

   lp20_dev     LPT device descriptor
   lp20_unit    LPT unit descriptor
   lp20_reg     LPT register list
*/

static DIB lp20_dib = {
    IOBA_LP20, IOLN_LP20, &lp20_rd, &lp20_wr,
    1, IVCL (LP20), VEC_LP20, { &lp20_inta }
    };

static UNIT lp20_unit = {
    UDATA (&lp20_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT,
    0, 0, 0, 0, (void *)printers,
    };

static REG lp20_reg[] = {
    { ORDATA (LPCSA, lpcsa, 16) },
    { ORDATA (LPCSB, lpcsb, 16) },
    { ORDATA (LPBA, lpba, 16) },
    { ORDATA (LPBC, lpbc, 12) },
    { ORDATA (LPPAGC, lppagc, 12) },
    { ORDATA (LPRDAT, lprdat, 13) },
    { ORDATA (LPCBUF, lpcbuf, 8) },
    { ORDATA (LPCOLC, lpcolc, 8) },
    { ORDATA (LPPDAT, lppdat, 8) },
    { ORDATA (LPCSUM, lpcsum, 8) },
    { ORDATA (DVPTR, dvptr, 7) },
    { ORDATA (DVLNT, dvlnt, 7), REG_RO + REG_NZ },
    { ORDATA (DVLD, dvld, 2), REG_RO | REG_HIDDEN },
    { ORDATA (DVLDH, dvld_hold, 6), REG_RO | REG_HIDDEN },
    { FLDATA (INT, int_req, INT_V_LP20) },
    { FLDATA (IRQ, lp20_irq, 0) },
    { FLDATA (ERR, lpcsa, CSR_V_ERR) },
    { FLDATA (DONE, lpcsa, CSR_V_DONE) },
    { FLDATA (IE, lpcsa, CSR_V_IE) },
    { DRDATA (POS, lp20_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, lp20_unit.wait, 24), PV_LEFT },
    { BRDATA (TXRAM, txram, 8, 13, TX_SIZE) },
    { BRDATA (DAVFU, davfu, 8, 12, DV_SIZE) },
    { DRDATA (LPI, lpi, 8), REG_RO | REG_HIDDEN },
    { DRDATA (LASTLPI, last_lpi, 8), PV_LEFT| REG_HRO },
    { ORDATA (DEVADDR, lp20_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, lp20_dib.vec, 16), REG_HRO },
    { NULL }
    };

static MTAB lp20_mod[] = {
    { MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "VFU", NULL, NULL, &lp20_show_vfu,
        NULL, "Display VFU tape/contents" },
    { MTAB_XTD|MTAB_VDV, 0, "VFU-STATUS", NULL, NULL, &lp20_show_vfu_state,
      NULL, "Display VFU status" },
    { MTAB_XTD|MTAB_VDV, 0, "PRINTER-MODEL", "PRINTER-MODEL=modelname Set characteristics of printer",
        &lp20_set_unit_type, &lp20_show_unit_type, NULL, "Display model of printer" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MODELS", NULL, NULL, &lp20_show_printers,
        NULL, "Display printer models" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "TAPE", 
      &lp20_set_tape, NULL, NULL, "Load a custom VFU tape (optical VFU only)" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALO, 0, "LPI", "LPI={6-LPI|8-LPI}  Printer lines per inch", &lp20_set_lpi, &lp20_show_lpi,
        NULL, "Display vertical pitch"  },
    { UNIT_DUMMY, 0, NULL, "TOPOFFORM", &lp20_set_tof, NULL,
        NULL, "Align VFU to top-of-form" },
    { UNIT_DUMMY, 0, NULL, "VFUCLEAR", &lp20_clear_vfu, NULL,
        NULL, "Clear the VFU & Translation RAM" },
    { 0 }
    };

/* Debug conditions */
#define DF_REGR 00001
#define DF_REGW 00002
#define DF_TIME 00004
#define DBG(x) { #x, DF_ ## x },
static DEBTAB lp20_debug[] = {
    DBG (REGR)
    DBG (REGW)
    DBG (TIME)
    {0}
};
#undef DBG

DEVICE lp20_dev = {
    "LP20", &lp20_unit, lp20_reg, lp20_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lp20_reset,
    NULL, &lp20_attach, &lp20_detach,
    &lp20_dib, DEV_DISABLE | DEV_UBUS | DEV_DEBUG, 0,
    lp20_debug, NULL, NULL, &lp20_help, &pdflpt_attach_help, NULL, &lp20_description,
    };

/* Line printer routines

   lp20_rd      I/O page read
   lp20_wr      I/O page write
   lp20_svc     process event (printer ready)
   lp20_reset   process reset
   lp20_attach  process attach
   lp20_detach  process detach
*/

static t_stat lp20_rd (int32 *data, int32 pa, int32 access)
{
update_lpcs (0);                                        /* update csr's */
switch ((pa >> 1) & 07) {                               /* case on PA<3:1> */

    case 00:                                            /* LPCSA */
        *data = lpcsa = lpcsa & ~CSA_MBZ;
        if (lpcsb & CSB_OVFU)                           /* Optical: no DAVFU present */
            *data &= ~CSA_DVON;
        break;

    case 01:                                            /* LPCSB */
        *data = lpcsb = lpcsb & ~CSB_MBZ;
        if (lpcsb & CSB_OVFU)
            *data &= ~CSB_DVOF;
        break;

    case 02:                                            /* LPBA */
        *data = lpba;
        break;

    case 03:                                            /* LPBC */
        *data = lpbc = lpbc & BC_MASK;
        break;

    case 04:                                            /* LPPAGC */
        *data = lppagc = lppagc & PAGC_MASK;
        break;

    case 05:                                            /* LPRDAT */
        *data = lprdat & RDAT_MASK;
        if (evenbits(*data))
            *data |= TX_PARITY;
        if (((lprdat & TX_PARITY) == 0) && (lpcsa & CSA_PAR)) /* Data invalid & parity checked? */
            *data ^= TX_PARITY;                         /* Invalid: Provide bad parity */
        break;

    case 06:                                            /* LPCOLC/LPCBUF */
        *data = (lpcolc << 8) | lpcbuf;
        break;

    case 07:                                            /* LPCSUM/LPPDAT */
        *data = (lpcsum << 8) | lppdat;
        break;
        }                                               /* end case PA */

sim_debug (DF_REGR, &lp20_dev, "LP20 CSR rd: addr=0%06o  SEL%d, data=%06o access=%d\n",
              pa, pa & 07, *data, access);
return SCPE_OK;
}

static t_stat lp20_wr (int32 data, int32 pa, int32 access)
{
update_lpcs (0);                                        /* update csr's */
sim_debug (DF_REGW, &lp20_dev, "LP20 CSR wr: addr=0%06o  SEL%d, data=%06o access=%d\n",
              pa, pa & 07, data, access);
switch ((pa >> 1) & 07) {                               /* case on PA<3:1> */

    case 00:                                            /* LPCSA */
        if (access == WRITEB)
            data = (pa & 1)? (lpcsa & 0377) | (data << 8): (lpcsa & ~0377) | data;
        /* In hardware, a write that sets GO must not change any other
         * bits in CSRA due to timing restrictions.  Modifying any bits in
         * CSRA while GO is set "may destroy the contents of the checksum register
         * and produce other undesirable effects."
         */
        if (data & CSA_ECLR) {                          /* error clear? */
            lpcsa = (lpcsa | CSA_DONE) & ~CSA_GO;       /* set done, clr go */
            lpcsb = lpcsb & ~CSB_ECLR;                  /* clear err */
            sim_cancel (&lp20_unit);                    /* cancel I/O */
            }
        if (data & CSA_INIT)                            /* init? */
            lp20_init (&lp20_dev);
        if (data & CSA_GO) {                            /* go set? */
            if ((lpcsa & CSA_GO) == 0) {                /* not set before? */
                if (lpcsb & CSB_ERR)
                    lpcsb = lpcsb | CSB_GOE;
                lpcsum = 0;                             /* clear checksum */
                lp20_unit.action = &lp20_svc;
                sim_activate (&lp20_unit, GO_TIME);
                }
            }
        else sim_cancel (&lp20_unit);                   /* go clr, stop DMA */
        lpcsa = (lpcsa & ~CSA_RW) | (data & CSA_RW);
        if (dvld && (CSA_GETFNC (lpcsa) != FNC_DVU)) {  /* DVU load aborted */
            change_rdy (0, CSA_DVON);                   /* Mark DVU off-line and empty */
            dvlnt = 0;
            }
        break;

    case 01:                                            /* LPCSB */
        break;                                          /* ignore writes to TEST */

    case 02:                                            /* LPBA */
        if (access == WRITEB)
            data = (pa & 1)? (lpba & 0377) | (data << 8): (lpba & ~0377) | data;
        lpba = data;
        break;

    case 03:                                            /* LPBC */
        if (access == WRITEB)
            data = (pa & 1)? (lpbc & 0377) | (data << 8): (lpbc & ~0377) | data;
        lpbc = data & BC_MASK;
        lpcsa = lpcsa & ~CSA_DONE;
        break;

    case 04:                                            /* LPPAGC */
        if (access == WRITEB)
            data = (pa & 1)? (lppagc & 0377) | (data << 8): (lppagc & ~0377) | data;
        lppagc = data & PAGC_MASK;
        lpcsa &= ~CSA_PZRO;                             /* Note that even if at TOF, PZRO does not set */
        break;

    case 05:                                            /* LPRDAT */
        if (access == WRITEB)
            data = (pa & 1)? (lprdat & 0377) | (data << 8): (lprdat & ~0377) | data;
        lprdat = data & RDAT_MASK;
        txram[lpcbuf & TX_AMASK] = lprdat | TX_PARITY;  /* load RAM and mark valid */
        break;

    case 06:                                            /* LPCOLC/LPCBUF */
        if ((access == WRITEB) && (pa & 1))             /* odd byte */
            lpcolc = data & 0377;
        else {
            lpcbuf = data & 0377;                       /* even byte, word */
            if (access == WRITE)
                lpcolc = (data >> 8) & 0377;
            }
        break;

    case 07:                                            /* LPCSUM/LPPDAT */
        break;                                          /* read only */
        }                                               /* end case PA */

update_lpcs (0);
return SCPE_OK;
}

/* Line printer service

   The translation RAM case table is derived from the LP20 spec and
   verified against the LP20 RAM simulator in TOPS10 7.04 LPTSPL.
   The equations are:

   flags := inter, delim, xlate, paper, delim_hold (from CSRA)
   actions : = print_input, print_xlate, davfu_action, interrupt

   if (inter) {
        if (!xlate || delim || delim_hold)
            interrupt;
        else if (paper)
            davfu_action;
        else print_xlate;
        }
   else if (paper) {
        if (xlate || delim || delim_hold)
            davfu_action;
        else print_input;
        }
   else {
        if (xlate || delim || delim_hold)
            print_xlate;
        else print_input;
        }
*/

static t_stat lp20_svc (UNIT *uptr)
{
int32 fnc, i, tbc, txst;
uint16 wd10;
t_bool cont;
a10 ba;

static const uint32 txcase[32] = {
    TX_CHR, TX_RAM, TX_CHR, TX_DVU, TX_RAM, TX_RAM, TX_DVU, TX_DVU,
    TX_RAM, TX_RAM, TX_DVU, TX_DVU, TX_RAM, TX_RAM, TX_DVU, TX_DVU,
    TX_INT, TX_INT, TX_INT, TX_INT, TX_RAM, TX_INT, TX_DVU, TX_INT,
    TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT
    };

lpcsa = lpcsa & ~CSA_GO;
ba = CSA_GETUAE (lpcsa) | lpba;
fnc = CSA_GETFNC (lpcsa);
tbc = 010000 - lpbc;
if (((fnc & FNC_INTERNAL) == 0) && ((uptr->flags & UNIT_ATT) == 0)) {
    update_lpcs (CSA_ERR);
    return SCPE_OK;
    }
if ((fnc == FNC_PR) && (lpcsb & CSB_DVOF)) {
    update_lpcs (CSA_ERR);
    return SCPE_OK;
    }
uptr->wait = 0;
for (i = 0, cont = TRUE; (i < tbc) && cont; ba++, i++) {
    if (Map_ReadW (ba, 2, &wd10)) {                     /* get word, err? */
        lpcsb = lpcsb | CSB_MTE;                        /* set NXM error */
        update_lpcs (CSA_ERR);                          /* set done */
        break;
        }
    lpcbuf = (wd10 >> ((ba & 1)? 8: 0)) & 0377;         /* get character */
    lpcsum = (lpcsum + lpcbuf) & 0377;                  /* add into checksum */
    switch (fnc) {                                      /* switch on function */

/* Translation RAM load */

    case FNC_RAM:                                       /* RAM load */
        txram[(i >> 1) & TX_AMASK] = (wd10 & TX_DMASK) | TX_PARITY;
        break;

/* DAVFU RAM load.  The DAVFU RAM is actually loaded in bytes, delimited by
   a start (354 to 356) and stop (357) byte pair.  If the number of bytes 
   loaded is odd, or no bytes are loaded, the DAVFU is invalid.
   Thus, with DVU load mode set in CSRA, there are three states:
   0) Inactive 2) Start code seen,even byte 3) Start code seen, odd byte.
   Normally, only a start or a stop code should be seen in (0), but any other
   code that is received is ignored.  A stop without a corresponding start is
   legal, and specified to reset the current line pointer to 0 without
   modifying the content of the RAM.  
   The DAVFU is physically in the printer, so printers with an optical
   VFU see load data as normal data to be printed.  The LP20 logic inhibits
   the translation RAM in this mode, so any translation will not occur.
   This is an unexpected condition; the OS/User should check the optical
   VFU bit before attempting to load a DAVFU.
*/

    case FNC_DVU:                                       /* DVU load */
        if (lpcsb & CSB_OVFU) {
             /* OS should not attempt to load VFU if printer has Optical VFU.
             * The DAVFU is in the printer, so it will see the attempted load
             * as print data.  The LP20 inhibits translation.
             */
            cont = lp20_print (uptr, lpcbuf);
            break;
            }
        if ((lpcbuf >= 0354) && (lpcbuf <= 0356)) { /* start DVU load? */
            dvlnt = 0;                              /* reset lnt */
            dvld = 2;                               /* Load is active, even */
            if (lpcbuf == 0354) {
                lpi = LPI_SET | 6;
                }
            else if (lpcbuf == 0355) {
                lpi = LPI_SET | 8;
               }
            }
        else if (lpcbuf == 0357) {                  /* stop DVU load? */
            dvptr = 0;                              /* reset ptr */
            dvld = 0;
            if ((dvld & 1) || !VFU_LEN_VALID(dvlnt, lpi)) { /* if odd or invalid length */
                dvlnt = 0;
                change_rdy (0, CSA_DVON);
                }
            else {
                change_rdy(CSA_DVON, 0);
                lp20_newform (uptr);
                }
            }
        else if (dvld == 2) {                       /* even state? */
            dvld_hold = lpcbuf & DV_DMASK;
            dvld = 3;
            }
        else if (dvld == 3) {                       /* odd state? */
            if (dvlnt < DV_SIZE) {
                davfu[dvlnt++] = dvld_hold | ((lpcbuf & DV_DMASK) << 6);
                dvld = 2;
                }
            else {
                change_rdy (0, CSA_DVON);
                dvlnt = dvld = 0;
                }
            }
        break;

/* Print characters through the translation RAM */

    case FNC_PR:                                        /* print */
        lprdat = txram[lpcbuf];                         /* get RAM char */
        if (((lprdat & TX_PARITY) == 0) && (lpcsa & CSA_PAR)) { /* Check for valid */
            lpcsb |= CSB_RPE;                           /* Declare RAM parity error */
            cont = FALSE;
            break;
            }
        txst = (TX_GETFL (lprdat) << 1) |               /* get state */
            ((lpcsa & CSA_DELH)? 1: 0);                 /* plus delim hold */ 
        if (lprdat & TX_DELH)
            lpcsa = lpcsa | CSA_DELH;
        else lpcsa = lpcsa & ~CSA_DELH;
        lpcsa = lpcsa & ~CSA_UNDF;                      /* assume char ok */
        switch (txcase[txst]) {                         /* case on state */

        case TX_CHR:                                    /* take char */
            cont = lp20_print (uptr, lpcbuf);
            break;

        case TX_RAM:                                    /* take translation */
            cont = lp20_print (uptr, lprdat);
            break;

        case TX_DVU:                                    /* DAVFU action */
            if (lprdat & TX_SLEW)
                cont = lp20_adv (uptr, lprdat & TX_VMASK, TRUE);
            else cont = lp20_davfu (uptr, lprdat & TX_VMASK);
            break;

        case TX_INT:                                    /* interrupt */
            lpcsa = lpcsa | CSA_UNDF;                   /* set flag */
            cont = FALSE;                               /* force stop */
            break;
            }                                           /* end case char state */
        break;

    case FNC_TST:                                       /* test */
        break;
        }                                               /* end case function */
    }                                                   /* end for */
lpba = ba & 0177777;
lpcsa = (lpcsa & ~CSA_UAE) | ((ba >> (16 - CSA_V_UAE)) & CSA_UAE);
lpbc = (lpbc + i) & BC_MASK;
if (uptr->wait) {
    uptr->action = &lp20_done;
    sim_activate_after (uptr, uptr->wait);
    sim_debug (DF_TIME, &lp20_dev, "LP20 Active: delay = %u\n", uptr->wait);
} else {
    lp20_done (uptr);
}
if ((fnc == FNC_PR) && pdflpt_error(uptr)) {
    pdflpt_perror (uptr, "LP I/O error");
    pdflpt_clearerr(uptr);
    return SCPE_OK;
    }
return SCPE_OK;
}

static t_stat lp20_done (UNIT *uptr) {
sim_debug (DF_TIME, &lp20_dev, "LP20 Done\n");
if (lpbc)                                               /* intr, but not done */
    update_lpcs (CSA_MBZ);
else update_lpcs (CSA_DONE);                            /* intr and done */

    return SCPE_OK;
}

/* Print routines

   lp20_print           print a character
   lp20_adv             advance n lines
   lp20_davfu           advance to channel on VFU

   Return TRUE to continue printing, FALSE to stop
*/

static t_bool lp20_print (UNIT *uptr, int32 c)
{
t_bool r = TRUE;
int32 rpt = 1;

lppdat = c & 0177;                                      /* mask char to 7b */
if (lppdat == 000)                                      /* NUL? no op */
    return TRUE;
if (lppdat == 012)                                      /* LF? adv carriage */
    return lp20_adv (uptr, 1, TRUE);
if (lppdat == 014)                                      /* FF? top of form */
    return lp20_davfu (uptr, DV_TOF);
if (lppdat == 015)                                      /* CR? reset col cntr */
    return lp20_adv (uptr, 0, FALSE);
else if (lppdat == 011) {                               /* TAB? simulate */
    lppdat = ' ';                                       /* with spaces */
    if (lpcolc >= 128) {
        r = lp20_adv (uptr, 1, TRUE);                   /* eol? adv carriage */
        rpt = 8;                                        /* adv to col 9 */
        }
    else rpt = 8 - (lpcolc & 07);                       /* else adv 1 to 8 */
    }           
else {
    switch (pch->chars) { /* Chars not on drum become spaces */
    case 95:
        if ((lppdat < 040) || (lppdat > 0176)) {
            lppdat = ' ';
            }
        break;
    case 96:
        if ((lppdat < 040) || (lppdat > 0177)) {
            lppdat = ' ';
            }
        break;
    case 64:
        if ((lppdat < 040) || (lppdat > 0137)) {
            lppdat = ' ';
            }
        break;
    case 256:
        break;
    default:  /* Unknown drum size, get attention */
        lppdat = '.';
        break;
        }
    if (lpcolc >= LP_WIDTH)                             /* line full? */
        r = lp20_adv (uptr, 1, TRUE);                   /* adv carriage */
    }
lpcolc = lpcolc + rpt;
while (rpt--) {
    pdflpt_putc (uptr, lppdat); 
}
lp20_unit.pos = pdflpt_where (uptr, NULL);
return r;
}

static t_bool lp20_adv (UNIT *uptr, int32 cnt, t_bool dvuadv)
{
int32 i;
int stoppc = FALSE;

/* All motion, even 0 line slew, flushes print buffer
 * Output 0 line slew as bare <CR> to allow overprinting.
 * n line slew will output as <CR><LF>..<LF>
 * Normal lines will output as <CR><LF>
 */

if (lpcolc) {
    pdflpt_putc (uptr, '\r');
    uptr->wait += (uint32) PRTTIME;
    lpcolc = 0;
}

if (cnt == 0)
    return TRUE;

if (lpcsb & CSB_DVOF)
    return FALSE;

/* This logic has changed because it did not account for the case of more than one TOF
 * occuring in the advance.  Consider a tape with odd/even pages, and a slew channel that
 * stops on the even.  If we slew from the bottom of the even, we will pass the TOF of the
 * odd page and stop on the odd; seeing a second TOF.  
 */
uptr->wait += (uint32) (pch->firstadv + ADVTIME (cnt - 1));

for (i = 0; i < cnt; i++) {                             /* print 'n' newlines; each can complete a page */
    pdflpt_putc (uptr, '\n');
    if (dvuadv) {                                       /* update DAVFU ptr */
        dvptr = (dvptr + cnt) % dvlnt;
        if (davfu[dvptr] & (1 << DV_TOF)) {              /* at top of form? */
            lppagc = (lppagc - 1) & PAGC_MASK;           /* decr page cntr */
            if (lppagc == 0) {
                lpcsa = lpcsa | CSA_PZRO;                /* stop if zero */
                stoppc = TRUE;
                }
            } /* At TOF */
        } /* update pointer */
    }
uptr->pos = pdflpt_where (uptr, NULL);
if (stoppc)                                            /* Crossed one or more TOFs? */
    return FALSE;

return TRUE;
}

static t_bool lp20_davfu (UNIT *uptr, int32 cnt)
{
int i;

if (lpcsb & CSB_DVOF)
    return FALSE;
if (cnt > DV_MAX)                                       /* inval chan? */
    cnt = 7;
for (i = 0; i < dvlnt; i++) {                           /* search DAVFU */
    dvptr = dvptr + 1;                                  /* adv DAVFU ptr */
    if (dvptr >= dvlnt)                                 /* wrap at end */
        dvptr = 0;
    if (davfu[dvptr] & (1 << cnt)) {                    /* channel stop set? */
        if (cnt)                                        /* !TOF channel, adv */
            return lp20_adv (uptr, i + 1, FALSE);
        if (lpcolc)                                     /* TOF, need to flush line? */
            lp20_adv (uptr, 0, FALSE);
        pdflpt_putc (uptr, '\f');                       /* print form feed */
        uptr->pos = pdflpt_where (uptr, NULL);
        lppagc = (lppagc - 1) & PAGC_MASK;              /* decr page cntr */
        if (lppagc != 0)
            return TRUE;
        else {
            lpcsa = lpcsa | CSA_PZRO;                   /* stop if zero */
            return FALSE;
            }
        }
    }                                                   /* end for */
cnt = dvlnt - cnt;
uptr->wait += (uint32) (pch->firstadv + ADVTIME (cnt - 1));
lp20_adv (uptr, 0, FALSE);
change_rdy (0,CSA_DVON);                                /* Code to channel with no channel stop */
return FALSE;
}

/* Update LPCSA, optionally request interrupt */

static void update_lpcs (int32 flg)
{
if (flg)                                                /* set int req */
    lp20_irq = 1;
lpcsa = (lpcsa | flg) & ~(CSA_MBZ | CSA_ERR | CSA_ONL);
lpcsb = (lpcsb | CSB_OFFL) & ~CSB_MBZ;
if (lp20_unit.flags & UNIT_ATT) {
    lpcsa = lpcsa | CSA_ONL;
    lpcsb = lpcsb & ~CSB_OFFL;
    }
else lpcsa = lpcsa & ~CSA_DONE;
if (lpcsb & CSB_ERR)
    lpcsa = lpcsa | CSA_ERR;
if ((lpcsa & CSA_IE) && lp20_irq)
    SET_INT (LP20);
else CLR_INT (LP20);
return;
}

/* Set and clear READY bits in csa.
 * used for bits where a transition should cause an interrupt.
 * also updates corresponding bits in csb.
 */
static void change_rdy (int32 setrdy, int32 clrrdy)
{
int32 newcsa = (lpcsa | setrdy) & ~clrrdy;

if ((newcsa ^ lpcsa) & (CSA_ONL | CSA_DVON) && !sim_is_active (&lp20_unit)) {
    lp20_irq |= 1;
    if (newcsa & CSA_IE)
        SET_INT(LP20);
    }
/* CSA_ERR is handled in update_csa */

if (newcsa & CSA_DVON) {
    lpcsb &= ~CSB_DVOF;
}
else {
    lpcsb |= CSB_DVOF;
}
if (newcsa & CSA_ONL) {
    lpcsb &= ~CSB_OFFL;
}
else {
    lpcsb |= CSB_OFFL;
}
lpcsa = newcsa;
}

/* Acknowledge interrupt (clear internal request) */

static int32 lp20_inta (void)
{
lp20_irq = 0;                                           /* clear int req */
return lp20_dib.vec;
}


/* Simulator RESET
 * Note that this does not reset the printer's DAVFU or the
 * translation RAM, which survive system bootstraps.  
 * (SET VFUCLEAR will do that.)
 * 
 */

t_stat lp20_reset (DEVICE *dptr)
{
    /* On power-up reset, clear DAVFU & RAM.  Set DAVFU off-line. */
if (sim_switches & SWMASK ('P')) {
    UNIT *uptr = dptr->units;
    memset (davfu, 0, sizeof(davfu));
    memset (txram, 0, sizeof(txram));
    dvlnt = dvptr = dvld= 0;
    lpcsa &= ~CSA_DVON;
    lpcsb |= CSB_DVOF;
    lpi = DEFAULT_LPI;
    /* Set unit type; if has OVFU, will load default tape */
    (void) lp20_set_unit_type (uptr, 0x100, DEFAULT_LPT, NULL);
}

return lp20_init (dptr);
}

/* Local init does NOT initialize the DAVFU/TRANSLATION RAMs.
 * They are in the printer, which reset does not reach.
 */

t_stat lp20_init (DEVICE *dptr)
{
lpcsa =  (lpcsa & CSA_DVON) | CSA_DONE;
lpcsb = lpcsb & (CSB_OVFU | CSB_DVOF);
lpba = lpbc = lppagc = lpcolc = 0;                      /* clear registers */
lprdat = lppdat = lpcbuf = lpcsum = 0;
lp20_irq = 0;                                           /* clear int req */
sim_cancel (&lp20_unit);                                /* deactivate unit */
pdflpt_reset (&lp20_unit);                              /* inactivate flush timer */
update_lpcs (0);                                        /* update status */
return SCPE_OK;
}

static t_stat lp20_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = pdflpt_attach (uptr, cptr);                    /* attach file */
last_lpi = lpi;
last_dvlnt = dvlnt;
if (lpcsa & CSA_DVON) {
    int i;
    for (i = 0; i < dvlnt; i++) {                       /* Align VFU with new file */
        if (davfu[dvptr] & (1 << DV_TOF))
            break;
        dvptr = (dvptr +1) % dvlnt;
        }
    if (!(davfu[dvptr] & (1 << DV_TOF)))                /* No TOP channel  -> bad VFU */
        change_rdy (0, CSA_DVON);
}
if (lpcsa & CSA_ONL)                                    /* just file chg? */
    return reason;
if (sim_is_active (&lp20_unit))                         /* busy? no int */
    update_lpcs (0);
else update_lpcs (CSA_MBZ);                             /* interrupt */
return reason;
}

static t_stat lp20_detach (UNIT *uptr)
{
t_stat reason;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;

reason = pdflpt_detach (uptr);
sim_cancel (&lp20_unit);
lpcsa = lpcsa & ~CSA_GO;
update_lpcs (CSA_MBZ);
return reason;
}

static t_stat lp20_set_unit_type (UNIT *uptr, int32 val, char *cptr, void *desc)
{
size_t i;

if ((uptr->flags & UNIT_ATT) && !(val & 0x100)) {
    return SCPE_NOATT;
}

if (!cptr || !*cptr)
    return SCPE_ARG;

for (i = 0; i < DIM (printers); i++) {
    if (!strcmp (printers[i].name, cptr)) {
        break;
    }
}
if (i >= DIM (printers)) {
    return SCPE_ARG;
}

uptr->up7 = (void *)&printers[i];
if (pch->davfu) {
    if (lpcsb & CSB_OVFU) {                             /* VFU type change? */
        lpcsb &= ~CSB_OVFU;                             /* Old was optical, invalidate VFU */
        change_rdy (0, CSA_DVON);                       /* DAVFU is off-line */
        dvptr = 0;
        dvlnt = 0;
    }
} else {
    lpcsb |= CSB_OVFU;
    lpcsa |= CSA_DVON;                                 /* OVFU is internally on-line */
    lpcsb &= ~CSB_DVOF;                                /* (Don't interrupt for this) */
    memcpy (davfu, defaultvfu, sizeof defaultvfu);
    dvlnt = sizeof (defaultvfu) / sizeof (defaultvfu[0]);
    dvptr = 0;
}

lp20_newform (uptr);
return SCPE_OK;
}

static void lp20_newform (UNIT *uptr) {
size_t i, tof;
char tbuf[sizeof ("columns=999 tof-offset=999 lpp=99999 lpi=99")];

/* Setup PDF defaults
 * Non-default columns
 * Set DAVFU tof-offset default to to match VFU.  The BOF
 * channel marks the end of data (or T20, beginning of break).
 * Set lpp to match vfu length.
 * Set lpi if opr or VFU specified it.
 */

tof = 0;
for (i = 0; i < (size_t)dvlnt; i++) {
    if (davfu[i] & (1u << DV_BOF)){
        if ((!(lpcsb & CSB_OVFU)) && cpu_unit.flags & UNIT_T20) {
            tof = dvlnt - i;
            }
        else {
            tof = dvlnt - (i+1);
            }
        break;
        }
    }
tbuf[0] = '\0';
if (lpi & LPI_SET) {
    sprintf (tbuf, "lpi=%u", (lpi & ~LPI_SET));
    }
if (pch->columns != 132) {
    sprintf (tbuf + strlen (tbuf), " columns=%u", pch->columns);
    }
if (tof != 0) {
    sprintf (tbuf + strlen (tbuf), " tof-offset=%u", tof);
    }

i = dvlnt? dvlnt : last_dvlnt;
if (i) {
    sprintf (tbuf + strlen (tbuf), " lpp=%u", i);
    }
pdflpt_set_defaults (uptr, tbuf);

if (pdflpt_getmode (uptr) == PDFLPT_IS_PDF) {
    tbuf[0] = '\0';
    if (dvlnt && dvlnt != last_dvlnt) {
        last_dvlnt = dvlnt;
        sprintf (tbuf, "lpp=%u", dvlnt);
        if (tof != 0) {
            sprintf (tbuf + strlen (tbuf), " tof-offset=%u", tof);
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

static t_stat lp20_show_unit_type (FILE *st, UNIT *uptr, int32 v, void *dp)
{
    fprintf (st, "%s, %u columns,\n\t%u LPM, %u character drum",
             pch->name, pch->columns, pch->lpm, pch->chars );

    return SCPE_OK;
}

static t_stat lp20_show_printers (FILE *st, UNIT *uptr, int32 val, void *desc) {
    size_t i;

    fprintf (st, "     Model Cols Chars  LPM    VFU   Technology notes\n"
                 "     ----- ---- ----- ----- ------- ------------------\n");

    for (i = 0; i < DIM (printers); i++) {
        const LPT *lpt = &printers[i];
        fprintf (st, "    %c%-5s  %3u %5u %5u %-7s %s\n",
                 ((pch == lpt)? '*': ' '),
                 lpt->name,
                 lpt->columns,
                 lpt->chars,
                 lpt->lpm,
                 (lpt->davfu? "DAVFU" : "Optical"),
                 lpt->desc
            );
    }
    fprintf (st, "\n    * - Selected\n");

    return SCPE_OK;
}

static t_stat lp20_set_tape (UNIT *uptr, int32 val, char *cptr, void *desc)
{
FILE *vfile;
int sum = 0;

if (!(lpcsb & CSB_OVFU)) { /* Only allowed if optical */
    return SCPE_NOATT;
}

if (!cptr || !*cptr) {
    return SCPE_ARG;
}

/* set lp20 tape file
 * This is OK when attached, so long as not changing from DAVFU.
 * Read an optical tape file.  These are line-oriented ASCII files:
 * # ! ; comment
 * lno: [ch [ch]...] Define line lno (0-length-1 with punches in
 *                   channel(s) ch (1-12)
 * Not required to be in order, if a lno appears more than once, the entries
 * are ORed.  (You can't unpunch a tape.)
 * The highest lno defines the VFU length. Note that there is confusion about
 * whether line numbers start at one or at zero.  The HRM uses 0.  Some of the
 * utilitites use 1.  We stick with 0 here..
 */

vfile = sim_fopen( cptr, "r" );
if (vfile == NULL) {
    return SCPE_OPENERR;
}
memset (davfu, 0, sizeof davfu);
dvptr = dvlnt = 0;

while (!feof(vfile)) {
    int32 line, hole;
    int c;
        
    /* Discard comments */
    c = fgetc(vfile);
    if (c == EOF)
        break;
    if ((c == '#') || (c == ';') || (c == '!')) {
        while (!feof(vfile) && (c != '\n'))
            c = fgetc(vfile);
        continue;
        }
    ungetc(c, vfile);

    /* Read a line number */
    c = fscanf (vfile, " %u:", (uint32 *)&line);
    if (c == EOF)
        break;
    if ((c < 1) || (line < 0) || (((uint32)line)  >= (sizeof (davfu)/sizeof davfu[0])))
        goto fmt_err;
    if (line+1 > dvlnt)
        dvlnt = line+1;

    /* Read channel numbers for current line */
    while (!feof(vfile)) {
        do {
            c = fgetc (vfile);
        } while (isspace(c) && (c != '\n'));
        if ((c == '\n') || (c == EOF))
            break;
        ungetc(c, vfile);
        c = fscanf (vfile, "%u", (uint32 *)&hole);
        if ((c == EOF) || (c < 1) || (c > 12))
            goto fmt_err;
        sum |= (davfu[line] |= 1 << (hole -1));
        } /* End of line */
    } /* EOF */

/* Validate VFU content */
if (!(sum & (1 << DV_TOF)))  /* Verify that at least one punch is in the TOF channel. */
    goto fmt_err;
if (!VFU_LEN_VALID(dvlnt, lpi))   /* Verify VFU has minimum number of lines */
    goto fmt_err;

fclose(vfile);
change_rdy (CSA_DVON, 0);
lp20_newform (uptr);
return SCPE_OK;

fmt_err:
dvlnt = 0;
change_rdy (0, CSA_DVON);
fclose(vfile);
return SCPE_FMT;
}

static t_stat lp20_show_vfu_state (FILE *st, UNIT *uptr, int32 v, void *dp)
{
if (lpcsb & CSB_OVFU)
    fprintf (st, "optical VFU");
else
    fprintf (st, "DAVFU");

if (lpcsa & CSA_DVON)
    fprintf (st, " loaded: %u lines, %.1f in", dvlnt, (((double)dvlnt)/(lpi & ~LPI_SET)));
else
    fprintf (st, " not ready");

return SCPE_OK;
}

static t_stat lp20_set_lpi (UNIT *uptr, int32 val, char *cptr, void *desc)
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

if ((lpcsa & CSA_DVON) && !VFU_LEN_VALID(dvlnt, newlpi))
    return SCPE_ARG;

lpi = newlpi;
lp20_newform (uptr);

return SCPE_OK;
}

static t_stat lp20_show_lpi (FILE *st, UNIT *uptr, int32 v, void *dp)
{
fprintf (st, "%u LPI", lpi & ~ LPI_SET);

return SCPE_OK;
}

static t_stat lp20_show_vfu (FILE *st, UNIT *uptr, int32 v, void *dp)
{
int l, c, sum;

if (lpcsb & CSB_OVFU)
    fprintf (st, "Tape");
else 
    fprintf (st, "DAFVU");

if (lpcsb & CSB_DVOF) {
    fprintf (st, " is not loaded\n");
    return SCPE_OK;
    }

fprintf (st, " contains:\n"
                "     1 1 1\n"
                "line 2 1 0 9 8 7 6 5 4 3 2 1\n"
                "---- - - - - - - - - - - - -\n");
sum = 0;
for (l = 0; l < dvlnt; l++) {
    if ( l && !(l % 5) )
        fputc ('\n', st);
    fprintf (st, "%4u", l);
    for (c = DV_MAX; c >= 0; c--)
        fprintf (st, " %c", (davfu[l] & (1 << c))? ((c >= 9)? 'X': '1'+c) : ' ');
    fputc ('\n', st);
    sum |= davfu[l];
    }

if (!(sum & (1 << DV_TOF))) {
    fprintf (st, "? No stop in channel %u (Top-of-Form)\n", DV_TOF+1);
    }
if (!(sum & (1 << DV_BOF))) {
    fprintf (st, "%% No stop in channel %u (Bottom-of-Form)\n", DV_BOF+1);
    }

return SCPE_OK;
}

static t_stat lp20_set_tof (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i;

if (cptr && *cptr)
    return SCPE_ARG;

if (lpcsb & CSB_DVOF)
    return SCPE_INCOMP;

/* TOF should always be line zero.
 * In case of an unusual VFU, advance to TOF.
 */
dvptr = 0;
for (i = 0; i < dvlnt; i++) {
    if (davfu[i] & (1u << DV_TOF)) {
        dvptr = i;
        break;
        }
    }

return SCPE_OK;
}

static int16 evenbits (int16 value)
{
int16 even = 1;
while (value) {
    even ^= 1;
    value &= value-1;
    }
return even;
}

static t_stat lp20_clear_vfu (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int i;

if (!get_yn ("Clear DAVFU & RAM? [N]", FALSE))
    return SCPE_OK;
for (i = 0; i < DV_SIZE; i++)
    davfu[i] = 0;
for (i = 0; i < TX_SIZE; i++)
    txram[i] = 0;
dvlnt = dvptr = dvld= 0;
change_rdy (0, CSA_DVON);
update_lpcs (0);
return SCPE_OK;
}

static t_stat lp20_help (FILE *st, struct sim_device *dptr,
                            struct sim_unit *uptr, int32 flag, char *cptr)
{
    const char *defaults;
    static const char text[] = {
" The LP20 DMA line printer controller is a UNIBUS device developed by DEC\'s\n"
" 36-bit product line.  The controller is used in the KS10, in PDP-11\n"
" based remote stations and in the KL10 console.  Several models of line\n"
" printer can be (and were) attached to the LP20; with the long lines\n"
" option, up to 100 feet from the controller.  Each LP20 controls one line\n"
" printer.\n"
"\n"
" Besides DMA, the LP20 incorporates a translation RAM that can\n"
" handle case translation and more sophisticated processing, such as\n"
" representing control characters as ^X and FORTRAN carriage control.\n"
" %1H\n"
" It also suports the DAVFU, (direct access) which is the electronic equivalent\n"
" of the optical tape.  The DAVFU is more convenient for operators, as the \n"
" print spooler changes it automatically to match the forms. Optical VFUs\n"
" are also supported.\n"
"\n"
" Several printer models are emulated and can be configured\n"
"1 Hardware Description\n"
" Only a single model of the LP20 was offically released.  However,\n"
" it is usually bundled with a line printer, so there are a number of part\n"
" numbers that contain the LP20.\n"
" \n"
" There is a variant that was designed to drive an LA180 with either a 7\n"
" or 8 bit parallel interface.  The prints label it 'not a standard\n"
" product'.  It is not implemented in this emulation.\n"
" \n"
" Actual device timing varies depending on the printer.\n"
" \n"
" Printers used with the LP20 include both drum and band printers.\n"
" Nominal speeds ranged from 200 LPM to 1250 LPM.\n"
" \n"
" Besides speed, the major variants were: Optical vs DAVFU, 64 vs. 96\n"
" character band/drum, and scientific vs. EDP fonts.\n"
" \n"
" Scientific fonts use slashed Z and 0; EDP does not.\n"
" \n"
" All supported 132 colum output at a pitch of 10 CPI.\n"
" \n"
" Some had operator switch-selectable vertical pitches for either 6 or 8\n"
" LPI.\n"
"2 Mechanical characteristics\n"
" Paper and ribbon are hit by a hammer onto the rotating drum when the\n"
" desired character is in front of the hammer.  Thus, a line that contains\n"
" all the characters on a drum would take one full revolution to print,\n"
" plus paper motion time.  (Assuming no overstrikes.)  At 100 RPM, this\n"
" translates to 16.7 ms printing + 41 ms motion for the LP05. The math\n"
" works out to 1,040 LPM, but the rated speeds account for slew in the\n"
" margins and some overstrikes (most commonly underline.)\n"
" \n"
" One could construct data patterns that overlapped some paper motion with\n"
" unused character time on the drum.  So the LP10, with 14 ms line advance\n"
" could print the alphabet using 1/2 a rotation and move the paper in the\n"
" other half - about 50%% faster than rated speed.  Bands move the\n"
" characters horizontally (similar to chain/train printers), but the basic\n"
" timing constraints are similar.\n"
" \n"
" Timing for several printers: a/b is 64/96 character set value.\n"
"++++LP05         LP07     LP10      LP14\n"
" Line advance:  41 ms        12.5 ms  14 ms       20 ms\n"
" Slew:          20 ips       60 ips   35 ips     22.5 @8LPi/30 @6\n"
" Drum Rotation: 1000/600 RPM band     1800/1200 1280/857\n"
" Rated LPM:     230/300      1220/905 1250/925  890/650\n"
" Weight lb/kg   340/154       800/363  800/363  420/191\n"
" \n"
"2 Vertical Forms Unit\n"
" The Vertical Forms Unit (VFU) controls forms positioning.  Technically\n"
" part of the printer, it is emulated with the LP20.  Two types of VFU\n"
" are supported by the hardware, and both are emulated.\n"
" \n"
" Optical VFUs use photocells to read the holes punched in a paper tape.\n"
" This is the traditional design.  It requires the operator to change\n"
" the paper tape when forms are changed, an the tapes are subject to\n"
" mechanical damage.  The traditional paper tape was read for every line\n"
" printed.  Later printers read the tape at power-up (or change) and\n"
" cached the data in on-board RAM.  This reduced wear on the tape (and\n"
" the operator), and provides an example of hardware emulating hardware.\n"
" \n"
" Direct Access VFUs (electronic VFUs) simulate the paper tape with RAM,\n"
" allowing the print spooler to automatically load them when forms are\n"
" changed.\n"
" \n"
" The LP20 also has a translation RAM, which is related to (but not\n"
" part of) the VFU.  The translation RAM allows mapping any 8-bit\n"
" character to any character in the printer, or any paper motion\n"
" command to the printer.  In addition, the translation RAM drives\n"
" a 2-state state machine, allowing it to implment FORTRAN carriage\n"
" control or other simple 'escape sequences' entirely in hardware.\n"
" It also allows any character code to halt printing and interrupt\n"
" the processor.  This can be used to map non-printing codes to\n"
" multi-character graphics.  (E.g. control-A to ^A).\n"
" \n"
" The RAM is loaded by the host procesor, and typically is associated\n"
" with a form.\n"
" \n"
"2 $Registers\n"
"1 Configuration\n"
" The list of emulated printer models and their characteristics can be\n"
" viewed with:\n"
"+SHOW LP20 MODELS\n"
" To select a model, use:\n"
"+SET PRINTER-MODEL=modelname\n"
" Selecting a model configures the printer's character set and speed.\n"
"2 VFU Configuration\n"
" The emulator is configured with either a optical or a DAVFU depending\n"
" on the printer model.\n"
" \n"
" The DAVFU contents can be viewed with the\n"
"+SHOW %D VFU\n"
" command.  The DAVFU is controlled entirely by the host\n"
" and requires no operator attention.\n"
" \n"
" When the optical VFU is configured a default tape is\n"
" provided, which is setup for a 66 line page with a 60 line printable\n"
" area, and the DEC standard channels punched.  (These correspond to\n"
" FORTRAN carriage control.)  \n"
"\n"
" The \"Normal\" DAVFU of TOPS-10 and TOPS-10 is a 63 line page.  Thus\n"
" the default TOf-OFFSET for DAVFUs is 3 lines.\n"
"\n"
" The VFU length determines the lines-per-page, which together with\n"
" LPI determines the form length used with PDF printing.  The LENGTH\n"
" ATTACH parameter has no effect if the VFU is on-line.\n"
" \n"
" A custom optical tape can also be configured.  To load an a custom tape\n"
" file, use:\n"
"+ SET %D TAPE tapefile\n"
"3 Optical VFU file format\n"
" A custom tape for the optical VFU has the following format.\n"
" \n"
" Generally, each line of the file represents one line of the form.\n"
"+#. ; ! at the beginning of a line are comments, and ignored.\n"
"+line: n,m,o\n"
"++These lines indicate the channels to be punched in each line.  \n"
"++That is, that the paper motion will stop when a \"move to channel n\"\n"
"++command is received.\n"
"++The line number is decimal, between 0 and the page length -1.\n"
"++The channel number can be between 1 and 12 - although most printers \n"
"++supported only channels 1-8\n"
" \n"
" Channel 0 is the TOP OF FORM channel, and must be punched in at least\n"
" one line.\n"
" \n"
" Channel 12 is the BOTTOM OF FORM channel. Some printers use this to allow\n"
" completion of the last page when PAPER OUT is detected.  Paper out sensors\n"
" tended to alert early. SimH uses it to determine the TOF-OFFSET.\n"
"2 $Set commands\n"
"2 $Show commands\n"
"1 Operation\n"
" These commands allow the operator to interact with the simulated printer\n"
" \n"
" SET LP20 TOPOFFORM\n"
"+advances the paper to the top of the next page by\n"
"+slewing to VFU channel 0, as does the TOP OF FORM button on a physical\n"
"+printer.\n"
" \n"
" The DAVFU and translation RAMs survive RESET unless RESET -P is used. \n"
" SET LP20 VFUCLEAR\n"
"+Clears both the VFU and translation RAM.  Since both are controlled by the\n"
"+host, this is only useful for debugging.\n"
"\n"
" SET %D LPI=6-LPI | 8-LPI\n"
"+sets the printer's vertical pitch.  This is currently ignored, but may be\n"
"+implemeneted in the future.  Note that a DAVFU tape can set the vertical\n"
"+pitch as well.  The LPI and LPP from the VFU determine the form length.\n"
    };

if (!uptr) {
    uptr = dptr->units;
}
defaults = pdflpt_get_defaults (uptr);

return scp_help (st, dptr, uptr, flag, text, cptr,
                 pdflpt_helptext,
                 (defaults? "T": "F"), defaults
        );
}

static char *lp20_description (DEVICE *dptr)
{
    return "DMA Line Printer controller";
}
