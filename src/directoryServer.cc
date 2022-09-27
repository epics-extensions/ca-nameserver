/*! \file directoryServer.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

#include <string.h>

#include <casdef.h>

#include "reserve_fd.h"
#include "directoryServer.h"
#ifdef linux
#include <time.h>
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
extern FILE *never_ptr;

struct nsStats stats = { 0.0,0.0,0.0,0.0,0.0,0.0,0.0 };


extern "C" void processChangeConnectionEvent( struct connection_handler_args args);
extern "C" void sigusr1(int sig);

pIoc::~pIoc()
{
	while ( this->get_pvE() ) {
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
            log_message (ERROR, "Unknown host name: %s \n", hostNameStr);
		}
}


/*! \brief Remove a pvEIoc from the ioc's pvEList
 *         delete  the pvEIoc and return it's pve
 *
 * \param pve - a pvE object
 *
*/
pvE *pIoc::get_pvE()
{
	pvE *pve = 0;
	// get removes first item from list
	pvEIoc * pvei = this->pvEList.get();
	if (!pvei) return 0;
	pve=pvei->get_pvE();
	delete pvei;
	return pve;
}

void pvE::myshow()
{
   	if ( this->get_pIoc()->get_pathToList()[0] == '\0'){
   	    printf ( "pvE: name=%s  ioc=%s \n",this->get_name(),this->get_pIoc()->get_iocname());
    }
}

void pIoc::myshow()
{
    printf ( "pIoc: name=%s  status=%d ADDR <%s> %d\n",this->iocname,this->status,
        inet_ntoa(((sockaddr_in)this->getAddr()).sin_addr),
        this->getAddr().sin_port);
}

void pIoc::show( unsigned level)
{
    if ( level >= 1u ) {
        printf ( "pIoc: name=%s  status=%d \n",this->iocname,this->status);
        if ( level > 5u ) {
                tsSLIter<pvEIoc> iter=this->pvEList.firstIter();
                while(iter.valid()) {
                    printf ("	%s\n", iter.pointer()->get_pvE()->get_name() );
                    iter++;
                }
        }
    }
}


/*! \brief Constructor for the directory server
 *
 *! \brief Add a pve to the ioc's pvEList
 *
 * \param pve - a pvE object
 *
*/
void pIoc::add( pvE *pve)
{
	pvEIoc *pveH = 0;

	pveH = new pvEIoc(pve);
	if (pveH) this->pvEList.add(*pveH);
	else log_message (WARNING, "cant add %s node to %s ioc pv table\n", pve->get_name(), this->get_iocname());
}


/*! \brief Constructor for the directory server
 *
 * Set up signal handling. Initialize two hash tables. Zero statistics
 *
 * \param pvCount - size of the pv hashtable
 *
*/
directoryServer::directoryServer( unsigned pvCount,const char* const pvPrefix) :
  pvServer(pvPrefix)
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
	this->iocResTbl.setTableSize(pvCount / 1000);
	this->neverResTbl.setTableSize(pvCount);

	stats.request = 0;
	stats.broadcast = 0;
	stats.broadcast_denied = 0;
	stats.pending = 0;
	stats.ioc_error = 0;
	stats.ioc_down = 0;
	stats.hit = 0;

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
extern "C" void sigusr1(int sig)
{
	int level;

	if( sig == SIGUSR2) {
		log_message (INFO,"********* SIGUSR2 \n");
		start_new_log = 1;
		signal(SIGUSR2, sigusr1);
	}

	else if( sig == SIGTERM || sig == SIGINT) {
		log_message (INFO,"********* SIGTERM \n");
		outta_here = 1;
	}
	else {	
		log_message (INFO,"********* SIGUSR1 \n");
		// level=2 gets summary info
		// level=10 gets ALL names ...be careful what you ask for...
                level = 2;
		self->show(level);
		log_message (INFO,"********* END SIGUSR1 report\n");
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
    tsSLList < never > tmpNeverList;
    tsSLList < pvE > tmpStringList;

	ca_context_destroy();

	// removeAll() puts entries on a tmpList.
	// and then traverses list deleting each entry
	this->iocResTbl.removeAll(tmpIocList);
    while ( pIoc * pI = tmpIocList.get() ) {
		delete pI;
    }
	this->neverResTbl.removeAll(tmpNeverList);
    while ( never * pN = tmpNeverList.get() ) {
		delete pN;
    }
	this->stringResTbl.removeAll(tmpStringList);
    while ( pvE * pve = tmpStringList.get() ) {
		delete pve;
    }
    while ( namenode * pNN = this->nameList.get() ) {
		delete pNN;
	}

    if (pgateAs) delete pgateAs;

	log_message (INFO,"EXIT\n");
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
			log_message (DEBUG, "added %s to never table\n", pName);
			return(0);
		}
		else {
			delete pnev;
			log_message (ERROR,"Unable to install %s. \n", pName);
		}
	}
	else {
		log_message (ERROR,"can't create new never %s\n", pName);
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
			log_message (DEBUG, "added %s to ioc table\n", pIocName);
			return(pI);
		}
		else {
			delete pI;
			log_message (ERROR,"Unable to install %s. \n", pIocName);
		}
	}
	else {
		log_message (ERROR,"can't create new ioc %s\n", pIocName);
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
	pvE	*pve2;
	pIoc *pIoc = 0;
	const char *iocName = 0;

	if (pI) iocName = pI->get_iocname();
	pve = new pvE( *this, pName, pI);
	if (!pve) {
		return(-1); //should never get here.
	}
	int resLibStatus;
	resLibStatus = this->stringResTbl.add(*pve);
	if (resLibStatus==0) {
		log_message (DEBUG, "Installed PV: %s  %s in hash table\n", pName, iocName);
		/* add this pvE to the ioc's pvEList */
		if (pI) pI->add(pve);
		return(0);
	}
	char    checkStr[PV_NAME_SZ];
	sprintf(checkStr,"%s%s",iocName, HEARTBEAT);
	if(!strcmp(checkStr,pName)) {
		delete pve;
		//log_message (DEBUG,"Special treatment for heartbeats\n");
		return(1);
	}
	// Remove existing pvE from stringResTable if it's ioc is DOWN
	// and add new pvE to stringResTable
	stringId id4(pName, stringId::refString);
	pve2 = this->stringResTbl.lookup(id4);
	if (pve2) pIoc = pve2->get_pIoc();	
	if (pIoc && pIoc->get_status() == 2) {
		pve2 = this->stringResTbl.remove(id4);

		// Remove pvEIoc from pvEList, delete pvEIoc, and delete it's pve 
        pvEIoc * pvei;
        pvEIoc * pprev = 0;
        //pvEIoc * pprev = 0;
        tsSLIter<pvEIoc> iter=pIoc->pvEList.firstIter();
        while(iter.valid()) {
            pvei = iter.pointer();
            if(pvei->get_pvE() == pve2){
                if (pprev) pIoc->pvEList.remove(*pprev);
                else pIoc->pvEList.get();
                delete pve2;
                delete pvei;
                break;
            }
            pprev = pvei;
            iter++;
        }

		resLibStatus = this->stringResTbl.add(*pve);
		if (resLibStatus==0) {
			log_message (INFO, "Moved PV: %s from %s to %s\n", pName, pIoc->get_iocname(), iocName);
			/* add this pvE to the ioc's pvEList */
			if (pI) pI->add(pve);
			return(0);
		}
	}
	delete pve;
	log_message (ERROR, "Unable to enter PV %s on %s in hash table. Duplicate?\n",
		 pName, iocName);
	return(-1);
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
	const pIoc 		*pI;
	namenode	*pNN;
	chid		chd;	
	int 		i, len, status;
	pvExistReturn pvReturn;


	log_message (DEBUG, "directoryServer:pvExistTest name=%s\n",pPVName);
	stats.request++;

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
		log_message (VERBOSE,"Invalid PV name %s\n",pPVName);
		return pverDoesNotExistHere;
	}

	stringId id(shortPV, stringId::refString);
	pve = this->stringResTbl.lookup(id);

	if(pve) {
		// Found pv in pv hash table. Check to see if ioc is up.
		pI = pve->get_pIoc();
		if(!pI) {
			log_message (DEBUG,"PV found and Ioc error for %s\n",shortPV);
			stats.ioc_error++;
			return pverDoesNotExistHere;
		}
		else {
			if(pI->get_status() == 1) {
				stats.hit++;
/*
				sockaddr_in tin = pI->getAddr();
				log_message (INFO,"f: %d p: %d a: %d\n",
					tin.sin_family,
					tin.sin_port,
					tin.sin_addr);
			    log_message (INFO,"ADDR <%s>\n", inet_ntoa(tin.sin_addr));
*/

				log_message (VERBOSE,"PV found and Ioc up for %s\n",shortPV);
//log_message (VERBOSE,"ADDR <%s> %s  ", inet_ntoa(((sockaddr_in)pI->getAddr()).sin_addr),shortPV);
//log_message (VERBOSE,"ADDR <%d> \n", pI->getAddr().sin_port); 
				return pvExistReturn (caNetAddr(pI->getAddr()));
	
			}
			else {
				stats.ioc_down++;
				log_message (VERBOSE,"PV found and Ioc down for %s\n",shortPV);
				return pverDoesNotExistHere;
			}
		}
	}
	else if (!pve) {

		// See if pv is a nameserver pv.
		pvReturn = this->pvServer::pvExistTest (ctx,pPVName);
		if(pvReturn.getStatus() == pverExistsHere){
			stats.hit++;
			log_message (VERBOSE,"Nameserver PV found for %s\n",pPVName);
			return pverExistsHere;
		}

		// pv not found. See if there is already a connection pending
	 	tsSLIter<namenode> iter = this->nameList.firstIter();
		while( iter.valid()) {
			pNN=iter.pointer();
			if(!strcmp(pNN->get_name(), shortPV)){
				stats.pending++;
				log_message (VERBOSE,"Connection pending for %s\n", shortPV);
				return (pverDoesNotExistHere);
			}
			iter++;
		}
		// Can we broadcast for the requested pv.
		if (this->pgateAs && this->pgateAs->isDenyFromListUsed()) {
			char *ptr;
			char ioc[HOST_NAME_SZ]="";

			// Get the client ioc name
			struct sockaddr_in inaddr=(struct sockaddr_in)addr;
			ipAddrToA(&inaddr,ioc,HOST_NAME_SZ);
			if ((ptr=strchr(ioc,HN_DELIM))) *ptr=0x0;
			if ((ptr=strchr(ioc,HN_DELIM2))) *ptr=0x0;

			if (this->pgateAs && !this->pgateAs->findEntry(shortPV,ioc)) {
				stats.broadcast_denied++;
				log_message (VERBOSE,"CA search broadcast denied from host %s for %s\n", ioc, shortPV);
				return (pverDoesNotExistHere);
			}
		} else {
			if (this->pgateAs && !this->pgateAs->findEntry(shortPV)) {
				log_message (VERBOSE,"CA search broadcast denied for %s\n", shortPV);
				stats.broadcast_denied++;
				return (pverDoesNotExistHere);
			}
		}

		// Broadcast for the requested pv.
		status = ca_search_and_connect(shortPV,&chd,processChangeConnectionEvent,0);
		if (status != ECA_NORMAL) {
			log_message (ERROR,"CA search failed for %s\n",shortPV);
			return pverDoesNotExistHere;
		}
		else {
			// Create a node for the linked list of pending connections
			pNN = new namenode(shortPV, chd, 0);
			if(pNN == NULL) {
				log_message (ERROR,"Failed to create namenode for %s\n", shortPV);
				return pverDoesNotExistHere;
			}
			pNN->set_otime();
			// Add this pv to the list of pending pv connections 
			this->addNN(pNN);
			log_message (VERBOSE, "CA search broadcasting for %s\n", shortPV);
			stats.broadcast++;
			return (pverDoesNotExistHere);
		}
	}
	return (pverDoesNotExistHere);
}


void directoryServer::setDebugLevel ( unsigned level )
{
	this->pvServer::setDebugLevel ( level );
}


void directoryServer:: generateBeaconAnomaly ()
{
	this->pvServer::generateBeaconAnomaly ();
}


/*! \brief Write status summary to the logfile
*/
void directoryServer::show (unsigned level) const
{
	epicsTime	first;
	local_tm_nano_sec   ansiDate;
	first = epicsTime::getCurrent ();
	ansiDate = first;
	fflush(stdout);

	static epicsTime	last_time = first;
	
    this->pvServer::show(level);

	fprintf(stdout,"Diag time: %s\n", asctime(&ansiDate.ansi_tm));
	never_ptr = reserve_fd_fopen("./never.log", "w");

	// Create a ptr to the fn we're gonna call.
	void (never::*fptr)() = &never::myshow;
	// create a ptr to T and cast it as non-const
	resTable<never,stringId> *junk = (resTable<never,stringId>* )&this->neverResTbl;
	junk->traverse(fptr); 
	reserve_fd_fclose(never_ptr);
	fprintf(stdout, "\n");

	fprintf(stdout, "NEVER Hash Table:\n");
	this->neverResTbl.show(level);
	fprintf(stdout, "\n");

	if (level>5) ((resTable<pIoc,stringId>* )&this->iocResTbl)->traverse(&pIoc::myshow); 

	fprintf(stdout, "Ioc Hash Table:\n");
	this->iocResTbl.show(level);
	fprintf(stdout, "\n");

	if (level>5) ((resTable<pvE,stringId>* )&this->stringResTbl)->traverse(&pvE::myshow); 

	fprintf(stdout, "PV Name Hash Table:\n");
	this->stringResTbl.show(level);
	fprintf(stdout, "\n");

	// print information about ca server library internals
	//this->caServer::show(level);

	fprintf(stdout,"PV's pending connections:\n" );
	tsSLIterConst<namenode> iter = this->nameList.firstIter();
	while (iter.valid()) {
		fprintf(stdout,"	%s\n", iter.pointer()->get_name());
		++iter;
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "Connected IOCS: %d\n", connected_iocs);
	fprintf(stdout, "Requested IOCS: %d\n", requested_iocs);
	fprintf(stdout, "\n");

	if(last_time != first) {
		double hours = (double)(first - last_time)/60.0/60.0;
		fprintf(stdout,"\nRequests: \t%10.0f (%9.2f/hour)\n", stats.request, stats.request/hours);
		fprintf(stdout,"\nHits: \t\t%10.0f (%9.2f/hour)\n", stats.hit, stats.hit/hours);
		fprintf(stdout,"Broadcasts: \t%10.0f (%9.2f/hour)\n", stats.broadcast, stats.broadcast/hours);
		fprintf(stdout,"Broadcasts_denied:  %10.0f (%9.2f/hour)\n", stats.broadcast_denied, stats.broadcast_denied/hours);
		fprintf(stdout,"Ioc: \t%10.0f (%9.2f/hour)\n", stats.ioc_error, stats.ioc_error/hours);
		fprintf(stdout,"Pending: \t%10.0f (%9.2f/hour)\n", stats.pending, stats.pending/hours);
		fprintf(stdout,"Ioc_down: \t%10.0f (%9.2f/hour)\n", stats.ioc_down, stats.ioc_down/hours);
		fprintf(stdout,"    in %f seconds (%3.2f hours) \n", first - last_time, hours);
	}
	else {
		fprintf(stdout,"\nRequests: %f\n", stats.request);
		fprintf(stdout,"\nHits: %f\n", stats.hit);
		fprintf(stdout,"Broadcasts: %f\n", stats.broadcast);
		fprintf(stdout,"Broadcasts_denied: %f\n", stats.broadcast_denied);
		fprintf(stdout,"Ioc_error: %f\n", stats.ioc_error);
		fprintf(stdout,"Pending: %f\n", stats.pending);
		fprintf(stdout,"Ioc_down: %f\n", stats.ioc_down);
		fprintf(stdout,"    since startup\n");
	}
	fprintf(stdout, "\n**********End diagnostics\n\n");
	fflush(stdout);

	stats.broadcast = 0;
	stats.broadcast_denied = 0;
	stats.pending = 0;
	stats.ioc_error = 0;
	stats.hit = 0;
	stats.ioc_down = 0;
	stats.request = 0;
	last_time = first;
}


//
// directoryServer::directoryAttach()
//
pvAttachReturn directoryServer::pvAttach
    (const casCtx &ctx, const char *pName)
{
    return this->pvServer::pvAttach(ctx,pName);
}



