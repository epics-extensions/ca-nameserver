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

static directoryServer *self = 0;

extern pid_t child_pid;
extern int outta_here;
extern int start_new_log;
extern int connected_iocs;
extern int requested_iocs;
extern int verbose;
extern FILE *never_ptr;

static struct {
	double requests;
	double broadcast;
	double pending;
	double host_error;
	double hit;
	double host_down;
}stat;

void processChangeConnectionEvent( struct connection_handler_args args);

pHost::~pHost()
{
}

pvE::~pvE()
{
}

never::~never()
{
}

/*! \brief Constructor for the directory server
 *
 * Set up signal handling. Initialize two hash tables. Zero statistics
 *
 * \param pvCount - size of the pv hashtable
 *
*/
directoryServer::directoryServer( unsigned pvCount) : 
	caServer( pvCount)
{
	int resLibStatus;

    assert(self==0);
	self = this;
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr1);
	signal(SIGTERM, sigusr1);
	signal(SIGINT, sigusr1);

    resLibStatus = this->stringResTbl.init(pvCount);
    if (resLibStatus) {
        fprintf(stderr, "CAS: string resource id table init failed\n");
		assert(resLibStatus==0);
    }

    resLibStatus = this->hostResTbl.init(MAX_IOCS);
    if (resLibStatus) {
		fprintf(stderr, "CAS: host resource id table init failed\n");
        assert(resLibStatus==0);
    }

    resLibStatus = this->neverResTbl.init(pvCount);
    if (resLibStatus) {
		fprintf(stderr, "CAS: never resource id table init failed\n");
        assert(resLibStatus==0);
    }
	stat.requests = 0;
	stat.broadcast = 0;
	stat.pending = 0;
	stat.host_error = 0;
	stat.host_down = 0;
	stat.hit = 0;
}

/*! \brief Signal handler
 *
 * SIGUSR1 writes summary info to the logfile.
 * SIGUSR2 starts a new logfile.
 * At JLab, SIGUSR2 deletes all knowledge of pioc pvs.
 * SIGTERM and SIGINT set the "outta_here" flag.
*/
void directoryServer::sigusr1(int sig)
{
	struct      timeval first;
	struct      timezone tzp;

	if( sig == SIGUSR2) {
		fprintf(stdout,"SIGUSR2\n");
		start_new_log = 1;
		signal(SIGUSR2, sigusr1);
	}

	else if( sig == SIGTERM || sig == SIGINT) {
		gettimeofday(&first, &tzp);
		fprintf(stdout,"SIGTERM time: %s\n", ctime((const time_t*)&first.tv_sec));
		//fprintf(stdout,"SIGTERM time: %s\n", ctime(first.tv_sec));
		fflush(stdout);
		fflush(stderr);
		outta_here = 1;
	}
	else {	
		gettimeofday(&first, &tzp);
		fprintf(stdout,"\n*********Sigusr1 time: %s\n", ctime(&first.tv_sec));
		// level=2 gets summary info
		// level=10 gets ALL names ...be careful what you ask for...
		self->show(2);
		fflush(stdout);
		signal(SIGUSR1, sigusr1);
	}
}

/*! \brief Destructor for the server
 *
 * Clean up the hashtables and the linked list.
 * Say goodbye to the logfile
 */
directoryServer::~directoryServer()
{
	// destroyAllEntries() calls the destroy method on each entry.
	// destroy calls delete the entry
	this->hostResTbl.destroyAllEntries();
	this->stringResTbl.destroyAllEntries();
	this->neverResTbl.destroyAllEntries();

	tsSLIterRm<namenode> iter(this->nameList);
	namenode *pNN;
	while((pNN=iter()))  {
		iter.remove();
		delete pNN;
		break;
	}

	struct      timeval first;
	struct      timezone tzp;
	gettimeofday(&first, &tzp);
	fprintf(stdout,"EXIT time: %s\n", ctime(&first.tv_sec));
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
int directoryServer::installHostName(const char *pHostName, const char *pPath,
	struct sockaddr_in &ipaIn)
{
	pHost *pH;

	pH = new pHost( *this, pHostName, pPath, ipaIn);
	if (pH) {
		int resLibStatus;
		resLibStatus = this->hostResTbl.add(*pH);
		if (resLibStatus==0) {
			if(verbose)fprintf(stdout, "added %s to host table\n", pHostName);
			return(0);
		}
		else {
			delete pH;
			fprintf(stderr,"Unable to install %s. \n", pHostName);
		}
	}
	else {
		fprintf(stderr,"can't create new host %s\n", pHostName);
	}
	return(-1);
}

/*! \brief Add a pv to the pv hashtable
 *
 * Create a pv node. Add to pv hash table. 0=success, -1=failure;
 *
 * \param pName - pv name
 * \param pHostName - IOC serving this pv
*/
int directoryServer::installPVName( const char *pName, const char *pHostName)
{
	pvE	*pve;

	pve = new pvE( *this, pName, pHostName/*, ipaIn*/);
	if (pve) {
		int resLibStatus;
		resLibStatus = this->stringResTbl.add(*pve);
		if (resLibStatus==0) {
			if(verbose)
				fprintf(stdout, "Installed PV: %s  %s in hash table\n", pName, pHostName);
			return(0);
		}
		else {
			delete pve;
			char    checkStr[PV_NAME_SZ];
            sprintf(checkStr,"%s%s",pHostName, HEARTBEAT);
            if(strcmp(checkStr,pName)) {
				fprintf(stdout, "Unable to enter PV %s on %s in hash table. Duplicate?\n",
					 pName, pHostName);
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
pvExistReturn directoryServer::pvExistTest (const casCtx&, const char *pPVName)
{
	char 		shortPV[PV_NAME_SZ];
	pvE 		*pve;
	pHost 		*pH;
	namenode	*pNN;
	chid 		chd;
	int 		i, len, status;

	stat.requests++;

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
		// Found pv in pv hash table. Check to see if host is up.
		if(verbose)
			fprintf(stdout,"found %s in PV TABLE\n", shortPV);
		stringId id2(pve->getHostname(), stringId::refString);
		pH = this->hostResTbl.lookup(id2);
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
		tsSLIter<namenode> iter(this->nameList);
		while( (pNN=iter())) {
			if(!strcmp(pNN->get_name(), shortPV)){
				stat.pending++;
				if(verbose)fprintf(stdout,"Connection pending\n");
				return (pverDoesNotExistHere);
			}
		}
		// Broadcast for the requested pv.
		status = ca_search_and_connect(shortPV,&chd,processChangeConnectionEvent,0);
		if (status != ECA_NORMAL) {
			fprintf(stderr,"ca_search failed on channel name: [%s]\n",shortPV);
			return pverDoesNotExistHere;
		}
		else {
			// Create a node for the linked list of pending connections
			pNN = new namenode(shortPV, chd, 0);
			pNN->set_otime();
			if(pNN == NULL) {
				fprintf(stderr,"Failed to create namenode %s\n", shortPV);
				return pverDoesNotExistHere;
			}
			// Add this pv to the list of pending pv connections 
			this->addNN(pNN);
			if(verbose)
				fprintf(stdout, "broadcasting for %s\n", shortPV);
			stat.broadcast++;
			return (pverDoesNotExistHere);
		}
		return (pverDoesNotExistHere);
	}
	return (pverDoesNotExistHere);
}

/*! \brief Write status summary to the logfile
 *
 *
*/
void directoryServer::show (unsigned level) const
{
	struct      timeval first;
	struct      timezone tzp;
	gettimeofday(&first, &tzp);

	static unsigned long last_time = 0;
	
	fprintf(stdout,"Diag time: %s\n", ctime(&first.tv_sec));
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
	namenode	*pNN;
	tsSLIter<namenode> iter(this->nameList);
	fprintf(stdout,"PV's pending connections:\n" );
	while( (pNN=iter())) {
			fprintf(stdout,"  %s\n", pNN->get_name());
	}
*/
	if(last_time != 0) {
		double hours = (double)(first.tv_sec - last_time)/60.0/60.0;
		fprintf(stdout,"\nRequests: \t%10.0f (%9.2f/hour)\n", stat.requests, stat.requests/hours);
		fprintf(stdout,"\nHits: \t\t%10.0f (%9.2f/hour)\n", stat.hit, stat.hit/hours);
		fprintf(stdout,"Broadcasts: \t%10.0f (%9.2f/hour)\n", stat.broadcast, stat.broadcast/hours);
		fprintf(stdout,"Host_error: \t%10.0f (%9.2f/hour)\n", stat.host_error, stat.host_error/hours);
		fprintf(stdout,"Pending: \t%10.0f (%9.2f/hour)\n", stat.pending, stat.pending/hours);
		fprintf(stdout,"Host_down: \t%10.0f (%9.2f/hour)\n", stat.host_down, stat.host_down/hours);
		fprintf(stdout,"    in %ld seconds (%3.2f hours) \n", first.tv_sec - last_time, hours);
	}
	else {
		fprintf(stdout,"\nRequests: %f\n", stat.requests);
		fprintf(stdout,"\nHits: %f\n", stat.hit);
		fprintf(stdout,"Broadcasts: %f\n", stat.broadcast);
		fprintf(stdout,"Host_error: %f\n", stat.host_error);
		fprintf(stdout,"Pending: %f\n", stat.pending);
		fprintf(stdout,"Host_down: %f\n", stat.host_down);
		fprintf(stdout,"    since startup\n");
	}
	fprintf(stdout, "\n**********End diagnostics\n\n");
	fflush(stdout);

	stat.broadcast = 0;
	stat.pending = 0;
	stat.host_error = 0;
	stat.hit = 0;
	stat.host_down = 0;
	stat.requests = 0;
	last_time = first.tv_sec;
}
