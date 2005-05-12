/*! \file reserve_fd.cc
 * \brief nameserver reserve file descriptor functions
 *
 * \author Janet Anderson
 * \Revision History:
 * Initial release May 2005
*/

#include "reserve_fd.h"

#define NS_RESERVE_FILE  "nameserver.reserve"

FILE *reserveFp = NULL;

// Functions to try to reserve a file descriptor to use for fopen.  On
// Solaris, at least, fopen is limited to FDs < 256.  These could all
// be used by CA and CAS sockets if there are connections to enough
// IOCs  These functions try to reserve a FD < 256.
FILE *reserve_fd_fopen(const char *filename, const char *mode)
{
    // Close the dummy file holding the FD open
    if(reserveFp) ::fclose(reserveFp);
    reserveFp=NULL;

    // Open the file.  It should use the lowest available FD, that is,
    // the one we just made available.
    FILE *fp=::fopen(filename,mode);
    if(!fp) {
        // Try to get the reserved one back
        reserveFp=::fopen(NS_RESERVE_FILE,"w");
    }

    return fp;
}

int reserve_fd_fclose(FILE *stream)
{
    // Close the file
    int ret=::fclose(stream);

    // Open the dummy file to reserve the FD just made available
    reserveFp=::fopen(NS_RESERVE_FILE,"w");

    return ret;
}

static FILE* reserve_fd_openReserveFile(void)
{
    reserveFp=::fopen(NS_RESERVE_FILE,"w");
    return reserveFp;
}

