/* s3_disk.c: IBM 5444 Disk Drives

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Adaptor -+--> Drive #1 -+--> r1 Removeable disk 1
            |              +--> f1 Fixed disk 1
            |
            +--> Drive #2 -+--> r2 Removeable disk 2
                           +--> f2 Fixed disk 2
*/

#include "s3_defs.h"

char dbuf[DSK_SECTSIZE];        /* Disk buffer */
t_stat disk_svc (UNIT *uptr);
t_stat disk_boot (int32 unitno, DEVICE *dptr);
t_stat disk_attach (UNIT *uptr, CONST char *cptr);
t_stat disk_reset (DEVICE *dptr);
t_stat disk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat disk_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat disk_tape2dsk (UNIT *uptr, const char **pcptr);
const char *disk_description (DEVICE *dptr);

static int32 read_sector(UNIT *uptr, char *dbuf, int32 sect);
static int32 write_sector(UNIT *uptr, char *dbuf, int32 sect);
static int32 dsk (int32 adap, int32 disk, int32 op, int32 m, int32 n, int32 data);

#define CYL     u3                  /* UNIT field containing drive cylinder */

int32 DDAR[2];                      /* Data address register */
int32 DCAR[2];                      /* Disk Control Address Register */
int32 diskerr[2] = { 0, 0 };        /* Error status */
int32 d_nrdy[2] = { 0, 0 };         /* Not ready error */
int32 seekbusy[2] = { 0, 0 };       /* Drive busy flags */
int32 seekhead[2] = { 0, 0 };       /* Disk head 0,1 */
int32 found[2] = { 0, 0 };          /* Scan found bit */
int32 RIDsect[2] = { 0, 0 };        /* for Read ID */

/* Disk data structures

   xy_dev   CDR descriptor
   xy_unit  CDR unit descriptor
   xy_reg   CDR register list

   x = F or R
   y = 1 or 2
*/

UNIT r1_unit = {
    UDATA (&disk_svc, UNIT_FIX+UNIT_ATTABLE, DSK_DISKSIZE), 100 };

REG r1_reg[] = {
    { FLDATAD (NOTRDY, d_nrdy[0], 0, "Drive not ready (not attached)") },
    { FLDATAD (SEEK, seekbusy[0], 0, "Drive is busy with a seek operation") },
    { HRDATAD (DAR, DDAR[0], 16, "Data Address Register") },
    { HRDATAD (CAR, DCAR[0], 16, "Control Address Register") },
    { HRDATAD (ERR, diskerr[0], 16, "Error Flags (16 bits)") },
    { DRDATAD (CYL, r1_unit.CYL, 8, "Current Cylinder (0 thru 203)") },
    { DRDATAD (HEAD, seekhead[0], 8, "Current head (0 or 1)") },
    { DRDATAD (POS, r1_unit.pos, T_ADDR_W, "Current position in attached disk file"), PV_LEFT },
    { DRDATAD (TIME, r1_unit.wait, 24, "Device Delay"), PV_LEFT },
    { BRDATAD (BUF, dbuf, 8, 8, 256, "Transfer Buffer") },
    { NULL }  };

DEVICE r1_dev = {
   "R1", &r1_unit, r1_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &disk_reset,
   &disk_boot, &disk_attach, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, &disk_help, &disk_attach_help, NULL, &disk_description };

UNIT f1_unit = {
    UDATA (&disk_svc, UNIT_FIX+UNIT_ATTABLE, DSK_DISKSIZE), 100 };

REG f1_reg[] = {
    { FLDATAD (NOTRDY, d_nrdy[0], 0, "Drive not ready (not attached)") },
    { FLDATAD (SEEK, seekbusy[0], 0, "Drive is busy with a seek operation") },
    { HRDATAD (DAR, DDAR[0], 16, "Data Address Register") },
    { HRDATAD (CAR, DCAR[0], 16, "Control Address Register") },
    { HRDATAD (ERR, diskerr[0], 16, "Error Flags (16 bits)") },
    { DRDATAD (CYL, f1_unit.CYL, 8, "Current Cylinder (0 thru 203)") },
    { DRDATAD (HEAD, seekhead[0], 8, "Current head (0 or 1)") },
    { DRDATAD (POS, f1_unit.pos, T_ADDR_W, "Current position in attached disk file"), PV_LEFT },
    { DRDATAD (TIME, f1_unit.wait, 24, "Device Delay"), PV_LEFT },
    { BRDATAD (BUF, dbuf, 8, 8, 256, "Transfer Buffer") },
    { NULL }  };

DEVICE f1_dev = {
   "F1", &f1_unit, f1_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &disk_reset,
   &disk_boot, &disk_attach, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &disk_description };

UNIT r2_unit = {
    UDATA (&disk_svc, UNIT_FIX+UNIT_ATTABLE, DSK_DISKSIZE), 100 };

REG r2_reg[] = {
    { FLDATAD (NOTRDY, d_nrdy[1], 0, "Drive not ready (not attached)") },
    { FLDATAD (SEEK, seekbusy[1], 0, "Drive is busy with a seek operation") },
    { HRDATAD (DAR, DDAR[0], 16, "Data Address Register") },
    { HRDATAD (CAR, DCAR[0], 16, "Control Address Register") },
    { HRDATAD (ERR, diskerr[1], 16, "Error Flags (16 bits)") },
    { DRDATAD (CYL, r2_unit.CYL, 8, "Current Cylinder (0 thru 203)") },
    { DRDATAD (HEAD, seekhead[1], 8, "Current head (0 or 1)") },
    { DRDATAD (POS, r2_unit.pos, T_ADDR_W, "Current position in attached disk file"), PV_LEFT },
    { DRDATAD (TIME, r2_unit.wait, 24, "Device Delay"), PV_LEFT },
    { BRDATAD (BUF, dbuf, 8, 8, 256, "Transfer Buffer") },
    { NULL }  };

DEVICE r2_dev = {
   "R2", &r2_unit, r2_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &disk_reset,
   NULL, &disk_attach, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &disk_description };

UNIT f2_unit = {
    UDATA (&disk_svc, UNIT_FIX+UNIT_ATTABLE, DSK_DISKSIZE), 100 };

REG f2_reg[] = {
    { FLDATAD (NOTRDY, d_nrdy[1], 0, "Drive not ready (not attached)") },
    { FLDATAD (SEEK, seekbusy[1], 0, "Drive is busy with a seek operation") },
    { HRDATAD (DAR, DDAR[0], 16, "Data Address Register") },
    { HRDATAD (CAR, DCAR[0], 16, "Control Address Register") },
    { HRDATAD (ERR, diskerr[1], 16, "Error Flags (16 bits)") },
    { DRDATAD (CYL, f2_unit.CYL, 8, "Current Cylinder (0 thru 203)") },
    { DRDATAD (HEAD, seekhead[1], 8, "Current head (0 or 1)") },
    { DRDATAD (T_ADDR_WPOS, f2_unit.pos, T_ADDR_W, "Current position in attached disk file"), PV_LEFT },
    { DRDATAD (TIME, f2_unit.wait, 24, "Device Delay"), PV_LEFT },
    { BRDATAD (BUF, dbuf, 8, 8, 256, "Transfer Buffer") },
    { NULL }  };

DEVICE f2_dev = {
   "F2", &f2_unit, f2_reg, NULL,
   1, 10, 31, 1, 8, 7,
   NULL, NULL, &disk_reset,
   NULL, &disk_attach, NULL, NULL,
   DEV_M10, 0, NULL, NULL, NULL, NULL, NULL, NULL, &disk_description };


/* -------------------------------------------------------------------- */

/* 5444: master routines */

int32 dsk1 (int32 op, int32 m, int32 n, int32 data)
{
    int32 r;

    r = dsk(0, 0, op, m, n, data);
    return (r);
}

int32 dsk2 (int32 op, int32 m, int32 n, int32 data)
{
    int32 r;

    r = dsk(0, 1, op, m, n, data);
    return (r);
}

/* 5444: operational routine */

static int32 dsk (int32 adap, int32 disk, int32 op, int32 m, int32 n, int32 data)
{
    int32 iodata, i, j, u, sect, nsects, addr, r, c, res;
    int32 F, C, S, N, usave;

    char opstr[5][5] = { "SIO", "LIO", "TIO", "SNS", "APL" };

    UNIT *uptr;

    u = m;
    if (disk == 1) u += 2;

    F = GetMem(DCAR[adap]+0, 0);        /* Flag bits */
    C = GetMem(DCAR[adap]+1, 0);        /* Cylinder */
    S = GetMem(DCAR[adap]+2, 0);        /* Sector */
    N = GetMem(DCAR[adap]+3, 0);        /* Number of sectors */
    switch (u) {
        case 0:
            uptr = r1_dev.units;
            break;
        case 1:
            uptr = f1_dev.units;
            break;
        case 2:
            uptr = r2_dev.units;
            break;
        case 3:
            uptr = f2_dev.units;
            break;
        default:
            break;
    }
    if (debug_lvl & 0x02)
        fprintf(trace, "=D=> %04X %s %01X,%d,%04X DAR=%04X CAR=%04X F=.. C=%02X, S=%02X, N=%02X\n",
            IAR[level],
            opstr[op],
            m, n, data,
            DDAR[adap],
            DCAR[adap],
            C, S, N);

    switch (op) {

        /* SIO 5444 */
        case 0:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT;
            diskerr[disk] = 0;          /* SIO resets errors */
            found[disk] = 0;            /* ... and found bit */
            iodata = 0;
            switch (n) {
                case 0x00:  /* Seek */
                    if (S & 0x80)
                        seekhead[disk] = 1;
                    else
                        seekhead[disk] = 0;
                    if (S & 0x01) {
                        uptr -> CYL += N;/* Seek forwards */
                    } else {
                        uptr -> CYL -= N;/* Seek backwards */
                    }
                    if (uptr -> CYL < 0)
                        uptr -> CYL = 0;
                    if (uptr -> CYL > 203) {
                        uptr -> CYL = 0;
                        diskerr[disk] |= 0x0100;   /* Set sense bit */
                        if (debug_lvl & 0x02)
                            fprintf(trace, "=D=> Seek Past End of Disk\n");
                    }

                    /* sim_activate(uptr, uptr -> wait); */
                    sim_activate(uptr, 1);

                    /* Seek arms are the same for both disks on a drive:
                             update the other arm */

                    usave = uptr -> CYL;
                    if (u == 0) uptr = f1_dev.units;
                    if (u == 1) uptr = r1_dev.units;
                    if (u == 2) uptr = f2_dev.units;
                    if (u == 3) uptr = r2_dev.units;
                    uptr -> CYL = usave;

                    seekbusy[disk] = 1;
                    iodata = SCPE_OK;
                    break;

                case 0x01:  /* Read */
                    switch (data) {
                        case 0:     /* Read data */
                        case 2:     /* Read Diagnostic */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[adap];

                            for (i = 0; i < nsects; i++) {
                                if (data == 2) {  /* Diagnostic read ? */
                                    addr = DDAR[adap];
                                }
                                r = read_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->CYL != C) {
                                    diskerr[disk] |= 0x0800;
                                    break;
                                }
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    PutMem(addr, 0, dbuf[j]);
                                    addr++;
                                }

//                        if (data == 2) {  /* Diagnostic read ? */
  //                         addr = DDAR[adap];
    //                    }
                                if (sect == 55) { /* HJS mod */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl */
                                    DDAR[adap] = addr & 0xFFFF;  /* HJS mod */
                                    PutMem(DCAR[adap]+2, 0, S << 2);
                                    PutMem(DCAR[adap]+3, 0, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }

                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[adap] = addr & 0xFFFF;   /* HJS mod */
                            PutMem(DCAR[adap]+2, 0, S << 2);
                            PutMem(DCAR[adap]+3, 0, N);
                            //  sim_activate(uptr, uptr -> wait);
                            sim_activate(uptr, 8);
                            iodata = SCPE_OK;
                            break;
                        case 1:     /* Read ID */
                            if (uptr -> CYL > 0 && uptr -> CYL < 4)
                                PutMem(DCAR[adap], 0, 1);
                            else
                                PutMem(DCAR[adap], 0, 0);
                            PutMem(DCAR[adap]+1, 0, uptr -> CYL);
                            PutMem(DCAR[adap]+2, 0, RIDsect[disk]);
                            RIDsect[disk]++;
                            if (RIDsect[disk] > 23)
                                RIDsect[disk] = 32;
                            if (RIDsect[disk] > 55)
                                RIDsect[disk] = 0;
                            break;
                        case 3:     /* Verify */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[adap];
                            for (i = 0; i < nsects; i++) {
                                r = read_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->CYL != C) {
                                    diskerr[disk] |= 0x0800;
                                    break;
                                }
                                if (sect == 55) { /* HJS mod */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl */
                                    DDAR[adap] = addr & 0xFFFF;
                                    PutMem(DCAR[adap]+2, 0, S << 2);
                                    PutMem(DCAR[adap]+3, 0, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[adap] = addr & 0xFFFF;
                            PutMem(DCAR[adap]+2, 0, S << 2);
                            PutMem(DCAR[adap]+3, 0, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;
                        default:
                            return STOP_INVDEV;
                    }
                    break;
                case 0x02:  /* Write */
                    switch (data) {
                        case 0:                     /* Write Data */
                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[adap];
                            for (i = 0; i < nsects; i++) {
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    dbuf[j] = GetMem(addr, 0);
                                    addr++;
                                }
                                r = write_sector(uptr, dbuf, sect);
                                if (r != 1 || uptr->CYL != C) {
                                    diskerr[disk] |= 0x0400;
                                    break;
                                }
                                if (sect == 55) { /* HJS mod */
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl */
                                    DDAR[adap] = addr & 0xFFFF;
                                    PutMem(DCAR[adap]+2, 0, S << 2);
                                    PutMem(DCAR[adap]+3, 0, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[adap] = addr & 0xFFFF;  /* HJS mod */
                            PutMem(DCAR[adap]+2, 0, S << 2);
                            PutMem(DCAR[adap]+3, 0, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;
                        case 1:                 /* Write identifier */
                            if (seekhead[disk] == 0)
                                S = 0;
                            else
                                S = 0x80;
                            N = 23;

                            sect = (S >> 2) & 0x3F;
                            nsects = N + 1;
                            addr = DDAR[adap];
                            for (i = 0; i < nsects; i++) {
                                for (j = 0; j < DSK_SECTSIZE; j++) {
                                    dbuf[j] = GetMem(addr, 0);
                                }
                                r = write_sector(uptr, dbuf, sect);
                                if (r != 1) {
                                    diskerr[disk] |= 0x0400;
                                    break;
                                }
                                if (sect == 55) {
                                    S = sect;
                                    N = nsects - i - 2;
                                    if (N > 0) diskerr[disk] |= 0x0020;
                                    DDAR[adap] = addr & 0xFFFF;
                                    PutMem(DCAR[adap]+2, 0, S << 2);
                                    PutMem(DCAR[adap]+3, 0, N);
                                    sim_activate(uptr, 1);
                                    iodata = SCPE_OK;
                                    break;
                                }
                                sect++;
                                S = sect - 1;
                                N = nsects - i - 2;
                                if (sect == 24)
                                    sect = 32;
                            }
                            DDAR[adap] = addr & 0xFFFF;
                            PutMem(DCAR[adap]+2, 0, S << 2);
                            PutMem(DCAR[adap]+3, 0, N);
                            /*sim_activate(uptr, uptr -> wait);*/
                            sim_activate(uptr, 1);
                            break;
                        default:
                            return STOP_INVDEV;
                    }
                    break;
                case 0x03:  /* Scan */
                    sect = (S >> 2) & 0x3F;
                    nsects = N + 1;
                    addr = DDAR[adap];
                    for (i = 0; i < nsects; i++) {
                        r = read_sector(uptr, dbuf, sect);
                        if (r != 1 || uptr->CYL != C) {
                            diskerr[disk] |= 0x0800;
                            break;
                        }
                        res = 0;             /* Equal */
                        for (j = 0; j < DSK_SECTSIZE; j++) {
                            c = GetMem(addr, 0);
                            if (c != 0xff) {  /* Masked off ? */
                                if (dbuf[i] < c)
                                    res = 1;    /* Lower */
                                if (dbuf[i] > c)
                                    res = 3;    /* Higher */
                            }
                            addr++;
                        }
                        if (res == 0)
                            found[disk] = 1;
                        if (res == data)
                            break;
                        if (sect == 55) { /* HJS mod */
                            S = sect;
                            N = nsects - i - 2;
                            if (N > -1) diskerr[disk] |= 0x0020; /* end of cyl. */
                            DDAR[adap] = addr & 0xFFFF;
                            PutMem(DCAR[adap]+2, 0, S << 2);
                            PutMem(DCAR[adap]+3, 0, N);
                            sim_activate(uptr, 1);
                            iodata = SCPE_OK;
                            break;
                        }
                        sect++;
                        S = sect - 1;
                        N = nsects - i - 2;
                        if (sect == 24)
                            sect = 32;
                    }
                    PutMem(DCAR[adap]+2, 0, S << 2);
                    PutMem(DCAR[adap]+3, 0, N);
                    /*sim_activate(uptr, uptr -> wait);*/
                    sim_activate(uptr, 1);
                    break;
                default:
                    return STOP_INVDEV;
            }
            return iodata;

        /* LIO 5444 */
        case 1:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT;
            switch (n) {
                case 0x04:  /* Data Addr */
                    DDAR[adap] = data;
                    break;
                case 0x06:  /* Control Addr */
                    DCAR[adap] = data;
                    break;
                default:
                    return STOP_INVDEV;
            }
            return SCPE_OK;

      /* TIO 5444 */
        case 2:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x00:  /* Error */
                    if (diskerr[disk] || d_nrdy[disk])
                        iodata = 1;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata = 1;
                    break;
                case 0x02:  /* Busy */
                    if (sim_is_active (uptr))
                        iodata = 1;
                    break;
                case 0x04:  /* Scan Found */
                    if (found[disk])
                        iodata = 1;
                    break;
                default:
                    return (STOP_INVDEV << 16);
            }
            return ((SCPE_OK << 16) | iodata);

        /* SNS 5444 */
        case 3:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x01:
                    break;
                case 0x02:
                    iodata = diskerr[disk];
                    if (d_nrdy[disk])
                        iodata |= 0x4000;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata |= 0x4000;
                    if (seekbusy[disk])
                        iodata |= 0x0010;
                    if (uptr -> CYL == 0)
                        iodata |= 0x0040;
                    break;
                case 0x03:
                    iodata = 0;
                    break;
                case 0x04:
                    iodata = DDAR[adap];
                    break;
                case 0x06:
                    iodata = DCAR[adap];
                    break;
                default:
                    return (STOP_INVDEV << 16);
            }
         if (debug_lvl & 0x02)
            fprintf(trace, "=D=> Sense = %04X \n", iodata);
            iodata |= ((SCPE_OK << 16) & 0xffff0000);
            return (iodata);

        /* APL 5444 */
        case 4:
            if ((uptr->flags & UNIT_ATT) == 0)
                return SCPE_UNATT << 16;
            iodata = 0;
            switch (n) {
                case 0x00:  /* Error */
                    if (diskerr[disk] || d_nrdy[disk])
                        iodata = 1;
                    if ((uptr -> flags & UNIT_ATT) == 0)
                        iodata = 1;
                    break;
                case 0x02:  /* Busy */
                    if (sim_is_active (uptr))
                        iodata = 1;
                    break;
                default:
                    return (STOP_INVDEV << 16);
            }
            return ((SCPE_OK << 16) | iodata);
        default:
            break;
    }
    return SCPE_IERR;
}

/* Disk unit service.  If a stacker select is active, copy to the
   selected stacker.  Otherwise, copy to the normal stacker.  If the
   unit is unattached, simply exit.
*/

static int disk_get_index (UNIT *uptr)
{
   DEVICE *dptr = find_dev_from_unit (uptr);

   return (int)(dptr->name[1] - '1');
}

t_stat disk_svc (UNIT *uptr)
{
   seekbusy[disk_get_index(uptr)] = 0;
   return SCPE_OK;
}

/* Disk reset */

t_stat disk_reset (DEVICE *dptr)
{
   UNIT *uptr = dptr->units;
   int index = disk_get_index(uptr);

   diskerr[index] = d_nrdy[index] = 0;      /* clear indicators */
   found[index] = seekbusy[index] = 0;
   sim_cancel (uptr);                       /* clear event */
   uptr -> CYL = 0;                         /* cylinder 0 */
   return SCPE_OK;
}

/* Disk unit attach */

t_stat disk_attach (UNIT *uptr, CONST char *cptr)
{
   int index = disk_get_index(uptr);
   t_stat r;

   diskerr[index] = d_nrdy[index] = 0;   /* clear status */
   found[index] = seekbusy[index] = 0;
   uptr -> CYL = 0;                      /* cylinder 0 */
   r = disk_tape2dsk (uptr, &cptr);      /* convert tape, if specified */
   if (r != SCPE_OK)
       return r;
   return attach_unit (uptr, cptr);
}

/* Bootstrap routine. Only valid for R1 & F1 */

t_stat disk_boot (int32 unitno, DEVICE *dptr)
{
   UNIT *uptr = dptr->units;
   int index = disk_get_index(uptr);
   int i;

   if (index != 0)
      return STOP_INVDEV;

   uptr->CYL = 0;                  /* reset error flags */
   read_sector(uptr, dbuf, 0);
   for (i = 0; i < 256; i++) {
      PutMem(i, 0, dbuf[i]);
   }
   return SCPE_OK;
}

/* Raw Disk Data In/Out */

static int32 read_sector(UNIT *uptr, char *dbuf, int32 sect)
{
    static int32 rtn, realsect;
    static long pos;

    /* calculate real sector no */
    if (sect > 23)
        realsect = sect - 8;
    else
        realsect = sect;

   /* physically read the sector */
   pos = DSK_CYLSIZE * uptr -> CYL;
   pos += DSK_SECTSIZE * realsect;
   rtn = fseek(uptr -> fileref, pos, 0);
   rtn = fread(dbuf, DSK_SECTSIZE, 1, uptr -> fileref);
   return (rtn);
}

static int32 write_sector(UNIT *uptr, char *dbuf, int32 sect)
{
    static int32 rtn, realsect;
    static long pos;

    /* calculate real sector no */
    if (sect > 23)
        realsect = sect - 8;
    else
        realsect = sect;
    if (uptr -> CYL == 0 && realsect == 32)
        rtn = 0;

   /* physically write the sector */
   pos = DSK_CYLSIZE * uptr -> CYL;
   pos += DSK_SECTSIZE * realsect;
   rtn = fseek(uptr -> fileref, pos, 0);
   rtn = fwrite(dbuf, DSK_SECTSIZE, 1, uptr -> fileref);
   return (rtn);
}

const char *disk_description (DEVICE *dptr)
{
   static char description[64];

   snprintf (description, sizeof(description), "IBM 5444 %s Disk Drive %c", (dptr->name[0] == 'F') ? "Fixed" : "Removable", dptr->name[1]);
   return description;
}

#include <sim_tape.h>
#define FLPSIZ 65536
t_stat disk_tape2dsk (UNIT *uptr, CONST char **pcptr)
{
   CONST char *cptr = *pcptr;
   char gbuf[CBUFSIZE];
   uint8 buf[FLPSIZ];
   uint8 zeros[2*DSK_TRKSIZE];
   t_stat r;
   int stopeof = (sim_switches & SWMASK ('E'));
   int verbose = (sim_switches & SWMASK ('V'));
   int prevsize = 1;
   int fcount, recs;
   t_mtrlnt wc, max, min;
   FILE *fdisk;

   if ((sim_switches & SWMASK ('T')) == 0)  /* Tape migrate option requested? */
      return SCPE_OK;
   memset(zeros, 0, sizeof(zeros));
   cptr = get_glyph (cptr, gbuf, 0);        /* get tape spec */
   sim_tape_set_fmt (uptr, 0, "aws", NULL); /* Default to aws tape format */
   sim_switches |= SWMASK ('E');            /* Tape image must exist */
   r = sim_tape_attach(uptr, gbuf);
   if (r != SCPE_OK)
      return sim_messagef (r, "Error opening tape image file: '%s'\n", gbuf);;
   fdisk = fopen (cptr, "wb");
   if (fdisk == NULL) {
      int saved_errno = errno;
      sim_tape_detach (uptr);
      sim_tape_set_fmt (uptr, 0, "simh", NULL); /* Unset tape format */
      return sim_messagef (r, "Can't open: '%s' - %s\n", cptr, strerror (saved_errno));
   }
   fcount = recs = max = 0;
   min = 65535;
   sim_printf("Copying file %d", fcount);
   while (1) {
      if (stopeof > 1)
         break;
      memset(buf, 0, sizeof(buf));
      r = sim_tape_rdrecf (uptr, buf, &wc, sizeof(buf));
      if (r == MTSE_TMK) {
         if (recs > 0) {
            sim_printf ("\nProcessed: %d blocks, min %d, max %d\n", recs, min, max);
         } else {
            if ((stopeof == 1) && (prevsize == 0))
               stopeof = 2;
         }
         fcount++;
         prevsize = recs;
         recs = max = 0;
         min = 65535;
         if (stopeof < 2)
             sim_printf ("\nCopying file %d...", fcount);
         continue;
      }
      if (r == MTSE_EOM)
          break;
      if (wc > max)
         max = wc;
      if (wc < min)
         min = wc;
      recs++;
      if (verbose)
          sim_printf ("\nRecord %d, %d bytes", recs, wc);
      wc = (wc + 1) & ~1;
      if (recs == 6) {
         fwrite (zeros, sizeof(*zeros), sizeof(zeros), fdisk);
         fwrite (zeros, sizeof(*zeros), sizeof(zeros), fdisk);
         fwrite (zeros, sizeof(*zeros), sizeof(zeros), fdisk);
      }
      if (recs > 1) {
         fwrite (buf+2, sizeof(*buf), wc-2, fdisk);
         sim_printf ("\nWriting record %d to disk", recs);
      }
   }

   if (recs > 0)
      sim_printf ("\nProcessed: %d blocks, min %d, max %d\n", recs, min, max);
   else
      sim_printf ("\n(Empty)");

   sim_printf ("\nEnd of Tape\n");

   fwrite (zeros, sizeof(*zeros), sizeof(zeros), fdisk);
   sim_tape_detach (uptr);
   sim_tape_set_fmt (uptr, 0, "simh", NULL); /* Unset tape format */
   fclose (fdisk);
   *pcptr = cptr;
   return SCPE_OK;
}

t_stat disk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s (%s)\n\n", disk_description (dptr), dptr->name);
fprintf (st, "The 5444 came as a set of two drives, each with two disks. The\n");
fprintf (st, "top disk in a drive was removeable, the bottom fixed. The first\n");
fprintf (st, "drive consists of disks R1 and F1, the second drive R2 and F2.\n");
fprintf (st, "Each disk holds 2,457,600 bytes of data.  Each disk has: 2\n");
fprintf (st, "surfaces, 100 tracks, each track has 24 sectors that are 256\n");
fprintf (st, "bytes each.  Physical drives had 2 additional tracks per surface \n");
fprintf (st, "for error replacement data (entire track replacement).\n\n");
fprint_set_help (st, dptr);
if (dptr->boot)
    fprintf (st, "\nThe %s device supports the BOOT command.\n", dptr->name);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\n");
return disk_attach_help(st, dptr, uptr, flag, cptr);
}

t_stat disk_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s Attach Help\n\n", dptr->name);
fprintf (st, "Attach of disk image files is done by:\n\n");
fprintf (st, "   sim> ATTACH %s {switches} filespec\n\n", dptr->name);
fprintf (st, "where optional switches include:\n");
fprintf (st, "  -N   Force create a new (empty disk image file)\n");
fprintf (st, "  -R   Open the disk image file in Read Only mode\n");
fprintf (st, "  -E   The disk image must exist already\n\n");
fprintf (st, "A special attach mode supports loading the contents of a tape image into\n");
fprintf (st, "a newly created (or overwritten) disk image.  This mode is engaged by\n");
fprintf (st, "specifying the -T switch on the attach command.  The default tape format\n");
fprintf (st, "is AWS.  Other tape formats may optionally be specified with additional\n");
fprintf (st, "arguments:\n\n");
fprintf (st, "   sim> ATTACH %s -T {-F alternate-tape-format} tape-filespec disk-filespec\n\n", dptr->name);
fprintf (st, "The following additional switches are also meaningful when performing a\n");
fprintf (st, "tape image to disk copy:\n\n");
fprintf (st, "  -E   Only copy a single file from the source tape\n");
fprintf (st, "  -V   Report details about the tape records being copy to disk\n\n");
return SCPE_OK;
}

