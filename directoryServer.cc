/*! \file directoryServer.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

static char *rcsid="$Header$";

#if 0
#include <strings.h>
#endif

#include "directoryServer.h"
#ifdef linux
#include <time.h>
#endif

#if 0
#ifdef BROADCAST_ACCESS
#include <cadef.h>
#if EPICS_REVISION > 13
#include <osiSock.h>
#endif
#endif
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

static directoryServer *self = 0;

extern int outta_here;
extern int start_new_log;
extern int connected_iocs;
extern int requested_iocs;
extern int verbose;
extern FILE *never_ptr;

static struct {
	double requests;
	double broadcast;
#ifdef BROADCAST_ACCESS
	double broadcast_denied;
#endif
#ifdef GWBROADCAST
	double broadcast_denied;
#endif
	double pending;
	double host_error;
	double hit;
	double host_down;
}stat;

extern "C" void processChangeConnectionEvent( struct connection_handler_args args);

pHost::~pHost()
{
}

pvEHost::~pvEHost()
{
}

pvE::~pvE()
{
}

never::~never()
{
}

/*! \brief Set the address values of the ca server
 *
 * Set sin_port, sin_family, and IP number
 *
 * \param hostname:port string
 *
*/
void pHost::setAddr( chid chid)
{
		char pvNameStr[PV_NAME_SZ];
		char *cport;
		int portNumber;
		int mystatus;

        this->addr.sin_family = AF_INET;

		strncpy (pvNameStr, ca_host_name(chid),PV_NAME_SZ-1);
        cport = strchr(pvNameStr,':');
        if(cport){
            portNumber = atoi(cport + 1);
            this->addr.sin_port = htons((aitUint16) portNumber);
            //printf("portNumber: %d\n", portNumber);
        } else {
            this->addr.sin_port = 0u;
            //printf("portNumber: 0u\n");
        }
		if (cport) *cport = 0;
        mystatus = aToIPAddr (pvNameStr, this->addr.sin_port, &this->addr);
        if (mystatus) {
            fprintf (stderr, "Unknown host name: %s \n", pvNameStr);
		}
}


/*! \brief Constructor for the directory server
 *
 * Set up signal handling. Initialize two hash tables. Zero statistics
 *
 * \param pvCount - size of the pv hashtable
 *
*/
directoryServer::directoryServer( unsigned pvCount) : 
	caServer()
{
    assert(self==0);
	self = this;
#ifndef _WIN32
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr1);
	signal(SIGTERM, sigusr1);
	signal(SIGINT, sigusr1);
#endif
#ifdef BROADCAST_ACCESS
	bcA = 0;
#endif
	this->stringResTbl.setTableSize(pvCount);
	this->hostResTbl.setTableSize(MAX_IOCS);
	this->neverResTbl.setTableSize(pvCount);

	stat.requests = 0;
	stat.broadcast = 0;
#ifdef BROADCAST_ACCESS
	stat.broadcast_denied = 0;
#endif
#ifdef GWBROADCAST
	stat.broadcast_denied = 0;
#endif
	stat.pending = 0;
	stat.host_error = 0;
	stat.host_down = 0;
	stat.hit = 0;
}

#ifndef _WIN32
/*! \brief Signal handler
 *
 * SIGUSR1 writes summary info to the logfile.
 * SIGUSR2 starts a new logfile.
 * At JLab, SIGUSR2 deletes all knowledge of pioc pvs.
 * SIGTERM and SIGINT set the "outta_here" flag.
*/
void directoryServer::sigusr1(int sig)
{
	epicsTime	first;
	local_tm_nano_sec   ansiDate;

	if( sig == SIGUSR2) {
		fprintf(stdout,"SIGUSR2\n");
		start_new_log = 1;
		signal(SIGUSR2, sigusr1);
	}

	else if( sig == SIGTERM || sig == SIGINT) {
		first = epicsTime::getCurrent ();
		ansiDate = first;
		fprintf(stdout,"SIGTERM time: %s\n", asctime(&ansiDate.ansi_tm));
		fflush(stdout);
		fflush(stderr);
		outta_here = 1;
	}
	else {	
		first = epicsTime::getCurrent ();
		ansiDate = first;
		fprintf(stdout,"\n*********Sigusr1 time: %s\n", asctime(&ansiDate.ansi_tm));
		// level=2 gets summary info
		// level=10 gets ALL names ...be careful what you ask for...
		self->show(2);
		fflush(stdout);
		signal(SIGUSR1, sigusr1);
	}
}
#endif

/*! \brief Destructor for the server
 *
 * Clean up the hashtables and the linked list.
 * Say goodbye to the logfile
 */
directoryServer::~directoryServer()
{
    tsSLList < pHost > tmpHostList;
    tsSLList < pvE > tmpStringList;
    tsSLList < never > tmpNeverList;

	// removeAll() puts entries on a tmpList.
	// and then traverses list deleting each entry
	this->hostResTbl.removeAll(tmpHostList);
    while ( pHost * pH = tmpHostList.get() ) {
    	while ( pvEHost * pveh = pH->pvEList.get() ) {
			delete pveh;
		}
        pH->~pHost ();
    }
	this->stringResTbl.removeAll(tmpStringList);
    while ( pvE * pS = tmpStringList.get() ) {
		delete pS;
    }
	this->neverResTbl.removeAll(tmpNeverList);
    while ( never * pN = tmpNeverList.get() ) {
		delete pN;
    }
    while ( namenode * pNN = this->nameList.get() ) {
		ca_clear_channel(pNN->get_chid());
		delete pNN;
	}

	epicsTime	first;
	local_tm_nano_sec   ansiDate;
	first = epicsTime::getCurrent ();
	ansiDate = first;
	fprintf(stdout,"EXIT time: %s\n", asctime(&ansiDate.ansi_tm));
	fflush(stdout);
	fflush(stderr);
}

/*! \brief Add a never to the never hashtable
 *
 * Create a never node. Add to never hash table. 0=success, -1=failure;
 *
*/
int directoryServer::installNeverName(const char *pName)
{
	never *pnev;

	pnev = new never( *this, pName);
	if (pnev) {
		int resLibStatus;
		resLibStatus = this->neverResTbl.add(*pnev);
		if (resLibStatus==0) {
			if(verbose)fprintf(stdout, "added %s to never table\n", pName);
			return(0);
		}
		else {
			delete pnev;
			fprintf(stderr,"Unable to install %s. \n", pName);
		}
	}
	else {
		fprintf(stderr,"can't create new never %s\n", pName);
	}
	return(-1);
}

/*! \brief Add a host to the host hashtable
 *
 * Create a host node. Add to host hash table. 0=success, -1=failure;
 *
 * \param pHostName - IOC name
 * \param pPath - full path to 'signal.list' file
 * \param ipaIn - network info structure
*/
pHost * directoryServer::installHostName(const char *pHostName, const char *pPath)
{
	pHost *pH;

	pH = new pHost( *this, pHostName, pPath);
	if (pH) {
		int resLibStatus;
		resLibStatus = this->hostResTbl.add(*pH);
		if (resLibStatus==0) {
			if(verbose)fprintf(stdout, "added %s to host table\n", pHostName);
			return(pH);
		}
		else {
			delete pH;
			fprintf(stderr,"Unable to install %s. \n", pHostName);
		}
	}
	else {
		fprintf(stderr,"can't create new host %s\n", pHostName);
	}
	return(0);
}

/*! \brief Add a pv to the pv hashtable
 *
 * Create a pv node. Add to pv hash table. 0=success, -1=failure;
 *
 * \param pName - pv name
 * \param pHostName - IOC serving this pv
*/
int directoryServer::installPVName( const char *pName, pHost *pH)
{
	pvE	*pve;
	char *hostName = 0;

	if (pH) hostName = pH->get_hostname();
	pve = new pvE( *this, pName, pH);
	if (pve) {
		int resLibStatus;
		resLibStatus = this->stringResTbl.add(*pve);
		if (resLibStatus==0) {
			if(verbose)
				fprintf(stdout, "Installed PV: %s  %s in hash table\n", pName, hostName);
			pvEHost *pveH = 0;
			if (pH) pveH = new pvEHost(pve);
			if (pveH) pH->pvEList.add(*pveH);
			else fprintf(stdout, "cant add %s node to %s host pv table\n", pName, hostName);
			return(0);
		}
		else {
			delete pve;
			char    checkStr[PV_NAME_SZ];
            sprintf(checkStr,"%s%s",hostName, HEARTBEAT);
            if(strcmp(checkStr,pName)) {
				fprintf(stdout, "Unable to enter PV %s on %s in hash table. Duplicate?\n",
					 pName, hostName);
				return(-1);
			}
			else {
				//printf("Special treatment for heartbeats\n");
				return(1);
			}
		}
	}
	return(-1); //should never get here.
}

pvExistReturn directoryServer::pvExistTest(const casCtx& ctx, const char *pvName)
{
    return pverDoesNotExistHere;
}

/*! \brief Handle client broadcasts
 *
 * The heart of the code is here. When the channel access library hears a
 * broadcast, it calls this function.
 *
 *   - Search the pv hashtable 
 *   - If pv is found
 *      - Search the host hashtable
 *      - If host is found
 *         - Check host status
 *         - If host is up
 *            - Return host address
 *  - In all other cases
 *     - Return pvNotFound
 *
 * \param casCtx - not used
 * \param pPVName - name of the pv we're looking for
*/
pvExistReturn directoryServer::pvExistTest (const casCtx& ctx, 
    const caNetAddr &, const char *pPVName)
{
	char 		shortPV[PV_NAME_SZ];
	pvE 		*pve;
	pHost 		*pH;
	namenode	*pNN;
	chid		chd;	
	int 		i, len, status;

	stat.requests++;

//fprintf(stdout,"pvExistTest for pv %s\n", pPVName); fflush(stdout);

	// strip the requested PV to just the record name, omit the field.
	strncpy(shortPV, pPVName,PV_NAME_SZ-1);

    len = strlen(shortPV);
    for(i=0; i<len; i++){
        if(shortPV[i] == '.'){
            shortPV[i] = 0x0;
            break;
        }
    }

    len = strlen(shortPV);
	if(len == 0){
		return pverDoesNotExistHere;
	}


	stringId id(shortPV, stringId::refString);
	pve = this->stringResTbl.lookup(id);

	if(pve) {
//fprintf(stdout,"Found pv in pv hash table. %s\n", shortPV); fflush(stdout);
		// Found pv in pv hash table. Check to see if host is up.
		if(verbose)
			fprintf(stdout,"found %s in PV TABLE\n", shortPV);
		pH = pve->get_pHost();
		if(!pH) {
			if(verbose)
				fprintf(stdout, "Host error\n");
			stat.host_error++;
			return pverDoesNotExistHere;
		}
		else {
			if(verbose)
				fprintf(stdout,"Host installed. status: %d\n",pH->get_status());
			if(pH->get_status() == 1) {
				stat.hit++;
/*
				sockaddr_in tin = pH->getAddr();
				printf("f: %d p: %d a: %d\n",
					tin.sin_family,
					tin.sin_port,
					tin.sin_addr);
			    printf("ADDR <%s>\n", inet_ntoa(tin.sin_addr));
*/

				return pvExistReturn (caNetAddr(pH->getAddr()));
	
			}
			else {
				stat.host_down++;
				return pverDoesNotExistHere;
			}
		}
	}
	else if (!pve) {
		if(verbose)
			fprintf(stdout, "%s NOT in PV TABLE\n", shortPV);
		// See if there is already a connection pending
	 	tsSLIter<namenode> iter = this->nameList.firstIter();
		while( iter.valid()) {
			pNN=iter.pointer();
			if(!strcmp(pNN->get_name(), shortPV)){
				stat.pending++;
				if(verbose)fprintf(stdout,"Connection pending\n");
//fprintf(stdout,"Connection pending for %s\n", shortPV); fflush(stdout);
				return (pverDoesNotExistHere);
			}
			iter++;
		}
		// Can we broadcast for the requested pv.
		if (!this->broadcastAllowed(ctx,caNetAddr,shortPV)) {
			stat.broadcast_denied++;
			return (pverDoesNotExistHere);
		}

//fprintf(stdout,"ca_search_and_connect for pv %s\n", shortPV); fflush(stdout);
		status = ca_search_and_connect(shortPV,&chd,processChangeConnectionEvent,0);
		if (status != ECA_NORMAL) {
			fprintf(stderr,"ca_search failed on channel name: [%s]\n",shortPV);
			return pverDoesNotExistHere;
		}
		else {
			// Create a node for the linked list of pending connections
			pNN = new namenode(shortPV, chd, 0);
			if(pNN == NULL) {
				fprintf(stderr,"Failed to create namenode %s\n", shortPV);
				return pverDoesNotExistHere;
			}
			pNN->set_otime();
			// Add this pv to the list of pending pv connections 
			this->addNN(pNN);
			if(verbose)
				fprintf(stdout, "broadcasting for %s\n", shortPV);
			stat.broadcast++;
			return (pverDoesNotExistHere);
		}
	}
	return (pverDoesNotExistHere);
}

/*! \brief Write status summary to the logfile
 *
 *
*/
void directoryServer::show (unsigned level) const
{
	epicsTime	first;
	local_tm_nano_sec   ansiDate;
	first = epicsTime::getCurrent ();
	ansiDate = first;
	fflush(stdout);

	static epicsTime	last_time = first;
	
	fprintf(stdout,"Diag time: %s\n", asctime(&ansiDate.ansi_tm));
	fprintf(stdout, "PV Hash Table:\n");
	this->stringResTbl.show(level);
	fprintf(stdout, "\n");

	never_ptr = fopen("./never.log", "w");
	fprintf(stdout, "Never Hash Table:\n");

	// Create a ptr to the fn we're gonna call.
	void (never::*fptr)() = &never::myshow;

	// create a ptr to T and cast it as non-const
	resTable<never,stringId> *junk = (resTable<never,stringId>* )&this->neverResTbl;
	junk->traverse(fptr); 
	fclose(never_ptr);
	fprintf(stdout, "\n");

	fprintf(stdout, "NEVER Hash Table:\n");
	this->neverResTbl.show(2);
	fprintf(stdout, "\n");

	fprintf(stdout, "Host Hash Table:\n");
	this->hostResTbl.show(5);
	fprintf(stdout, "\n");

	// print information about ca server library internals
	//this->caServer::show(level);

	fprintf(stdout, "Connected IOCS: %d\n", connected_iocs);
	fprintf(stdout, "Requested IOCS: %d\n", requested_iocs);
/*
	tsSLIter<namenode> iter = this->nameList.firstIter ();
	fprintf(stdout,"PV's pending connections:\n" );
	while (iter.valid()) {
		fprintf(stdout,"  %s\n", iter.pointer()->get_name());
		++iter;
	}
*/
	if(last_time != first) {
		double hours = (double)(first - last_time)/60.0/60.0;
		fprintf(stdout,"\nRequests: \t%10.0f (%9.2f/hour)\n", stat.requests, stat.requests/hours);
		fprintf(stdout,"\nHits: \t\t%10.0f (%9.2f/hour)\n", stat.hit, stat.hit/hours);
		fprintf(stdout,"Broadcasts: \t%10.0f (%9.2f/hour)\n", stat.broadcast, stat.broadcast/hours);
#ifdef BROADCAST_ACCESS
		fprintf(stdout,"Broadcasts_denied:  %10.0f (%9.2f/hour)\n", stat.broadcast_denied, stat.broadcast_denied/hours);
#endif
#ifdef GWBROADCAST
		fprintf(stdout,"Broadcasts_denied:  %10.0f (%9.2f/hour)\n", stat.broadcast_denied, stat.broadcast_denied/hours);
#endif
		fprintf(stdout,"Host_error: \t%10.0f (%9.2f/hour)\n", stat.host_error, stat.host_error/hours);
		fprintf(stdout,"Pending: \t%10.0f (%9.2f/hour)\n", stat.pending, stat.pending/hours);
		fprintf(stdout,"Host_down: \t%10.0f (%9.2f/hour)\n", stat.host_down, stat.host_down/hours);
		fprintf(stdout,"    in %f seconds (%3.2f hours) \n", first - last_time, hours);
	}
	else {
		fprintf(stdout,"\nRequests: %f\n", stat.requests);
		fprintf(stdout,"\nHits: %f\n", stat.hit);
		fprintf(stdout,"Broadcasts: %f\n", stat.broadcast);
#ifdef BROADCAST_ACCESS
		fprintf(stdout,"Broadcasts_denied: %f\n", stat.broadcast_denied);
#endif
#ifdef GWBROADCAST
		fprintf(stdout,"Broadcasts_denied: %f\n", stat.broadcast_denied);
#endif
		fprintf(stdout,"Host_error: %f\n", stat.host_error);
		fprintf(stdout,"Pending: %f\n", stat.pending);
		fprintf(stdout,"Host_down: %f\n", stat.host_down);
		fprintf(stdout,"    since startup\n");
	}
	fprintf(stdout, "\n**********End diagnostics\n\n");
	fflush(stdout);

	stat.broadcast = 0;
#ifdef BROADCAST_ACCESS
	stat.broadcast_denied = 0;
#endif
#ifdef GWBROADCAST
	stat.broadcast_denied = 0;
#endif
	stat.pending = 0;
	stat.host_error = 0;
	stat.hit = 0;
	stat.host_down = 0;
	stat.requests = 0;
	last_time = first;
}

int directoryServer::broadcastAllowed (const casCtx& ctx, const caNetAddr& addr)
{
#ifdef BROADCAST_ACCESS
	char *ptr;
    char hostName[HOST_NAME_SZ]="";

	if (this->bcA==0) return TRUE;

    casClient *pClient;
    pClient=(casClient *)ctx.getClient();

	// Make the hash name
    struct sockaddr_in inaddr=(struct sockaddr_in)addr;
    ipAddrToA(&inaddr,hostname,HOST_NAME_SZ);
	if ((ptr=strchr(hostname,HN_DELIM))) *ptr=0x0;
	if ((ptr=strchr(hostname,HN_DELIM2))) *ptr=0x0;

//fprintf(stdout,"Broadcast requested from host %s \n", hostname);
	// See if broadcast is allowed for this host
	if (this->bcA->broadcastAllowed(hostname))
	{
//fprintf(stdout,"Broadcast from %s IS allowed\n", hostname);
        if (verbose) fprintf(stdout,"Broadcast from %s IS allowed\n", hostname);
        //fflush(stdout);
		return TRUE;
	}

//fprintf(stdout,"Broadcast from %s NOT allowed for %s\n", hostname,pvname); fflush(stdout);
       if (verbose) fprintf(stdout,"Broadcast from %s NOT allowed\n", hostname);
       //fflush(stdout);
		return FALSE;
#endif
#ifdef GWBROADCAST
		// Broadcast only for the gateway pvs.
        if (strncmp(shortPV,"GW",2) && strncmp(shortPV,"Xorbit",6) && strncmp(shortPV,"evans",5)) {
            fprintf(stdout,"Broadcast NOT allowed for %s\n", pPVName); fflush(stdout);
            return FALSE;
		else {
            return TRUE;
		}
#endif
}
