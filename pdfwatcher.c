/* 
 *  Copyright (c) 2013, Timothe Litt
 *                       litt @acm.org
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:

 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.

 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 *  Except as contained in this notice, the name of the author shall not be
 *  used in advertising or otherwise to promote the sale, use or other dealings
 *  in this Software without prior written authorization from the author.
 *
 */

/* This is a demonstration of how to automatically watch for and print
 * files that appear in a spool directory.  It is a windows-only utility,
 * but similar mechanisms are available under other OSs.  It is provided
 * for your information, but is not supported by the SimH project.
 *
 * A full-featured version would be setup to run as a Windows service.
 * It would also scan the directory on startup & deal with any left-over
 * files.
 *
 * See the usage, below for details.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>

#define MAX_BUFFER (64*1024)
#define MAX_FN (1000)

void WatchDirectory(TCHAR *dir, TCHAR **ext, TCHAR *cmdFmt);

void _tmain(int argc, TCHAR *argv[]) {
    TCHAR *cmdfmt = NULL;
    int arg;
    size_t len = 0;
    TCHAR *ext[] = { ".pdf", ".PDF" };

    if (argc > 1) {
        if (!strcmp (argv[1], "-p")) {
            ext[0] = ".pdf";
            ext[1] = ".PDF";
            argc--;
            argv++;
        } else if (!strcmp (argv[1], "-l")) {
            ext[0] = ".lpt";
            ext[1] = ".LPT";
            argc--;
            argv++;
        }
    }
    if(argc < 3 || !strcmp (argv[1], _T("--help")) || !strcmp (argv[1], _T("-h"))) {
        _tprintf(TEXT("\nUsage: %s <dir> \"<print cmd>\"\n       use %%s for filename\n\n"
            "This is a demonstration of how to automatically watch for and print\n"
            "files that appear in a spool directory.  It is a windows-only utility,\n"
            "but similar mechanisms are available under other OSs.\n"
            "\n"
            "Usage:\n"
            " %s -switch directory command\n"
            "\n"
            "-switch may be -p (default) - watch for .pdf files, or -l - watch for .lpt files\n"
            "\n"
            "When a file with the selected extension appears and has been closed, the watcher executes\n"
            "'command', and if the command exits with a success status, deletes the file.\n"
            "\n"
            "command should contain %%s where the filename is to be substituted.\n"
            "%%s can appear more than once if required.\n"
            "\n"
            "Example: (This is one long line)\n"
            " %s spool \"C:\\Program Files (x86)\\Adobe\\Reader 11.0\\Reader\\AcroRd32.exe\"\n"
            "               /n /h /s /t %%s \"My Printer\" \"HPWJ65N3.GPD\" \"HP_printer_port\"\n"
            "\n"
            "This will watch the directory \".\\spool\" and invoke Adobe Acrobat Reader to print\n"
            "each file that appears on the named printer. Acrobat requires the driver and port names.\n"
            "You can obtain the driver and port names from the printer's property page in the control panel.\n"
            "\n"
            "If you want to have the ability to adjust the print options, AcroRd32 can be started\n"
            "with /n /s /p %%s\n"
            "\n"
            "Other PDF readers, including Acrobat, ghostscript and Foxit, can be substituted for AcroRd32.\n"
            "Consult their documentation for the required command syntax.\n"
            "\n"
            "The watcher will only exit if it encounters a fatal error.\n"
            "To stop it, type ^C to its console window.\n"
            ), argv[0], argv[0], argv[0]);
        return;
    }

    /* Construct a command line from the argument (s) passed.
     *  Quote any argument that contains spaces or double quotes.
     */

    for (arg = 2; arg < argc; arg++) {
        size_t alen = strlen (argv[arg]) + 2;
        size_t slen = (len != 0);
        TCHAR *p = argv[arg];
        int quote = strchr (p, ' ') != NULL || strchr (p, '"') != NULL;

        /* Allocate space for "" and "" at beginning and end */

        if (quote) {
            alen += 4;
        }

        /* Allocate space for each interior " that will be doubled */

        for ( ; *p; *p++ ) {
            if (*p == '"') {
                alen++;
            }
        }

        /* Get memory for the command string */

        cmdfmt = (TCHAR *) realloc (cmdfmt, (len + slen + alen +1) * sizeof (TCHAR));
        if (!cmdfmt) {
            printf ("No memory\n");
            return;
        }

        /* Build the string.  argumements after the first are separated by spaces */

        if (slen) {
            cmdfmt[len++] = ' ';
        }
        if (quote) {
            cmdfmt[len++] = '"';
            cmdfmt[len++] = '"';
        }
        for (p = argv[arg]; *p; p++) {
            cmdfmt[len++] = *p;
            if (*p == '"') {
                cmdfmt[len++] = '"';
            }
        }
        if (quote) {
            cmdfmt[len++] = '"';
            cmdfmt[len++] = '"';
        }
        cmdfmt[len] = '\0';
    }

    /* Invoke the directory watcher with the directory and command */

    WatchDirectory(argv[1], ext, cmdfmt);
    free (cmdfmt);
}


void WatchDirectory(TCHAR *dir, TCHAR **ext, TCHAR *cmdFmt) {
    HANDLE hDir;
    DWORD err;
    CHAR buffer[MAX_BUFFER];
    DWORD dwBuffLen, ofs;
    PFILE_NOTIFY_INFORMATION ni;
    TCHAR fullDirPath[MAX_PATH + 1];
    size_t n = 0;
    TCHAR *p;

    /* Scan the command string for occurences of %x.
     * %% will produce a %.
     * Each %s will insert the .pdf file name.
     * %anything else is an error.
     */

    for (p = cmdFmt; *p; p++) {
        if (*p == '%') {
            if (p[1] == '%') {
                p++;
                continue;
            }
            if (p[1] != 's') {
                printf ("Invalid command argument\n");
                return;
            }
            n++;
            p++;
        }
    }

    /* Resolve the directory path as some utilities can't deal with relative paths */

    if (!GetFullPathName (dir, sizeof (fullDirPath) / sizeof (TCHAR), fullDirPath, NULL)) {
        printf ("Unable to resolve directory: %s\n", dir);
        return;
    }

    /* Open the directory for monitoring */

    hDir = CreateFile( fullDirPath,
                  FILE_LIST_DIRECTORY,
                  FILE_SHARE_READ |
                  FILE_SHARE_WRITE |
                  FILE_SHARE_DELETE,
                  NULL,
                  OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS,
                  NULL);
    if (hDir == INVALID_HANDLE_VALUE) {
        wprintf(L"Unable to open directory %s\n", fullDirPath);
        err = GetLastError();
        exit (err);
    }

    /* Watch for directory changes:
     * File creation, deletion, renames.
     * Last write
     */

    while (TRUE) {
        if (!ReadDirectoryChangesW(hDir, buffer, MAX_BUFFER, FALSE, 
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &dwBuffLen, NULL, NULL)
            || !dwBuffLen) {
            continue;                               /* Buffer overflow */
        }
        
        ni = (PFILE_NOTIFY_INFORMATION)buffer;

        for (ofs = 1; ofs; ni = (PFILE_NOTIFY_INFORMATION)((LPBYTE) ni + ofs)) {
            WCHAR FileName[MAX_PATH+1+MAX_FN];
            HANDLE hFile;
            TCHAR fullPath[MAX_PATH + 1];
            TCHAR *cmd;

            /* ofs is the byte offset of the next entry in the buffer */

            ofs = ni->NextEntryOffset;

            /* See what happened */

            switch (ni->Action) {
            case FILE_ACTION_ADDED:
                break;
            case FILE_ACTION_REMOVED:
                continue;
            case FILE_ACTION_MODIFIED:
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                continue;
            case FILE_ACTION_RENAMED_NEW_NAME:
                continue;  
            default:
                continue;
            }

            /* Make sure the filename will fit; ignore if not */

            if (ni->FileNameLength > MAX_FN * sizeof (WCHAR)) {
                continue;
            }

            /* Get the filename and convert from WCHAR to current encoding */

            memcpy (FileName, ni->FileName, ni->FileNameLength);
            FileName[ni->FileNameLength / sizeof (WCHAR)] = '\0';
            wsprintf (fullPath, _T("%s\\%lS"), fullDirPath, FileName);

            /* Ignore any file that is not a .PDF or a .LPT */

            if (strlen (fullPath) < 4 || (strcmp (fullPath+(strlen(fullPath)-4), ext[0]) &&
                                          strcmp (fullPath+(strlen(fullPath)-4), ext[1]))) {
                continue;
            }

            /* Attempt to open the file non-shared.
             * If this fails, the file is still being written.
             * Another notification will occur when it is closed.
             */

            hFile = CreateFile( fullPath,
                    GENERIC_READ,
                  0,                        /* Exclusive */
                  NULL,
                  OPEN_EXISTING,
                  0,
                  NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                continue;
            }
            CloseHandle(hFile);

            /* Allocate enough space for the command, including the filename
             * substutitions.
             */

#ifdef DEBUG
            wprintf (L"File: %S\n", fullPath);
#endif
            cmd = (TCHAR *) malloc (((strlen (fullPath) * n) + strlen (cmdFmt) + 1) * sizeof (TCHAR));
            if (!cmd) {
                printf ("no memory\n");
                return;
            }

            /* Generate and execute the command.
             * If successful, delete the file.
             */
            sprintf (cmd, cmdFmt, fullPath);
            if (system ( cmd ) == 0) {
                DeleteFile (fullPath);
            }
            free (cmd);
        }
    }
}
