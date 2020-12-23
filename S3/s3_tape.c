/* s3_tape.c: IBM 3411 Tape Units

   Copyright (c) 2006 Henk Stegeman
   Copyright (c) 2001 Charles E. Owen
   Copyright (c) 1993-2001, Robert M. Supnik

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

   Adaptor --+--> Unit #0 (3411) t0
             +--> Unit #1 (3410) t1
             +--> Unit #2 (3410) t2
             +--> Unit #3 (3410) t3

*/

#include "s3_defs.h"
#include "sim_tape.h"

int32 tap (int32 tape, int32 op, int32 m, int32 n, int32 data);

t_stat tap_svc (UNIT *uptr);
t_stat tap_boot (int32 unitno, DEVICE *dptr);
t_stat tap_attach (UNIT *uptr, CONST char *cptr);
t_stat tap_reset (DEVICE *dptr);
const char *tape_description (DEVICE *dptr);

#define TERR u3                     /* Tape/Unit error status */

int32 MTAR;                         /* Data Address Register */
int32 MBCR;                         /* Byte Count Register */
int32 t_nrdy[4] = { 0, 0, 0, 0 };   /* Not ready error */
int32 ss_busy = 0;                  /* Subsystem busy flag */
int32 blk_length = 0;               /* Block length (integer) */
static char tbuf[MAX_TAPESIZE];     /* Tape buffer */

/* Disk data structures

   tx_dev   CDR descriptor
   tx_unit  CDR unit descriptor
            u3 is used for sense bytes 0 & 1.
            u4 is not used.
   tx_reg   CDR register list

   x = 0, 1, 2 or 3
*/

MTAB tap_mod[] = {
   { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "FORMAT", "FORMAT",
       &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, "Set/Display tape format (SIMH, E11, TPC, P7B, AWS)" },
   { 0 }

   };

UNIT t0_unit = {
   UDATA (&tap_svc, UNIT_ATTABLE|UNIT_ROABLE, 0), 100 };

REG t0_reg[] = {
   { FLDATA (NOTRDY, t_nrdy[0], 0) },
   { HRDATA (TAR, MTAR, 16) },
   { HRDATA (BCR, MBCR, 16) },
   { HRDATA (ERR, t0_unit.TERR, 16) },
   { DRDATA (POS, t0_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, t0_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, tbuf, 8, 8, MAX_TAPESIZE) },
   { NULL } };

DEVICE t0_dev = {
   "T1", &t0_unit, t0_reg, tap_mod,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &tap_reset,
   &tap_boot, &tap_attach, NULL, NULL,
   DEV_TAPE|DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &tape_description };

UNIT t1_unit = {
   UDATA (&tap_svc, UNIT_ATTABLE|UNIT_ROABLE, 0), 500 };

REG t1_reg[] = {
   { FLDATA (NOTRDY, t_nrdy[1], 0) },
   { HRDATA (TAR, MTAR, 16) },
   { HRDATA (BCR, MBCR, 16) },
   { HRDATA (ERR, t1_unit.TERR, 16) },
   { DRDATA (POS, t1_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, t1_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, tbuf, 8, 8, MAX_TAPESIZE) },
   { NULL } };

DEVICE t1_dev = {
   "T2", &t1_unit, t1_reg, tap_mod,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &tap_reset,
   &tap_boot, &tap_attach, NULL, NULL,
   DEV_TAPE|DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &tape_description };

UNIT t2_unit = {
   UDATA (&tap_svc, UNIT_ATTABLE|UNIT_ROABLE, 0), 100 };

REG t2_reg[] = {
   { FLDATA (NOTRDY, t_nrdy[2], 0) },
   { HRDATA (TAR, MTAR, 16) },
   { HRDATA (BCR, MBCR, 16) },
   { HRDATA (ERR, t2_unit.TERR, 16) },
   { DRDATA (POS, t2_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, t2_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, tbuf, 8, 8, MAX_TAPESIZE) },
   { NULL } };

DEVICE t2_dev = {
   "T3", &t2_unit, t2_reg, tap_mod,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &tap_reset,
   &tap_boot, &tap_attach, NULL, NULL,
   DEV_TAPE|DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &tape_description };

UNIT t3_unit = {
   UDATA (&tap_svc, UNIT_ATTABLE|UNIT_ROABLE, 0), 100 };

REG t3_reg[] = {
   { FLDATA (NOTRDY, t_nrdy[3], 0) },
   { HRDATA (TAR, MTAR, 16) },
   { HRDATA (BCR, MBCR, 16) },
   { HRDATA (ERR, t3_unit.TERR, 16) },
   { DRDATA (POS, t3_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, t3_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, tbuf, 8, 8, MAX_TAPESIZE) },
   { NULL } };

DEVICE t3_dev = {
   "T4", &t3_unit, t3_reg, tap_mod,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &tap_reset,
   &tap_boot, &tap_attach, NULL, NULL,
   DEV_TAPE|DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &tape_description };


/* -------------------------------------------------------------------- */

int32 tap_device_to_num (DEVICE *dptr)
{
    if (dptr == &t0_dev)
        return 0;
    if (dptr == &t1_dev)
        return 1;
    if (dptr == &t2_dev)
        return 2;
    if (dptr == &t3_dev)
        return 3;
    return -1; /* Should never happen */
}

/* 3411: master routines */

int32 tap1 (int32 op, int32 m, int32 n, int32 data)
{
   int32 r;
   if (m == 0)
      r = tap(0, op, m, n, data);
   else
      r = tap(1, op, m, n, data);
   return (r);
}

int32 tap2 (int32 op, int32 m, int32 n, int32 data)
{
   int32 r;
   if (m == 0)
      r = tap(2, op, m, n, data);
   else
      r = tap(3, op, m, n, data);
   return (r);
}

/* 3411: operational routine */

int32 tap (int32 tape, int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata, i;
   t_stat r;
   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };
   UNIT *uptr = 0;

   switch (tape) {
      case 0:
         uptr = t0_dev.units;
         break;
      case 1:
         uptr = t1_dev.units;
         break;
      case 2:
         uptr = t2_dev.units;
         break;
      case 3:
         uptr = t3_dev.units;
         break;
      default:
         break;
   }

   if (debug_lvl & 0x04)
      fprintf(trace, "=T=> %04X %s %01X,%d,%04X MTAR=%04X MBCR=%04X \n",
         IAR[level],
         opstr[op],
         m, n, data,
         MTAR,
         MBCR);

   switch (op) {      
      case 0:     /* SIO 3411 */
         if ((uptr -> flags & UNIT_ATT) == 0)
            return SCPE_UNATT;
         uptr -> TERR = 0x0000; /* SIO resets all errors */
         iodata = 0;
         switch (n) {
            case 0x00:   /* Control */
               switch (data) {
                  case 0x07:   /* rewind */
                  case 0x0F:   /* rewind with unload */
                     sim_tape_rewind(uptr);
                     sim_activate(uptr, uptr -> wait);
                     ss_busy = ON;
                     if (data == 0x0F)
                        detach_unit(uptr);   /* unload tape */
                     break;
                  case 0x1F:   /* write tape mark */
                     sim_tape_wrtmk (uptr);
                     sim_activate(uptr, uptr -> wait);
                     ss_busy = ON;
                     break;
                  case 0xC3:   /* Mode 2 set (9 track) */
                     break;
                  default:
                     return(STOP_INVDEV);
               }
               MBCR = 0;
               iodata = SCPE_OK;
               break;
            case 0x01:   /* Read Forward */
            case 0x03:   /* Read Backward */
               r = (n == 1) ? sim_tape_rdrecf (uptr, tbuf, &blk_length, MBCR) :
                              sim_tape_rdrecr (uptr, tbuf, &blk_length, MBCR);
               if (r == MTSE_TMK)
                  uptr -> TERR |= 0x2000;/* tapemark detected */
               if (blk_length > MBCR)
                  blk_length = MBCR;
               if (blk_length != MBCR)
                  uptr -> TERR |= 0x4000;/* wrong length record */
               for (i = 0; i < blk_length; i++) {
                  PutMem(MTAR, 0, tbuf[i]);
                  MTAR++;
               }
               sim_activate(uptr, uptr -> wait);
               ss_busy = ON;           /* subsystem is busy */
               MBCR = 0;               /* reset byte count */
               iodata = SCPE_OK;
               break;
            case 0x02:   /* Write */
               for (i = 0; i < MBCR; i++) {
                  tbuf[i] = GetMem(MTAR, 0);
                  MTAR++;
               }
               sim_tape_wrrecf (uptr, tbuf, MBCR);
               sim_activate(uptr, uptr -> wait);
               ss_busy = ON;
               iodata = SCPE_OK;
               break;
            default:
               return STOP_INVDEV;
         }
         return iodata;

      case 1:    /* LIO 3411 */
         switch (n) {
            case 0x00:   /* Byte Count Register */
               MBCR = data;
               break;
            case 0x04:   /* Data Address Register */
               MTAR = data;
               break;
            case 0x06:   /* Interrupt Control Register */
               break;
            default:
               return STOP_INVDEV;
         }
         return SCPE_OK;

      case 2:     /* TIO 3411 */
      case 4:     /* APL 3411 */
         iodata = 0;
         switch (n) {
            case 0x00:   /* Not ready/check ? */
               if (uptr -> TERR != 0x0000)
                  iodata = TRUE;
               if ((uptr -> flags & UNIT_ATT) == 0)
                  iodata = TRUE;
               break;
            case 0x01:   /* Op-end pending ? */
               break;
            case 0x02:   /* Subsystem busy ? */
               if (ss_busy == ON)
                  iodata = TRUE;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         return ((SCPE_OK << 16) | iodata);

      case 3:     /* SNS 3411 */
//         if ((uptr -> flags & UNIT_ATT) == 0)
//            return SCPE_UNATT << 16;
         iodata = 0x0000;
         switch (n) {
            case 0x00:   /* subsystem bytes 0 & 1. */
               iodata = uptr -> TERR;
               if (uptr -> pos == 0)
                  iodata |= 0x0020;   /* at load point */
               iodata |= 0x0100;      /* sense valid */
               break;
            case 0x01:   /* subsystem bytes 2 & 3. */
               break;
            case 0x02:   /* subsystem bytes 4 & 5. */
               break;
            case 0x03:   /* subsystem bytes 6 & 7. */
               break;
            case 0x04:   /* data address register. */
               iodata = MTAR;
               break;
            case 0x05:   /* attachment sense bytes. */
               break;
            case 0x06:   /* subsystem hw error sense. */
               break;
            case 0x07:   /* op-end sense byte */
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         iodata |= ((SCPE_OK << 16) & 0xffff0000);
         return (iodata);

      default:
         break;
   }
   return SCPE_OK;
}


/*** Tape unit service. ***/

t_stat tap_svc (UNIT *uptr)
{
   ss_busy = OFF;
   return SCPE_OK;
}


/*** Tape reset ***/

t_stat tap_reset (DEVICE *dptr)
{
   UNIT *uptr = dptr->units;

   if (sim_switches & SWMASK ('P'))                 /* power on initialization? */
      sim_tape_set_fmt (uptr, 0, "AWS", NULL);      /*   default to AWS tape format */
   t_nrdy[tap_device_to_num(dptr)] = ss_busy = 0;   /* clear indicators */
   sim_cancel (uptr);					            /* clear event */
   uptr->TERR = 0x0000;                             /* clear any errors */
   sim_tape_rewind (uptr);						    /* tape is at load point */
   return SCPE_OK;
}


/*** Tape unit attach ***/

t_stat tap_attach (UNIT *uptr, CONST char *cptr)
{
   t_nrdy[tap_device_to_num(find_dev(sim_uname(uptr)))] = 0;    /* clear status */
   ss_busy = 0;	
   sim_tape_rewind (uptr);			      	        /* tape is at load point */
   uptr -> TERR = 0x0000;                           /* clear any error */
   return sim_tape_attach (uptr, cptr);
}


/*** Bootstrap routine, not valid for 3411 ***/

t_stat tap_boot (int32 unitno, DEVICE *dptr)
{
   return STOP_INVDEV;
}


const char *tape_description (DEVICE *dptr)
{
   return "IBM 3411 Tape Unit";
}
