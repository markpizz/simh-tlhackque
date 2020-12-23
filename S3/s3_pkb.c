/* s3_pkb.c: System/3 5471 console terminal simulator

   Copyright (c) 2001-2005, Charles E Owen
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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   pkb   5471 printer/keyboard

   25-Apr-03    RMS     Revised for extended file support
   08-Oct-02    RMS     Added impossible function catcher

*/

#include "s3_defs.h"

t_stat pkb_svc (UNIT *uptr);
t_stat pkb_reset (DEVICE *dptr);
const char *pkb_description (DEVICE *dptr);

/* 5471 data structures

   pkb_dev   TTI device descriptor
   pkb_unit  TTI unit descriptor
   pkb_reg   TTI register list
   pkb_mod   TTI/TTO modifiers list
*/

/* Flag bits : (kept in pkb_unit.u3) */

#define PKB_INTRST   0x0001   /* Reset interrupts */
#define KBD_KEYINT   0x0002   /* Other key interrupts enabled */
#define KBD_REQINT   0x0004   /* Request key interrupts enabled */
//                   0x0008   /* Spare */
#define KBD_PROLIGHT 0x0010   /* Proceed Indicator (light on/off) */
#define KBD_REQLIGHT 0x0020   /* Request Pending Indicator (light on/off) */
//                   0x0040   /* Spare */
//                   0x0080   /* Spare */
#define KBD_INTKEY   0x0100   /* Return or other key interrupt pending */
#define KBD_INTEND   0x0200   /* End or cancel key interrupt pending */
#define KBD_INTREQ   0x0400   /* Request key interrupt pending */
#define PRT_INTREQ   0x0800   /* Printer interrupt pending */
#define PRT_BUSY     0x1000   /* Printer busy */
#define PRT_PRTINT   0x2000   /* Printer interrupts enabled */

/* Keys mapped to 5471 functions */

int32 key_req = 0x01;         /* Request key: ^A */
int32 key_rtn = 0x12;         /* Return key: ^R */
int32 key_can = 0x1B;         /* Cancel key: ESC */
int32 key_end = 0x0D;         /* End key: CR */

UNIT pkb_unit = { UDATA (&pkb_svc, 0, 0), KBD_POLL_WAIT };

REG pkb_reg[] = {
   { HRDATA (FLAG, pkb_unit.u3, 16) },
   { HRDATA (IBUF, pkb_unit.buf, 8) },
   { HRDATA (OBUF, pkb_unit.u4, 8) },
   { HRDATA (REQKEY, key_req, 8) },
   { HRDATA (RTNKEY, key_rtn, 8) },
   { HRDATA (CANKEY, key_can, 8) },
   { HRDATA (ENDKEY, key_end, 8) },
   { DRDATA (POS, pkb_unit.pos, T_ADDR_W), PV_LEFT },
   { DRDATA (TIME, pkb_unit.wait, 24), REG_NZ + PV_LEFT },
   { NULL }
};

MTAB pkb_mod[] = {
   { 0 }
};

DEVICE pkb_dev = {
   "PKB", &pkb_unit, pkb_reg, pkb_mod,
   1, 10, 31, 1, 8, 8,
   NULL, NULL, &pkb_reset,
   NULL, NULL, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &pkb_description
};


/*-------------------------------------------------------------------*/
/* EBCDIC to ASCII translate table                                   */
/*-------------------------------------------------------------------*/
const unsigned char ebcdic_to_ascii[] = {
/*  0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F */
 0x00,0x01,0x02,0x03,0xA6,0x09,0xA7,0x7F,0xA9,0xB0,0xB1,0x0B,0x0C,0x0D,0x0E,0x0F,
 0x10,0x11,0x12,0x13,0xB2,0xB4,0x08,0xB7,0x18,0x19,0x1A,0xB8,0xBA,0x1D,0xBB,0x1F,
 0xBD,0xC0,0x1C,0xC1,0xC2,0x0A,0x17,0x1B,0xC3,0xC4,0xC5,0xC6,0xC7,0x05,0x06,0x07,
 0xC8,0xC9,0x16,0xCB,0xCC,0x1E,0xCD,0x04,0xCE,0xD0,0xD1,0xD2,0x14,0x15,0xD3,0xFC,
 0x20,0xD4,0x83,0x84,0x85,0xA0,0xD5,0x86,0x87,0xA4,0x7E,0x2E,0x3C,0x28,0x2B,0x7C,
 0x26,0x82,0x88,0x89,0x8A,0xA1,0x8C,0x8B,0x8D,0xD8,0x21,0x24,0x2A,0x29,0x3B,0x5E,
 0x2D,0x2F,0xD9,0x8E,0xDB,0xDC,0xDD,0x8F,0x80,0xA5,0x7C,0x2C,0x25,0x5F,0x3E,0x3F,
 0xDE,0x90,0xDF,0xE0,0xE2,0xE3,0xE4,0xE5,0xE6,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
 0xE7,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xAE,0xAF,0xE8,0xE9,0xEA,0xEC,
 0xF0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xF1,0xF2,0x91,0xF3,0x92,0xF4,
 0xF5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xAD,0xA8,0xF6,0x5B,0xF7,0xF8,
 0x9B,0x9C,0x9D,0x9E,0x9F,0xB5,0xB6,0xAC,0xAB,0xB9,0xAA,0xB3,0xBC,0x5D,0xBE,0xBF,
 0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xCA,0x93,0x94,0x95,0xA2,0xCF,
 0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xDA,0x96,0x81,0x97,0xA3,0x98,
 0x5C,0xE1,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xFD,0xEB,0x99,0xED,0xEE,0xEF,
 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xFE,0xFB,0x9A,0xF9,0xFA,0xFF,
};

/*-------------------------------------------------------------------*/
/* ASCII to EBCDIC translate table                                   */
/*-------------------------------------------------------------------*/
const unsigned char ascii_to_ebcdic[] = {
/*  0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F */
 0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
 0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x1A,0x27,0x22,0x1D,0x35,0x1F,
 0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
 0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
 0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
 0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x5F,0x6D,
 0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
 0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0x4A,0x07,
 0x68,0xDC,0x51,0x42,0x43,0x44,0x47,0x48,0x52,0x53,0x54,0x57,0x56,0x58,0x63,0x67,
 0x71,0x9C,0x9E,0xCB,0xCC,0xCD,0xDB,0xDD,0xDF,0xEC,0xFC,0x9B,0xB1,0xB2,0xB3,0xB4,
 0x45,0x55,0xCE,0xDE,0x49,0x69,0x04,0x06,0xAB,0x08,0xBA,0xB8,0xB7,0xAA,0x8A,0x8B,
 0x09,0x0A,0x14,0xBB,0x15,0xB5,0xB6,0x17,0x1B,0xB9,0x1C,0x1E,0xBC,0x20,0xBE,0xBF,
 0x21,0x23,0x24,0x28,0x29,0x2A,0x2B,0x2C,0x30,0x31,0xCA,0x33,0x34,0x36,0x38,0xCF,
 0x39,0x3A,0x3B,0x3E,0x41,0x46,0x4A,0x4F,0x59,0x62,0xDA,0x64,0x65,0x66,0x70,0x72,
 0x73,0xE1,0x74,0x75,0x76,0x77,0x78,0x80,0x8C,0x8D,0x8E,0xEB,0x8F,0xED,0xEE,0xEF,
 0x90,0x9A,0x9B,0x9D,0x9F,0xA0,0xAC,0xAE,0xAF,0xFD,0xFE,0xFB,0x3F,0xEA,0xFA,0xFF,
};

/* -------------------------------------------------------------------- */

/* Console Input: master routine */

int32 pkb (int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata= 0,  ec, ac;
   char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

   if (debug_lvl & 0x80)
      fprintf(trace, "=1=> %04X %s %01X,%d,%04X LIO=%04X \n",
              IAR[level],
              opstr[op],
              m, n, data,
              pkb_unit.u4);

   switch (op) {
      case 0:      /* SIO 5471 */
         if (n != 0)
            return STOP_INVDEV;
         if (m == 0) {         /* Keyboard */
            pkb_unit.u3 &= 0x3FC0;
            pkb_unit.u3 |= (data & 0x003F);
            if (pkb_unit.u3 & KBD_REQLIGHT)
               sim_printf ("\n\r REQ light turned on ");
            if (data & PKB_INTRST) {
               pkb_unit.u3 &= ~KBD_INTREQ;
               pkb_unit.u3 &= ~KBD_INTKEY;
               pkb_unit.u3 &= ~KBD_INTEND;
               return RESET_INTERRUPT;
            }
         } else {      /* Printer */
            if (data & 0x80) {   /* start print bit */
               ec = pkb_unit.u4 & 0xff;
               ac = ebcdic_to_ascii[ec];
               sim_putchar(ac);
               pkb_unit.u3 |= PRT_BUSY;
            }
            if (data & 0x40) {   /* Carr. Return */
               sim_putchar('\n');
               sim_putchar('\r');
               pkb_unit.u3 |= PRT_BUSY;
            }
//            pkb_unit.u3 &= 0x1FFE;
            if (data & 0x04)    /* Printer interrupt flag */
               pkb_unit.u3 |=  PRT_PRTINT;
            else
               pkb_unit.u3 &= ~PRT_PRTINT;
            if (data & 0x01) {   /* Reset Interrupt */
               if (level < 8) {
                  if (!(data & 0x80))
                     pkb_unit.u3 &= ~PRT_INTREQ;
                  return RESET_INTERRUPT;
               }
            }
         }
         return SCPE_OK;
      case 1:    /* LIO 5471 */
         if (n != 0)
            return STOP_INVDEV;
         if (m != 1)
            return STOP_INVDEV;
         pkb_unit.u4 = (data >> 8) & 0xff;
         return SCPE_OK;
      case 2:      /* TIO 5471 */
         return STOP_INVDEV;
      case 3:      /* SNS 5471 */
         if (n != 1 && n != 3)
            return (STOP_INVDEV << 16);
         if (m == 0) {      /* Keyboard data */
            if (n == 1) {       /* Sense bytes 0 & 1 */
               iodata = (pkb_unit.buf << 8) & 0xFF00;
               if (pkb_unit.buf == 0x12)
                  iodata |= 0x0004;    /* Return key */
               if (pkb_unit.u3 & KBD_INTKEY)
                  iodata |= 0x0008;    /* Data or CR key */
               if (pkb_unit.buf == 0x0D)
                  iodata |= 0x0010;    /* End key */
               if (pkb_unit.buf == 0x03)
                  iodata |= 0x0020;    /* Cancel key */
               if (pkb_unit.u3 & KBD_INTEND)
                  iodata |= 0x0040;    /* END or Cancel */
               if (pkb_unit.u3 & KBD_INTREQ)
                  iodata |= 0x0080;    /* Req key int */
            } else {            /* Sense bytes 2 & 3 */
               iodata = 0x0000; /* Manual says most CE use only */
               if (pkb_unit.u3 & KBD_KEYINT)
                  iodata |= 0x0040;    /* Other key enabled */
               if (pkb_unit.u3 & KBD_REQINT)
                  iodata |= 0x0080;    /* Req key enabled */
            }
         } else {           /* Printer Data */
            if (n == 1) {       /* Sense bytes 0 & 1 */
               iodata = 0x0000;
               if (pkb_unit.u3 & PRT_BUSY)
                  iodata |= 0x0010;    /*             */
               if (pkb_unit.u3 & PRT_INTREQ)
                  iodata |= 0x0080;    /*             */
            } else {            /* Sense bytes 2 & 3 */
               iodata = 0x0000; /* CE use only */
            }
         }
         if (debug_lvl & 0x80)
            fprintf(trace, "=1=> SENSE = %04X\n",
                    iodata);
         iodata |= ((SCPE_OK << 16) & 0xffff0000);
         return (iodata);
      case 4:      /* APL 5471 */
         return STOP_INVDEV;
      default:
         break;
   }
   return (0);
}

/* Unit service */

t_stat pkb_svc (UNIT *uptr)
{
int32 temp, ac, ec;

sim_activate (&pkb_unit, pkb_unit.wait);      /* continue poll */

if (pkb_unit.u3 & PRT_BUSY) {         /* Printer Interrupt */
   if (pkb_unit.u3 & PRT_PRTINT) {
      pkb_unit.u3 &= ~PRT_BUSY;
      pkb_unit.u3 |= PRT_INTREQ;
      dev_int_req |= 0x0002;
    return SCPE_OK;
   }
}

/* Keyboard : handle input */

if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;   /* no char or error? */

ac = temp & 0x7f;                   /* placed type ASCII char in ac */
if (ac == key_req) {                /* Request Key */
   if (pkb_unit.u3 & KBD_REQINT) {
      pkb_unit.u3 |= KBD_INTREQ;
      dev_int_req |= 0x0002;
   }
   return SCPE_OK;
}

if (islower(ac))
   ac = toupper(ac);
ec = ascii_to_ebcdic[ac];            /* Translate */
pkb_unit.buf = ec;                  /* put in buf */
pkb_unit.pos = pkb_unit.pos + 1;

if (ac == key_end) {                /* End key */
   if (pkb_unit.u3 & KBD_KEYINT) {
      pkb_unit.u3 |= KBD_INTEND;
      pkb_unit.buf = 0x0d;
      dev_int_req |= 0x0002;
   }
   return SCPE_OK;
}

if (ac == key_can) {                  /* Cancel key */
   if (pkb_unit.u3 & KBD_KEYINT) {
      pkb_unit.u3 |= KBD_INTEND;
      pkb_unit.buf = 0x03;
      dev_int_req |= 0x0002;
   }
   return SCPE_OK;
}

if (ac == key_rtn) {                  /* Return key */
   if (pkb_unit.u3 & KBD_KEYINT) {
      pkb_unit.u3 |= KBD_INTKEY;
      pkb_unit.buf = 0x12;
      dev_int_req |= 0x0002;
   }
   return SCPE_OK;
}

if (pkb_unit.u3 & KBD_KEYINT) {      /* Key interupts enabled ? */
   pkb_unit.u3 |= KBD_INTKEY;         /* Set pending flag */
   dev_int_req |= 0x0002;               /* Device 1 Interrupt! */
}

return SCPE_OK;
}

/* Reset routine */

t_stat pkb_reset (DEVICE *dptr)
{
   pkb_unit.buf = 0;
   dev_int_req = dev_int_req & ~0x0002;      /* reset interrupt */
   sim_activate (&pkb_unit, pkb_unit.wait);   /* activate unit */
   return SCPE_OK;
}

t_stat pkb_setmod (UNIT *uptr, int32 value)
{
   return SCPE_OK;
}

const char *pkb_description (DEVICE *dptr)
{
   return "IBM 5471 Printer/Keyboard console terminal";
}
