/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 Southeastern Universities Research Association, as
* Operator of Thomas Jefferson National Accelerator Facility.
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution. 
\*************************************************************************/
/* version.h */

/************************DESCRIPTION***********************************
 *  nameserver Version and Credits Information
**********************************************************************/

#ifndef INCversionh
#define INCversionh 1

#define NS_VERSION       2
#define NS_REVISION      1
#define NS_MODIFICATION  0

#define stringOf(TOKEN) #TOKEN
#define NS_VERSION_STRING "Nameserver Version " \
    stringOf(NS_VERSION) "." stringOf(NS_REVISION) "." stringOf(NS_MODIFICATION)

#define NS_CREDITS_STRING  \
    "Developed at Thomas Jefferson National Accelerator Facility and\n" \
    "Argonne National Laboratory, by Joan Sage and Janet Anderson\n\n"

#endif /* INCversionh */
