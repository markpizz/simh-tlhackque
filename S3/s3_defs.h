/* s3_defs.h: IBM System/3 simulator definitions

   Copyright (c) 2001-2005, Charles E. Owen
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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

*/

#ifndef S3_DEFS_H_
#define S3_DEFS_H_ 0

#include "sim_defs.h"             /* simulator definitions */

/* General */

#define ON            1
#define OFF           0
#define TRUE          1
#define FALSE         0
#define FILLED        1
#define EMPTY         0

/* Simulator stop codes. Model 15 */

#define STOP_RSRV     1           /* must be 1 */
#define STOP_HPL      2           /* HPL instruction */
#define STOP_IBKPT    3           /* breakpoint */
#define STOP_INVOP    4           /* program check - invalid op */
#define STOP_INVQ     5           /* Prog check - invalid Q */
#define STOP_INVADDR  6           /* Prog check - invalid addr */
#define STOP_INVDEV   7           /* Prog check - invalid dev cmd */
#define STOP_NOCD     8           /* ATTN card reader */
#define STOP_PROCCHK  9           /* Processor check */
#define STOP_BUSY    10           /* Device is busy */
#define STOP_SAR_HIT 11           /* SAR equals dial switches */
#define RESET_INTERRUPT 77        /* Reset device interrupt */ 

/* Memory */

#define MAXMEMSIZE 65536          /* max memory size 16 bits */
#define AMASK  (MAXMEMSIZE - 1)   /* logical addr mask */
#define MEMSIZE  (cpu_unit.capac) /* actual memory size */
#define PAMASK  (MEMSIZE - 1)     /* physical addr mask */

#define MAX_DECIMAL_DIGITS 31     /* Max size of a decimal number */
#define MAX_TAPESIZE 32000        /* Max tape block length */
#define MFCU_WIDTH 96             /* Max S/3 card size */
#define CDR_WIDTH 80              /* Max card size */
#define CDP_WIDTH 80              /* Punch width */
#define LPT_WIDTH 132             /* Printer width */
#define CCT_LNT 132

#define DSK_SECTSIZE 256          /* Sector length */
#define DSK_TRKSIZE 256*24        /* Track length */
#define DSK_CYLSIZE 256*24*2      /* Cylinder length */
#define DSK_DISKSIZE 204*DSK_CYLSIZE /* Total Disk length */

#define DKT_SECTSIZE 128          /* Sector length */
#define DKT_CYLSIZE 128*26        /* Cylinder length */
/* Machine cycles */

#define I    1                    /* Instruction fetch cycles */
#define EA   2                    /* A operand */
#define EB   3                    /* B operand */
#define IO   4                    /* I/O cycle */

/* Program Check Status */

#define ADDR_VIOL    0x80         /* Storage violation */
#define INV_QCODE    0x40         /* Invalid Q code */
#define INV_OPCODE   0x20         /* Invalid opcode */
#define INV_ADDR     0x10         /* Invalid address */
#define PRIV_INST    0x08         /* Privileged instruction */
#define MEM_ERRORS   0x07         /* Memory errors */

/* ANSI control characters */

#define ANSI_CYN_BLK "\x1B[0;36;40m"
#define ANSI_WHT_BLU "\x1B[1;37;44m"
#define ANSI_WHT_BLK "\x1B[1;37;40m"
#define ANSI_GRN_BLK "\x1B[0;32;40m"
#define ANSI_RED_BLK "\x1B[1;31;40m"
#define ANSI_YLW_BLK "\x1B[1;33;40m"
#define ANSI_GRY_BLU "\x1B[1;30;44m"
#define ANSI_WHT_BLU "\x1B[1;37;44m"
#define ANSI_WHT_GRN "\x1B[1;37;42m"
#define ANSI_GRY_GRN "\x1B[1;30;42m"
#define ANSI_WHT_RED "\x1B[1;37;41m"
#define ANSI_GRY_RED "\x1B[1;30;41m"
#define ANSI_GRY_BLK "\x1B[0m"
#define ANSI_LGRN_BLK  "\x1B[1;32;40m"
#define ANSI_CLEAR     "\x1B[2J"
#define ANSI_CLEAR_EOL "\x1B[K"
#define ANSI_CURSOR    "\x1B[%d;%dH"
#define ANSI_SAVE_CURSOR        "\x1B[s"
#define ANSI_CURSOR_UP          "\x1B[1A"
#define ANSI_CURSOR_DOWN        "\x1B[1B"
#define ANSI_CURSOR_FORWARD     "\x1B[1C"
#define ANSI_CURSOR_BACKWARD    "\x1B[1D"
#define ANSI_BLACK_GREEN        "\x1B[30;42m"
#define ANSI_YELLOW_RED         "\x1B[33;1;41m"
#define ANSI_WHITE_BLACK        "\x1B[0m"
#define ANSI_HIGH_INTENSITY     "\x1B[1m"
#define ANSI_ERASE_EOL          "\x1B[K"
#define ANSI_ERASE_SCREEN       "\x1B[2J"
#define ANSI_RESTORE_CURSOR     "\x1B[u"

/* I/O structure

   The I/O structure is tied together by dev_table, indexed by
   the device number.  Each entry in dev_table consists of

   level      Interrupt level for device (0-7)
   priority   Priority for device (1-8)
   routine      IOT action routine
*/

struct ndev {
   int32   level;          /* interrupt level */
   int32   pri;            /* device priority */
   int32   (*routine)(int32 op, int32 m, int32 n, int32 data);   /* dispatch routine */
};

/* Structure to define operation codes */

struct opdef {
   const char op[6];  /* Mnemonic for op */
   int32   opmask;    /* Bits set on in opcode */
   int32   q;         /* Qbyte */
   int32   form;      /* Forms are:
                  0 - 1-byte hex operand
                  1 - 1-byte register addr, A-Addr
                  2 - A-addr,B-addr,Qbyte
                  3 - A-addr,Qbyte
                  4 - da,m,n
                  5 - da,m,n,cc
                  6 - da,m,n,A-addr
                  7 - 1-address implict Q
                  8 - 2-address implict Q */
   int32   group;     /* Group Code:
                  0 - Command Format (0xFx)
                  1 - 1-address A (0x<C,D,E>x)
                  2 - 2-address (0x<0,1,2,4,5,6,8,9,A>x)
                  3 - 1-address B (0x<3,7,B>x) */
};

extern unsigned char M[];
extern int32 saved_PC, IAR[];
extern const unsigned char ebcdic_to_ascii[256];
extern const unsigned char ascii_to_ebcdic[256];
extern int32 IAR[], level, dev_int_req;
extern FILE *trace;
extern int32 debug_lvl;
extern int32 debug_flag;
extern int32 GetMem (int32 addr, int8 cycle);
extern int32 PutMem (int32 addr, int8 cycle, int32 data);

#define LDA 1

#define DEV_S_M10   (DEV_V_UF + 0)
#define DEV_S_M15   (DEV_V_UF + 1)
#define DEV_S_M15AB (DEV_V_UF + 2)
#define DEV_S_M15D  (DEV_V_UF + 3)

#define DEV_M10     (1 << DEV_S_M10)                /* device supported on S3 Model 10      */
#define DEV_M15     (1 << DEV_S_M15)                /* device supported on S3 Model 15      */
#define DEV_M15AB   (1 << DEV_S_M15AB)              /* device supported on S3 Model 15AB    */
#define DEV_M15D    (1 << DEV_S_M15D)               /* device supported on S3 Model 15D     */

#endif
