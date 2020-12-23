/* s3_bsca.c: IBM BSCA Binary Synchronous Communications Adaptor

   Copyright (c) 2010 Henk Stegeman

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

   bsca     BSCA Binary Synchronous Communications Adaptor

*/

#include "s3_defs.h"

t_stat bsca_reset (DEVICE *dptr);
t_stat bsca_svc (UNIT *uptr);
t_stat bsca_attach (UNIT *uptr, char *cptr);

int32 BSSAR;                    /* Stop Address Register */
int32 BSTAR;                    /* Transition Address Register */
int32 BSCAR;                    /* Current Address Register */
int32 bsca_int_enabled = OFF;   /* Interrupt enabled flag */
int32 bsca_opend_int = OFF;     /* Op End interrupt pending flag */
int32 bsca_busy = OFF;          /* BSCA busy flag */
int32 bsca_err  = OFF;          /* BSCA error flag */

/* BSCA data structures

   bsca_dev  BSCA device descriptor
   bsca_unit BSCA unit descriptor
   bsca_reg  BSCA register list
*/

UNIT bsca_unit = {
   UDATA (&bsca_svc, UNIT_SEQ+UNIT_ATTABLE, 0) };

REG bsca_reg[] = {
   { FLDATA (ERR, bsca_err, 0) },
   { HRDATA (BSSAR, BSSAR, 16) },
   { HRDATA (BSTAR, BSTAR, 16) },
   { HRDATA (BSCAR, BSCAR, 16) },
   { DRDATA (POS, bsca_unit.pos, T_ADDR_W), PV_LEFT },
   { NULL } };

DEVICE bsca_dev = {
   "BSCA", &bsca_unit, bsca_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &bsca_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10 };


/* -------------------------------------------------------------------- */

/* BSCA: master routine */

int32 bsca (int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata;
   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

   if (debug_lvl & 0x40)
      fprintf(trace, "=B=> %04X %s %01X,%d,%04X BSSAR=%04X BSTAR=%04X BSCAR=%04X\n",
         IAR[level],
         opstr[op],
         m, n, data,
         BSSAR, BSTAR, BSCAR);

   switch (op) {
      case 0:      /* SIO */
         if (m != 0)        /* BSCA-2 not supported */
            return STOP_INVDEV;

         iodata = 0;
         switch (n) {
            case 0x00:      /*  */
               sim_activate (&bsca_unit, 256);
               break;
            case 0x02:      /*  */
               sim_activate (&bsca_unit, 256);
               break;
            case 0x03:      /* Interrupt control */
               if (data & 0x80)
                  bsca_int_enabled = ON;    /* 1..00000 */
               else
                  bsca_int_enabled = OFF;   /* 0..00000 */
               if (data & 0x40) {
                  bsca_opend_int = OFF;     /* .1.00000 */
                  dev_int_req &= ~0x4000;   /* reset device interrupt */
               }
               if (data & 0x20) {
                  bsca_opend_int = OFF;     /* ..100000 */
                  dev_int_req &= 0xbfff;    /* reset device interrupt */
               }
               break;
            default:
               return STOP_INVDEV;
         }
         return iodata;

      case 1:     /* LIO */
         switch (n) {
            case 0x01:      /* Start Addr Reg. */
               BSSAR = data & 0xffff;
               break;
            case 0x02:      /* Transition Addr Reg. */
               BSTAR = data & 0xffff;
               break;
            case 0x04:      /* Current Addr Reg. */
               BSCAR = data & 0xffff;
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
               if (bsca_err)
                  iodata = TRUE;
               if ((bsca_unit.flags & UNIT_ATT) == 0)
                  iodata = TRUE;
               break;
            case 0x02:      /* Busy */
               iodata = TRUE;
               break; 
            default:
               return (STOP_INVDEV << 16);
         }
         return ((SCPE_OK << 16) | iodata);

      case 3:      /* SNS */
         switch (n) {
            case 0x01:      /* Start Addr Reg. */
               iodata = BSSAR;
               break;
            case 0x02:      /* Transition Addr Reg. */
               iodata = BSTAR;
               break;
            case 0x04:      /* Current Addr Reg. */
               iodata = BSCAR;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         if (debug_lvl & 0x40)
            fprintf (trace, "=B=> Sense = %04X\n", iodata);
         return ((SCPE_OK << 16) | iodata);

      default:
         break;
   }
   return SCPE_OK;
}


/*** SERVICE ROUTINE ***/

t_stat bsca_svc (UNIT *uptr)
{
   if (debug_lvl & 0x40)
      fprintf(trace, "=B=> BSCA: Communication has ended. \n");
   bsca_busy = OFF;

   if (bsca_int_enabled) {
      dev_int_req |= 0x4000;    /* Request device interrupt */
      bsca_opend_int = ON;      /* Set interrupt pending */
   }
   return SCPE_OK;
}


/*** RESET ROUTINE ***/

t_stat bsca_reset (DEVICE *dptr)
{
   bsca_int_enabled = OFF;      /* Reset some flags */
   bsca_opend_int = OFF;
   bsca_err = OFF;
   return SCPE_OK;
}


/*** ATTACH ROUTINE ***/

t_stat bsca_attach (UNIT *uptr, char *cptr)
{
   bsca_err = OFF;
   return attach_unit (uptr, cptr);
}
