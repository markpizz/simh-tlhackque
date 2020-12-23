Copyright (c) 2013, Henk Stegeman 

This readme file explains how to use the IBM 3741 in a SIMH
environment.

Quick course about IBM 3740 diskettes:
The IBM diskette type 1 geometrics are:
- single sided.
- 74 tracks, numbered 00-73.
- 26 secters per track, numbered 001-026.
- 128 bytes per sector.
Volume record (VOL1) is located at 01007 (ccsss).
Dataset records (HDR1) are located at 01008 - 01026

Layout dataset record:
 1- 4  "HDR1" 
 6-22  dataset name.
23-27  record length (080, 096 or 128).
29-33  beginning of extent (ccsss).
35-39  end of extent (ccsss).
75-79  end of data (ccsss).


*** The IBM 3741 in the real world ***

0) Power up the IBM 3741.             

1) Insert diskette in the diskette drive and close it.

2) Step through the HDR1 records until the correct dataset 
   is found and open the file.

3) Scroll through the file to the desired record OR
   leave it at the top of file.

4) Put the 3741 in read or write mode.

5) Start the program on the System/3 that uses the diskette 
   (or IPL from diskette). 

Note: the sequence of the steps in this procedure is important. 


*** The IBM 3741 in the emulator world ***

0) Start the emulator and IPL the system. 
   Example is for the model 10.

   root@IBM5410:/home/snhstq/5410-ssh# ./go

   System/3 simulator V2.7
   Invalid argument
   LPT: creating new file
   sim> b f1

    // DATE 123456

1) Press cntl/e to get the sim prompt and attach the diskette file.
   sim> at dkt <file_name.dkt>

2) Select the correct HDR1 record.
   sim> d dkt hdr1 2

3) Skip this step if you want to start reading/writing at the 
   top of file.
   If not: go the de desired record (in this example 03012)
   sim> d dkt cs 03012 

4) Put the 3741 in read or write mode.
   sim> d dkt iomode 0     (for read and 1 for write)

   You can check all setting with:
   sim> e dkt state
      ...
   IOMODE: 0
   HDR1:   02
   CS:     03012

5) Start the program that uses the diskette (or IPL from diskette). 


*** Example system IPL from diskette ***

root@IBM5410:/home/snhstq/5410-ssh# ./go

System/3 simulator V2.7
Invalid argument
LPT: creating new file
sim> at dkt IPL_TXT.dkt
sim> d dkt hdr1 1
sim> d dkt cs 01001
sim> d dkt iomode 0
sim> b dkt

IPL from CS = 01001

Invalid Opcode, IAR: 0006 (00)
sim>


*** Example writing a file to diskette ***

root@IBM5410:/home/snhstq/5410-ssh# ./go

System/3 simulator V2.7
Invalid argument
LPT: creating new file
sim> at dkt IPL_TXT.dkt
sim> d dkt hdr1 1
sim> d dkt cs 01012
sim> d dkt iomode 1
sim> b f1

 // DATE 123456
 // LOAD DUMP54,R1
 // RUN
 GIVE START CYL/SECT FOR R2 DISK.
  _   _
 !   !_
 !_   _!

HALT, IAR: 1239 (SNS 0,0,0,1561)
sim>

Attach (next) diskette image file...

Unit not attached, IAR: 093B (SIO 4,0,3,08)
sim> at dkt IPL_TXT.dkt
sim>

Attach (next) diskette image file...

Unit not attached, IAR: 093B (SIO 4,0,3,08)
sim>


Note: during attach of a diskette the files are checked for valid:
1) "VOL1" record. 
2) "HDR1" record.  
3) hdr parameter not bigger then 18. 
4) CS is within the dataset boundaries (boe and eoe).



