/* s3_mfcu.c: IBM 5424 card reader/punch for the model 10.

   Copyright (c) 2006-2009, Henk Stegeman
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

   pchop    primairy card hopper.
   schop    secundairy card hopper
   stk1     card stacker 1
   stk2     card stacker 2
   stk3     card stacker 3
   stk4     card stacker 4

   pchop
     |                prim ws
     +---> reader --->       ---> punch ---+----+----+----+
     |                sec  ws               \    \    \    \
   schop                                    stk1 stk2 stk3 stk4

   Operation sequence:
   1 - Move card from ws -> punch -> print -> stacker.
       The selected wait station is now empty.
   2 - Feed a card from p/s hopper -> read -> ws.

   Normally, cards are represented as ASCII text streams terminated by newlines.
   This allows cards to be created and edited as normal files.  Set the EBCDIC
   flag on the card unit allows cards to be read or punched in EBCDIC format,
   suitable for binary data.
   IPL requires two transalation tables:
   1) ebcdic to 6 bits BA8421 punch card code.
   2) BA8421 6 bits + DC bits to IPL code.

*/

#include "s3_defs.h"

#define EMPTY 0
#define FILLED 1

unsigned char pw_buf[MFCU_WIDTH + 1];  /* prim wait station/buffer */
int32 pw_stat = EMPTY;                 /* status prim wait station */
unsigned char sw_buf[MFCU_WIDTH + 1];  /* sec wait station/buffer  */
int32 sw_stat = EMPTY;                 /* status sec wait station  */
unsigned char pch_buf[MFCU_WIDTH + 1]; /* punch buffer            */

t_stat card_svc (UNIT *uptr);
t_stat prtbuf1_svc (UNIT *uptr);
t_stat prtbuf2_svc (UNIT *uptr);
t_stat pchop_attach (UNIT *pchop_unit, CONST char *cptr);
t_stat schop_attach (UNIT *schop_unit, CONST char *cptr);
t_stat mfcu_boot (int32 unitno, DEVICE *dptr);
const char *mfcu_description (DEVICE *dptr);
t_stat mfcu_reset (DEVICE *dptr);
t_stat feed_card (int32 m, UNIT *stkptr);
t_stat read_card (int32 m);
t_stat punch_card (int32 m, UNIT *stkptr);
t_stat drop_card (int32 m, UNIT *stkptr);

UNIT *stkptr;

int32 TBAR;                   /* Print buffer address register */
int32 RBAR;                   /* Read buffer address register */
int32 PBAR;                   /* Punch buffer address register */
int32 ICR;                    /* Interrupt control register */
int32 iplread = OFF;          /* IPL read flag */
int32 readerr = OFF;          /* Read error flag */
int32 puncherr = OFF;         /* Punch error flag */
int32 mfcu_notready = OFF;    /* Not ready error flag */
int32 mfcu_int_enabled = OFF; /* Interrupt enabled flag */
int32 mfcu_opend_int = OFF;   /* Op End interrupt pending flag */
int32 mfcu_prt1_int = OFF;    /* Print buffer 1 interrupt */
int32 mfcu_prt2_int = OFF;    /* Print buffer 2 interrupt */
int32 pch_busy = OFF;         /* Punch busy */
int32 fdrd_busy = OFF;        /* Feed/read busy */
int32 pb1_busy = OFF;         /* Printer buffer 1 busy */
int32 pb2_busy = OFF;         /* Printer buffer 2 busy */
int32 pchop_ebcdic = 0;       /* 1 = EBCDIC when feeding from prim hopper */
int32 schop_ebcdic = 0;       /* 1 = EBCDIC when feeding from sec hopper */

/* Primairy card hopper data structures

   pchop_dev   PCH device descriptor
   pchop_unit  PCH unit descriptor
   pchop_reg   PCH register list
*/

UNIT pchop_unit = {
   UDATA (&card_svc, UNIT_SEQ+UNIT_ATTABLE, 0), 2 };

REG pchop_reg[] = {
   { FLDATA (ERR, readerr, 0) },
   { FLDATA (NOTRDY, mfcu_notready, 0) },
   { HRDATA (TBAR, TBAR, 16) },
   { HRDATA (RBAR, RBAR, 16) },
   { HRDATA (PBAR, PBAR, 16) },
   { HRDATA (ICR, ICR, 16) },
   { FLDATA (EBCDIC, pchop_ebcdic, 0) },
   { DRDATA (POS, pchop_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, pchop_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, pw_buf, 8, 8, MFCU_WIDTH) },
   { NULL } };

DEVICE pchop_dev = {
   "PCH", &pchop_unit, pchop_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   &mfcu_boot, &pchop_attach, NULL, NULL,
   DEV_M10|DEV_DISABLE, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };


/* Secundairy card hopper data structures

   schop_dev   SCH device descriptor
   schop_unit  SCH unit descriptor
   schop_reg   SCH register list
*/

UNIT schop_unit = {
   UDATA (&card_svc, UNIT_SEQ+UNIT_ATTABLE, 0), 75 };

REG schop_reg[] = {
   { FLDATA (ERR, readerr, 0) },
   { FLDATA (NOTRDY, mfcu_notready, 0) },
   { HRDATA (TBAR, TBAR, 16) },
   { HRDATA (RBAR, RBAR, 16) },
   { HRDATA (PBAR, PBAR, 16) },
   { HRDATA (ICR, ICR, 16) },
   { FLDATA (EBCDIC, schop_ebcdic, 0) },
   { DRDATA (POS, schop_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, schop_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, sw_buf, 8, 8, MFCU_WIDTH) },
   { NULL }  };

DEVICE schop_dev = {
   "SCH", &schop_unit, schop_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   NULL, &schop_attach, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };


/* Printer buffer 1 & 2.

   These units are needed to issue an async interrupt after
   an op-end interrupt.
*/
UNIT prtbuf1_unit = {
   UDATA (&prtbuf1_svc, 0, 0), 150 };

UNIT prtbuf2_unit = {
   UDATA (&prtbuf2_svc, 0, 0), 150 };


/* Stacker data structures

   stackn_dev   STACK device descriptor
   stackn_unit  STACK unit descriptors
   stackn_reg   STACK register list
*/

UNIT stack1_unit = {
   UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) } ;

REG stack1_reg[] = {
   { DRDATA (POS1, stack1_unit.pos, 31), PV_LEFT },
   { NULL } };

DEVICE stack1_dev = {
   "STK1", &stack1_unit, stack1_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };

UNIT stack2_unit = {
   UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) } ;

REG stack2_reg[] = {
   { DRDATA (POS2, stack2_unit.pos, 31), PV_LEFT },
   { NULL }  };

DEVICE stack2_dev = {
   "STK2", &stack2_unit, stack2_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };

UNIT stack3_unit = {
   UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) } ;

REG stack3_reg[] = {
   { DRDATA (POS3, stack3_unit.pos, 31), PV_LEFT },
   { NULL }  };

DEVICE stack3_dev = {
   "STK3", &stack3_unit, stack3_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };

UNIT stack4_unit = {
   UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) } ;

REG stack4_reg[] = {
   { DRDATA (POS4, stack4_unit.pos, 31), PV_LEFT },
   { NULL }  };

DEVICE stack4_dev = {
   "STK4", &stack4_unit, stack4_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &mfcu_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &mfcu_description };


/*-------------------------------------------------------------------*/
/* EBCDIC to BA8421 96 column translate table                        */
/*-------------------------------------------------------------------*/
unsigned char ebcdic_to_BA8421[] = {
/*  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
"\x00\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
"\x1A\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
"\x20\x11\x12\x13\x14\x15\x16\x17\x18\x19\x30\x1B\x1C\x1D\x1E\x1F"
"\x10\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
"\x00\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
"\x1A\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
"\x20\x11\x12\x13\x14\x15\x16\x17\x18\x19\x30\x1B\x1C\x1D\x1E\x1F"
"\x10\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
"\x00\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
"\x30\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
"\x20\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
"\x10\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
"\x00\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
"\x30\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
"\x20\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
"\x10\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
};

/*-------------------------------------------------------------------*/
/* BA8421 96 column + DC T3 bits IPL translate table.                */
/*-------------------------------------------------------------------*/
unsigned char BA8421_to_IPL[] = {
/*  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
"\x40\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\x7A\x7B\x7C\x7D\x7E\x7F"
"\xF0\x61\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\x50\x6B\x6C\x6D\x6E\x6F"
"\x60\xD1\xD2\xD3\xD4\xD5\xD6\xD7\xD8\xD9\x5A\x5B\x5C\x5D\x5E\x5F"
"\xD0\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\x4A\x4B\x4C\x4D\x4E\x4F"
/* D = 0; C = 1; */
"\x00\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\x3A\x3B\x3C\x3D\x3E\x3F"
"\xB0\x21\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\x10\x2B\x2C\x2D\x2E\x2F"
"\x20\x91\x92\x93\x94\x95\x96\x97\x98\x99\x1A\x1B\x1C\x1D\x1E\x1F"
"\x90\x81\x82\x83\x84\x85\x86\x87\x88\x89\x0A\x0B\x0C\x0D\x0E\x0F"
/* D = 1; C = 0; */
"\xC0\x71\x72\x73\x74\x75\x76\x77\x78\x79\xFA\xFB\xFC\xFD\xFE\xFF"
"\x70\xE1\x62\x63\x64\x65\x66\x67\x68\x69\xEA\xEB\xEC\xED\xEE\xEF"
"\xE0\x51\x52\x53\x54\x55\x56\x57\x58\x59\xDA\xDB\xDC\xDD\xDE\xDF"
"\x6A\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\xCB\xCC\xCD\xCE\xCF"
/* D = 1; C = 1; */
"\x80\x31\x32\x33\x34\x35\x36\x37\x38\x39\xBA\xBB\xBC\xBD\xBE\xBF"
"\x30\xA1\x22\x23\x24\x25\x26\x27\x28\x29\xAA\xAB\xAC\xAD\xAE\xAF"
"\xA0\x11\x12\x13\x14\x15\x16\x17\x18\x19\x9A\x9B\x9C\x9D\x9E\x9F"
"\x2A\x01\x02\x03\x04\x05\x06\x07\x08\x09\x8A\x8B\x8C\x8D\x8E\x8F"
};

/* -------------------------------------------------------------------- */

/* 5424: Master routine */

int32 mfcu (int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata = 0;
   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };
   if (debug_lvl & 0x08)
      fprintf(trace, "=F=> %04X %s %01X,%d,%04X RBAR=%04X PBAR=%04X ICR=%04X \n",
         IAR[level],
         opstr[op],
         m, n, data,
         RBAR, PBAR, ICR);

   switch (op) {
      case 0:      /* SIO 5424 */
         switch (data & 0x1F) {           /* Select stacker */
            case 0x04:                    /* stacker 4 */
               stkptr = &stack4_unit;
               break;
            case 0x05:                    /* stacker 1 */
               stkptr = &stack1_unit;
               break;
            case 0x06:                    /* stacker 2 */
               stkptr = &stack2_unit;
               break;
            case 0x07:                    /* stacker 3 */
               stkptr = &stack3_unit;
               break;
            default:
               if (m == 0)                /* default... */
                  stkptr = &stack1_unit;  /* stack 1 for prim hopper */
               if (m == 1)
                  stkptr = &stack4_unit;  /* stack 4 for sec hopper */
               break;
         }
         if ((data & 0x58) == 0x40)       /* SIO IPL read ? */
            iplread = ON;
         else
            iplread = OFF;                /* normal read */

         switch (n) {
            case 0x00:        /* Feed */
            case 0x04:        /* Print and feed */
               iodata = drop_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = feed_card(m, stkptr);
               fdrd_busy = ON;
               break;

            case 0x01:        /* Feed and read */
            case 0x05:        /* Print feed and read */
               iodata = drop_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = feed_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = read_card( m );
               fdrd_busy = ON;
               break;

            case 0x02:        /* Punch and feed */
            case 0x06:        /* Punch, print and feed */
               iodata = punch_card(m, stkptr);
               pch_busy = ON;
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = drop_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = feed_card(m, stkptr);
               fdrd_busy = ON;
               break;

            case 0x03:        /* Punch, feed and read */
            case 0x07:        /* Punch, print, feed and read */
               iodata = punch_card(m, stkptr);
               pch_busy = ON;
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = drop_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = feed_card(m, stkptr);
               if (iodata != SCPE_OK)
                  return iodata;
               iodata = read_card( m );
               fdrd_busy = ON;
               break;
         }
         if (m == 0)
            sim_activate (&pchop_unit, pchop_unit.wait);   /* activate */
         if (m == 1)
            sim_activate (&schop_unit, schop_unit.wait);   /* activate */
         if (data > 0x03) {                                /* print op ? */ 
            switch(data & 0x80) {
               case 0x00:
                  pb1_busy = ON;                  
                  sim_activate (&prtbuf1_unit, prtbuf1_unit.wait);
                  break;
               case 0x80:
                  pb2_busy = ON;
                  sim_activate (&prtbuf2_unit, prtbuf2_unit.wait);
                  break;
            }   
         }
         return iodata;

      case 1:       /* LIO 5424 */
         switch (n) {
            case 0x04:     /* Load Print data buffer addr */
               TBAR = data & 0xffff;
               break;
            case 0x05:     /* Load Read data buffer addr  */
               RBAR = data & 0xffff;
               break;
            case 0x06:     /* Load Punch data buffer addr */
               PBAR = data & 0xffff;
               break;
            case 0x07:     /* Load Interrupt Control Reg  */
               ICR = data & 0x00FF;
               if (ICR & 0x08) {          /* 0000x000 */
                  mfcu_int_enabled = ON;  /* Enable interrupt */
               } else {
                  mfcu_int_enabled = OFF; /* Disable interrupt */
               }
               if (ICR & 0x10) {          /* 00010000 */
                  mfcu_opend_int = OFF;   /* Reset op-end interrupt */
               }
               if (ICR & 0x20) {          /* 00100000 */
                  mfcu_prt1_int = OFF;    /* Reset prt buf 1 interrupt */
               }
               if (ICR & 0x40) {          /* 01000000 */
                  mfcu_prt2_int = OFF;    /* Reset prt buf 2 interrupt */
               }
               if (!(mfcu_opend_int || mfcu_prt1_int || mfcu_prt2_int))
                  dev_int_req &= ~0x8000; /* Any op-end interrupts ? */ 
               break;
            default:
               return STOP_INVDEV;
         }
         return SCPE_OK;

      case 2:      /* TIO 5424 */
      case 4:      /* APL 5424 */
         iodata = FALSE;
         switch (n) {
            case 0x00:     /* Not ready/check */
               if (readerr || puncherr || mfcu_notready)
                  iodata = TRUE;
               if ((m == 0) &&     /* pchop & stacker not attached ? */
                  ((stack1_unit.flags & UNIT_ATT) == 0) &&
                  ((pchop_unit.flags & UNIT_ATT) == 0) )
                     iodata = TRUE;         /* prim hopper not ready */
               if ((m == 1) &&     /* schop & stacker not attached ? */
                  ((stack4_unit.flags & UNIT_ATT) == 0) &&
                  ((schop_unit.flags & UNIT_ATT) == 0) )
                     iodata = TRUE;          /* sec hopper not ready */
               break;
            default:       /* All other conditions */ 
               if (data & 0x01) {  /* Read/feed busy ?  */
                  if (fdrd_busy) 
                     iodata = TRUE;
               }
               if (data & 0x02) {  /* Punch data busy ? */
                  if (pch_busy)
                     iodata = TRUE;
               }
               if (data & 0x04) {  /* Printer busy ?    */
                  if (pb1_busy || pb2_busy)
                     iodata = TRUE;
               }
               break; 
         }
         return ((SCPE_OK << 16) | iodata);
      break;

      case 3:      /* SNS 5424 */
         iodata = 0;
         switch (n) {
            case 0x00:      /* Interrupt pending */
               if (mfcu_opend_int || mfcu_prt1_int || mfcu_prt2_int)
                  iodata = 0x1000;
               break;
            case 0x01:      /* CE diagnostic bytes */
               break;
            case 0x03:      /* Status bytes */
               if (pw_stat)
                  iodata |= 0x2000; /* Card in wait 1 */
               if (sw_stat)
                  iodata |= 0x1000; /* Card in wait 2 */
               if (readerr)
                  iodata |= 0x0080; /* Read check */
               if (puncherr)
                  iodata |= 0x0040; /* Punch check */
//             if (????)
//                iodata |= 0x2000; /* Invalid punch char */
               break;
            case 0x04:      /* Printer buffer address */
               iodata = TBAR;
               break;
            case 0x05:      /* Read buffer address */
               iodata = RBAR;
               break;
            case 0x06:      /* Punch buffer address */
               iodata = PBAR;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         iodata |= ((SCPE_OK << 16) & 0xffff0000);
         return (iodata);

      default:
         break;
   }
   return (0);
}


/* Card drop routine

   m = 0 drop the card in primairy wait station in selected stacker.
   m = 1 drop the card in secundairy wait station in selected stacker.
   stkptr = points to selected stacker.
*/
t_stat drop_card (int32 m, UNIT *stkptr)
{
   int32 i;
   switch (m) {
      case 0:
         /********************************************/
         /*** Drop card from primairy wait station ***/
         /********************************************/
         if ((stkptr -> flags & UNIT_ATT) == 0) {     /* no stacker file attached ? */
            if (pw_stat == FILLED) {                  /* card in ws ? */
               /* No stacker file attached. Flush card in wait station. */
               for (i = 0; i < MFCU_WIDTH; i++)
                  pw_buf[i] = 0;                      /* clear wait station. */
               pw_stat = EMPTY;                       /* ws is now empty */
            }
            return SCPE_OK;
         } else {
            if (pw_stat == FILLED) {
               /* Drop card in wait station in selected stacker */
               if (pchop_ebcdic == 0) {
                  for (i = 0; i < MFCU_WIDTH; i++)    /* translate it back to ASCII */
                     pw_buf[i] = ebcdic_to_ascii[pw_buf[i]];
                  for (i = MFCU_WIDTH - 1; (i >= 0) && (pw_buf[i] == ' '); i--)
                     pw_buf[i] = 0;
               }

               pw_buf[MFCU_WIDTH] = 0x00;             /* null at end of record */
               if (pchop_ebcdic == 1) {
                  for (i = 0; i < MFCU_WIDTH; i++)
                     fputc(pw_buf[i], stkptr -> fileref); /* EBCDIC output */
               } else {
                  fputs (pw_buf, stkptr -> fileref);  /* ASCII output  */
                  fputc ('\n', stkptr -> fileref);    /* plus new line */
               }

               stkptr -> pos = ftell (stkptr -> fileref); /* update position */
               for (i = 0; i < MFCU_WIDTH; i++)
                  pw_buf[i] = 0;                      /* clear wait station. */
               pw_stat = EMPTY;

               if (ferror (stkptr -> fileref)) {      /* error ? */
                  sim_perror ("Card stacker I/O error");
                  clearerr (stkptr -> fileref);
               }
            }
            return SCPE_OK;
         }
      break;

      case 1:
         /**********************************************/
         /*** Drop card from secundairy wait station ***/
         /**********************************************/
         if ((stkptr -> flags & UNIT_ATT) == 0) {     /* no stacker file attached ? */
            if (sw_stat == FILLED) {                  /* card in ws ? */
               /* No stacker file attached. Flush card in wait station. */
               for (i = 0; i < MFCU_WIDTH; i++)
                  sw_buf[i] = 0;                      /* clear wait station. */
               sw_stat = EMPTY;                       /* ws is now empty */
            }
            return SCPE_OK;
         } else {
            if (sw_stat == FILLED) {
               /* Drop card in wait station in selected stacker */
               if (schop_ebcdic == 0) {
                  for (i = 0; i < MFCU_WIDTH; i++)    /* translate it back to ASCII */
                     sw_buf[i] = ebcdic_to_ascii[sw_buf[i]];
                  for (i = MFCU_WIDTH - 1; (i >= 0) && (sw_buf[i] == ' '); i--)
                     sw_buf[i] = 0;
               }

               sw_buf[MFCU_WIDTH] = 0x00;             /* null at end of record */
               if (schop_ebcdic == 1) {
                  for (i = 0; i < MFCU_WIDTH; i++)
                     fputc(sw_buf[i], stkptr -> fileref); /* EBCDIC output */
               } else {
                  fputs (sw_buf, stkptr -> fileref);  /* ASCII output  */
                  fputc ('\n', stkptr -> fileref);    /* plus new line */
               }

               stkptr -> pos = ftell (stkptr -> fileref); /* update position */
               for (i = 0; i < MFCU_WIDTH; i++)
                  sw_buf[i] = 0;                      /* clear wait station. */
               sw_stat = EMPTY;

               if (ferror (stkptr -> fileref)) {      /* error ? */
                  sim_perror ("Card stacker I/O error");
                  clearerr (stkptr -> fileref);
               }
            }
            return SCPE_OK;
         }
      break;
   }
   return (0);
}


/* Card feed routine

   m = 0 feed a card from primairy hopper.
   m = 1 feed a card from secundairy hopper.
   stkptr = points to selected stacker.
*/
t_stat feed_card (int32 m, UNIT *stkptr)
{
   int32 i;
   readerr = mfcu_notready = 0;                       /* reset flags */
   switch (m) {
      case 0:
         /*********************************/
         /*** Feed from primairy hopper ***/
         /*********************************/

         /* If selected stacker is attached and not the hopper:
            assume feeding blank cards from prim hopper */
         if (((stkptr -> flags & UNIT_ATT) != 0) &&
            ((pchop_unit.flags & UNIT_ATT) == 0)) {
            for (i = 0; i < MFCU_WIDTH; i++) {        /* feed blank card */
               pw_buf[i] = 0x40;                      /* into wait tstaion. */
            }
            pw_stat = FILLED;                         /* card in ws */
            return SCPE_OK;
         }

         if ((pchop_unit.flags & UNIT_ATT) == 0)      /* un-attached ? */
            return SCPE_UNATT;

         for (i = 0; i < MFCU_WIDTH; i++)             /* clear buffer */
            pw_buf[i] = 0x00;
         if (pchop_ebcdic) {
            for (i = 0; i < MFCU_WIDTH; i++) {
               pw_buf[i] = fgetc(pchop_unit.fileref); /* read EBCDIC */
            }
         } else {
            if (fgets ((char *)pw_buf, sizeof(pw_buf), pchop_unit.fileref)) {}; /* read ASCII */
//fprintf(trace, "=F=> HJS %s \n", pw_buf);
            for (i = 0; i < MFCU_WIDTH; i++) {
               if (pw_buf[i] == '\n' ||               /* remove ASCII CR/LF */
                   pw_buf[i] == '\r' ||
                   pw_buf[i] == 0x00)  pw_buf[i] = ' ';
               pw_buf[i] = ascii_to_ebcdic[pw_buf[i]]; /* make it EBCDIC */
            }
         }
         pw_stat = FILLED;                            /* card in ws */
         pchop_unit.pos = ftell (pchop_unit.fileref); /* update position */

         if (feof (pchop_unit.fileref)) {             /* last card ? */
            mfcu_notready = 1;
            detach_unit(&pchop_unit);                 /* detach card deck */
            return SCPE_UNATT;
         }
         if (ferror (pchop_unit.fileref)) {           /* error ? */
            sim_perror ("Card reader I/O error");
            clearerr (pchop_unit.fileref);
            readerr = 1;
            return SCPE_OK;
         }
         return SCPE_OK;
      break;

      case 1:
         /***********************************/
         /*** Feed from secundairy hopper ***/
         /***********************************/

         /* If selected stacker is attached and not the hopper:
            assume feeding blank cards from sec hopper */
         if (((stkptr -> flags & UNIT_ATT) != 0) &&
            ((schop_unit.flags & UNIT_ATT) == 0)) {
            for (i = 0; i < MFCU_WIDTH; i++) {        /* feed blank card */
               sw_buf[i] = 0x40;                      /* into wait tstaion. */
            }
            sw_stat = FILLED;                         /* card in ws */
            return SCPE_OK;
         }

         if ((schop_unit.flags & UNIT_ATT) == 0)      /* un-attached ? */
            return SCPE_UNATT;

         for (i = 0; i < MFCU_WIDTH; i++)             /* clear buffer */
            sw_buf[i] = 0x00;
         if (schop_ebcdic) {
            for (i = 0; i < MFCU_WIDTH; i++) {
               sw_buf[i] = fgetc(schop_unit.fileref); /* read EBCDIC */
            }
         } else {
            if (fgets ((char *)sw_buf, sizeof(sw_buf), schop_unit.fileref)) {}; /* read ASCII */
            for (i = 0; i < MFCU_WIDTH; i++) {
               if (sw_buf[i] == '\n' ||               /* remove ASCII CR/LF */
                   sw_buf[i] == '\r' ||
                   sw_buf[i] == 0x00)  sw_buf[i] = ' ';
               sw_buf[i] = ascii_to_ebcdic[sw_buf[i]]; /* make it EBCDIC */
            }
         }
         sw_stat = FILLED;                            /* card in ws */
         schop_unit.pos = ftell (schop_unit.fileref); /* update position */

         if (feof (schop_unit.fileref)) {             /* last card ? */
            mfcu_notready = 1;
            detach_unit(&schop_unit);                 /* detach card deck */
            return SCPE_UNATT;
         }
         if (ferror (schop_unit.fileref)) {           /* error ? */
            sim_perror ("Card reader I/O error");
            clearerr (schop_unit.fileref);
            readerr = 1;
            return SCPE_OK;
         }
         return SCPE_OK;
      break;
   }
   return (0);
}


/* Card read routine

   m = 0 read from primairy wait station.
   m = 1 read from secundairy wait station.
   stkptr = points to selected stacker.
*/
t_stat read_card (int32 m)
{
   int32 i, t;
   switch(m) {
      case 0:
         /**********************************/
         /*** Read primairy wait station ***/
         /**********************************/
         if (pw_stat == EMPTY) readerr = 1;
         if (iplread == OFF) {            /* Normal read */
            for (i = 0; i < MFCU_WIDTH; i++) {
               PutMem(RBAR, IO, pw_buf[i]); /* copy to main */
               RBAR++;
            }
         }
         if (iplread == ON) {             /* IPL read */
            /* IPL read bytes 1-32 */
            for (i = 0; i < 32; i++) {
               t = ebcdic_to_BA8421[pw_buf[i]];
               /* add DC bits from T3 */
               t = t | (pw_buf[i+64] & 0x0C) << 4;
               t = BA8421_to_IPL[t];
               PutMem(RBAR, IO, t);       /* copy to main */
               RBAR++;
            }
            /* IPL read bytes 33-64 */
            for (i = 32; i < 64; i++) {
               t = ebcdic_to_BA8421[pw_buf[i]];
               /* add DC bits from T3 */
               t = t | (pw_buf[i+32] & 0x03) << 6;
               t = BA8421_to_IPL[t];
               PutMem(RBAR, IO, t);       /* copy to main */
               RBAR++;
            }
            /* Read remaining (meaningless) 32 bytes */            
            for (i = 64; i < 96; i++) {
               t = ebcdic_to_BA8421[pw_buf[i]];
               t = BA8421_to_IPL[t];
               PutMem(RBAR, IO, t);       /* copy to main */
               RBAR++;
            }

         }
         return SCPE_OK;
      break;

      case 1:
         /************************************/
         /*** Read secundairy wait station ***/
         /************************************/
         if (sw_stat == EMPTY) readerr = 1;
         for (i = 0; i < MFCU_WIDTH; i++) {
            PutMem(RBAR, IO, sw_buf[i]);  /* copy to main mem */
            RBAR++;
         }
         return SCPE_OK;
      break;
   }
   return (0);
}


/* Card punch routine

   m = 0 punch card in primairy wait station.
   m = 1 punch card in secundairy wait station.
*/
t_stat punch_card (int32 m, UNIT *stkptr)
{
   int32 i;

   if ((stkptr -> flags & UNIT_ATT) == 0) /* un-attached ? */
      return(SCPE_UNATT);

   /* Copy punch data from main memory to pch_buf */
   for (i = 0; i < MFCU_WIDTH; i++) {
      pch_buf[i] = GetMem(PBAR, IO);      /* copy from main memory */
      PBAR++;
   }
   switch(m) {
      case 0:
         /***********************************/
         /*** Punch primairy wait station ***/
         /***********************************/
         if (pw_stat == EMPTY) puncherr = 1;
         for (i = 0; i < MFCU_WIDTH; i++) {
            /* check for punching in old data */
            if ((pw_buf[i] != 0x40) && (pch_buf[i] != 0x40)) {
               pw_buf[i] = pw_buf[i] | pch_buf[i];
               puncherr = 1;
            }
            if ((pw_buf[i] == 0x40) && (pch_buf[i] != 0x40))
               pw_buf[i] = pch_buf[i];
         }
         return SCPE_OK;
      break;

      case 1:
         /*************************************/
         /*** Punch secundairy wait station ***/
         /*************************************/
         if (sw_stat == EMPTY) puncherr = 1;
         for (i = 0; i < MFCU_WIDTH; i++) {
            /* check for punching in old data */
            if ((sw_buf[i] != 0x40) && (pch_buf[i] != 0x40)) {
               sw_buf[i] = sw_buf[i] | pch_buf[i];
               puncherr = 1;
            }
            if ((sw_buf[i] == 0x40) && (pch_buf[i] != 0x40))
               sw_buf[i] = pch_buf[i];
         }
         return SCPE_OK;
      break;
   }
   return (0);
}


/*** Card reader service. ***/

t_stat card_svc (UNIT *uptr)     /* uptr points to the prim or sec hopper */
{
   if (fdrd_busy == ON) {        /* FEED/READ op ended ? */
      fdrd_busy = OFF;
      if (debug_lvl & 0x08)
         fprintf(trace, "=F=> MFCU: Feed/read Op has ended.\n");
      if (mfcu_int_enabled) {
         dev_int_req |= 0x8000;  /* Request device interrupt */
         mfcu_opend_int = ON;    /* Set interrupt pending */
      }
   }
   if (pch_busy == ON) {         /* PUNCH op ended ? */
      pch_busy = OFF;
      if (debug_lvl & 0x08)
         fprintf(trace, "=F=> MFCU: Punch Op has ended.\n");
      if (mfcu_int_enabled) {
         dev_int_req |= 0x8000;  /* Request device interrupt */
         mfcu_opend_int = ON;    /* Set interrupt pending */
      }
   }

   return SCPE_OK;
}


t_stat prtbuf1_svc (UNIT *uptr)  /* uptr points to the prim or sec hopper */
{
   pb1_busy = OFF;
   if (debug_lvl & 0x08)
      fprintf(trace, "=F=> MFCU: Print1 Op has ended.\n");
   if (mfcu_int_enabled) {
      dev_int_req |= 0x8000;     /* Request device interrupt */
      mfcu_prt1_int = ON;        /* Set interrupt pending */
   }
   return SCPE_OK;
}


t_stat prtbuf2_svc (UNIT *uptr)  /* uptr points to the prim or sec hopper */
{
   pb2_busy = OFF;
   if (debug_lvl & 0x08)
      fprintf(trace, "=F=> MFCU: Print2 Op has ended.\n");
   if (mfcu_int_enabled) {
      dev_int_req |= 0x8000;     /* Request device interrupt */
      mfcu_prt2_int = ON;        /* Set interrupt pending */
   }
   return SCPE_OK;
}


/*** Primairy card hopper attach ***/

t_stat pchop_attach (UNIT *stkptr, CONST char *cptr)
{
   readerr = mfcu_notready = OFF;
   return attach_unit (stkptr, cptr);
}


/*** Secondairy card hopper attach ***/

t_stat schop_attach (UNIT *stkptr, CONST char *cptr)
{
   readerr = mfcu_notready = OFF;
   return attach_unit (stkptr, cptr);
}


static void mfcu_set_dev (const char *name, t_bool enable)
{
   DEVICE *dptr = find_dev (name);

   if (dptr != NULL) {
      if (enable) {
         dptr->flags &= ~DEV_DIS;
      }
      else {
         dptr->flags |= DEV_DIS;
         sim_cancel(dptr->units);
      }
   }
}

/*** Card reader/punch reset ***/

t_stat mfcu_reset (DEVICE *dptr)
{
   readerr = mfcu_notready = puncherr = OFF;/* clear indicators */
   pw_stat = sw_stat = EMPTY;               /* clear wait stations */
   sim_cancel (&pchop_unit);                /* clear reader events */
   sim_cancel (&schop_unit);
   mfcu_int_enabled = OFF;
   iplread = OFF;
   if (0 == strcmp("PCH", dptr->name)) {
      if (pchop_dev.flags & DEV_DIS) {
         if ((schop_dev.flags & DEV_DIS) == 0) {
            sim_cancel(pchop_dev.units);
            mfcu_set_dev("SCH",  FALSE);
            mfcu_set_dev("STK1", FALSE);
            mfcu_set_dev("STK2", FALSE);
            mfcu_set_dev("STK3", FALSE);
            mfcu_set_dev("STK4", FALSE);
            mfcu_set_dev("CDR",  TRUE);
            mfcu_set_dev("CDP",  TRUE);
            mfcu_set_dev("CDP2", TRUE);
         }
      }
      else {
         if ((schop_dev.flags & DEV_DIS) != 0) {
            mfcu_set_dev("SCH",  TRUE);
            mfcu_set_dev("STK1", TRUE);
            mfcu_set_dev("STK2", TRUE);
            mfcu_set_dev("STK3", TRUE);
            mfcu_set_dev("STK4", TRUE);
            mfcu_set_dev("CDR",  FALSE);
            mfcu_set_dev("CDP",  FALSE);
            mfcu_set_dev("CDP2", FALSE);
         }
       }
   }
   return SCPE_OK;
}


/*** Bootstrap routine (pchop only) ***/

t_stat mfcu_boot (int32 unitno, DEVICE *dptr)
{
   TBAR = 0x0000;                /* clear registers */
   RBAR = 0x0000;
   PBAR = 0x0000;
   pchop_ebcdic = 0;             /* IPL with ascii char */
   pw_stat = EMPTY;              /* prim wait station */
   drop_card (0, &stack1_unit);
   feed_card (0, &stack1_unit);
   iplread = ON;                 /* IPL Read */
   read_card (0);                /* read first card */
   return SCPE_OK;
}

const char *mfcu_description (DEVICE *dptr)
{
   static char description[40];

   if (0 == strcmp("PCH", dptr->name))
       return "IBM 5424 MFCU Primary Card Hopper";
   if (0 == strcmp("SCH", dptr->name))
       return "IBM 5424 MFCU Secondary Card Hopper";
   if (0 == memcmp("STK", dptr->name, 3))
       sprintf (description, "IBM 5424 MFCU Card Stacker %c", dptr->name[3]);
   return description;
}
