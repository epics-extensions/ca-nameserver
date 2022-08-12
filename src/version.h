/*************************************************************************\
* Copyright (c) 2022 UChicago Argonne LLC, as Operator of Argonne
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
***********************************************************************/

#ifndef INC_version_H
#define INC_version_H

#define NS_VERSION       2
#define NS_REVISION      1
#define NS_MODIFICATION  1
#define NS_SNAPSHOT      "-DEV"

#define stringOf(TOKEN) #TOKEN
#define xstringOf(TOKEN) stringOf(TOKEN)
#define NS_VERSION_STRING "Nameserver Version " \
    xstringOf(NS_VERSION) "." \
    xstringOf(NS_REVISION) "." \
    xstringOf(NS_MODIFICATION) \
    NS_SNAPSHOT

#define NS_CREDITS_STRING  \
    "Developed at Thomas Jefferson National Accelerator Facility and\n" \
    "Argonne National Laboratory, by Joan Sage and Janet Anderson\n\n"

#endif /* INC_version_H */
