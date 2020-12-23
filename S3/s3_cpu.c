/* s3_cpu.c: IBM System/3 CPU simulator

   Copyright (c) 2001-2012, Charles E. Owen
   HPL & SLC instruction code Copyright (c) 2001 by Henk Stegeman
   Decimal Arithmetic Copyright (c) 2000 by Roger Bowler

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
   CHARLES E. OWEN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   ------------------------------------------------------------------------------

   cpu      System/3 (models 10 and 15) central processor

   The IBM System/3 was a popular small-business computing system introduced
   in 1969 as an entry-level system for businesses that could not afford
   the lowest rungs of the System/360.  Its architecture is inspired by and
   in some ways similar to the 360, but to save cost the instruction set is
   much smaller and the I/O channel system greatly simplified.  There is no
   compatibilty between the two systems.

   The original System/3 had two models, 6 and 10, and these came in two
   configurations:  card system and disk system.  The unique feature of
   the /3 was the use of 96-column cards, although traditional 80-column
   cards were supprted also via attachment of a 1442 reader/punch.
   System/3 is a batch-oriented system, controlled by an operating
   system known as SCP (System Control Program), with it's own job control
   language known as OCL (simpler and more logical than the JCL used on
   the mainframes).  Original models did not support multiprogramming
   or any form of interactivity. (There was a hardware dual-program
   facility available on the model 10 at the high end).

   The line grew throughout the 1970s, overlapping the low end of the 360
   line with the introduction of the model 15.  The 15 (and later larger
   variations of the model 12) broke the 64K limit designed in the original
   models by adding a simple address translation unit to support up to 512K
   bytes.  The model 15 added a system of storage protection and allowed
   multiprogramming in up to 3 partitions.  Communications were added to
   allow support of multiple 3270 terminals and the models 12 and 15 broke
   the batch orientation and facilitated interactive use via the CCP
   (communications control program).  The System/3 was effectively replaced
   by the much easier to manage and use System/34 and System/36 at the
   low and middle of the range, and by System/370 or System/38 at the
   high end.

   This simulator implements the model 10 and model 15.  Models 4, 6,
   8, and 12 are not supported (these were technical variations on the
   design which offered no functionality not present on either 10 or 15).

   The System/3 is a byte-oriented machine with a data path of 8 bits
   in all models, and an address width of 16 bits.

   The register state for the System/3 CPU is:

   BAR <0:15>      Operand 1 address register
   AAR <0:15>      Operand 2 address register
   XR1 <0:15>      Index Register 1
   XR2 <0:15>      Index Register 2
   PSR <0:15>      Condition Register
   IAR [0:9]<0:15> Instruction Address Register (p1, p2, plus 1 for each interrupt)
   ARR [0:9]<0:15> Address Recall Register (p1, p2, plus 1 for each interrupt)
            (The P2 IAR & ARR are used for the Dual Program feature)

   Instruction formats follow the same basic pattern:  a 1-byte opcode, a
   1-byte "Q byte", and one or two addresses following in a format defined
   by the first 4 bits of the opcode:

          Op Code                    Q Byte                   Address(es)

     0  1  2  3  4  5  6  7      0  1  2  3  4  5  6  7
  +--+--+--+--+--+--+--+--+  +--+--+--+--+--+--+--+--+  +--+--+--+--+--+--+--..
  | A 1 | A 2 | operation |  | (defined by operation)|  | Format based on A1,A2
  +--+--+--+--+--+--+--+--+  +--+--+--+--+--+--+--+--+  +--+--+--+--+--+--+--..

        { ^-^ } <--------- Bits 00 = Operand 2 specified by 2-byte direct addr
                           Bits 01 = Operand 2 is 1-byte displacement + XR1
                           Bits 10 = Operand 2 is 1-byte displacement + XR2
                           Bits 11 = Operand 2 is not used

  { ^-^ } <--------------- Bits 00 = Operand 1 specified by 2-byte direct addr
                           Bits 01 = Operand 1 is 1-byte displacement + XR1
                           Bits 10 = Operand 1 is 1-byte displacement + XR2
                           Bits 11 = Operand 1 is not used

   Instructions come in 3 basic formats, of varying lengths which are determined
   by the top 4 bits of opcode defined above.
   Minimum instruction length is 3 bytes, maximum is 6.

   1) Command Format (Bits 0-3 are 1111):

   +------------+  +------------+  +------------+
   |   Opcode   |  |   Q-byte   |  |   R-byte   |
   +------------+  +------------+  +------------+

      (The meaning of Q-byte and R-byte defined by the operation)


   2) One Address Instructions (either bits 0-1 or bits 2-3 are 01):


      Direct Addressing Format:

   +------------+  +------------+  +---------+---------+
   |   Opcode   |  |   Q-byte   |  |   MSB   +   LSB   |
   +------------+  +------------+  +---------+---------+

      Base-Displacement Format:

   +------------+  +------------+  +------------+
   |   Opcode   |  |   Q-byte   |  |displacement|
   +------------+  +------------+  +------------+

      Opcodes are 0011xxxx or 1100xxxx.

      Q-byte can be:
            1) An immediate operand
            2) A mask
            3) A branch condition
            4) A data selection

   2) Two Address Instructions (neither bits 0-1 nor bits 2-3 are both 11):

   Operand 1 Address Direct (opcodes 0001 or 0010):

   +-----------+  +-----------+  +---------+---------+  +------------+
   |   Opcode  |  |   Q-byte  |  |   MSB   +   LSB   |  |displacement|
   +-----------+  +-----------+  +---------+---------+  +------------+

   Operand 2 Address Direct (opcodes 0100 or 1000):

   +-----------+  +-----------+  +------------+  +---------+---------+
   |   Opcode  |  |   Q-byte  |  |displacement|  |   MSB   |   LSB   |
   +-----------+  +-----------+  +------------+  +---------+---------+

   Both Addresses Direct (opcode 0000):

   +-----------+  +-----------+  +---------+---------+  +---------+---------+
   |   Opcode  |  |   Q-byte  |  |   MSB   +   LSB   +  +   MSB   +   LSB   +
   +-----------+  +-----------+  +---------+---------+  +---------+---------+

      Both Addresses Displacement (opcodes 0101, 0110, 1001, or 1010):

   +-----------+  +-----------+  +------------+  +------------+
   |   Opcode  |  |   Q-byte  |  |displacement|  |displacement|
   +-----------+  +-----------+  +------------+  +------------+


Assembler Mnemonic Format
-------------------------

   The assembler format contains the same elements as the machine language operation,
but not always in the same format.  The operation code frequently specifies both
the opcode and the Q byte, and the top nybble of the opcode is determined by
the format of the addresses.

   Addresses take two forms:  the direct address in hex, or a relative address
specified thusly:  (byte,XRx)  where 'byte' is a 1-byte offset, and XRx is
either XR1 or XR2 for the two index registers.  Use these formats when
'address' is indicated below:

   When 'reg' is mentioned, a mnemonic may be used for the register, thusly:
   IAR   Instruction Address Register for the current program level
   ARR   Address Recall Register for the current program level
   P1IAR IAR for Program Level
   PSR   Program Status Register
            0x01 - Equal
            0x02 - Low
            0x04 - High
            0x08 - Decimal overflow
            0x10 - Test false
            0x20 - Binary overflow
            0x40 - Not used
            0x80 - Not used
   XR1      Index Register 1
   XR2   Index Register 2
   IARx  IAR for the interrupt level x (x = 0 thru 7)

   All other operands mentioned below are single-byte hex, except for the
length (len) operand of the two-address instructions, which is a decimal length
in the range 1-256.

   No-address formats:
   ------------------

   HPL hex,hex      Halt Program Level, the operands are the Q and R bytes


   One-address formats:
   -------------------

   A reg,address      Add to register
   CLI address,byte   Compare Logical Immediate
   MVI address,byte   Move Immediate
   TBF address,mask   Test Bits Off
   TBN address,mask   Test Bits On
   SBF address,mask   Set Bits Off
   SBN address,mask   Set Bits On
   ST reg,address     Store Register
   L reg,address      Load Register
   LA reg,address     Load Address
   JC address,cond    Jump on Condition
   BC address,cond    Branch on Condition

   These operations do not specify a qbyte, it is implicit in the opcode:

   B address       Unconditional branch to address
   BE address      Branch Equal
   BNE address     Branch Not Equal
   BH address      Branch High
   BNH address     Branch Not High
   BL address      Branch Low
   BNL address     Branch Not Low
   BT address      Branch True
   BF address      Branch False
   BP address      Branch Plus
   BM address      Branch Minus
   BNP address     Branch Not Plus
   BNM address     Branch Not Minus
   BZ address      Branch Zero
   BNZ address     Branch Not Zero
   BOZ address     Branch Overflow Zoned
   BOL address     Branch Overflow Logical
   BNOZ address    Branch No Overflow Zoned
   BNOL address    Branch No Overflow Logical
   NOPB address    No - never jump

   (substitute J for B above for a set of Jumps -- 1-byte operand (not 2),
   always jumps forward up to 255 bytes. In this case, 'address' cannot be
   less than the current address, nor greater than the current address + 255)

   Two-address formats (first address is destination, len is decimal 1-256):
   -------------------

   MVC address,address,len  Move Characters
   CLC address,address,len  Compare Logical Characters
   ALC address,address,len  Add Logical Characters
   SLC address,address,len  Subtract Logical Characters
   ED address,address,len   Edit
   ITC address,address,len  Insert and Test Characters
   AZ address,address,len   Add Zoned Decimal
   SZ address,address,len   Subtract Zoned Decimal

   MNN address,address      Move Numeric to Numeric
   MNZ address,address      Move Numeric to Zone
   MZZ address,address      Move Zone to Zone
   MZN address,address      Move Zone to Numeric

   I/O Format
   ----------

   In the I/O format, there are always 3 fields:

      da - Device Address 0-15 (decimal)
      m - Modifier 0-1
      n - Function 0-7

   The meaning of these is entirely defined by the device addressed.

   There may be an optional control byte, or an optional address (based on
   the type of instruction).

   SNS da,m,n,address      Sense I/O
   LIO da,m,n,address      Load I/O
   TIO da,m,n,address      Test I/O

   SIO da,m,n,cc           Start I/O -- cc is a control byte

   APL da,m,n              Advance Program Level



    ---------------------------------------------
    Here is a handy opcode cross-reference table:
    ---------------------------------------------

   |  x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF
---+------------------------------------------------------------------
0x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
1x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
2x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
3x | SNS LIO  -   -   ST  L   A   -  TBN TBF SBN SBF MVI CLI  -   -
   |
4x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
5x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
6x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
7x | SNS LIO  -   -   ST  L   A   -  TBN TBF SBN SBF MVI CLI  -   -
   |
8x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
9x |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
Ax |  -   -   -   -  ZAZ  -   AZ  SZ MVX  -   ED ITC MVC CLC ALC SLC
Bx | SNS LIO  -   -   ST  L   A   -  TBN TBF SBN SBF MVI CLI  -   -
   |
Cx |  BC TIO  LA  -   -   -   -   -   -   -   -   -   -   -   -   -
Dx |  BC TIO  LA  -   -   -   -   -   -   -   -   -   -   -   -   -
Ex |  BC TIO  LA  -   -   -   -   -   -   -   -   -   -   -   -   -
Fx | HPL APL  JC SIO  -   -   -   -   -   -   -   -   -   -   -   -

*/

/* This routine is the instruction decode routine for System/3.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

      HPL instruction
      breakpoint encountered
      program check caused by invalid opcode or qbyte or address or I/O spec
      unknown I/O device and STOP_DEV flag set
      I/O error in I/O simulator

   2. Interrupts.

      There are 8 levels of interrupt, each with it's own IAR (program
      counter).  When an interrupt occurs, execution begins at the
      location in the IAR for that level interrupt.  The program
      must save and restore state.  Each device is assigned both a
      level and a priority in hardware.  Interrupts are reset via
      an SIO instruction, when this happens, the program level
      IAR resumes control.

      Interrupts are maintained in the global variable dev_int_req,
      which is zero if no interrupts are pending, otherwise, the
      lower 16 bits represent devices, rightmost bit being device 0.
      Each device requesting an interrupt sets it's bit on.


   3. Non-existent memory.  On the System/3, any reference to non-existent
      memory (read or write) causes a program check and machine stop.

   4. Adding I/O devices.  These modules must be modified:

   ibms3_defs.h  add interrupt request definition
   ibms3_cpu.c   add IOT mask, PI mask, and routine to dev_table
   ibms3_sys.c   add pointer to data structures to sim_devices
*/

#include "s3_defs.h"

#define ILL_ADR_FLAG 0x10000
#define save_ibkpt   (cpu_unit.u3)
#define UNIT_V_DPF   (UNIT_V_UF)        /* Dual Programming */
#define UNIT_DPF     (1 << UNIT_V_DPF)
#define UNIT_V_MSIZE (UNIT_V_UF+1)      /* dummy mask */
#define UNIT_MSIZE   (1 << UNIT_V_MSIZE)

int32 GetMem (int32 addr, int8 cycle);
int32 PutMem (int32 addr, int8 cycle, int32 data);
int32 compare(int32 byte1, int32 byte2, int32 cond);
int32 condition(int32 qbyte);

uint8 M[MAXMEMSIZE] = { 0 };  /* Memory */
int32 AAR = 0;                /* Operand 1 addr reg */
int32 BAR = 0;                /* Operand 2 addr reg */
int32 XR1 = 0;                /* Index register 1 */
int32 XR2 = 0;                /* Index register 2 */
int32 PSR = 0;                /* Condition Register */
int32 IAR[10] = { 0 };        /* IAR 0-7 = int level 8=P1 9=P2 */
int32 ARR[10] = { 0 };        /* ARR 0-7 = int level 8=P1 9=P2 */
int32 dev_disable = 0;        /* interrupt disable mask */
int32 dev_int_req = 0x0000;   /* Interrupt request device bitmap */
int32 level = 8;              /* Current Execution Level*/
int32 stop_dev = 0;           /* stop on ill dev */
int32 ibkpt_addr = ILL_ADR_FLAG | AMASK;  /* ibreakpoint addr */
int32 SR = 0;                 /* Frontpanel Switches Register */
int32 saved_PC;               /* Saved (old) PC) */
int32 debug_lvl = 0x00;       /* set for debug/trace */
int32 debug_flag = 0;         /* 1 when trace.log open */
struct {
    const char *name;
    int32 flag;
    } sys_models[] = {
        {"10",     DEV_M10},
        {"15",     DEV_M15},
        {NULL,      0}
    };
int32 sys_model = 0;          /* system model */
FILE *trace;

/* Debug levels:
     .......1  CPU>  trace all CPU instructions.
     ......1.  =D=>  trace Disk activity.
     .....1..  =T=>  trace Tape activity.
     ....1...  =F=>  trace MFCU activity.
     ...1....  =P=>  trace Printer activity.
     ..1.....  =K=>  trace disKette activity.
     .1......  =L=>  trace LDA activity.
     1.......  =1=>  trace printer/keyboard activity.
*/

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_model (UNIT *uptr, int32 value, CONST char *cp, void *dp);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_size (UNIT *uptr, int32 value, CONST char *cp, void *dp);
extern int32 pkb (int32 op, int32 m, int32 n, int32 data);
extern int32 crd (int32 op, int32 m, int32 n, int32 data);
extern int32 lpt (int32 op, int32 m, int32 n, int32 data);
extern int32 mfcu (int32 op, int32 m, int32 n, int32 data);
extern int32 dsk1 (int32 op, int32 m, int32 n, int32 data);
extern int32 dsk2 (int32 op, int32 m, int32 n, int32 data);
extern int32 lda1 (int32 op, int32 m, int32 n, int32 data);
extern int32 lda2 (int32 op, int32 m, int32 n, int32 data);
extern int32 tap1 (int32 op, int32 m, int32 n, int32 data);
extern int32 tap2 (int32 op, int32 m, int32 n, int32 data);
extern int32 dkt (int32 op, int32 m, int32 n, int32 data);
extern int32 cpu (int32 op, int32 m, int32 n, int32 data);
int32 nulldev (int32 opcode, int32 m, int32 n, int32 data);
int add_zoned (int32 addr1, int32 len1, int32 addr2, int32 len2);
int32 subtract_zoned (int32 addr1, int32 len1, int32 addr2, int32 len2);
static void store_decimal (int32 addr, int32 len, uint8 *dec, int sign);
static void load_decimal (int32 addr, int32 len, uint8 *result, int32 *count, int32 *sign);
static void add_decimal (uint8 *dec1, uint8 *dec2, uint8 *result, int32 *count);
static void subtract_decimal (uint8 *dec1, uint8 *dec2, uint8 *result, int *count, int *sign);


/* IOT dispatch table */

/* System/3 supports only 16 unique device addresses! */

struct ndev dev_table[16] = {
   { 0, 0, &cpu },     /* Device 0: CPU control */
   { 1, 0, &pkb },     /* Device 1: 5471 console printer/keyboard */
   { 0, 0, &nulldev },
   { 0, 0, &nulldev },
   { 0, 0, &dkt },     /* Device 4: 3741 Data station / LDA */
   { 0, 0, &lda1 },    /* Device 5: LDA Attachment Control */
   { 0, 0, &tap1 },    /* Device 6: 3410 Tape drives 1 & 2 */
   { 0, 0, &tap2 },    /* Device 7: 3410 Tape drives 3 & 4 */
   { 2, 0, &lda2 },    /* Device 8: LDA Terminal Control */
   { 0, 0, &nulldev },
   { 0, 0, &dsk1 },    /* Device A: 5444 Disk Drive 1 */
   { 0, 0, &dsk2 },    /* Device B: 5444 Disk Drive 2 */
   { 0, 0, &nulldev }, /* Device C: 5448 Disk Drive 1 */
   { 0, 0, &nulldev }, /* Device D: 5448 Disk Drive 2 */
   { 0, 0, &lpt },     /* Device E: 1403/5203 Printer */
   { 0, 0, &mfcu }     /* Device F: 5424 MFCU */
};

/* Priority assigned to interrupt levels */

int32 priority[8] = {8, 7, 5, 4, 3, 6, 2, 1};

/* CPU data structures

   cpu_dev   CPU device descriptor
   cpu_unit  CPU unit descriptor
   cpu_reg   CPU register list
   cpu_mod   CPU modifiers list
*/

UNIT cpu_unit = { UDATA (&cpu_svc, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
   { HRDATA (IAR, saved_PC, 16) },
   { HRDATA (IARP1, IAR[8], 16) },
   { HRDATA (IARP2, IAR[9], 16) },
   { HRDATA (ARRP1, ARR[8], 16) },
   { HRDATA (ARRP2, ARR[9], 16) },
   { HRDATA (AAR, AAR, 16) },
   { HRDATA (BAR, BAR, 16) },
   { HRDATA (XR1, XR1, 16) },
   { HRDATA (XR2, XR2, 16) },
   { HRDATA (PSR, PSR, 16) },
   { HRDATA (SR, SR, 16) },
   { HRDATA (INT, dev_int_req, 16) },
   { HRDATA (LEVEL, level, 16) },
   { HRDATA (IAR0, IAR[0], 16) },
   { HRDATA (IAR1, IAR[1], 16) },
   { HRDATA (IAR2, IAR[2], 16) },
   { HRDATA (IAR3, IAR[3], 16) },
   { HRDATA (IAR4, IAR[4], 16) },
   { HRDATA (IAR5, IAR[5], 16) },
   { HRDATA (IAR6, IAR[6], 16) },
   { HRDATA (IAR7, IAR[7], 16) },
   { HRDATA (ARR0, ARR[0], 16) },
   { HRDATA (ARR1, ARR[1], 16) },
   { HRDATA (ARR2, ARR[2], 16) },
   { HRDATA (ARR3, ARR[3], 16) },
   { HRDATA (ARR4, ARR[4], 16) },
   { HRDATA (ARR5, ARR[5], 16) },
   { HRDATA (ARR6, ARR[6], 16) },
   { HRDATA (ARR7, ARR[7], 16) },
   { HRDATA (DISABLE, dev_disable, 16), REG_RO },
   { FLDATA (STOP_DEV, stop_dev, 0) },
   { HRDATA (WRU, sim_int_char, 8) },
   { HRDATA (BREAK, ibkpt_addr, 17) },
   { HRDATA (DEBUG, debug_lvl, 16) },
   { HRDATA (MODEL, sys_model, 32) },
   { NULL } };

MTAB cpu_mod[] = {
   { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={10|15}",
     &cpu_set_model, &cpu_show_model, NULL, "Set/Display processor model" },
   { UNIT_DPF, UNIT_DPF, "DPF", "DPF", NULL },
   { UNIT_DPF, 0, "NODPF", "NODPF", NULL },
   { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
   { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
   { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
   { UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
   { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
   { 0 } };

DEVICE cpu_dev = {
   "CPU", &cpu_unit, cpu_reg, cpu_mod,
   1, 16, 16, 1, 16, 8,
   &cpu_ex, &cpu_dep, &cpu_reset,
   NULL, NULL, NULL };

/* Simulate S/3 instruction routine */
t_stat sim_instr (void)
{
t_addr PC;
int32 IR;
int32 i, j, carry, zero, t, op1, op2;
int32 opcode = 0, qbyte = 0, rbyte = 0;
int32 opaddr, addr1, addr2, dlen1, dlen2, r;
int32 int_savelevel = 8, intpri, intlev, intdev = 0, intmask;
int32 devno, devm, devn;
char HPL_rbyte = 0x00;
char display[3][9];
t_value val [32];
t_stat reason;

/* Restore register state */

PC = IAR[level];                     /* load local PC */
reason = 0;

/* Main instruction fetch/decode loop */

while (reason == 0) {                /* loop until halted */ 

if (sim_interval <= 0) {             /* check clock queue */
   reason = sim_process_event ();
   if (reason != SCPE_OK)
      break;
}

if (dev_int_req) {                   /* interrupt ? */
   intpri = 16;
   for (i = 0; i < 16; i++) {        /* Get highest priority device */
      if ((dev_int_req >> i) & 0x01) {
         intlev = dev_table[i].level;
         if (priority[intlev] < intpri) {
            intdev = i;
            intpri = priority[intlev];
         }
      }
   }
   intmask = 1 << intdev;            /* mask is interrupting dev bit */
   dev_int_req = ~dev_int_req & intmask; /* Turn off int_req for device */
   int_savelevel = level;            /* save current level for reset */
   level = dev_table[intdev].level;  /* get int level from device */
   PC = IAR[level];                  /* Use int level IAR for new PC */
}                                    /* end interrupt */

if (PC == ibkpt_addr) {              /* breakpoint ? */
   save_ibkpt = ibkpt_addr;          /* save address */
   ibkpt_addr = ibkpt_addr | ILL_ADR_FLAG;      /* disable */
   sim_activate (&cpu_unit, 1);      /* sched re-enable */
   reason = STOP_IBKPT;              /* stop simulation */
   break;
}

/* Machine instruction execution starts here... */

if ((debug_lvl == 0) && debug_flag == 1) {
   fclose(trace);
   debug_flag = 0;
}

if (debug_lvl) {
   if (!debug_flag) {
      trace = fopen("trace.log", "w");
      debug_flag = 1;
   }
}

if (debug_lvl & 0x01) {
   fprintf(trace, "CPU> ARR=%04X XR1=%04X XR2=%04X IAR=%04X LVL=%1X ",
           ARR[level], XR1, XR2, PC, level);

   val[0] = GetMem(PC, 0);
   val[1] = GetMem(PC+1, 0);
   val[2] = GetMem(PC+2, 0);
   val[3] = GetMem(PC+3, 0);
   val[4] = GetMem(PC+4, 0);
   val[5] = GetMem(PC+5, 0);

   fprint_sym(trace, PC, val, &cpu_unit, SWMASK('M'));
   fprintf(trace, "\n");
}

saved_PC = PC;
opaddr = GetMem(PC, 0) & 0xf0;          /* fetch addressing mode */
opcode = GetMem(PC, 0) & 0x0f;          /* fetch opcode */
PC = (PC + 1) & AMASK;
sim_interval = sim_interval - 1;

qbyte = GetMem(PC, 0) & 0xFF;           /* fetch qbyte */
PC = (PC + 1) & AMASK;

if (opaddr == 0xf0) {                /* is it command format? */
   rbyte = GetMem(PC, 0) & 0xFF;
   PC = (PC + 1) & AMASK;
   switch (opcode) {
      case 0x00:      /* HPL: Halt Program Level */
         for (i = 0; i < 3; i++) {
            for (j = 0; j < 9; j++) {
                   display[i][j] = ' ';
            }
         }

         /* First line */
         if (qbyte & 0x04) display[0][2] = '_' ;
         if (rbyte & 0x04) display[0][6] = '_' ;
         /* Second line */
         if (qbyte & 0x08) display[1][1] = '!' ;
         if (rbyte & 0x08) display[1][5] = '!' ;
         if (qbyte & 0x10) display[1][2] = '_' ;
         if (rbyte & 0x10) display[1][6] = '_' ;
         if (qbyte & 0x02) display[1][3] = '!' ;
         if (rbyte & 0x02) display[1][7] = '!' ;
         /* Third line */
         if (qbyte & 0x20) display[2][1] = '!' ;
         if (rbyte & 0x20) display[2][5] = '!' ;
         if (qbyte & 0x40) display[2][2] = '_' ;
         if (rbyte & 0x40) display[2][6] = '_' ;
         if (qbyte & 0x01) display[2][3] = '!' ;
         if (rbyte & 0x01) display[2][7] = '!' ;

         if (qbyte == 0x19) {        /* CCP HPL ?   */
            PC = PC - 3;             /* Loop on HPL */
            if (rbyte == HPL_rbyte) {
               break;
            } else {
               /* Print display segment array */
               sim_printf("\n\r");
               for (i = 0; i < 3; i++) {
                  for (j = 0; j < 9; j++) {
                     sim_printf ("%c", display[i][j]);
                  }
               sim_printf ("\n\r");
               }
            }
            HPL_rbyte = rbyte;
            break;
         } else {              /* All other HPL's */
            /* Print display segment array */
            sim_printf("\n\n\r");
            for (i = 0; i < 3; i++) {
               for (j = 0; j < 9; j++) {
                  sim_printf ("%c", display[i][j]);
               }
               sim_printf ("\n\r");
            }
         }
         reason = STOP_HPL;
         break;
      case 0x01:      /* APL: Advance Program Level */
         devno = (qbyte >> 4) & 0x0f;
         devm = (qbyte >> 3) & 0x01;
         devn = qbyte & 0x07;
         op1 = dev_table[devno].routine(4, devm, devn, rbyte);
         if (op1 & 0x01) {
            if (cpu_unit.flags & UNIT_DPF) {    /* Dual Programming? */
               if (level == 8)       /* Yes: switch program levels */
                  level = 9;
               else
                  level = 8;
               PC = IAR[level];
            } else {                 /* No: Loop on this inst */
               PC = PC - 3;
            }
         }
         reason = (op1 >> 16) & 0xffff;
         break;
      case 0x02:      /* JC: Jump on Condition */
         if (condition(qbyte) == 1) {
            PC = (PC + rbyte) & AMASK;
         }
         break;
      case 0x03:      /* SIO: Start I/O */
         devno = (qbyte >> 4) & 0x0f;
         devm = (qbyte >> 3) & 0x01;
         devn = qbyte & 0x07;
         reason = dev_table[devno].routine(0, devm, devn, rbyte);
         if (reason == RESET_INTERRUPT) {
            reason = SCPE_OK;
            IAR[level] = PC;
            level = int_savelevel;
            PC = IAR[level];
         }
         if (reason == SCPE_UNATT)
            PC = PC - 3;             /* re-execute SIO after attach */
         break;
      default:
         reason = STOP_INVOP;
         break;
   } /* switch (opcode) */
   IAR[level] = PC;
   continue;
}
/* Not command format: fetch the addresses */

addr1 = (opaddr >> 6) & 3;
addr2 = (opaddr >> 4) & 3;

switch (addr1) {
   case 0:
      BAR = GetMem(PC, 0) << 8;
      PC = (PC + 1) & AMASK;
      BAR |= GetMem(PC, 0);
      PC = (PC + 1) & AMASK;
      break;
   case 1:
      BAR = GetMem(PC, 0);
      BAR = (BAR + XR1) & AMASK;
      PC = (PC + 1) & AMASK;
      break;
   case 2:
      BAR = GetMem(PC, 0);
      BAR = (BAR + XR2) & AMASK;
      PC = (PC + 1) & AMASK;
      break;
   case 3:
      break;
   default:
      break;
} /* switch (addr1) */

switch (addr2) {
   case 0:
      AAR = GetMem(PC, 0) << 8;
      PC = (PC + 1) & AMASK;
      AAR |= GetMem(PC, 0);
      PC = (PC + 1) & AMASK;
      break;
   case 1:
      AAR = GetMem(PC, 0);
      AAR = (AAR + XR1) & AMASK;
      PC = (PC + 1) & AMASK;
      break;
   case 2:
      AAR = GetMem(PC, 0);
      AAR = (AAR + XR2) & AMASK;
      PC = (PC + 1) & AMASK;
      break;
   case 3:
      break;
   default:
      break;
} /* switch (addr1) */

switch (opaddr) {
   case 0x00:
   case 0x10:
   case 0x20:
   case 0x40:
   case 0x50:
   case 0x60:
   case 0x80:
   case 0x90:
   case 0xa0:
      switch (opcode) {
         case 4:        /* ZAZ: Zero and Add Zoned */
            dlen2 = qbyte & 0x0f;
            dlen1 = (qbyte >> 4) & 0xf;
            dlen1 += dlen2;
            ARR[level] = BAR;        /* Required for running DCP */
            op1 = BAR;
            for (i = 0; i < (dlen1+1); i++) {
               PutMem(op1, 0, 0xF0); /* Zero operand 1 */
               op1--;
            }
            r = add_zoned(BAR, dlen1+1, AAR, dlen2+1);
            PSR &= 0xF8;
            switch (r) {
               case 0:
                  PSR |= 0x01;
                  break;
               case 1:
                  PSR |= 0x02;
                  break;
               case 2:
                  PSR |= 0x04;
                  break;
               default:
                  break;
            }
            break;
         case 6:        /* AZ: Add Zoned */
            dlen2 = qbyte & 0x0f;
            dlen1 = (qbyte >> 4) & 0xf;
            dlen1 += dlen2;
            r = add_zoned(BAR, dlen1+1, AAR, dlen2+1);
            PSR &= 0xF0;
            switch (r) {
               case 0:
                  PSR |= 0x01;
                  break;
               case 1:
                  PSR |= 0x02;
                  break;
               case 2:
                  PSR |= 0x04;
                  break;
               case 3:
                  PSR |= 0x08;
                  break;
               default:
                  break;
            }
            break;
         case 7:        /* SZ: Subtract Zoned */
            dlen2 = qbyte & 0x0f;
            dlen1 = (qbyte >> 4) & 0xf;
            dlen1 += dlen2;
            r = subtract_zoned(BAR, dlen1+1, AAR, dlen2+1);
            PSR &= 0xF0;
            switch (r) {
               case 0:
                  PSR |= 0x01;
                  break;
               case 1:
                  PSR |= 0x02;
                  break;
               case 2:
                  PSR |= 0x04;
                  break;
               case 3:
                  PSR |= 0x08;
                  break;
               default:
                  break;
            }
            break;
         case 8:        /* MVX: Move Hex */
            op1 = GetMem(BAR, 0);
            op2 = GetMem(AAR, 0);
            switch (qbyte) {
               case 0: /* Zone to zone */
                  op1 = (op1 & 0x0F) | (op2 & 0xF0);
                  break;
               case 1:  /* Numeric to zone */
                  op1 = (op1 & 0x0F) | (op2 << 4);
                  break;
               case 2:  /* Zone to numeric */
                  op1 = (op1 & 0xF0) | (op2 >> 4);
                  break;
               case 3:  /* Numeric to numeric */
                  op1 = (op1 & 0xF0) | (op2 & 0x0F);
                  break;
               default:
                  reason = STOP_INVQ;
                  break;
            }
            PutMem(BAR, 0, op1);
            break;
         case 0xa:      /* ED: Edit */
            zero = 1;
            PSR &= 0xF8;
            IR = GetMem(AAR, 0);
            if ((IR & 0xf0) != 0xF0)
               PSR |= 0x02;
            else
               PSR |= 0x04;
            while (qbyte > -1) {
               op2 = GetMem(AAR, 0);
               op1 = GetMem(BAR, 0);
               if (op1 == 0x20) {
                  op2 |= 0xf0;
                  PutMem(BAR, 0, op2);
                  AAR--;
                  if (op2 != 0xF0) zero = 0;
               }
               BAR--;
               qbyte--;
            }
            if (zero)
               PSR |= 0x01;
            break;
         case 0xb:      /* ITC: Insert and Test Chars */
            op2 = GetMem(AAR, 0);
            while (qbyte > -1) {
               op1 = GetMem(BAR, 0);
               if (op1 >= 0xF1 && op1 <= 0xF9)
                  break;
               PutMem(BAR, 0, op2);
               BAR++;
               qbyte--;
            }
            ARR[level] = BAR;
            break;
         case 0xc:      /* MVC: Move Characters */
            while (qbyte > -1) {
               PutMem(BAR, 0, GetMem(AAR, 0));
               BAR--;
               AAR--;
               qbyte--;
            }
            break;
         case 0xd:      /* CLC: Compare Characters */
            PSR &= 0xF8;
            i = BAR = BAR - qbyte;
            j = AAR = AAR - qbyte;
            while (qbyte > -1) {
               if (GetMem(i, 0) > GetMem(j, 0)) {
                  PSR |= 0x04;
                  break;
               }
               if (GetMem(i, 0) < GetMem(j, 0)) {
                  PSR |= 0x02;
                  break;
               }
               i++;
               j++;
               qbyte--;
            }
            if (qbyte == -1)
               PSR |= 0x01;
            break;
         case 0xe:      /* ALC: Add Logical Characters */
            carry = 0;
            zero = 1;
            while (qbyte > -1) {
               IR = GetMem(BAR, 0) + GetMem(AAR, 0) + carry;
               if (IR & 0x100)
                  carry = 1;
                  else
                  carry = 0;
               if ((IR & 0xFF) != 0) zero = 0; /* HJS mod */
               PutMem(BAR, 0,(IR & 0xFF));
               BAR--;
               AAR--;
               qbyte--;
            }
            PSR &= 0xD8;
            if (zero)
               PSR |= 0x01; /* Equal */
            if (!zero && !carry)
               PSR |= 0x02; /* Low */
            if (!zero && carry)
               PSR |= 0x04; /* High */
            if (carry)
               PSR |= 0x20; /* Overflow */
            break;
         case 0xf:      /* SLC: Subtract Logical Characters */
            carry = 1;
            zero = 1;
            while (qbyte > -1) {
               IR = GetMem(BAR, 0) + (0xFF - GetMem(AAR, 0)) + carry;
               if (IR & 0x100)
                  carry = 1;
                  else
                  carry = 0;
               if ((IR & 0xFF) != 0) zero = 0; /* HJS mod */
               PutMem(BAR, 0,(IR & 0xFF));
               BAR--;
               AAR--;
               qbyte--;
            }
            PSR &= 0xF8;
            if (zero)
               PSR |= 0x01; /* Equal */
            if (!zero && !carry)
               PSR |= 0x02; /* Low */
            if (!zero && carry)
               PSR |= 0x04; /* High */
               break;
          default:
             reason = STOP_INVOP;
             break;
      }
      IAR[level] = PC;
      continue;
      break;
   case 0x30:
   case 0x70:
   case 0xb0:
      switch (opcode) {
         case 0:        /* SNS: Sense I/O */
            devno = (qbyte >> 4) & 0x0f;
            devm = (qbyte >> 3) & 0x01;
            devn = qbyte & 0x07;
            i = dev_table[devno].routine(3, devm, devn, rbyte);
            PutMem(BAR, 0, i & 0xff);
            BAR--;
            PutMem(BAR, 0, (i >> 8) & 0xff);
            reason = (i >> 16) & 0xffff;
            break;
         case 1:         /* LIO: Load I/O */
            devno = (qbyte >> 4) & 0x0f;
            devm = (qbyte >> 3) & 0x01;
            devn = qbyte & 0x07;
            op1 = GetMem(BAR, 0);
            BAR--;
            op1 |= (GetMem(BAR, 0) << 8) & 0xff00;
            reason = dev_table[devno].routine(1, devm, devn, op1);
            break;
         case 4:        /* ST: Store Register */
            if (qbyte & 0x80) {
               /* qbyte = 1xxxxxxx */
               switch(qbyte) {
                  case 0x80:
                     PutMem(BAR, 0, IAR[0] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[0] >> 8) & 0xff);
                     break;
                  case 0x81:
                     PutMem(BAR, 0, IAR[7] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[7] >> 8) & 0xff);
                     break;
                  case 0x82:
                     PutMem(BAR, 0, IAR[6] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[6] >> 8) & 0xff);
                     break;
                  case 0x84:
                     PutMem(BAR, 0, IAR[5] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[5] >> 8) & 0xff);
                     break;
                  case 0x88:
                     PutMem(BAR, 0, IAR[4] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[4] >> 8) & 0xff);
                     break;
                  case 0x90:
                     PutMem(BAR, 0, IAR[3] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[3] >> 8) & 0xff);
                     break;
                  case 0xA0:
                     PutMem(BAR, 0, IAR[2] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[2] >> 8) & 0xff);
                     break;
                  case 0xC0:
                     PutMem(BAR, 0, IAR[1] & 0xff);
                     BAR--;
                     PutMem(BAR, 0, (IAR[1] >> 8) & 0xff);
                     break;
                  default:
                     reason = STOP_INVQ;
                     break;
               }  /* switch (opcode) */
            } else {
               /* qbyte = 0xxxxxxx */
               t = 0x0000;
               /* Store the lower byte */
               if (qbyte & 0x01)     /* XR1 ? */
                  t = t | XR1;
               if (qbyte & 0x02)     /* XR2 ? */
                  t = t | XR2;
               if (qbyte & 0x04)     /* PSR ? */
                  t = t | (PSR & 0xFF);
               if (qbyte & 0x08)     /* ARR ? */
                  t = t | ARR[level];
               if (qbyte & 0x10)     /* IAR ? */
                  t = t | IAR[level];
               if (qbyte & 0x20)     /* IAR-P1 ? */
                  t = t | IAR[8];
               if (qbyte & 0x40)     /* IAR-P2 ? */
                  t = t | IAR[9];
               PutMem(BAR, 0, t & 0xff);
               BAR--;
               PutMem(BAR, 0, (t >> 8) & 0xff);
            }
            break;
         case 5:        /* L: Load Register */
            if (qbyte & 0x80) {
               /* qbyte = 1xxxxxxx */
               switch(qbyte) {
                  case 0x80:
                     IAR[0] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[0] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0x81:
                     IAR[7] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[7] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0x82:
                     IAR[6] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[6] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0x84:
                     IAR[5] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[5] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0x88:
                     IAR[4] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[4] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0x90:
                     IAR[3] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[3] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0xA0:
                     IAR[2] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[2] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  case 0xC0:
                     IAR[1] = GetMem(BAR, 0) & 0xFF;
                     BAR--;
                     IAR[1] |= (GetMem(BAR, 0) << 8) & 0xff00;
                     break;
                  default:
                     reason = STOP_INVQ;
                     break;
               }  /* switch (opcode) */
            } else {
               /* qbyte = 0xxxxxxx */
               /* Load the lower byte */
               if (qbyte & 0x01)     /* XR1 ? */
                  XR1 = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x02)     /* XR2 ? */
                  XR2 = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x04) {   /* PSR ? */
                  PSR = GetMem(BAR, 0) & 0xFF;
                  if ((PSR & 0x07) == 0)
                     PSR = PSR | 0x04; /* Set high bit on */
               }
               if (qbyte & 0x08)     /* ARR ? */
                  ARR[level] = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x10)     /* IAR ? */
                  IAR[level] = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x20)     /* IAR-P1 ? */
                  IAR[8] = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x40)     /* IAR-P2 ? */
                  IAR[9] = GetMem(BAR, 0) & 0xFF;
               BAR--;
               /* Load the higher byte */
               if (qbyte & 0x01)     /* XR1 ? */
                  XR1 |= (GetMem(BAR, 0) << 8) & 0xff00;
               if (qbyte & 0x02)     /* XR2 ? */
                  XR2 |= (GetMem(BAR, 0) << 8) & 0xff00;
//             if (qbyte & 0x04)     /* LCRR ? */
//                LCRR = GetMem(BAR, 0) & 0xFF;
               if (qbyte & 0x08)     /* ARR ? */
                  ARR[level] |= (GetMem(BAR, 0) << 8) & 0xff00;
               if (qbyte & 0x10) {   /* IAR ? */
                  IAR[level] |= (GetMem(BAR, 0) << 8) & 0xff00;
                  PC = IAR[level];
               }
               if (qbyte & 0x20)     /* IAR-P1 ? */
                  IAR[8] |= (GetMem(BAR, 0) << 8) & 0xff00;
               if (qbyte & 0x40)     /* IAR-P2 ? */
                  IAR[9] |= (GetMem(BAR, 0) << 8) & 0xff00;
            }
            break;
         case 6:        /* A: Add to Register */
            IR = GetMem(BAR, 0) & 0x00ff;
            BAR--;
            IR |= (GetMem(BAR, 0) << 8) & 0xff00;
            switch (qbyte) {
               case 0x01:
                  IR += XR1;
                  XR1 = IR & AMASK;
                  break;
               case 0x02:
                  IR += XR2;
                  XR2 = IR & AMASK;
                  break;
               case 0x04:
                  IR += PSR;
                  PSR = IR & AMASK;
                  break;
               case 0x08:
                  IR += ARR[level];
                  ARR[level] = IR & AMASK;
                  break;
               case 0x10:
                  IR += IAR[level];
                  IAR[level] = IR & AMASK;
                  break;
               case 0x20:
                  IR += IAR[8];
                  IAR[8] = IR & AMASK;
                  break;
               case 0x40:
                  IR += IAR[9];
                  IAR[9] = IR & AMASK;
                  break;
               case 0x80:
                  IR += IAR[0];
                  IAR[0] = IR & AMASK;
                  break;
               case 0x81:
                  IR += IAR[7];
                  IAR[7] = IR & AMASK;
                  break;
               case 0x82:
                  IR += IAR[6];
                  IAR[6] = IR & AMASK;
                  break;
               case 0x84:
                  IR += IAR[5];
                  IAR[5] = IR & AMASK;
                  break;
               case 0x88:
                  IR += IAR[4];
                  IAR[4] = IR & AMASK;
                  break;
               case 0x90:
                  IR += IAR[3];
                  IAR[3] = IR & AMASK;
                  break;
               case 0xA0:
                  IR += IAR[2];
                  IAR[2] = IR & AMASK;
                  break;
               case 0xC0:
                  IR += IAR[1];
                  IAR[1] = IR & AMASK;
                  break;
               default:
                  reason = STOP_INVQ;
                  break;
            }
            PSR &= 0xD8;
            if ((IR & 0xffff) == 0)
               PSR |= 0x01;   /* Zero */
            if ((IR & 0xffff) != 0 && !(IR & 0x10000))
               PSR |= 0x02;   /* Low */
            if ((IR & 0xffff) != 0 && (IR & 0x10000))
               PSR |= 0x04;   /* High */
            if ((IR & 0x10000))
               PSR |= 0x20;   /* Bin overflow */
            break;
         case 8:        /* TBN: Test Bits On */
            IR = GetMem(BAR, 0);
            PSR &= 0xFF;
            if ((IR & qbyte) != qbyte)
               PSR |= 0x10;
            break;
         case 9:        /* TBF: Test Bits Off */
            IR = GetMem(BAR, 0);
            PSR &= 0xFF;
            if ((IR & qbyte))
               PSR |= 0x10;
            break;
         case 0xa:      /* SBN: Set Bits On */
            IR = GetMem(BAR, 0);
            IR |= qbyte;
            PutMem(BAR, 0, IR);
            break;
         case 0xb:      /* SBF: Set Bits Off */
            IR = GetMem(BAR, 0);
            IR &= ~qbyte;
            PutMem(BAR, 0, IR);
            break;
         case 0xc:      /* MVI: Move Immediate */
            PutMem(BAR, 0, qbyte);
            break;
         case 0xd:      /* CLI: Compare Immediate */
            PSR = compare(GetMem(BAR, 0), qbyte, PSR);
            break;
         default:
            reason = STOP_INVOP;
            break;
      }
      IAR[level] = PC;
      continue;
      break;
   case 0xc0:
   case 0xd0:
   case 0xe0:
      switch (opcode) {
         case 0:        /* BC: Branch on Condition */
            ARR[level] = AAR & AMASK;
            if (condition(qbyte) == 1) {
               IR = ARR[level];
               ARR[level] = PC & AMASK;
               PC = IR;
            }
            break;
         case 1:        /* TIO: Test I/O */
            devno = (qbyte >> 4) & 0x0F;
            devm = (qbyte >> 3) & 0x01;
            devn = qbyte & 0x07;
            op1 = dev_table[devno].routine(2, devm, devn, rbyte);
            if (op1 & 0x01) {
               ARR[level] = AAR & AMASK;
               IR = ARR[level];
               ARR[level] = PC & AMASK;
               PC = IR;
            }
            reason = (op1 >> 16) & 0xffff;
            break;
         case 2:        /* LA: Load Address */
            switch (qbyte) {
               case 0x01:
                  XR1 = AAR;
                  break;
               case 0x02:
                  XR2 = AAR;
                  break;
               case 0x03:  /* Required for running DCP */
                  XR1 = AAR;
                  XR2 = AAR;
                  break;
               default:
                  reason = STOP_INVQ;
                  break;
            }
            break;
         default:
            reason = STOP_INVOP;
            break;
      } /* switch (opcode) */
      IAR[level] = PC;
      continue;

   default:
      reason = STOP_INVOP;
      break;
} /* switch (opaddr) */

} /* end while (reason == 0) */

/* Simulation halted */

saved_PC = PC;
return reason;
}

/* On models 4-12, these memory functions could be inline, but
   on a model 15 with ATU address mapping must be performed so
   they are in functions here for future expansion.
*/

/* Fetch a byte from memory */

int32 GetMem(int32 addr, int8 cycle)
{
   /* the cycle parameter is only used by the model 15 */
   addr = addr & 0xffff;                   /* Make sure it's 16 bits */
   return M[addr] & 0xFF;                  /* Read from memory */
}

/* Place a byte in memory */

int32 PutMem(int32 addr, int8 cycle, int32 data)
{
   /* the cycle parameter is only used by the model 15 */
   addr = addr & 0xffff;                   /* Make sure it's 16 bits */
   M[addr] = data & 0xFF;                  /* Write to memory */
   return(0);
}

/* Check the condition register against the qbyte and return 1 if true */

int32 condition(int32 qbyte)
{
   int32 r = 0, t, q;
   t = (qbyte & 0xf0) >> 4;
   q = qbyte & 0x0f;
   if (qbyte & 0x80) {  /* True if any condition tested = 1*/
      if (((qbyte & 0x3f) & PSR) != 0) r = 1;
   } else {             /* True if all conditions tested = 0 */
      if (((qbyte & 0x3f) & PSR) == 0) r = 1;
   }
   /* these bits are reset by a test */
   if (qbyte & 0x10)
      PSR &= 0xEF;      /* Reset test false if used */
   if (qbyte & 0x08)
      PSR &= 0xF7;      /* Reset decimal overflow if tested */
   if (qbyte == 0x00)
      r = 1;            /* unconditional branch */
   if (qbyte == 0x80)
      r = 0;            /* force no branch */
   if (t >= 0 && t < 8 && (q == 7 || q == 0x0f))
      r = 0;            /* no-op */
   if (t > 7 && t < 0x10 && (q == 7 || q == 0x0f))
      r = 1;            /* force branch */
return (r);
}

/* Given operand 1 and operand 2, compares the two and returns
   the System/3 condition register bits appropriately, given the
   condition register initial state in parameter 3
*/

int32 compare(int32 byte1, int32 byte2, int32 cond)
{
    int32 r;

    r = cond & 0xF8;      /* mask off all but unaffected bits 2,3,4 */
    if (byte1 == byte2)
       r |= 0x01;         /* set equal bit */
    if (byte1 < byte2)
       r |= 0x02;         /* set less-than bit */
    if (byte1 > byte2)
       r |= 0x04;         /* set greater than bit */
    return r;
}

/*-------------------------------------------------------------------*/
/* Add two zoned decimal operands                                    */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected.  Depending on the PSW      */
/*      program mask, decimal overflow may cause a program check.    */
/*-------------------------------------------------------------------*/
int32 add_zoned (int32 addr1, int32 len1, int32 addr2, int32 len2)
{
int     cc;                             /* Condition code            */
uint8   dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
uint8   dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
uint8   dec3[MAX_DECIMAL_DIGITS];       /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, dec1, &count1, &sign1);
    load_decimal (addr2, len2, dec2, &count2, &sign2);

    /* Add or subtract operand values */
    if (count2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (dec3, dec1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (count1 == 0)
    {
        /* If first operand is zero then result is second operand */
        memcpy (dec3, dec2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = sign2;
    }
    else if (sign1 == sign2)
    {
        /* If signs are equal then add operands */
        add_decimal (dec1, dec2, dec3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are opposite then subtract operands */
        subtract_decimal (dec1, dec2, dec3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > len1)
        cc = 3;

    /* Set positive sign if result is zero */
    if (count3 == 0)
        sign3 = 1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, dec3, sign3);

    /* Return condition code */
    return cc;

} /* end function add_packed */

/*-------------------------------------------------------------------*/
/* Subtract two zoned decimal operands                               */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected.  Depending on the PSW      */
/*      program mask, decimal overflow may cause a program check.    */
/*-------------------------------------------------------------------*/
int32 subtract_zoned (int32 addr1, int32 len1, int32 addr2, int32 len2)
{
int     cc;                             /* Condition code            */
uint8   dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
uint8   dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
uint8   dec3[MAX_DECIMAL_DIGITS];       /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, dec1, &count1, &sign1);
    load_decimal (addr2, len2, dec2, &count2, &sign2);

    /* Add or subtract operand values */
    if (count2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (dec3, dec1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (count1 == 0)
    {
        /* If first operand is zero then result is -second operand */
        memcpy (dec3, dec2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = -sign2;
    }
    else if (sign1 != sign2)
    {
        /* If signs are opposite then add operands */
        add_decimal (dec1, dec2, dec3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are equal then subtract operands */
        subtract_decimal (dec1, dec2, dec3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > len1)
        cc = 3;

    /* Set positive sign if result is zero */
    if (count3 == 0)
        sign3 = 1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, dec3, sign3);

    /* Return condition code */
    return cc;

} /* end function subtract_packed */


/*-------------------------------------------------------------------*/
/* Add two decimal byte strings as unsigned decimal numbers          */
/*                                                                   */
/* Input:                                                            */
/*      dec1    A 31-byte area containing the decimal digits of      */
/*              the first operand.  Each byte contains one decimal   */
/*              digit in the low-order 4 bits of the byte.           */
/*      dec2    A 31-byte area containing the decimal digits of      */
/*              the second operand.  Each byte contains one decimal  */
/*              digit in the low-order 4 bits of the byte.           */
/* Output:                                                           */
/*      result  Points to a 31-byte area to contain the result       */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero, */
/*              or to MAX_DECIMAL_DIGITS+1 if overflow occurred.     */
/*-------------------------------------------------------------------*/
static void add_decimal (uint8 *dec1, uint8 *dec2, uint8 *result, int32 *count)
{
int     d;                              /* Decimal digit             */
int     i;                              /* Array subscript           */
int     n = 0;                          /* Significant digit counter */
int     carry = 0;                      /* Carry indicator           */

    /* Add digits from right to left */
    for (i = MAX_DECIMAL_DIGITS - 1; i >= 0; i--)
    {
        /* Add digits from first and second operands */
        d = dec1[i] + dec2[i] + carry;

        /* Check for carry into next digit */
        if (d > 9) {
            d -= 10;
            carry = 1;
        } else {
            carry = 0;
        }

        /* Check for significant digit */
        if (d != 0)
            n = MAX_DECIMAL_DIGITS - i;

        /* Store digit in result */
        result[i] = d;

    } /* end for */

    /* Check for carry out of leftmost digit */
    if (carry)
        n = MAX_DECIMAL_DIGITS + 1;

    /* Return significant digit counter */
    *count = n;

} /* end function add_decimal */

/*-------------------------------------------------------------------*/
/* Subtract two decimal byte strings as unsigned decimal numbers     */
/*                                                                   */
/* Input:                                                            */
/*      dec1    A 31-byte area containing the decimal digits of      */
/*              the first operand.  Each byte contains one decimal   */
/*              digit in the low-order 4 bits of the byte.           */
/*      dec2    A 31-byte area containing the decimal digits of      */
/*              the second operand.  Each byte contains one decimal  */
/*              digit in the low-order 4 bits of the byte.           */
/* Output:                                                           */
/*      result  Points to a 31-byte area to contain the result       */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero. */
/*      sign    -1 if the result is negative (operand2 > operand1)   */
/*              +1 if the result is positive (operand2 <= operand1)  */
/*-------------------------------------------------------------------*/
static void subtract_decimal (uint8 *dec1, uint8 *dec2, uint8 *result, int *count, int *sign)
{
int     d;                              /* Decimal digit             */
int     i;                              /* Array subscript           */
int     n = 0;                          /* Significant digit counter */
int     borrow = 0;                     /* Borrow indicator          */
int     rc;                             /* Return code               */
uint8   *higher;                         /* -> Higher value operand   */
uint8   *lower;                          /* -> Lower value operand    */

    /* Compare digits to find which operand has higher numeric value */
    rc = memcmp (dec1, dec2, MAX_DECIMAL_DIGITS);

    /* Return positive zero result if both operands are equal */
    if (rc == 0) {
        memset (result, 0, MAX_DECIMAL_DIGITS);
        *count = 0;
        *sign = +1;
        return;
    }

    /* Point to higher and lower value operands and set sign */
    if (rc > 0) {
        higher = dec1;
        lower = dec2;
       *sign = +1;
    } else {
        lower = dec1;
        higher = dec2;
       *sign = -1;
    }

    /* Subtract digits from right to left */
    for (i = MAX_DECIMAL_DIGITS - 1; i >= 0; i--)
    {
        /* Subtract lower operand digit from higher operand digit */
        d = higher[i] - lower[i] - borrow;

        /* Check for borrow from next digit */
        if (d < 0) {
            d += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }

        /* Check for significant digit */
        if (d != 0)
            n = MAX_DECIMAL_DIGITS - i;

        /* Store digit in result */
        result[i] = d;

    } /* end for */

    /* Return significant digit counter */
    *count = n;

} /* end function subtract_decimal */

/*-------------------------------------------------------------------*/
/* Load a zoned decimal storage operand into a decimal byte string   */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of zoned decimal storage operand     */
/*      len     Length minus one of storage operand (range 0-15)     */
/* Output:                                                           */
/*      result  Points to a 31-byte area into which the decimal      */
/*              digits are loaded.  One decimal digit is loaded      */
/*              into the low-order 4 bits of each byte, and the      */
/*              result is padded to the left with high-order zeroes  */
/*              if the storage operand contains less than 31 digits. */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero. */
/*      sign    Points to an integer which will be set to -1 if a    */
/*              negative sign was loaded from the operand, or +1 if  */
/*              a positive sign was loaded from the operand.         */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if the operand causes a data exception         */
/*      because of invalid decimal digits or sign.                   */
/*-------------------------------------------------------------------*/
static void load_decimal (int32 addr, int32 len, uint8 *result, int32 *count, int32 *sign)
{
int     h;                              /* Hexadecimal digit         */
int     i, j;                           /* Array subscripts          */
int     n = 0;                          /* Significant digit counter */

   if ((GetMem(addr, 0) & 0xF0) == 0xD0) /* Minus ? */
      *sign = -1;
   else
      *sign = 1;
   j = len;
   for (i = MAX_DECIMAL_DIGITS; i > -1; i--) {
      if (j) {
         h = GetMem(addr, 0) & 0x0F;
         addr--;
         j--;
      } else {
         h = 0;
      }
      result [i-1] = h;
      if (h > 0) n = i;
   }
   *count = 32 - n;

} /* end function load_decimal */

/*-------------------------------------------------------------------*/
/* Store decimal byte string into packed decimal storage operand     */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of packed decimal storage operand    */
/*      len     Length minus one of storage operand (range 0-15)     */
/*      dec     A 31-byte area containing the decimal digits to be   */
/*              stored.  Each byte contains one decimal digit in     */
/*              the low-order 4 bits of the byte.                    */
/*      sign    -1 if a negative sign is to be stored, or +1 if a    */
/*              positive sign is to be stored.                       */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection exception.  */
/*-------------------------------------------------------------------*/
static void store_decimal (int32 addr, int32 len, uint8 *dec, int sign)
{
int     i, j, a, t;              /* Array subscripts          */

   j = len;
   a = addr;
   for (i = MAX_DECIMAL_DIGITS; i > -1; i--) {
      if (j) {
         PutMem(a, 0, (dec[i-1] | 0xF0));
         a--;
         j--;
      } else {
         break;
      }
   }
   if (sign == -1) {
      t = GetMem(addr, 0) & 0x0F;
      PutMem(addr, 0, t);
      t = GetMem(addr, 0) | 0xD0;
      PutMem(addr, 0, t);
   }

} /* end function store_decimal */

/* CPU Device Control */

int32 cpu (int32 op, int32 m, int32 n, int32 data)
{
   int32 iodata = 0;

   switch (op) {
      case 0x00:   /* SIO */
         return SCPE_OK;
      case 0x01:   /* LIO */
         return SCPE_OK;
      case 0x02:   /* TIO */
         break;
      case 0x03:   /* SNS */
         /* SNS CPU gets the data switches */
         iodata = SR;
         if (debug_lvl & 0x80)
            fprintf(trace, "=0=> SENSE = %04X\n",
                    iodata & 0x0000ffff);
         break;
      case 0x04:   /* APL */
         break;
      default:
         break;
   }
   return ((SCPE_OK << 16) | iodata);
}



/* Null device */

int32 nulldev (int32 opcode, int32 m, int32 n, int32 data)
{
   if (opcode == 1)
      return SCPE_OK;      /* Ok to LIO unconfigured devices? */
   return STOP_INVDEV;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
   sprintf (sim_name, "System/3 Model %s", sys_models[sys_model].name);
   dev_int_req = 0;
   level = 8;
   IAR[8] = 0x0000;
   IAR[9] = 0x0000;
   return cpu_svc (&cpu_unit);
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
   if (addr >= MEMSIZE) return SCPE_NXM;
   if (vptr != NULL) *vptr = GetMem(addr, 0) & 0xFF;
   return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
   if (addr >= MEMSIZE) return SCPE_NXM;
   PutMem(addr, 0, val);
   return SCPE_OK;
}

/* Breakpoint service */

t_stat cpu_svc (UNIT *uptr)
{
   if ((ibkpt_addr & ~ILL_ADR_FLAG) == save_ibkpt)
      ibkpt_addr = save_ibkpt;
   save_ibkpt = -1;
   return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 value, CONST char *cp, void *dp)
{
   int32 mc = 0;
   t_addr i;

   if ((value <= 0) || (value > MAXMEMSIZE) || ((value & 07777) != 0))
      return SCPE_ARG;
   for (i = value; i < MEMSIZE; i++) mc = mc | M[i];
   if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
      return SCPE_OK;
   MEMSIZE = value;
   for (i = MEMSIZE; i < MAXMEMSIZE; i++) PutMem(i, 0, 0x00);
   return SCPE_OK;
}

t_stat cpu_set_model (UNIT *uptr, int32 value, CONST char *cptr, void *dp)
{
   char gbuf[CBUFSIZE];
   int model, device;

   if ((cptr == NULL) || (!*cptr))
      return SCPE_ARG;
   cptr = get_glyph (cptr, gbuf, 0);
   for (model = 0; sys_models[model].name; model++)
       if (0 == strcasecmp  (sys_models[model].name, gbuf))
           break;
   if (sys_models[model].name == NULL)
      return SCPE_ARG;
   if (*cptr)
      return SCPE_ARG;
   if (sys_model == model)
       return SCPE_OK;
   sys_model = model;
   while (sim_clock_queue != QUEUE_LIST_END)
       sim_cancel(sim_clock_queue);
   for (device = 1; sim_devices[device] != NULL; device++)
      if (sim_devices[device]->flags & sys_models[sys_model].flag)
         sim_devices[device]->flags &= ~DEV_DIS;
      else
         sim_devices[device]->flags |= DEV_DIS;
   return reset_all_p (0);
}

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   fprintf (st, "Model=%s", sys_models[sys_model].name);
   return SCPE_OK;
}

