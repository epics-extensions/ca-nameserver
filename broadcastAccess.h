#ifndef _BROADCASTACCESS_H_
#define _BROADCASTACCESS_H_

/***********************************************************************
 *
 * File:       broadcastAccess.h
 * Project:    CA Nameserver
 * Descr.:     Host broadcast access
 * Author:     J. Anderson (APS)
 *
 ***********************************************************************/

#include "resourceLib.h"

class hostId : public stringId, public tsSLNode<hostId> {
public:
    hostId (const char *pNameIn) : stringId (pNameIn) {}
    hostId (const char *pNameIn, allocationType typeIn) : stringId (pNameIn,typeIn) {}

    inline void destroy(void) { }
};

class broadcastAccess
{
public:
	broadcastAccess(const char* broadcast_file,unsigned hostCount);
	~broadcastAccess(void);

	inline bool broadcastAllowed(const char* hn);

private:
	int readHostList(const char* broadcast_file);
    resTable <hostId,stringId> hostTbl;
	bool allowFromList;
};

inline bool broadcastAccess::broadcastAllowed(const char* hn)
{
	hostId id(hn,stringId::refString);

	if(this->hostTbl.lookup(id)) return allowFromList;
	return !allowFromList;
}

#endif /* _BROADCASTACCESS_H_ */

