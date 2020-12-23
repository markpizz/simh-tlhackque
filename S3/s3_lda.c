/* s3_lda.c: System/3  LDA/3270 Local Display Adaptor

   Copyright (c) 2001, Charles E Owen
   Copyright (c) 2009, Henk Stegeman

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

   lda       LDA/3270 local display adaptor
*/

#include "s3_defs.h"

int32 lda (int32 adap, int32 op, int32 m, int32 n, int32 data);
t_stat lda_svc (UNIT *uptr);
t_stat lda_reset (DEVICE *dptr);
const char *lda_description (DEVICE *dptr);

/*  LDA data structures

   lda_dev  TTI device descriptor
   lda_unit TTI unit descriptor
   lda_reg  TTI register list
*/

int32 LSAR;                   /* Stop Address Register */
int32 LTAR;                   /* Transition Address Register */
int32 LCAR;                   /* Current Address Register */
int32 att_enabled = OFF;      /* Attachment enabled flag */
int32 MC_enabled = OFF;       /* Micro-Controller enabled flag */
int32 lda_int_enabled = OFF;  /* Interrupt enabled flag */
int32 lda_pend_int = OFF;     /* Interrupt pending flag */
int32 lda_opend_int = OFF;    /* Op End Interrupt flag */
int32 lda_ITB_int = OFF;      /* Intermediate Text Block flag */
int32 lda_busy = OFF;         /* MC is busy flag */
int32 TX_cbuf = OFF;          /* Transmit cbuf flag */
unsigned char cbuf[512];      /* Console buffer */
int32 fd;                     /* File Descriptor */
int32 newtio_flag = OFF;      /* New tio is set */
static unsigned char rbuf[246];/* 246 bytes recv buffer */
static unsigned char tbuf[1]; /* 1 byte xmit buffer */

int32 Cntl_Stor[2048];        /* Micro-Controller Control Storage */

#define SOH 0x01              /* Start of Header */
#define STX 0x02              /* Start of Text */
#define ETX 0x03              /* End of Text */
#define ENQ 0x05              /* Enquiry */
#define ACK 0x06              /* Acknowlegment */
#define NAK 0x15              /* Not Ack. */
#define ITB 0x1F              /* Intermediate block */

/* Local Display Adaptor */
UNIT lda_unit = {
     UDATA (&lda_svc, UNIT_FIX+UNIT_ATTABLE, 0), 40000 };

REG lda_reg[] = {
   { HRDATA (LSAR, LSAR, 16) },
   { HRDATA (LTAR, LTAR, 16) },
   { HRDATA (LCAR, LCAR, 16) },
   { HRDATA (FLAG, lda_unit.u3, 16) },
   { HRDATA (IBUF, lda_unit.buf, 8) },
   { HRDATA (OBUF, lda_unit.u4, 8) },
   { DRDATA (POS,  lda_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, lda_unit.wait, 16), REG_NZ },
   { NULL } };

DEVICE lda_dev = {
   "LDA", &lda_unit, lda_reg, NULL,
   1, 10, 31, 1, 8, 8,
   NULL, NULL, &lda_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &lda_description };

int32 t = 0x70;

/* -------------------------------------------------------------------- */

/* LDA: master routine */

int32 lda1 (int32 op, int32 m, int32 n, int32 data)
{
   int32 r;
   r = lda(5, op, m, n, data);   /* Attachment Control */
   return (r);
}

int32 lda2 (int32 op, int32 m, int32 n, int32 data)
{
   int32 r;
   r = lda(8, op, m, n, data);   /* Terminal Control */
   return (r);
}


/* LDA: operational routine */

int32 lda (int32 adap, int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata= 0x0000;

   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

   UNIT *uptr;
   uptr = lda_dev.units;

   if (debug_lvl & 0x40)
      fprintf(trace,
         "=L=> %04X %s %01X,%d,%04X DA=%1X LSAR=%04X LTAR=%04X LCAR=%04X \n",
         IAR[level],
         opstr[op],
         m, n, data,
         adap, LSAR, LTAR, LCAR);

   lda_unit.u4 = data & 0xFF;       /* save R-byte for lda_svc routine */

/*******************************************************************/
/*    Process all DA=4 instructions - Attachment Control           */
/*******************************************************************/
   if (adap == 4) {
      switch (op) {
         case 0:    /* SIO  LDA */
            return STOP_INVDEV;
            break;

         case 1:    /* LIO  LDA */
            break;

         case 2:    /* TIO  LDA */
            break;

         case 3:    /* SNS  LDA */
            if (n == 7)
               iodata = 0x4000;
            else
               iodata = 0x0000;
            if (debug_lvl & 0x40)
               fprintf(trace, "=L=> Sense = %04X \n", iodata);
            iodata |= ((SCPE_OK << 16) & 0xffff0000);
            return iodata;
            break;

         case 4:    /* APL  LDA */
            return STOP_INVDEV;

         default:   /* None of the above */
            break;
      }
   }

/*******************************************************************/
/*    Process all DA=5 instructions - Attachment Control           */
/*******************************************************************/
   if (adap == 5) {
      switch (op) {
         case 0:    /* SIO  LDA */
            if (n != 0) {
               return STOP_INVDEV;           /* Invalid n code */
            }
            if (n == 0)   {
               if (data & 0x80) {
                  if (data & 0x40) {
                     att_enabled = ON;       /* 11...... */
                  } else {
                     att_enabled = OFF;      /* 10...... */
                     lda_pend_int = OFF;
                     /* reset device interrupt */
                     dev_int_req &= ~0x0100;
                     LSAR = 0x0000;          /* Reset LSAR */
                  }

                  if (data & 0x20) {
                     MC_enabled = ON;        /* 1.1..... */
                  } else {
                     MC_enabled = OFF;       /* 1.0..... */
                     lda_pend_int = OFF;
                     /* reset device interrupt */
                     dev_int_req &= ~0x0100;
                  }
                  return SCPE_OK;
               }
            }
            break;

         case 1:    /* LIO  LDA */
            switch (n) {
               case 0x00:  /* Control storage */
                  Cntl_Stor[LSAR++] = data;  /* Load control storage */
                  return SCPE_OK;
                  break;
               case 0x01:  /* Op decode registers */
                  return SCPE_OK;
               default:
                  return STOP_INVDEV;
                  break;
            }
            break;

         case 2:    /* TIO  LDA */
            /* All TIO's are for diagnostic purposes */
            /* TIO M=1, N=5 checks for any attachments checks */
            return (SCPE_OK << 16);
            break;

         case 3:    /* SNS  LDA */
            switch (n) {
               case 0x00:  /* Control storage */
                  iodata = Cntl_Stor[LSAR++]; /* Read control storage */
                  break;
               case 0x01:  /* Op decode registers */
                  iodata = 0x55AA;
                  break;
               default:
                  return (STOP_INVDEV << 16);
                  break;
            }
            if (debug_lvl & 0x40)
               fprintf(trace, "=L=> Sense = %04X \n", iodata);
            iodata |= ((SCPE_OK << 16) & 0xffff0000);
            return iodata;
            break;

         case 4:    /* APL  LDA */
            return STOP_INVDEV;

         default:   /* None of the above */
            break;
      }
   }

/*******************************************************************/
/*    Process all DA=8 instructions - Terminal Control             */
/*******************************************************************/
   if (adap == 8) {
      switch (op) {
         case 0:    /* SIO  LDA */
            switch (n) {
               case 0x00:   /* Control */
                  iodata = SCPE_OK;
//                if ((data & 0xC0) = 0xC0) {  /* Enable adaptor */
//                }
                  if (data & 0x01) {  /* Reset interrupt req. */
                     lda_pend_int = OFF;
                     iodata = RESET_INTERRUPT;
                  }
                  if (data & 0x02) {  /* Enable interrupt req. */
                     lda_int_enabled = ON;
                  } else {
                     lda_int_enabled = OFF;
                  }
                  break;
               case 0x02:   /* Transmit & receive */
                  iodata = SCPE_OK;
                  if (data & 0x01) {  /* Reset interrupt req. */
                     lda_pend_int = OFF;
                     iodata = RESET_INTERRUPT;
                  }
                  if (data & 0x02) {  /* Enable interrupt req. */
                     lda_int_enabled = ON;
                  } else {
                     lda_int_enabled = OFF;
                  }
                  if ((uptr -> flags & UNIT_ATT) == 0)
                     return SCPE_UNATT;

                  sim_activate(uptr, uptr -> wait);
                  lda_busy = ON;
                  break;
               default:
                  iodata = STOP_INVDEV;
                  break;
            }
            return iodata;
            break;

         case 1:    /* LIO  LDA */
            if (m == 1) {
               switch (n) {
                  case 0x01: /* Stop addres register */
                     LSAR = data;
                     return SCPE_OK;
                     break;
                  case 0x02: /* Transition address register */
                     LTAR = data;
                     return SCPE_OK;
                     break;
                  case 0x04: /* Current addres register */
                     LCAR = data;
                     return SCPE_OK;
                     break;
                  default:   /* n codes 1, 2, 4 are valid */
                     return STOP_INVDEV;
                     break;
               }
            } else {    /* m == 0 */
               return STOP_INVDEV;
            }
            break;

         case 2:    /* TIO  LDA */
         case 4:    /* APL  LDA */
            if (m == 1) {
               /* m == 1: BSCA 2 */
               iodata = 0;
               switch (n) {
                  case 0x00: /* Not ready/unit check */
                     if (uptr -> u3 != 0x0000)
                        iodata = 1;
                     if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata = 1;
                     break;
                  case 0x01: /* Op End interrupt */
                     if (lda_opend_int == ON)
                        iodata = 1;
                     lda_opend_int = OFF;  /* TIO resets int */
                     break;
                  case 0x02: /* Busy */
                     if (lda_busy == ON)
                        iodata = 1;
                     break;
                  case 0x03: /* ITB interrupt */
                     if (lda_ITB_int == ON)
                        iodata = 1;
                     lda_ITB_int = OFF;    /* TIO resets int */
                     break;
                  case 0x04: /* Interrupt pending */
                     if (lda_pend_int == ON)
                        iodata = 1;
                     break;
                  default:   /* n codes 0 - 4 are valid */
                     return (STOP_INVDEV << 16);
                     break;
               }
               return ((SCPE_OK << 16) | iodata);
            } else {
               /* m == 0: BSCA 1 */
               iodata = 0;
               return ((SCPE_OK << 16) | iodata);
            }
            break;

         case 3:    /* SNS  LDA */
            switch (n) {
               case 0x00:  /* CE diagnostics */
               case 0x05:
               case 0x06:
               case 0x07:
                  iodata = 0x0000;
                  break;
               case 0x01:  /* Stop adress register */
                  iodata = LSAR;
                  break;
               case 0x02:  /* Transition adress register */
                  iodata = LTAR;
                  break;
               case 0x03:  /* Status bytes 1 and 2 */
                  iodata = 0x0002;
                  break;
               case 0x04:  /* Current adress register */
                  iodata = LCAR;
                  break;
               default:
                  return (STOP_INVDEV << 16);
                  break;
            }

            if (debug_lvl & 0x40)
               fprintf(trace, "=L=> Sense = %04X \n", iodata);
            iodata |= ((SCPE_OK << 16) & 0xffff0000);
            return iodata;
            break;

         default:   /* None of the above */
            break;
      }
   }
return SCPE_OK;
}


/* LDA unit service */

t_stat lda_svc (UNIT *uptr)
{
   int32 tt, i;
   tt = LCAR;
   LCAR = LTAR;

   PutMem(LCAR++, 0, 0x01);
   PutMem(LCAR++, 0, 0x6C);
   PutMem(LCAR++, 0, 0xD9);
   PutMem(LCAR++, 0, 0x02);
   PutMem(LCAR++, 0, 0x40);
   PutMem(LCAR++, 0, 0x40);
   PutMem(LCAR++, 0, 0xC2);
   PutMem(LCAR++, 0, 0x40);
   PutMem(LCAR++, 0, 0x03);
   PutMem(LCAR  , 0, 0xFF);

i = LCAR + 1;
sim_printf("\n\rLDA:");
while (i    > tt)
sim_printf(" %02X",
   GetMem(tt++, IO));
sim_printf("\n\rLDA: %04X - %04X - %04X ", LCAR, LTAR, LSAR);

   lda_busy = OFF;
   lda_pend_int = ON;
   lda_opend_int = ON;

   if (lda_int_enabled == ON) {
      if (lda_pend_int == ON)
         dev_int_req |= 0x0100;      /* Interrupt CPU */
   }
   return SCPE_OK;
}


/* LDA reset routine */

t_stat lda_reset (DEVICE *dptr)
{
   lda_unit.buf = 0;
   lda_int_enabled = OFF;        /* Reset all interrupt flags */
   lda_pend_int = OFF;
   lda_busy = OFF;
   MC_enabled = OFF;
   TX_cbuf = OFF;
   att_enabled = OFF;
   dev_int_req &= ~0x0100;       /* Reset interrupt */
   lda_unit.u3 = 0x00;           /* Reset sense bits */
   newtio_flag = OFF;            /* TRUE if new tio set. */
   return SCPE_OK;
}

const char *lda_description (DEVICE *dptr)
{
   return "IBM LDA/3270 Local Display Adaptor";
}
