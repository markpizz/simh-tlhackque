/* 3741.c: IBM 3741 Data Station (model 10).

   Copyright (c) 2008-2011, Henk Stegeman
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

   Adaptor -----> IBM 3741 dkt

   The IBM 3471 Data Station is attached/put online AFTER the IPL of the
   System/3 CPU. Therefor you can not specify a 'at dkt <filename>'
   in the config file.  When the System/3 SCP issues a read or write SIO you
   will be asked by simh to attach a diskette image.

      sim> d dkt iomode 0        0 = READ; 1 = WRITE
      sim> d dkt hdr1 <n>        dataset nr on diskette
      sim> d dkt cs 01001        diskette c/s address
      sim> at dkt <dkt_image>    attach diskette image file
      sim> det dkt <dkt_image>   detach diskette image file

*/

#include "s3_defs.h"

int32 read_ksect(UNIT *uptr, char *dktbuf, int32 cylsect);
int32 write_ksect(UNIT *uptr, char *dktbuf, int32 cylsect);
void  next_ksect();
int32 verify_dkt (UNIT *uptr, char *dktbuf);
int32 etoi(char *dktbuf, int32 pos, int32 nod);
int32 lda (int32 adap, int32 op, int32 m, int32 n, int32 data);

t_stat dkt_svc (UNIT *uptr);
t_stat dkt_boot (int32 unitno, DEVICE *dptr);
t_stat dkt_attach (UNIT *uptr, CONST char *cptr);
t_stat dkt_detach (UNIT *uptr);
t_stat dkt_reset (DEVICE *dptr);
const char *dkt_description (DEVICE *dptr);

int32 KDAR;                         /* Data Address Register */
int32 KLCR;                         /* Byte Count Register */
int32 KIOF;                         /* I/O Functional Register */
int32 KDTR;                         /* Data Transfer Register (8bits) */
int32 dkt_online = OFF;             /* 3741 online */
int32 dkt_int_enabled = OFF;        /* Interrupt enabled flag */
int32 dkt_opend_int = OFF;          /* Operation ended int flag */
int32 dkt_busy = OFF;               /* Attachment busy flag */
int32 diag_byte = 0;                /* Diagnostic byte */
int32 eod_flag = OFF;               /* End of Data flag */
int32 eor_flag = OFF;               /* End of Record flag */
int32 eoe_flag = OFF;               /* End of Extent flag */
int32 eoj_flag = OFF;               /* End of Job flag */
int32 skip_flag = OFF;              /* Skip deleted record flag */
int32 verify_flag = ON;             /* Verify/check diskette */
int32 IO_mode = 0;                  /* 0 = Read from 3741
                                       1 = Write to 3741 */
int32 Rd_call = OFF;                /* Read call latch */
int32 Wr_call = OFF;                /* Write call latch */
int32 hdr_nr = 1;                   /* Dataset number */
int32 cylsect = 00000;              /* Cylinder sector address */
int32 boe_cs;                       /* Begin of extent (cylsect) */
int32 eod_cs;                       /* End of data (cylsect) */
int32 eoe_cs;                       /* End of extent (cylsect) */
int32 recl;                         /* Record length file */
char  dktbuf[DKT_SECTSIZE];         /* Diskette buffer */

/* Disk data structures

   dkt_dev  DKT descriptor
   dkt_unit DKT unit descriptor
            u3 is not used.
            u4 is not used.
   dkt_reg  DKT register list

   x = 0, 1, 2 or 3
*/

UNIT dkt_unit = {
   UDATA (&dkt_svc, UNIT_FIX+UNIT_ATTABLE, 0), 50 };

REG dkt_reg[] = {
   { HRDATA (KDAR, KDAR, 16) },
   { HRDATA (KLCR, KLCR, 16) },
   { HRDATA (KIOF, KIOF, 16) },
   { FLDATA (IOMODE, IO_mode, 0) },
   { HRDATA (HDR1, hdr_nr, 8) },
   { DRDATA (CS, cylsect, 16) },
   { DRDATA (TIME, dkt_unit.wait, 24), PV_LEFT },
   { BRDATA (BUF, dktbuf, 8, 8, DKT_SECTSIZE) },
   { NULL } };

DEVICE dkt_dev = {
   "DKT", &dkt_unit, dkt_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &dkt_reset,
   &dkt_boot, &dkt_attach, &dkt_detach, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &dkt_description };


/* -------------------------------------------------------------------- */

/* 3741: operational routine */

int32 dkt (int32 op, int32 m, int32 n, int32 data)
   {
   int32 iodata, i, rc;
   const char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };
   const unsigned char delrec[2] = { 0xC4, 0xE0 };               /* EBCDIC "D1" */

   UNIT *uptr;
#if LDA
   /* If LDA attachment control instr. (m == 1),
      go to LDA device code handler.             */
   if (m == 1) {
      int32 r;
      r = lda(4, op, m, n, data);   /* Attachment Control */
      return (r);
      }
#endif
   /* m = 0 */
   uptr = dkt_dev.units;

   if (debug_lvl & 0x20)
      fprintf(trace, "=K=> %04X %s %01X,%d,%04X KDAR=%04X KLCR=%04X \n",
         IAR[level],
         opstr[op],
         m, n, data,
         KDAR,
         KLCR);

   switch (op) {

      /* SIO 3741 */
      case 0:
         iodata = 0x0000;

         if ((uptr -> flags & UNIT_ATT) == 0) {
            sim_printf("\n\rAttach (next) diskette image file...\n\r");
            verify_flag = ON;
            return SCPE_UNATT;
         }
         if (verify_flag == ON) {         /* check for valid diskette */
            rc = verify_dkt(uptr, dktbuf);
            if (rc == FALSE)              /* diskette is not valid */
               return SCPE_UNATT;         /* get new one */
         }
         iodata = 0x0000;
         switch (n) {
            case 0x00: /* Control */
               if (data & 0x01) {  /* Reset interrupt request */ 
               }
               if (data & 0x02) {  /* Enable interrupt requests */
               }
               if (data & 0x04) {  /* Disable interrupt requests */
               }
               if (data & 0x08) {  /* Reset Rd/Wr call latch */
                  Rd_call = OFF;
                  Wr_call = OFF;
               }
               
//             if (data & 0x10)    /* reset 3741 attachment */
                  // no idea...
               if (data & 0xE0)
                  return(STOP_INVDEV);

//             KLCR = 0;
               iodata = SCPE_OK;
               break;
            case 0x01: /* Read from 3741 */
               Rd_call = ON; 
               if ((uptr -> flags & UNIT_ATT) == 0) {
                  sim_printf("\n\rAttach (next) diskette image file...\n\r");
                  verify_flag = ON;
                  return SCPE_UNATT;
               }
               if (verify_flag == ON) {         /* check for valid diskette */
                  rc = verify_dkt(uptr, dktbuf);
                  if (rc == FALSE)              /* diskette is not valid */
                     return SCPE_UNATT;         /* get new one */
               }
               read_ksect(uptr, dktbuf, cylsect);
               /* Skip any 'deleted' 3740 records */
               while (memcmp(dktbuf, delrec, 2) == 0) {
                  next_ksect();      /* skip deleted sector */
                  read_ksect(uptr, dktbuf, cylsect);
                  if (eod_flag == ON) {
                     sim_activate(uptr, uptr -> wait);
                     dkt_busy = ON; /* subsystem is busy */
                     iodata = SCPE_OK;
                     break;         /* no data transfer */
                  }
               }
               i = 0;
               /* Transfer buffer content to cpu storage */
               while ((KLCR != 256) && (recl > i)) {
                  PutMem(KDAR, IO, dktbuf[i++]);
                  KLCR++;
                  KDAR++;
               }
               sim_activate(uptr, uptr -> wait);
               dkt_busy = ON;       /* subsystem is busy */
               iodata = SCPE_OK;
               break;
            case 0x02: /* Write to 3741 */
               Wr_call = ON;
               if ((uptr -> flags & UNIT_ATT) == 0) {
                  sim_printf("\n\rAttach (next) diskette image file...\n\r");
                  verify_flag = ON;
                  return SCPE_UNATT;
               }
               if (verify_flag == ON) {         /* check for valid diskette */
                  rc = verify_dkt(uptr, dktbuf);
                  if (rc == FALSE)              /* diskette is not valid */
                     return SCPE_UNATT;         /* get new one */
               }
               eor_flag = OFF;
               for (i = 0; i < DKT_SECTSIZE; i++) {
                  dktbuf[i] = 0x00; /* clear buffer */
               }
               i = 0;
               /* Transfer cpu storage to buffer */
               while ((KLCR != 256) && (recl > i)) {
                  dktbuf[i++] = GetMem(KDAR, IO);
                  KLCR++;
                  KDAR++;
               }
               if (KLCR == 256)    /* Transfer complete ? */
                  write_ksect(uptr, dktbuf, cylsect);
               sim_activate(uptr, uptr -> wait);
               dkt_busy = ON;
               KLCR = 0;
               iodata = SCPE_OK;
               break;
            case 0x03: /* Response */
               switch (data) {
                  case 0x08:
                     if (debug_lvl & 0x20)
                        sim_printf("\n\rDKT: Normal response received.");
                     break;
                  case 0x10:
                     sim_printf("\n\rDKT: Record-length error.");
                     break;
                  case 0x14:
                     sim_printf("\n\rDKT: Attachment I/O mode error.");
                     break;
                  case 0x30:
                     sim_printf("\n\rDKT: End of dataset.");
                     break;
                  case 0x50:
                     sim_printf("\n\rDKT: End of job.");
                     break;
                  default:
                     return(STOP_INVDEV);
               }
               break;
            default:
               return STOP_INVDEV;
         }
         return iodata;

      /* LIO 3741 */
      case 1:
         switch (n) {
            case 0x01: /* I/O Function Register */
               KIOF = data;
               break;
            case 0x02: /* Length Count Register */
               KLCR = data & 0x00FF;
               break;
            case 0x04: /* Data Address Register */
               KDAR = data;
               break;
            case 0x05: /* Data Transfer Register */
               KDTR = data & 0x00FF;
               break;
            default:
               return STOP_INVDEV;
         }
         return SCPE_OK;

      /* TIO 3741 */
      case 2:
      /* APL 3741 */
      case 4:
         iodata = 0;
         switch (n) {
            case 0x00: /* Attachment not ready/check */
               if (cylsect != 00000)
                  iodata = 1;
               if ((uptr -> flags & UNIT_ATT) == 0)
                  iodata = 1;
               break;
            case 0x02: /* Attachment busy ? */
               if (dkt_busy == ON)
                  iodata = 1;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         return ((SCPE_OK << 16) | iodata);

      /* SNS 3741 */
      case 3:
         iodata = 0x0000;
         switch (n) {
            case 0x01: /* I/O Functional Register */
               iodata = KIOF;
               break;
            case 0x02: /* Length Count Register + Status byte */
               iodata = KLCR & 0x00FF;
               if (dkt_opend_int == ON)
                  iodata |= 0x2000; /* Indicate opend. */
               if ((KLCR & 0xFF) == 0)
                  iodata |= 0x0200; /* LCR = 0 */
               if (!(dkt_busy))
                  iodata |= 0x0100; /* I/O ready */
               break;
            case 0x03:   /* I/O transfer lines */
               iodata |= 0x4800;    /* 3741 attached */
               if (dkt_online)
                  iodata |= 0x0400; /* 3741 online */
               if (IO_mode == 0)
                  iodata |= 0x0200; /* Read from 3741 */
               else        
                  iodata |= 0x0100; /* Write to 3741 */
               if (eod_flag)
                  iodata |= 0x0040; /* End of data */
               if (eor_flag)
                  iodata |= 0x0010; /* End of record */
               if (eoj_flag)
                  iodata |= 0x0008; /* End of job */
               break;
            case 0x04: /* Data Address Register */
               iodata = KDAR;
               break;
            case 0x05: /* Data Transfer Register + Diag byte */
               iodata = KDTR & 0x00FF;
               iodata |= (diag_byte << 8) & 0xFF00;
               break;
            default:
               return (STOP_INVDEV << 16);
         }
         if (debug_lvl & 0x20)
            fprintf(trace, "=K=> Sense = %04X \n", iodata);
         iodata |= ((SCPE_OK << 16) & 0xffff0000);
         return iodata;
         break;

      default:
         break;
   }
return SCPE_OK;
}


/* Diskette unit service. */

t_stat dkt_svc (UNIT *uptr)
{
   int i;
   next_ksect();                     /* seek to next sector */

   if (debug_lvl & 0x20)
      fprintf(trace, "=K=> DKT: Read/write Op has ended.\n");

   /* close diskette image file at eod or eoe */
   if ((eoe_flag == ON) || (eod_flag == ON)) {
      if (IO_mode == 1) {
         /* Update EOD pointer in "HDR1" record. */
         read_ksect (uptr, dktbuf, 7 + hdr_nr);   /* read dataset record */
         snprintf(&dktbuf[74], 10, "%05d", cylsect);
         for (i = 0; i < 5; i++) {                /* make it EBCDIC */
            dktbuf[74+i] = dktbuf[74+i] | 0xF0;
         }
         write_ksect (uptr, dktbuf, 7 + hdr_nr);  /* write updated dataset record */
      }
      fclose(uptr -> fileref);
      uptr -> flags &= (0xffffff - UNIT_ATT);
   }
   dkt_busy = OFF;
   dkt_opend_int = ON;
   eor_flag = ON;
   return SCPE_OK;
}


/* Diskette unit reset */

t_stat dkt_reset (DEVICE *dptr)
{
   dkt_busy = OFF;                  /* clear indicators */
   sim_cancel (&dkt_unit);          /* clear event */
   dkt_opend_int = OFF;             /* reset opend */
   return SCPE_OK;
}


/* Diskette unit attach */

t_stat dkt_attach (UNIT *uptr, CONST char *cptr)
{
   /* Check cptr for extra parms. No parms implies input and dataset 1. */
   dkt_busy = OFF;                 
   cylsect = 00000;                 /* set r/w head */
   eod_flag = OFF;                  /* new diskette attached */
   dkt_online = ON;                 /* 3741 is now online */
   return attach_unit (uptr, cptr);
}


/* Diskette unit detach */

t_stat dkt_detach (UNIT *uptr)
{
   int i;
   /* Update EOD pointer in "HDR1" record. */
   if (IO_mode == 1) {
      read_ksect (uptr, dktbuf, 7 + hdr_nr);   /* read dataset record */
      snprintf(&dktbuf[74], 10, "%05d", cylsect);
      for (i = 0; i < 5; i++) {                /* make it EBCDIC */
         dktbuf[74+i] = dktbuf[74+i] | 0xF0;
      }
      write_ksect (uptr, dktbuf, 7 + hdr_nr);  /* write updated dataset record */
   }

   return detach_unit (uptr);
}


/* Bootstrap routine */

t_stat dkt_boot (int32 unitno, DEVICE *dptr)
{
   int32 i, rc;

   UNIT *uptr;

   if (debug_lvl) {
      if (!debug_flag) {
         trace = fopen("trace.log", "w");
         debug_flag = 1;
      }
   }

   KDAR = 0x0000;                   /* set data address register */
   uptr = dkt_dev.units;

   if ((dkt_unit.flags & UNIT_ATT) == 0)
      return SCPE_UNATT;
   rc = verify_dkt(uptr, dktbuf);    /* check diskette & set C/S */
   if (rc == FALSE)                 /* diskette is not valid */
      return SCPE_UNATT;            /* get new one */
   if (IO_mode == 1) {
      sim_printf("\n\rDKT: Attachment I/O mode error. ");
      return SCPE_UNATT;   
   }
   sim_printf ("\nIPL from CS = %05d \n", cylsect);
   read_ksect(uptr, dktbuf, cylsect);  /* read boot sector */
   next_ksect();                    /* go to the next sector */

   /* Transfer buffer content to cpu storage */
   for (i = 0; i < 128; i++) {
      PutMem(KDAR, IO, dktbuf[i]);
      KDAR++;
   }
   sim_activate(uptr, uptr -> wait);
   dkt_busy = ON;                   /* subsystem is busy */

   return SCPE_OK;
}


/* Verify & check diskette and set boundaries */

int32 verify_dkt (UNIT *uptr, char *dktbuf)
{
   const unsigned char vol1[4] = { 0xE5, 0xD6, 0xD3, 0xF1 };   /* EBCDIC "VOL1" */
   const unsigned char hdr1[4] = { 0xC8, 0xC4, 0xD9, 0xF1 };   /* EBCDIC "HDR1" */

   dkt_busy = OFF;

   /* Check for IBM diskette 1: "VOL1" @ C/S 00007. */
   read_ksect (uptr, dktbuf, 00007);    /* read vol label */
   if (memcmp(dktbuf, vol1, 4) != 0) {
      sim_printf("\n\rDKT: No VOL1 record found.");
      return FALSE;
   }
   /* Check for valid HDR1 nr */
   if (hdr_nr > 18) {                   /* hdr1 too big 1 */
      sim_printf("\n\rDKT: HDR1 nr not valid (too big).");
      return FALSE;
   }

   /* Check for "HDR1" in dataset record */
   read_ksect (uptr, dktbuf, 7 + hdr_nr);   /* read dataset label */
   if (memcmp(dktbuf, hdr1, 4) != 0) {
      sim_printf("\n\rDKT: No HDR1 record found. ");
      return FALSE;
   }

   boe_cs = etoi(dktbuf, 32, 5);    /* get C/S of BOE */
   eod_cs = etoi(dktbuf, 78, 5);    /* get C/S of EOD */
   eoe_cs = etoi(dktbuf, 38, 5);    /* get C/S of EOE */
   recl   = etoi(dktbuf, 26, 3);    /* get record size */

   /* Set cyl/sect to start position of dataset */
   if (cylsect == 00000) { 
      cylsect = boe_cs;
   } else {
      /* Check if CS are within boundaries */
      if ((cylsect < boe_cs) ||
          (cylsect > eod_cs)) {
         sim_printf("\n\rDKT: CS not within BOE/EOD boundaries.");
      }
   }  
   eod_flag = OFF;                  /* New diskette attached */
   eoe_flag = OFF;
   dkt_online = ON;                 /* 3741 is online */
   verify_flag = OFF;               /* It's oke */
   return TRUE;
}


/* Go to next sector and check for eod or eoe. */

void next_ksect()
{
   static int32 c, s; 
   cylsect = cylsect + 1;           /* r/w hd to next sector */

   /* End of cylinder update of cyl/sect */
   c = cylsect / 1000;
   s = cylsect - c * 1000;
   if (s > 26) {
      s = 1;                        /* reset sector nr */
      c = c + 1;                    /* next cylinder */
      cylsect = c * 1000 + s;
   }

   switch (IO_mode) {
      case 0:                       /* READ mode */
         /* end of data -OR- end of extent ? */
         if (cylsect > eod_cs)
            eod_flag = ON;          /* yes */
         if (cylsect > eoe_cs)
            eoe_flag = ON;          /* yes */
         break;
      case 1:                       /* WRITE mode */
         /* end of extent ? */
         if (cylsect > eoe_cs) {
            eod_flag = ON;          /* yes */
         }
         break;
   }
}


/* Convert max five EBCDIC figures to an integer. */

int32 etoi(char *dktbuf, int32 pos, int32 nod)
{
   int32 t = 0;
   if (nod >= 1) /* Nr of digits ge 1 ? */
      t = t + (dktbuf[pos-0] & 0x0F) * 1;
   if (nod >= 2) /* Nr of digits ge 2 ? */
      t = t + (dktbuf[pos-1] & 0x0F) * 10;
   if (nod >= 3) /* Nr of digits ge 3 ? */
      t = t + (dktbuf[pos-2] & 0x0F) * 100;
   if (nod >= 4) /* Nr of digits ge 4 ? */
      t = t + (dktbuf[pos-3] & 0x0F) * 1000;
   if (nod >= 5) /* Nr of digits ge 5 ? */
      t = t + (dktbuf[pos-4] & 0x0F) * 10000;
   return t;
}


/* Raw Diskette Data read/write */

int32 read_ksect(UNIT *uptr, char *dktbuf, int32 cylsect)
{
   static int32 t, c, s;
   static long pos, realsect;

   /* set cylinder & sector */
   c = cylsect / 1000;
   s = cylsect - c * 1000;

   /* physically read the sector */
   realsect = s - 1;           /* 3740 start counting at 1 */
   if (debug_lvl & 0x20)
      fprintf(trace, "=K=> Sector read from CS = %02d%03d \n",
         c, s);
   pos = DKT_CYLSIZE * c;
   pos += DKT_SECTSIZE * realsect;
   t = fseek(uptr -> fileref, pos, 0);
   t = fread(dktbuf, DKT_SECTSIZE, 1, uptr -> fileref);
   return (t);
}


int32 write_ksect(UNIT *uptr, char *dktbuf, int32 cylsect)
{
   static int32 t, c, s;
   static long pos, realsect;

   /* set cylinder & sector */
   c = cylsect / 1000;
   s = cylsect - c * 1000;

   /* physically write the sector */
   realsect = s - 1;            /* 3740 start counting at 1 */
   pos = DKT_CYLSIZE * c;
   pos += DKT_SECTSIZE * realsect;
   t = fseek(uptr -> fileref, pos, 0);
   t = fwrite(dktbuf, DKT_SECTSIZE, 1, uptr -> fileref);
   if (debug_lvl & 0x20)
      fprintf(trace, "=K=> Sector write to CS = %02d%03d \n",
         c, s);       
   return (t);
}

const char *dkt_description (DEVICE *dptr)
{
   return "IBM 3741 Data Station";
}
