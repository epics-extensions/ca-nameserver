/*! \file directoryServer.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

static char *rcsid="$Header$";

#include "directoryServer.h"
#ifdef linux
#include <time.h>
#endif

#include <casdef.h>

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
	double broadcast_denied;
	double pending;
	double ioc_error;
	double hit;
	double ioc_down;
}stat;

extern "C" void processChangeConnectionEvent( struct connection_handler_args args);

pIoc::~pIoc()
{
	while ( pvEIoc * pvei = this->pvEList.get() ) {
		delete pvei;
	}
}

pvEIoc::~pvEIoc()
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
 * \param iocname:port string
 *
*/
void pIoc::set_addr( chid chid)
{
		char hostNameStr[HOST_NAME_SZ];
		char *cport;
		int portNumber=0;
		int mystatus;

        this->addr.sin_family = AF_INET;

		ca_get_host_name(chid,hostNameStr,HOST_NAME_SZ);

        cport = strchr(hostNameStr,':');
        if(cport){
            portNumber = atoi(cport + 1);
            this->addr.sin_port = htons((aitUint16) portNumber);
			*cport = 0;
        } else {
            this->addr.sin_port = 0u;
        }
        mystatus = aToIPAddr (hostNameStr, portNumber, &this->addr);
        if (mystatus) {
            fprintf (stderr, "Unknown host name: %s \n", hostNameStr);
		}
}


/*! \brief Remove a pvEIoc from the ioc's pvEList
 *         delete  the pvEioc and return it's pve
 *
 * \param pve - a pvE object
 *
*/
pvE *pIoc::get()
{
	pvE *pve = 0;
	// get removes first item from list
	pvEIoc * pvei = this->pvEList.get();
	if (!pvei) return 0;
	pve=pvei->get_pvE();
	delete pvei;
	return pve;
}


/*! \brief Constructor for the directory server
 *
/*! \brief Add a pve to the ioc's pvEList
 *
 * \param pve - a pvE object
 *
*/
void pIoc::add( pvE *pve)
{
	pvEIoc *pveH = 0;

	pveH = new pvEIoc(pve);
	if (pveH) this->pvEList.add(*pveH);
	else fprintf(stdout, "cant add %s node to %s ioc pv table\n", pve->get_name(), this->get_iocname());
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
	this->stringResTbl.setTableSize(pvCount);
	this->iocResTbl.setTableSize(MAX_IOCS);
	this->neverResTbl.setTableSize(pvCount);

	stat.requests = 0;
	stat.broadcast = 0;
	stat.broadcast_denied = 0;
	stat.pending = 0;
	stat.ioc_error = 0;
	stat.ioc_down = 0;
	stat.hit = 0;

    pgateAs  = 0;
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
    tsSLList < pIoc > tmpIocList;
    tsSLList < pvE > tmpStringList;
    tsSLList < never > tmpNeverList;

	ca_context_destroy();
	// removeAll() puts entries on a tmpList.
	// and then traverses list deleting each entry
	this->iocResTbl.removeAll(tmpIocList);
    while ( pIoc * pI = tmpIocList.get() ) {
		delete pI;
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
		delete pNN;
	}

    if (pgateAs) delete pgateAs;

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

/*! \brief Add a ioc to the ioc hashtable
 *
 * Create a ioc node. Add to ioc hash table. 0=success, -1=failure;
 *
 * \param pIocName - IOC name
 * \param pPath - full path to 'signal.list' file
 * \param ipaIn - network info structure
*/
pIoc * directoryServer::installIocName(const char *pIocName, const char *pPath)
{
	pIoc *pI;

	pI = new pIoc( *this, pIocName, pPath);
	if (pI) {
		int resLibStatus;
		resLibStatus = this->iocResTbl.add(*pI);
		if (resLibStatus==0) {
			if(verbose)fprintf(stdout, "added %s to ioc table\n", pIocName);
			return(pI);
		}
		else {
			delete pI;
			fprintf(stderr,"Unable to install %s. \n", pIocName);
		}
	}
	else {
		fprintf(stderr,"can't create new ioc %s\n", pIocName);
	}
	return(0);
}

/*! \brief Add a pv to the pv hashtable
 *
 * Create a pv node. Add to pv hash table. 0=success, -1=failure;
 *
 * \param pName - pv name
 * \param pIocName - IOC serving this pv
*/
int directoryServer::installPVName( const char *pName, pIoc *pI)
{
	pvE	*pve;
	char *iocName = 0;

	if (pI) iocName = pI->get_iocname();
	pve = new pvE( *this, pName, pI);
	if (pve) {
		int resLibStatus;
		resLibStatus = this->stringResTbl.add(*pve);
		if (resLibStatus==0) {
			if(verbose)
				fprintf(stdout, "Installed PV: %s  %s in hash table\n", pName, iocName);
			/* add this pvE to the ioc's pvEList */
			if (pI) pI->add(pve);
			return(0);
		}
		else {
			delete pve;
			char    checkStr[PV_NAME_SZ];
            sprintf(checkStr,"%s%s",iocName, HEARTBEAT);
            if(strcmp(checkStr,pName)) {
				fprintf(stdout, "Unable to enter PV %s on %s in hash table. Duplicate?\n",
					 pName, iocName);
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
 *      - Search the ioc hashtable
 *      - If ioc is found
 *         - Check ioc status
 *         - If ioc is up
 *            - Return ioc address
 *  - In all other cases
 *     - Return pvNotFound
 *
 * \param casCtx - not used
 * \param pPVName - name of the pv we're looking for
*/
pvExistReturn directoryServer::pvExistTest (const casCtx& ctx, 
    const caNetAddr & addr, const char *pPVName)
{
	char 		shortPV[PV_NAME_SZ];
	pvE 		*pve;
	pIoc 		*pI;
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
		// Found pv in pv hash table. Check to see if ioc is up.
		if(verbose)
			fprintf(stdout,"found %s in PV TABLE\n", shortPV);
		pI = pve->get_pIoc();
		if(!pI) {
			if(verbose)
				fprintf(stdout, "Ioc error\n");
			stat.ioc_error++;
			return pverDoesNotExistHere;
		}
		else {
			if(verbose)
				fprintf(stdout,"Ioc installed. status: %d\n",pI->get_status());
			if(pI->get_status() == 1) {
				stat.hit++;
/*
				sockaddr_in tin = pI->getAddr();
				printf("f: %d p: %d a: %d\n",
					tin.sin_family,
					tin.sin_port,
					tin.sin_addr);
			    printf("ADDR <%s>\n", inet_ntoa(tin.sin_addr));
*/

//printf("ADDR <%s> %s  ", inet_ntoa(((sockaddr_in)pI->getAddr()).sin_addr),shortPV);
//printf("ADDR <%d> \n", pI->getAddr().sin_port); fflush(stdout);
				return pvExistReturn (caNetAddr(pI->getAddr()));
	
			}
			else {
				stat.ioc_down++;
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
				if(verbose)fprintf(stdout,"Connection pending for %s\n", shortPV);
//fprintf(stdout,"Connection pending for %s\n", shortPV); fflush(stdout);
				return (pverDoesNotExistHere);
			}
			iter++;
		}
		// Can we broadcast for the requested pv.
        if (this->pgateAs && this->pgateAs->isDenyFromListUsed()) {
//fprintf(stdout,"Deny from list is used for pv %s\n", shortPV); fflush(stdout);
			char *ptr;
			char ioc[HOST_NAME_SZ]="";

			// Get the client ioc name
			struct sockaddr_in inaddr=(struct sockaddr_in)addr;
			ipAddrToA(&inaddr,ioc,HOST_NAME_SZ);
			if ((ptr=strchr(ioc,HN_DELIM))) *ptr=0x0;
			if ((ptr=strchr(ioc,HN_DELIM2))) *ptr=0x0;

			if (this->pgateAs && !this->pgateAs->findEntry(shortPV,ioc)) {
//fprintf(stdout,"no entry: Broadcast denied for pv %s\n", shortPV); fflush(stdout);
				stat.broadcast_denied++;
				return (pverDoesNotExistHere);
			}
		} else {
			if (this->pgateAs && !this->pgateAs->findEntry(shortPV)) {
//fprintf(stdout,"File exists: Broadcast denied for pv %s\n", shortPV); fflush(stdout);
				stat.broadcast_denied++;
				return (pverDoesNotExistHere);
			}
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

	fprintf(stdout, "Ioc Hash Table:\n");
	this->iocResTbl.show(5);
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
		fprintf(stdout,"Broadcasts_denied:  %10.0f (%9.2f/hour)\n", stat.broadcast_denied, stat.broadcast_denied/hours);
		fprintf(stdout,"Ioc: \t%10.0f (%9.2f/hour)\n", stat.ioc_error, stat.ioc_error/hours);
		fprintf(stdout,"Pending: \t%10.0f (%9.2f/hour)\n", stat.pending, stat.pending/hours);
		fprintf(stdout,"Ioc_down: \t%10.0f (%9.2f/hour)\n", stat.ioc_down, stat.ioc_down/hours);
		fprintf(stdout,"    in %f seconds (%3.2f hours) \n", first - last_time, hours);
	}
	else {
		fprintf(stdout,"\nRequests: %f\n", stat.requests);
		fprintf(stdout,"\nHits: %f\n", stat.hit);
		fprintf(stdout,"Broadcasts: %f\n", stat.broadcast);
		fprintf(stdout,"Broadcasts_denied: %f\n", stat.broadcast_denied);
		fprintf(stdout,"Ioc_error: %f\n", stat.ioc_error);
		fprintf(stdout,"Pending: %f\n", stat.pending);
		fprintf(stdout,"Ioc_down: %f\n", stat.ioc_down);
		fprintf(stdout,"    since startup\n");
	}
	fprintf(stdout, "\n**********End diagnostics\n\n");
	fflush(stdout);

	stat.broadcast = 0;
	stat.broadcast_denied = 0;
	stat.pending = 0;
	stat.ioc_error = 0;
	stat.hit = 0;
	stat.ioc_down = 0;
	stat.requests = 0;
	last_time = first;
}

