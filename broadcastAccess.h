/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 Berliner Speicherring-Gesellschaft fuer Synchrotron-
* Strahlung mbH (BESSY).
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution. 
\*************************************************************************/
#ifndef _GATEAS_H_
#define _GATEAS_H_

/*+*********************************************************************
 *
 * File:       gateAs.h
 * Project:    CA Proxy Gateway
 *
 * Descr.:     Access Security part - handles all Gateway configuration:
 *             - Reads PV list file
 *             - Reads Access Security file
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 * $Revision$
 * $Date$
 *
 * $Author$
 *
 * $Log$
 * Revision 1.1  2003/07/03 18:41:35  jba
 * Added access security files.
 *
 * Revision 1.19  2002/08/16 15:39:36  evans
 * Fixed it to not try to get the client host name, an expensive
 * calculation, in pvExistTest if the deny_from_table is empty as it is
 * for many sites.
 *
 * Revision 1.18  2002/07/29 16:06:01  jba
 * Added license information.
 *
 * Revision 1.17  2002/07/18 15:07:45  lange
 * Optimisation (connect time): expression matching only once in pvExistTest()
 *
 * Revision 1.16  2002/02/08 12:01:03  lange
 * Bugfix: NULL asg group when adding DENY entry.
 * (Reported and suggested by Stephanie Allison)
 *
 * Revision 1.15  2001/08/16 15:44:13  lange
 * Fixed timestamped connection loss messages (reported by Joan Sage)
 *
 * Revision 1.14  2001/03/15 19:11:13  lange
 * Fixed a few minor Solaris warnings
 *
 * Revision 1.13  2000/06/15 12:51:04  lange
 * Patch for using regex.h with the HP aCC Compiler.
 *
 * Revision 1.12  2000/05/03 17:08:30  lange
 * Minor Bugfix, enhanced report functions.
 *
 * Revision 1.11  2000/05/02 13:49:39  lange
 * Uses GNU regex library (0.12) for pattern matching;
 * Fixed some CAS beacon problems (reconnecting IOCs)
 *
 *********************************************************************-*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


extern "C" {
#include "asLib.h"
#include "gpHash.h"
                    // Patch for regex.h not testing __cplusplus, only __STDC__
#ifndef __STDC__
#    define __STDC__ 1
#    include "regex.h"
#    undef __STDC__
#else
#    if ! __STDC__
#        undef __STDC__
#        define __STDC__ 1
#    endif
#    include "regex.h"
#endif
}

#if 0
#include "tsSLList.h"
#endif
#include "resourceLib.h"
#include "tsHash.h"
#include "aitTypes.h"

/*
 * Standard FALSE and TRUE macros
 */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define GATE_DENY_FIRST 0
#define GATE_ALLOW_FIRST 1

class gateAsEntry;
class gateAsHost;

typedef tsSLList<gateAsEntry> gateAsList;
typedef tsSLList<gateAsHost> gateHostList;


//  ----------------- AS host (to build up host list) ------------------ 

class gateAsHost : public tsSLNode<gateAsHost>
{
public:
	const char* host;

	gateAsHost(void) : host(NULL) { }
	gateAsHost(const char* name) : host(name) { }
};

//  ------------ AS entry (deny or deny from or alias or allow) ------------ 

class gateAsEntry : public tsSLNode<gateAsEntry>
{
public:
	gateAsEntry(void) :
		name(NULL), alias(NULL), group(NULL), level(1), as(NULL) { }

								               // ALLOW / ALIAS
	gateAsEntry(const char* pvname,			   //   PV name pattern (regex)
				const char* rname,             //   Real name substitution pattern
				const char* g, int l) :        //   ASG / ASL
		name(pvname), alias(rname), group(g), level(l), as(NULL) { }

												// DENY / DENY FROM
	gateAsEntry(const char* pvname) :			//   PV name pattern (regex)
		name(pvname), alias(NULL), group(NULL), level(1), as(NULL) { }

	aitBool init(gateAsList& n,                 // Where this entry is added to
				 int line)						// Line number
	{
		if (compilePattern(line)==aitFalse) return aitFalse;
		n.add(*this);
		if (group == NULL || asAddMember(&as,(char*)group) != 0) as = NULL;
		else asPutMemberPvt(as,this);
		return aitTrue;
	}

	aitBool init(const char* host,				// Host name to deny
				 tsHash<gateAsList>& h,         // Where this entry is added to
				 gateHostList& hl,				// Where a new key should be added
				 int line)						// Line number
	{
		gateAsList* l;

		if(compilePattern(line)==aitFalse) return aitFalse;
		if(h.find(host,l)==0)
			l->add(*this);
		else
		{
			l = new gateAsList;
			l->add(*this);
			h.add(host,*l);
			hl.add(*(new gateAsHost(host)));
		}
		return aitTrue;
	}

	void getRealName(const char* pv, char* real, int len);

	const char* name;
	const char* alias;
	const char* group;
	int level;
	ASMEMBERPVT as;
	char pat_valid;
	struct re_pattern_buffer pat_buff;
	struct re_registers regs;

private:
	aitBool compilePattern(int line)
	{
		const char *err;
		pat_buff.translate=0; pat_buff.fastmap=0;
		pat_buff.allocated=0; pat_buff.buffer=0;

		if((err = re_compile_pattern(name, strlen(name), &pat_buff)))
		{
			fprintf(stderr,"Line %d: Error in regexp %s : %s\n", line, name, err);
			return aitFalse;
		}
		return aitTrue;
	}
};

//  -------------- AS node (information for CAS Channels) -------------- 

class gateAsNode
{
public:
	gateAsNode(void)
		{ asc=NULL; entry=NULL; }
	gateAsNode(gateAsEntry* e,const char* user, const char* host):entry(e)
	{
		asc=NULL;
		user_func=NULL;
		if(e&&asAddClient(&asc,e->as,e->level,(char*)user,(char*)host)==0)
			asPutClientPvt(asc,this);
		asRegisterClientCallback(asc,client_callback);
	}

	~gateAsNode(void)
		{ if(asc) asRemoveClient(&asc); asc=NULL; }

	aitBool readAccess(void)  const
		{ return (asc==NULL||asCheckGet(asc))?aitTrue:aitFalse; }
	aitBool writeAccess(void) const
		{ return (asc&&asCheckPut(asc))?aitTrue:aitFalse; }

	gateAsEntry* getEntry(void)
		{ return entry; }
	long changeInfo(const char* user, const char* host)
		{ return asChangeClient(asc,entry->level,(char*)user,(char*)host);}

	const char* user(void) { return (const char*)asc->user; }
	const char* host(void) { return (const char*)asc->host; }

	void setUserFunction(void (*ufunc)(void*),void* uarg)
		{ user_arg=uarg; user_func=ufunc; }

private:
    static void client_callback(ASCLIENTPVT,asClientStatus);
	ASCLIENTPVT asc;
	gateAsEntry* entry;
	void* user_arg;
	void (*user_func)(void*);
};

class gateAsLine : public tsSLNode<gateAsLine>
{
public:
	gateAsLine(void) : buf(NULL) { }
	gateAsLine(const char* line, int len, tsSLList<gateAsLine>& n) :
		buf(new char[len+1])
	{
		strncpy(buf,line,len+1);
		buf[len] = '\0';
		n.add(*this);
	}
	~gateAsLine(void)
		{ delete [] buf; }

	char* buf;
};

class gateAs
{
public:
	gateAs(const char* pvlist_file, const char* as_file_name);
	gateAs(const char* pvlist_file);
	~gateAs(void);

	// user must delete the gateAsNode that the following function returns
	gateAsNode* getInfo(const char* pv,const char* usr,const char* hst);
	gateAsNode* getInfo(gateAsEntry* e,const char* usr,const char* hst);

	inline gateAsEntry* findEntry(const char* pv, const char* host = 0);

	bool isDenyFromListUsed(void) const { return denyFromListUsed; }

	int readPvList(const char* pvlist_file);
	void report(FILE*);
	static long reInitialize(const char* as_file_name);

	static const char* default_group;
	static const char* default_pattern;

private:
	gateAsList deny_list;
	gateAsList allow_list;
	gateHostList host_list;
	tsSLList<gateAsLine> line_list;
	tsHash<gateAsList> deny_from_table;

	bool denyFromListUsed;

	static unsigned char eval_order;

	// only one set of access security rules allowed in a program
	static aitBool rules_installed;
	static aitBool use_default_rules;
	static FILE* rules_fd;
	static long initialize(const char* as_file_name);
	static int readFunc(char* buf, int max_size);

	void deleteAsList(gateAsList& list)
	{
		tsSLIterRm<gateAsEntry>* pi = new tsSLIterRm<gateAsEntry>(list);
		while(pi->next()) pi->remove();
		delete pi;
	}
	void deleteAsList(tsSLList<gateAsLine>& list)
	{
		tsSLIterRm<gateAsLine>* pi = new tsSLIterRm<gateAsLine>(list);
		while(pi->next()) pi->remove();
		delete pi;
	}
	gateAsEntry* findEntryInList(const char* pv, gateAsList& list) const
	{
		tsSLIter<gateAsEntry>* pi = new tsSLIter<gateAsEntry>(list);
		gateAsEntry* pe;

		while((pe=pi->next()) &&
			  (re_match(&pe->pat_buff,pv,strlen(pv),0,&pe->regs) != (int)strlen(pv)));
		delete pi;
		return pe;
	}
};

inline gateAsEntry* gateAs::findEntry(const char* pv, const char* host)
{
	gateAsList* pl=NULL;

	if(host && deny_from_table.find(host,pl)==0 &&	// DENY FROM
	   findEntryInList(pv, *pl)) return NULL;

	if(eval_order == GATE_ALLOW_FIRST &&			// DENY takes precedence
	   findEntryInList(pv, deny_list)) return NULL;

	return findEntryInList(pv, allow_list);
}


#define GATE_MAX_PVLIST_LINE_LENGTH 1024u

#ifndef DEBUGLEVEL
#define gateDebug(l,f,v) ;
#define gateDebug0(l,f) ;
#define gateDebug1(l,f,v) ;
#define gateDebug2(l,f,v1,v2) ;
#define gateDebug3(l,f,v1,v2,v3) ;
#define gateDebug4(l,f,v1,v2,v3,v4) ;
#else

#define debugLevel DEBUGLEVEL;

#define gateDebug(l,f,v) { if(l<=debugLevel) \
   { fprintf(stderr,f,v); fflush(stderr); }}
#define gateDebug0(l,f) { if(l<=debugLevel) \
   { fprintf(stderr,f); fflush(stderr); } }
#define gateDebug1(l,f,v) { if(l<=debugLevel) \
   { fprintf(stderr,f,v); fflush(stderr); }}
#define gateDebug2(l,f,v1,v2) { if(l<=debugLevel) \
   { fprintf(stderr,f,v1,v2); fflush(stderr); }}
#define gateDebug3(l,f,v1,v2,v3) { if(l<=debugLevel) \
   { fprintf(stderr,f,v1,v2,v3); fflush(stderr); }}
#define gateDebug4(l,f,v1,v2,v3,v4) { if(l<=debugLevel) \
   { fprintf(stderr,f,v1,v2,v3,v4); fflush(stderr); }}
#endif



#endif /* _GATEAS_H_ */

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
