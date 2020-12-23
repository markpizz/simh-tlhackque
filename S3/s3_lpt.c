/* s3_lpt.c: IBM 1403/5203 line printer simulator

   Copyright (c) 2001 Charles E. Owen
   Copyright (c) 1993-2001, Robert M. Supnik
   Copyright (c) 2009-2010 Henk Stegeman

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

   lpt      1403/5203 line printer

*/

#include "s3_defs.h"

t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_attach (UNIT *uptr, char *cptr);
const char *lpt_description (DEVICE *dptr);
t_stat write_line();
t_stat space (int32 lines, int32 lflag);
t_stat carriage_control (int32 action, int32 mod);

#define SPACE  0
#define SKIP   1

int32 LPDAR;                    /* Data Address Register */
int32 LPFLR;                    /* Forms Length Register */
int32 LPIAR;                    /* Image Address Register */
int32 linectr = 1;              /* Current line # */
int32 lpt_int_enabled = OFF;    /* Interrupt enabled flag */
int32 lpt_opend_int = OFF;      /* Op End interrupt pending flag */
int32 print_busy = OFF;         /* Printer busy flag */
int32 carr_busy = OFF;          /* Carriage busy flag */
int32 lpterror = OFF;           /* Printer error flag */

/* LPT data structures

   lpt_dev   LPT device descriptor
   lpt_unit  LPT unit descriptor
   lpt_reg   LPT register list
*/

UNIT lpt_unit = {
   UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0) };

REG lpt_reg[] = {
   { FLDATA (ERR, lpterror, 0) },
   { HRDATA (LPDAR, LPDAR, 16) },
   { HRDATA (LPFLR, LPFLR, 8) },
   { HRDATA (LPIAR, LPIAR, 16) },
   { DRDATA (LINECT, linectr, 8) },
   { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
   { NULL } };

DEVICE lpt_dev = {
   "LPT", &lpt_unit, lpt_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &lpt_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &lpt_description };


/* -------------------------------------------------------------------- */

/* Printer: master routine */

int32 lpt (int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata;
   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

   if (debug_lvl & 0x10)
      fprintf(trace, "=P=> %04X %s %01X,%d,%04X LPDAR=%04X LPIAR=%04X LPFLR=%02X lc=%02d\n",
         IAR[level],
         opstr[op],
         m, n, data,
         LPDAR, LPIAR, LPFLR, linectr);

   switch (op) {
      case 0:      /* SIO */
         if (m != 0)        /* Dual feed carr. not supported */       
            return STOP_INVDEV;

         iodata = 0;
         switch (n) {
            case 0x00:      /* Spacing only */
               if (data > 3)
                   data = 0;
               iodata = carriage_control(SPACE, data);
               if (data != 0)
                  carr_busy = ON;
               print_busy = ON;
               sim_activate (&lpt_unit, 256);
               break;
            case 0x02:      /* Print & spacing */
               /* Check if LPIAR & LPDAR are at correct boundary */
//             if (((LPDAR & 0x00FF) != 0x7C) &&  /* At xx7C ? */
//                 ((LPDAR & 0x00FF) != 0x00))    /* At xx00 ? */
//                return STOP_INVDEV;
               if ((LPIAR & 0x00FF) != 0x00)      /* At xx00 ? */
                  return STOP_INVDEV;
               iodata = write_line();             /* Print line */
               if (data > 3)              
                  data = 0;
               if (iodata == SCPE_OK)   
                  iodata = carriage_control(SPACE, data);
               if (data != 0)
                  carr_busy = ON;
               print_busy = ON;
               sim_activate (&lpt_unit, 256);
               break;
            case 0x03:      /* Interrupt control */
               if (data & 0x80)
                  lpt_int_enabled = ON;     /* 1..00000 */
               else
                  lpt_int_enabled = OFF;    /* 0..00000 */
               if (data & 0x40) {
                  lpt_opend_int = OFF;      /* .1.00000 */
                  dev_int_req &= ~0x4000;   /* reset device interrupt */
               }
               if (data & 0x20) {
                  lpt_opend_int = OFF;      /* ..100000 */
                  dev_int_req &= 0xbfff;    /* reset device interrupt */
               }
               break;
            case 0x04:      /* Skip only */
//             if (data > LPFLR)            /* valid skip value ? */
//                return STOP_INVDEV;
               iodata = carriage_control(SKIP, data);
               carr_busy = ON;
               print_busy = ON;
               sim_activate (&lpt_unit, 256);
               break;
            case 0x06:      /* Print and skip */
               /* Check if LPIAR & LPDAR are at correct boundary */
               if (((LPDAR & 0x00FF) != 0x7C) &&  /* At xx7C ? */
                   ((LPDAR & 0x00FF) != 0x00))    /* At xx00 ? */
                  return STOP_INVDEV;
               if ((LPIAR & 0x00FF) != 0x00)      /* At xx00 ? */
                  return STOP_INVDEV;
               iodata = write_line();             /* Print line */
               if (data > LPFLR)                  /* valid skip value ? */
                  return STOP_INVDEV;
               if (iodata == SCPE_OK)
                  iodata = carriage_control(SKIP, data);
               carr_busy = ON;
               print_busy = ON;
               sim_activate (&lpt_unit, 256);
               break;
            default:
               return STOP_INVDEV;
         }
         return iodata;

      case 1:     /* LIO */
         switch (n) {
            case 0x00:      /* LPFLR */
               LPFLR = (data >> 8) & 0x00ff;
               break;
            case 0x04:
               LPIAR = data & 0xffff;
               break;
            case 0x06:
               LPDAR = data & 0xffff;
               break;
            default:
               return STOP_INVDEV;
         }
         return SCPE_OK;

      case 2:      /* TIO */
      case 4:      /* APL */
         iodata = 0;
         switch (n) {
            case 0x00:      /* Not ready/check */
               if (lpterror)
                  iodata = TRUE;
               if ((lpt_unit.flags & UNIT_ATT) == 0)
                  iodata = TRUE;
               break;
            case 0x02:      /* Buffer Busy */
               if (print_busy || carr_busy)
                  iodata = TRUE;
               else
                  iodata = FALSE;
               break;
            case 0x03:      /* Interrupt pending ? */
               if (lpt_opend_int)
                  iodata = TRUE;
               else
                  iodata = FALSE;
               break;
            case 0x04:      /* Carriage Busy */
               if (carr_busy)
                  iodata = TRUE;
               else
                  iodata = FALSE;
               break;
            case 0x06:      /* Printer busy */
               if (print_busy)
                  iodata = TRUE;
               else
                  iodata = FALSE;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         return ((SCPE_OK << 16) | iodata);

      case 3:      /* SNS */
         switch (n) {
            case 0x00:      /* Line count */
               iodata = (linectr << 8);
               break;
            case 0x01:      /* Cntr & incr values */
               iodata = 0x0000;
               break;
            case 0x02:      /* Timing data */
               iodata = 0x0000;
               break;
            case 0x03:      /* Check status */
               iodata = 0x0008;
               break;
            case 0x04:      /* LPIAR */
               iodata = LPIAR;
               break;
            case 0x06:      /* LPDAR */
               iodata = LPDAR;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         if (debug_lvl & 0x10)
            fprintf (trace, "=P=> Sense = %04X\n", iodata);
         return ((SCPE_OK << 16) | iodata);

      default:
         break;
   }
   return SCPE_OK;
}


/*** PRINT ROUTINE ***/

t_stat write_line ()
{
   int32 bp;                             /* buffer pointer */
   int32 i, t;
   static char lbuf[LPT_WIDTH + 1];      /* + null eol */

   if ((lpt_unit.flags & UNIT_ATT) == 0)
      return SCPE_UNATT;

   lpterror = OFF;                       /* clear error */
   bp = LPDAR;                           

   for (i = 0; i < LPT_WIDTH; i++) {     /* convert print buf */
      t = GetMem(bp, IO);                /* get character */
      t = ebcdic_to_ascii[t];            /* make it ASCII */
 
      if ((t < 0x20) || (t > 0x80)) {    /* is it printable ? */
         lbuf[i] = 0x20;                 /* NO  */
      } else { 
         lbuf[i] = t;                    /* YES */
         PutMem(bp, IO, 0x40);           /* clear storage after print */
      }
      bp++;                              /* bump buffer pointer */
   }

   for (i = LPT_WIDTH - 1; (i >= 0) && (lbuf[i] == ' '); i--) 
       lbuf[i] = 0;                      /* place eol */

   fputs (lbuf, lpt_unit.fileref);       /* write line */

   if (ferror (lpt_unit.fileref)) {      /* error ? */
      sim_perror ("Line printer I/O error");
      clearerr (lpt_unit.fileref);
      lpterror = ON;
   }
   fflush(lpt_unit.fileref);             // jb

   return SCPE_OK;
}


/*** CARRIAGE CONTROL ROUTINE ***

   Inputs:
   action = SPACE or SKIP
   mod    = nr of lines or line nr
*/

t_stat carriage_control (int32 action, int32 data)
{
   int32 i;
   if ((lpt_unit.flags & UNIT_ATT) == 0)
      return SCPE_UNATT;

   switch (action) {
      case SPACE:                              /* space lines now */
         if (data == 0) break;
         for (i = 0; i < data; i++) {          /* space */
            linectr = linectr + 1;
            if (linectr > LPFLR) {
               fputs ("\n\f", lpt_unit.fileref); /* force ff */
               linectr = 1;
            } else {
               fputs ("\n", lpt_unit.fileref); /* space */
            }
         }
         break;
      case SKIP:                               /* skip to line # */
         if (data < linectr) {                 /* skip to next page ? */
            fputs ("\f", lpt_unit.fileref);    /* TOF */
            linectr = 1;
         }
         data = data - linectr;                /* # of lines to skip */
         if (data == 0) break;
         for (i = 0; i < data; i++) {          /* space */
            linectr = linectr + 1;
            if (linectr > LPFLR) {
               fputs ("\n\f", lpt_unit.fileref); /* force ff */
               linectr = 1;
            } else {
               fputs ("\n", lpt_unit.fileref); /* space */
            }
         } 
         break;
   }
   lpt_unit.pos = ftell (lpt_unit.fileref);    /* update position */
   return SCPE_OK;
}


/*** SERVICE ROUTINE ***/

t_stat lpt_svc (UNIT *uptr)
{
   if (debug_lvl & 0x10)
      fprintf(trace, "=P=> LPT: Print or Skip/Space has ended. \n");
   print_busy = OFF;
   carr_busy = OFF;

   if (lpt_int_enabled) {
      dev_int_req |= 0x4000;    /* Request device interrupt */
      lpt_opend_int = ON;       /* Set interrupt pending */
   }
   return SCPE_OK;
}


/*** RESET ROUTINE ***/

t_stat lpt_reset (DEVICE *dptr)
{
   lpt_int_enabled = OFF;       /* Reset some flags */
   lpt_opend_int = OFF;
   print_busy = OFF;
   carr_busy = OFF;
   linectr = 0;
   lpterror = OFF;
   return SCPE_OK;
}


/*** ATTACH ROUTINE ***/

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
   lpterror = OFF;
   linectr = 1;
   return attach_unit (uptr, cptr);
}

const char *lpt_description (DEVICE *dptr)
{
   return "IBM 1403/5203 line printer";
}
