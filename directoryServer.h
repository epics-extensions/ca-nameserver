/*! \file directoryServer.h
 * \brief class definitions
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include "epicsAssert.h"
#define caNetAddrSock 	// Allows pvExistReturn to return pv address
#include "cadef.h"
#include "casdef.h"
#include "resourceLib.h"
#include "tsMinMax.h"
#include "osiTime.h"
#include "bsdSocketResource.h"

// *** SITE SPECIFIC MODIFICATIONS TO BE EDITED  ***
// Handles cases where ca_host_name(chid) returns iocname.jlab.acc.org:5064
// and iocname:5064
#define HN_DELIM '.'
#define HN_DELIM2 ':'
// The name of the files containing lists of pvs on each ioc.
#define SIG_LIST "signal.list"
// The suffix of a pv which exists on every ioc in the form 'iocname<suffix>'
#define HEARTBEAT ":heartbeat"
// Next two can be defined to suit your site
#define DEFAULT_HASH_SIZE 300000
#define MAX_IOCS 120

#define HOST_NAME_SZ 80
#define PV_NAME_SZ 80
#define PATH_NAME_SZ 120

extern FILE *never_ptr;

class directoryServer;

/*! \brief hashtable for pvs which never connect
*/
class never :public stringId, public tsSLNode<never> {
public:
	never( directoryServer &casIn, const char *pNameIn) :
		stringId(pNameIn),cas(casIn), ct(1)
	{
	assert(this->stringId::resourceName()!=NULL);
    strncpy(this->name, pNameIn, PV_NAME_SZ-1);
	}
	virtual ~never(); 					// not inline to keep aCC happy
	void incCt()	{ct++;}
	void zeroCt()	{ct = 0;}
	inline void destroy ();
	void myshow() {fprintf(never_ptr,"%s %ld\n",name,ct); }
private:
	directoryServer	&cas;
	char name[PV_NAME_SZ];
	unsigned long ct;
};


/*! \brief  Node for linked list of pv's with pending connections
*/
class namenode : public tsSLNode<namenode>
{
public:
	namenode( const char * nameIn, chid chidIn, int rebroadcastIn) 
	{
		strncpy(this->name, nameIn, PV_NAME_SZ-1);
		this->chd = chidIn;
		this->rebroadcast = rebroadcastIn;
	}
	const char *get_name() 	{ return name; }
	void set_otime() 		{ this->otime = osiTime::getCurrent(); }
	osiTime get_otime() 	{ return otime;}
	chid get_chid() 		{ return chd;}
	int get_rebroadcast() 	{ return rebroadcast; }
	inline ~namenode() 		{}
	void set_chid(chid chdIn) { chd = chdIn; }
private:
	char 	name[PV_NAME_SZ];
	osiTime otime;
	chid 	chd;
	int		rebroadcast;
};

#ifdef JS_FILEWAIT
/*! \brief  Node for linked list of pv's reconnecting
*/
class filewait : public tsSLNode<filewait>
{
public:
	filewait( const char * hname, int sizeIn) 
	{
		strncpy(this->hostname, hname, HOST_NAME_SZ-1);
		this->size = sizeIn;
	}
	int get_size() {return size;}
	const char *get_hostname() 	{ return hostname; }
	void set_size(int sizeIn) {this->size = sizeIn;}
private:
	char hostname[HOST_NAME_SZ];
	int size;
};
#endif

/*! \brief  pv hash table
*/
class pvE: public stringId, public tsSLNode<pvE> {
public:
	pvE( directoryServer &casIn, const char *pNameIn, const char *pHostNameIn) : 
		stringId(pNameIn), cas(casIn)
	{
		assert(this->stringId::resourceName()!=NULL);
		strncpy(this->hostname, pHostNameIn, HOST_NAME_SZ-1);
		strncpy(this->name, pNameIn, PV_NAME_SZ-1);
	}
	virtual ~pvE(); 					// not inline to keep aCC happy
	const char *getHostname () 			const { return this->hostname; }
	inline void destroy ();

private:
	directoryServer 		&cas;
	char 					hostname[HOST_NAME_SZ];
	char 					name[PV_NAME_SZ];
};

/*! \brief  host hash table
*/
class pHost : public stringId, public tsSLNode<pHost> {
public:
	pHost (directoryServer &casIn, const char *pHostNameIn, 
		const char *pPath, sockaddr_in &addrIn) : 
		stringId(pHostNameIn), status(0), cas(casIn), addr(addrIn)
	{
		assert(this->stringId::resourceName()!=NULL);
		strncpy(this->hostname, pHostNameIn, HOST_NAME_SZ-1);
		strncpy(this->pathToList, pPath,PATH_NAME_SZ-1);
	}
	virtual ~pHost(); 				// not inline to keep aCC happy
	int get_status() 				{ return this->status; }
	void set_status(int statIn) 	{ this->status = statIn; }
	char *get_pathToList() 			{ return this->pathToList;}
	inline void destroy ();
    const struct sockaddr_in getAddr()  const { return this->addr; }
    void setPort(int valIn) {this->addr.sin_port = valIn;}

private:
	int 				status;
	directoryServer 	&cas;
    struct sockaddr_in  addr;
	char 				hostname[HOST_NAME_SZ];
	char 				pathToList[PATH_NAME_SZ];
};

/*! \brief directory server class
*/
class directoryServer : public caServer {
public:
	directoryServer (unsigned pvCount);
	~directoryServer();
	void show (unsigned level) const;

	pvExistReturn pvExistTest (const casCtx&, const char *pPVName);
	int installPVName (const char *pvname, const char *pHostName);
	int installHostName ( const char *pHostName, const char *pPath, 
	struct sockaddr_in &ipaIn);
	int installNeverName (const char *pvname);
	void addNN(namenode *pNN) {this->nameList.add(*pNN); }
#ifdef JS_FILEWAIT
	void addFW(filewait *pF) {this->fileList.add(*pF); }
	tsSLList<filewait> fileList;			//!< list of signal.list files pending
#endif
	tsSLList<namenode> nameList;			//!< list of pv's with pending connections
	resTable<pHost,stringId> hostResTbl;	//!< hash of installed ioc's 
	resTable<pvE,stringId> stringResTbl;	//!< hash of installed pv's
	resTable<never,stringId> neverResTbl;	//!< hash of never connected pv's
private:

	static  void sigusr1(int); 

};

/*! \brief pHost remove and delete
 *
 * Called by destroyAllEntries.
 * hosts are always created with new.
 *
*/
inline void pHost::destroy ()
{
	pHost *pH = cas.hostResTbl.remove(*this);
	if(pH)
		delete this;
}

/*! \brief pvEntry remove and delete
 *
 * Called by destroyAllEntries.
 * pvs are always created with new.
*/
inline void pvE::destroy ()
{
	pvE *pve = cas.stringResTbl.remove(*this);
	if(pve)
		delete this;
}

/*! \brief never remove and delete
 *
 * Called by destroyAllEntries.
 * nevers are always created with new.
*/
inline void never::destroy ()
{
	never *nve = cas.neverResTbl.remove(*this);
	if(nve)
		delete this;
}
