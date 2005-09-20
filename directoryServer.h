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
#include "epicsTime.h"
#include "gateAs.h"

#include "pvServer.h"
#include "nsIO.h"

// *** SITE SPECIFIC MODIFICATIONS TO BE EDITED  ***
// If ca_host_name(chid) returns iocname.jlab.acc.org:5064, set the
// delimiter to '.'. If the return is iocname:5064, set it to ':'.
#define HN_DELIM '.'
#define HN_DELIM2 ':'

// The name of the files containing lists of pvs on each ioc.
#define SIG_LIST "signal.list"
// The suffix of a pv which exists on every ioc in the form 'iocname<suffix>'
#define HEARTBEAT ":heartbeat"
// Next two can be defined to suit your site
#define DEFAULT_HASH_SIZE 300000
#define MAX_IOCS 300

#define HOST_NAME_SZ 80
#define PV_NAME_SZ 80
#define PATH_NAME_SZ 120

extern FILE *never_ptr;

class directoryServer;
class pIoc;
class pvE;

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
	const char *get_name() const	{ return name; }
	void set_otime() 		{ this->otime = epicsTime::getCurrent(); }
	epicsTime get_otime() 	{ return otime;}
	chid get_chid() 		{ return chd;}
	int get_rebroadcast() 	{ return rebroadcast; }
	inline ~namenode() 		{ }
	void set_chid(chid chdIn) { chd = chdIn; }
private:
	char 	name[PV_NAME_SZ];
	epicsTime otime;
	chid 	chd;
	int		rebroadcast;
};

#ifdef JS_FILEWAIT
/*! \brief  Node for linked list of pv's reconnecting
*/
class filewait : public tsSLNode<filewait>
{
public:
	filewait( pIoc * pIocIn, int sizeIn) : pI(pIocIn)
	{
		this->size = sizeIn;
		this->read_tries = 0;
		this->connectTime = time(NULL);
	}
	int read_tries;
	int get_size() {return size;}
	int get_connectTime() {return connectTime;}
	pIoc *get_pIoc() 	{ return pI; }
	void set_size(int sizeIn) {this->size = sizeIn;}
private:
	pIoc *pI;
	time_t connectTime;
	int size;
};
#endif


/*! \brief  Node for an ioc's linked list of pv's
     Using the pvE was an easy way to avoid copying the pvname
     strings to an ioc's pvname list (650k pvnames) or creating
     a seperate pvname class.
     NOTE: Every pvE exists in some pvEIoc in some ioc's pvElist.
     NOTE: A pvE may not be in the stringResTbl if the
           pvname was moved from a DOWN ioc to another ioc.
*/
class pvEIoc: public tsSLNode<pvEIoc> {
public:
	pvEIoc( pvE *pvEIn) : 
		pve(pvEIn)
	{
	}
	virtual ~pvEIoc(); 		// not inline to keep aCC happy
	pvE *get_pvE () 		{ return this->pve;}

private:
	pvE 		*pve;
};

/*! \brief  pv hash table
*/
class pvE: public stringId, public tsSLNode<pvE> {
public:
	pvE( directoryServer &casIn, const char *pNameIn, pIoc* pHIn) : 
		stringId(pNameIn),	cas(casIn), pI(pHIn)
	{
		assert(this->stringId::resourceName()!=NULL);
		strncpy(this->name, pNameIn, PV_NAME_SZ-1);
	}
	virtual ~pvE(); 				// not inline to keep aCC happy
	inline void destroy ();
	pIoc *get_pIoc () 		{ return this->pI; }
	const char *get_name () 		 	const { return this->name; }
	void myshow ();

private:
	directoryServer 		&cas;
	pIoc 					*pI;
	char 					name[PV_NAME_SZ];
};

/*! \brief  ioc hash table
*/
class pIoc : public stringId, public tsSLNode<pIoc> {
public:
	pIoc (directoryServer &casIn, const char *pIocNameIn, 
		const char *pPath) : 
		stringId(pIocNameIn), status(0), cas(casIn)
	{
		assert(this->stringId::resourceName()!=NULL);
		strncpy(this->iocname, pIocNameIn, HOST_NAME_SZ-1);
		if (pPath) strncpy(this->pathToList, pPath,PATH_NAME_SZ-1);
		else this->pathToList[0]= '\0';

		memset((char *)&this->addr,0,sizeof(this->addr));
	}
	virtual ~pIoc(); 				// not inline to keep aCC happy
	const int get_status() const 				{ return this->status; }
	void set_status(int statIn) 	{ this->status = statIn; }
	char *get_pathToList() 	{ return this->pathToList;}
	const char *get_iocname() const			{ return this->iocname;}
	inline void destroy ();
	void show ( unsigned level );
	void myshow ();
    const struct sockaddr_in getAddr()  const { return this->addr; }
    void set_port(int valIn) {this->addr.sin_port = valIn;}
	void set_addr( chid chid);
	void add( pvE *pve);            // add a pvE to the pvEList
	pvE *get_pvE(); // remove pvEIoc from pvEList, delete pvEIoc, returns pvE

	void show ( unsigned level ) const;
	tsSLList<pvEIoc> 	pvEList;		//! list of pv's on this ioc

private:
	int 				status;
	directoryServer 	&cas;
    struct sockaddr_in  addr;
	char 				iocname[HOST_NAME_SZ];
	char 				pathToList[PATH_NAME_SZ];
};

/*! \brief directory server class
*/
class directoryServer : private pvServer {
public:
	directoryServer (unsigned pvCount,const char* const pvPrefix);
	~directoryServer();
	void show (unsigned level) const;
	void setDebugLevel ( unsigned level );
	void generateBeaconAnomaly ();

	int installPVName (const char *pvname, pIoc *pIocName);
	pIoc* installIocName ( const char *pIocName, const char *pPath);
	int installNeverName (const char *pvname);
	void addNN(namenode *pNN) {this->nameList.add(*pNN); }
#ifdef JS_FILEWAIT
	void addFW(filewait *pF) {this->fileList.add(*pF); }
	tsSLList<filewait> fileList;			//!< list of signal.list files pending
#endif
	tsSLList<namenode> nameList;			//!< list of pv's with pending connections
	resTable<pIoc,stringId> iocResTbl;	//!< hash of installed ioc's 
	resTable<pvE,stringId> stringResTbl;	//!< hash of installed pv's
	resTable<never,stringId> neverResTbl;	//!< hash of never connected pv's
    gateAs *pgateAs;
private:
	pvExistReturn pvExistTest (const casCtx&, const char *pPVName );
	pvExistReturn pvExistTest (const casCtx&, const caNetAddr&, const char *pPVName );
	pvAttachReturn pvAttach (const casCtx&, const char *pPVName );
};

/*! \brief pIoc remove and delete
 *
 * Called by destroyAllEntries.
 * iocs are always created with new.
 *
*/
inline void pIoc::destroy ()
{
	pIoc *pI = cas.iocResTbl.remove(*this);
	if(pI)
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
