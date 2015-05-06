/*! \file main.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

//#include <stdio.h>
//#include <string.h>
#include <sys/stat.h>
#ifdef linux
#include <libgen.h>
#include <sys/wait.h>
#endif
#ifdef SOLARIS
#include <libgen.h>
#endif

#include <fdmgr.h>
#include <fdManager.h>

#include "version.h"
#include "directoryServer.h"
#include "reserve_fd.h"
#include "nsIO.h"

#ifndef TRUE
#define TRUE 1
#endif

#define WAITSECONDS 5

//prototypes
static int parseDirectoryFile (const char *pFileName);
static int parseDirectoryFP (FILE *pf, pIoc *pI);
static void start_ca_monitor();
extern "C" void registerCA(void *pfdctx,int fd,int condition);
extern void remove_CA_mon(int fd);	//!< CA utility
extern void add_CA_mon(int fd);		//!< CA utility
extern "C" void processChangeConnectionEvent( struct connection_handler_args args);
extern "C" void WDprocessChangeConnectionEvent( struct connection_handler_args args);
extern "C" void sig_chld(int);
extern "C" void kill_the_kid(int);
extern "C" void sig_dont_dump(int);
static int start_daemon();
static int remove_all_pvs(pIoc *pI);
static int add_all_pvs(pIoc *pI);
static char *iocname(int isFilname,char *pPath);
static void processPendingList (epicsTime now,double tooLong);
#ifdef JS_FILEWAIT
static void processReconnectingIocs ();
#endif
static int connectIocHeartbeat(pIoc *pI);
static int connectAllIocHeartbeats();
static int cleanup ( pIoc *pIoc);

// globals
directoryServer	*pCAS;
#ifndef WIN32
pid_t child_pid =0;		//!< pid of the child process
pid_t parent_pid =0;		//!< pid of the parent process
pid_t parent_pgrp =0;		//!< pid of the parent process
#endif
int outta_here;			//!< flag indicating kill signal has been received
int start_new_log;		//!< flag indicating user wants to start a new log
						//!< At Jlab, used to rm pioc pvs and dir:
						//!< remove_all_pvs("opbat1");
						//!< remove_all_pvs("opbat2");
						//!< remove_all_pvs("devsys01");
						//!< system("rm -r /usr/local/bin/nameserver/piocs");
int connected_iocs;		//!< count of iocs which are currently connected
int requested_iocs;		//!< count of iocs which we would like to be connected
FILE *never_ptr;
int filenameIsIocname = 0;  //!< Signal list filename is iocname (else basename dirname)


/*! \brief Initialization and main loop
 *
*/
extern int main (int argc, char *argv[])
{
	// Runtime option args:
	int 		debugLevel = 2;
	char		*fileName;					//!< input filename
	char		*log_file;					//!< log filename
	char		*home_dir;					//!< home directory
	char		defaultFileName[] = "pvDirectory.txt";
	char		defaultLog_file[] = "log.file";
	char		defaultHome_dir[] = "./";
	char*		pvPrefix = NULL;
	char*		pvlist_file=0;				//!< broadcast access pvlist filename
	int			server_mode = 0;				//!< running as a daemon = 1
	unsigned 	hash_table_size = 0;			//!< user requested size
	aitBool		forever = aitTrue;
 	epicsTime	begin(epicsTime::getCurrent());
	int			logging_to_file = 0;			//!< default is logging to terminal
	//int			nPV = DEFAULT_HASH_SIZE;		//!< default pv hash table size
	int			pv_count;						//!< count of pv's in startup lists
#ifndef WIN32
	int			daemon_status;					//!< did the forks work?
#endif
	epicsTime   first;                  //!< time loading begins
	epicsTime   second;                 //!< time loading begins
	double 		lapsed;					//!< loading time
	int 		parm_error=0;
	int 		i;
	int 		c;

    fileName = defaultFileName;
    log_file = defaultLog_file;
    home_dir = defaultHome_dir;

	// Parse command line args.
	for (i = 1; i<argc && !parm_error; i++) {
		switch(c = argv[i][1]) {
           case 'd':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
						debugLevel=atoi(argv[i]);
						set_log_level(debugLevel);
					}
				}
				break;
                break;
           case 'f':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
                        fileName = argv[i];
					}
				}
				break;
           case 'c':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
                        home_dir = argv[i];
					}
				}
				break;
           case 'x':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
                        pvPrefix = argv[i];
					}
				}
				break;
           case 'l':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
						log_file = argv[i];
						log_message(INFO, "logging to file: %s\n",log_file);
						logging_to_file = 1;
					}
				}
                break;
           case 'p':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
						pvlist_file=argv[i];
						log_message(INFO, "pvlist file: %s\n",pvlist_file);
					}
				}
				break;
           case 's':
                server_mode = 1;
				logging_to_file = 1;
                break;
           case 'h':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
						hash_table_size=atoi(argv[i]);
					}
				}
				break;
           case 'n':
                filenameIsIocname = 1;
                break;
           case '?':
				parm_error = -1;
           default:
				parm_error = 1;

        }
    }

    if (parm_error) {
        if (parm_error == 1) fprintf(stderr,"\nInvalid command line option %s\n ", argv[i-1]);
        if (parm_error == 2) fprintf(stderr,"\nInvalid command line option %s %s\n ",
             argv[i-2], argv[i-1]);
        fprintf (stderr, "usage: %s [-d<debug level> -f<PV directory file> "
           "-p<pvlist file> -x<pvname prefix> "
           "-l<log file> -s -h<hash table size>] [-c cd to -v]\n", argv[0]);
       	exit(-1);
    }


#ifndef WIN32
	if(server_mode) {
		log_message(INFO, "Starting daemon\n");
		daemon_status = start_daemon();
		if(daemon_status) {
			exit(0);
		}
	}
	else{
		parent_pid=getpid();
	}

    if ( cd_home_dir(home_dir) == -1) {
		return -1;
	}

    create_killer_script(parent_pid, home_dir, pvlist_file, log_file, fileName);

    create_restart_script();

    increase_process_limits();
#endif
    setup_logging(log_file);

    //time  in  seconds since 00:00:00 UTC, January 1, 1970.
    first = epicsTime::getCurrent ();
    log_message(INFO,"Start time\n");

    fprintf(stdout,"%s [%s %s]\n", NS_VERSION_STRING,__DATE__,__TIME__);

    fprintf(stdout,"\n");
    if (pvlist_file) fprintf(stdout,"pvlist file: %s\n",pvlist_file);
    if (home_dir) fprintf(stdout,"home directory: %s\n",home_dir);
    fprintf(stdout,"\n");

    print_env_vars(stdout);

	// Initialize the server and hash tables
	if(hash_table_size) {
		pCAS = new directoryServer(hash_table_size,pvPrefix);
	}
	else {
		pCAS = new directoryServer(DEFAULT_HASH_SIZE,pvPrefix);
	}
	if (!pCAS) {
		return (-1);
	}

	//pCAS->setDebugLevel(debugLevel);

	// Setup broadcast access security
	if (pvlist_file) pCAS->pgateAs = new gateAs(pvlist_file);

	// Enable watchdog monitoring
	start_ca_monitor();

	outta_here = 0;
	start_new_log = 0;

	// Read the signal lists and fill the hash tables
	pv_count = parseDirectoryFile (fileName);
	connectAllIocHeartbeats();

    fprintf(stdout,"Total PVs in signal lists: %d\n", pv_count);

	// Get startup timing information.
	second = epicsTime::getCurrent ();
    log_message(INFO,"START time\n");

    lapsed = second-first;
    log_message(INFO,"Load time: %f sec\n", lapsed);

//Begin PROPOSED CHANGE add an exec of a script to copy signal.list files to ./iocs

	// Main loop
	if (forever) {
		// if delay = 0.0 select will not block)
		//double delay=.010; // (10 ms)
		double delay=1000.0; // (1000 sec)
 		double purgeTime=30.0; // set interval between purge checks to 30 seconds 
 		double tooLong=300.0; // remove from pending list after 5 minutes

		while (!outta_here) {
			fileDescriptorManager.process(delay);
			ca_pend_event(.001);
			//ca_poll();

#ifdef JS_FILEWAIT
			processReconnectingIocs ();
#endif
			if(start_new_log) {
#ifdef PIOC
				pIoc 		*pI;
				stringId id("opbat1", stringId::refString);
				pI = pCAS->iocResTbl.lookup(id);
				remove_all_pvs(pI);
				stringId id2("opbat2", stringId::refString);
				pI = pCAS->iocResTbl.lookup(id2);
				remove_all_pvs(pI);
				stringId id3("devsys01", stringId::refString);
				pI = pCAS->iocResTbl.lookup(id3);
				remove_all_pvs(pI);
				system("rm -r /usr/local/bin/nameserver/piocs");
				start_new_log = 0;
				log_message("INFO","REMOVE DONE\n");
#else
				start_new_log = 0;
				setup_logging(log_file);
#endif
			}
			// Is it time to purge the list of pending pv connections?
			epicsTime		now(epicsTime::getCurrent());
			if((now - begin) > purgeTime) {
				// reset the timer.
				begin = epicsTime::getCurrent();
				processPendingList(now,tooLong);
			} // end if time to purge
		} // end while(!outta_here)
	}
	pCAS->show(2u);
	delete pCAS;
	return (0);
}

#ifdef JS_FILEWAIT
static void processReconnectingIocs (){
	int	pv_count;						//!< count of pv's in startup lists
	// For each ioc on the linked list of reconnecting iocs
	// see if the ioc finished writing the signal.list file?
	struct stat sbuf;			
	filewait *pFW;
	filewait *pprevFW=0;
	int size = 0;
	int now = 0;

	tsSLIter<filewait> iter2=pCAS->fileList.firstIter();
	while(iter2.valid()) {
		tsSLIter<filewait> tmp = iter2;
		tmp++;
		pFW=iter2.pointer();
		const char *file_to_wait_for;
		file_to_wait_for = pFW->get_pIoc()->get_pathToList();
		// Delay test for WAITSECONDS
        now = time(0);
        if ( now > (pFW->get_connectTime()+WAITSECONDS) ) {
		// We're waiting to get the filesize the same twice in a row.
		// This is at best a poor test to see if the ioc has finished writing signal.list.
		// Better way would be to find a token at the end of the file but signal.list
		// files are also used by the old nameserver and can't be changed.
		if(stat(file_to_wait_for, &sbuf) == 0) {
			// size will be 0 first time thru
			// So at least we will have a delay in having to get here a second time.
			size = pFW->get_size();
			if((sbuf.st_size == size) && (size != 0)) {
				//log_message(INFO,"SIZE EQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
				remove_all_pvs(pFW->get_pIoc());
				pv_count = add_all_pvs(pFW->get_pIoc()); 
				pFW->get_pIoc()->set_status(1);
				if (pv_count >= 0 || pFW->read_tries >2 ) {
					if (pv_count > 0) pCAS->generateBeaconAnomaly();
                   	if(pprevFW) pCAS->fileList.remove(*pprevFW);
					else pCAS->fileList.get();
					delete pFW;
					pFW =0;
				} else {
					pFW->read_tries++;
				}
			}
			else{
				pFW->set_size(sbuf.st_size);
				//log_message(INFO,"UNEQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
			}
		}
		else {
			log_message(ERROR,"Stat failed for %s because %s\n", file_to_wait_for, strerror(errno));
		}
		}
		if (pFW) pprevFW = pFW;
		iter2 = tmp;
	}
}
#endif

static void processPendingList (epicsTime now,double tooLong){
 
	// Look for purgable pv's.
	pIoc *pI;
	namenode *pNN;
	namenode *pprevNN = 0;
	tsSLIter<namenode> iter1=pCAS->nameList.firstIter();
	tsSLIter<namenode> tmp;
	while(iter1.valid()) {
	 	tmp = iter1;
		tmp++;
		pNN=iter1.pointer();
		if((pNN->get_otime() + tooLong < now )) {
			if(pNN->get_rebroadcast()) {

				// Rebroadcast for heartbeat
				chid tchid;
				int chid_status = ca_state(pNN->get_chid());
				pI = (pIoc*)ca_puser(pNN->get_chid());
				//log_message(INFO, "1. chid_status: %d name: %s\n", 
				//	chid_status, pNN->get_name());fflush(stdout);
				if(chid_status != 3) {
				    //enum channel_state {cs_never_conn, cs_prev_conn, cs_conn, cs_closed};
					int stat = ca_clear_channel(pNN->get_chid());
					ca_flush_io();
					if(stat != ECA_NORMAL) {
						log_message(ERROR,"Purge: Can't clear channel for %s\n", pNN->get_name());
						log_message(ERROR, "Error: %s\n", ca_message(stat));
					}
					else {
						stat = ca_search_and_connect((char *)pNN->get_name(), &tchid, 
							WDprocessChangeConnectionEvent, 0);
						if(stat != ECA_NORMAL) {
							log_message(ERROR, "Purge: Can't search for %s\n", pNN->get_name());
						} else {
							pNN->set_chid(tchid);
							pNN->set_otime();
							ca_set_puser (tchid, pI);
							//log_message(DEBUG, "Purge: New search for %s\n", pNN->get_name());
						}
					}
				}
				fflush(stdout); fflush(stderr);
				continue;
			}

			// Update the "never" hash table
			stringId id(pNN->get_name(), stringId::refString);
			never *pnev;
			pnev = pCAS->neverResTbl.lookup(id);
			if(!pnev) pCAS->installNeverName(pNN->get_name());
			else pnev->incCt();

			// clear the channel
			int chid_status = ca_state(pNN->get_chid());
			//log_message(DEBUG, "2. chid_status: %d name: %s\n", 
			//	chid_status, pNN->get_name());fflush(stdout);
			if(chid_status != 3) {
				int stat = ca_clear_channel(pNN->get_chid());
				ca_flush_io();
				if(stat != ECA_NORMAL) 
					log_message(ERROR,"Channel clear error for %s\n", pNN->get_name());
			}
			fflush(stdout);
			if (pprevNN) pCAS->nameList.remove(*pprevNN);
			else pCAS->nameList.get();
			delete pNN;
			pNN = 0;
		} // end purge
		if (pNN) pprevNN = pNN;
		iter1 = tmp;
	} // end iter looking for things to purge

}

#ifndef WIN32

/*! \part that watches the nameserver process and ensures that it stays up
 *
*/
static int start_daemon()
{
	signal(SIGCHLD, sig_chld);
	parent_pgrp=getpgrp();
	switch(fork()) {
		case -1:	//error
			perror("Cannot create daemon process");
			return -1;
		case 0:		//child
			setpgid (0, 0);
			setsid();
			break;
		default:	//parent
			return 1;
	}

	parent_pid=getpid();
	do {
		switch(child_pid=fork()){
			case -1:	//error
				perror("Cannot create daemon process");
				return -1;
			case 0:     //child
				signal(SIGBUS, sig_dont_dump);
				signal(SIGSEGV, sig_dont_dump);
				break;
			default:	//parent
				signal(SIGTERM, kill_the_kid);
				pause();	//Wait here for any signal.
				break;
		}
	} while (child_pid );

	if( child_pid == -1) {
		return 1;
	}
	else {
		return 0;
	}
}


/*! \brief  Prevent core dumps which delay restart
 *
*/
extern "C" void sig_dont_dump(int sig)
{
	switch(sig) {
		case SIGBUS:
			log_message(INFO, "Abort on SIGBUS\n"); fflush(stderr);
			break;
		case SIGSEGV:
			log_message(INFO, "Abort on SIGSEGV\n"); fflush(stderr);
			break;
	}
	exit(0);
}


/*! \brief  Allow termination of parent process to terminate the child.
 *
*/
extern "C" void kill_the_kid(int )
{
        kill(child_pid, SIGTERM);
		exit(1);
}
#endif

/*! \brief Process the input directory file.
 *
 * Open the specified input file which contains a list of pathnames
 * to pv list files called signal.list. The  parent directories of list files
 * must be the ioc name for all pvs in the list.
 * For example, if !filenameIsIocname "/xxx/xxx/iocmag/signal.list" contains all pvs on iocmag.
 * For example, if  filenameIsIocname "/xxx/xxx/xxx/iocmag" contains all pvs on iocmag.
 *
 * \param  pFileName - name of the input directory file.
*/
static int parseDirectoryFile (const char *pFileName)
{
	FILE	*pf;
	int	count = 0;
	int	newcount;
	char	input[200];
	pIoc 	*pIcheck;
	pIoc 	*pI;
	char 	iocName[HOST_NAME_SZ];
	char 	*fileNameCopy;
	char 	*pIocname;
	char 	tStr[PV_NAME_SZ];

	if (!pFileName) return 0;

	// Open the user specified input file or the default input file(pvDirectory.txt).
	pf = fopen(pFileName, "r");
	if (!pf) {
		log_message(ERROR, "file access problems with file=\"%s\" because \"%s\"\n",
			pFileName, strerror(errno));
		return (-1);
	}

	// Loop over  each of the files listed in the specified file.
	while(TRUE) {
		if (fscanf(pf, " %s ", input) != 1) break;
		if(input[0] == '#') continue;

		fileNameCopy = strdup(input); 
		if (!fileNameCopy) return 0;
		pIocname = iocname(filenameIsIocname, fileNameCopy);
		free(fileNameCopy);
		strncpy (iocName,pIocname,HOST_NAME_SZ-1);
		iocName[HOST_NAME_SZ-1]='\0';
		stringId id(iocName, stringId::refString);

		pIcheck = pCAS->iocResTbl.lookup(id);
		if(pIcheck) {
			log_message(ERROR,"Duplicate iocname in list: %s\n", pFileName);
			continue;
		}
		pI = pCAS->installIocName(iocName,input);
		sprintf(tStr,"%s%s", pI->get_iocname(),HEARTBEAT);
		pCAS->installPVName( tStr, pI);

		newcount = add_all_pvs (pI);
		if (newcount == -1 ) {
			cleanup(pI);
		} else {
			count =  count + newcount;
			requested_iocs++;
		}
	}
	fclose (pf);
	return count;
}

/*! \brief Process the 'signal.list' files
 *
 * Add ioc to ioc hash table.
 * Create watchdog channels.
 * Read the PV name entries from the signal.list file.
 * Populate the pv hash table.
 *
 * \param pf - pointer to the open 'signal.list' file
 * \param pFileName - full path name to the file
 * \param startup_flag - 1=initialization, 0=reconnect
*/
static int parseDirectoryFP (FILE *pf, pIoc *pI)
{
	char 	pvNameStr[PV_NAME_SZ];
	int 	count = 0;
	int 	have_a_heart = 0;


	while (TRUE) {
		// Read the PV name entries until end of file.
		if (fscanf(pf, " %127s ", pvNameStr) != 1) {
			break;
		}
		// Add the pv to the pv hash table. This will always fail for 
		// xxx:heartbeat. On first_connect, we've already installed it. 
		// On reconect, the heartbeat is still in the hashtable.
		// This is faster than doing a strcmp on every signal read from the file.

		//log_message(INFO,"INSTALLING pv %s\n", pvNameStr);
		int failed = pCAS->installPVName( pvNameStr, pI);
		if(failed == 1 ) {
			have_a_heart ++;
		//log_message(INFO,"HEARTBEAT found:  %s\n", pvNameStr);
		}
		else if(failed == -1){
			log_message(ERROR,"HB ERROR! FAILED TO INSTALL %s\n", pvNameStr);
		}
		count++;
	}

	fprintf(stdout, "%s ct: %d\n", pI->get_iocname(), count);

        if(!have_a_heart) return -1;
	else return count;
}

// If the signal.list file did not contain a heartbeat, clean up.
// This seems like too much work, but it does avoid having to do strncmp's
// during startup. Also, in practice, this should rarely be called.
static int cleanup ( pIoc *pIocIn)
{
	namenode *pNN;
	char 	tStr[PV_NAME_SZ];
	pIoc    *pI = pIocIn;
	int 	removed = 0;

        if(!pI) return 0;

	log_message(ERROR, "NO HEARTBEAT in %s\n", pI->get_pathToList());
	tsSLIter<namenode> iter=pCAS->nameList.firstIter();
	namenode *pprevNN=0;
	while( iter.valid()) {
		pNN=iter.pointer();
		if(!strcmp(pNN->get_name(), tStr )) {
			if (pprevNN) pCAS->nameList.remove(*pprevNN);
			else pCAS->nameList.get();
			delete pNN;
			break;
		}
		pprevNN = pNN;
		iter++;
	}

	removed = remove_all_pvs(pI);
	pI= pCAS->iocResTbl.remove(*pI);
	if(pI) { delete pI; }
	else { log_message(ERROR,"iocResTbl remove failed for %s\n", pI->get_iocname());}

        return removed;
}


static int connectAllIocHeartbeats()
{
	pIoc 	*pI;

	resTableIter<pIoc, stringId> iter=pCAS->iocResTbl.firstIter();
	while( iter.valid()) {
		pI=iter.pointer();
		connectIocHeartbeat(pI);
		iter++;
	}
	ca_flush_io();
	return 0;
}

// Install the heartbeat signal as a watchdog to get disconnect
// signals to invalidate the IOC. DON'T do this on a reconnect
// because we won't have deleted the heartbeat signal.
static int connectIocHeartbeat(pIoc *pI)
{
	char 	tStr[PV_NAME_SZ];
	chid 	chd;
	int 	status;
	namenode *pNN;

	sprintf(tStr,"%s%s", pI->get_iocname(),HEARTBEAT);
	status = ca_search_and_connect(tStr,&chd, WDprocessChangeConnectionEvent, 0);
	if (status != ECA_NORMAL) {
		log_message(ERROR,"ca_search failed on channel name: [%s]\n",tStr);
		if (chd) ca_clear_channel(chd);
		return(0);
	}
	ca_set_puser (chd, pI);

	// Add this pv to the list of pending pv connections
	pNN = new namenode(tStr, chd, 1);
	pNN->set_otime();
	if(pNN == NULL) {
		log_message(ERROR,"Failed to create namenode %s\n", tStr);
	}
	else {
            	pCAS->addNN(pNN);
	}
	return 0;
}

/*! \brief Get ready to monitor channel access signals
 *
*/
static void start_ca_monitor()
{

    SEVCHK(ca_context_create(ca_enable_preemptive_callback),
        "initializeCA: error in ca_context_create");
    SEVCHK(ca_add_fd_registration(registerCA,NULL),
        "initializeCA: error adding CA's fd to X");
}

/*! \brief Callback for watchdog connection events
 *
 * These are events which will cause change to the IOC hash table.
*/
extern "C" void WDprocessChangeConnectionEvent(struct connection_handler_args args)
{
	char pvname[PV_NAME_SZ];
	const char *iocname;
	pIoc *pI;
	namenode *pNN;
#ifdef JS_FILEWAIT
	filewait *pFW;
#endif

	strncpy(pvname, ca_name(args.chid),PV_NAME_SZ-1);
	pI = (pIoc*)ca_puser(args.chid);
	iocname = pI->get_iocname();  // this is iocname

	if (args.op == CA_OP_CONN_DOWN) {
		if(pI) pI->set_status(2);
		connected_iocs --;
		log_message(INFO,"WATCHDOG CONN DOWN for %s\n", iocname);

		// Add this heartbeat pv back to the list of pending pv connections
		pNN = new namenode(pvname, args.chid, 1);
		pNN->set_otime();
		if(pNN == NULL) {
			log_message(ERROR,"Failed to create namenode %s\n", pvname);
		}
		pCAS->addNN(pNN);
		return;
	}

	connected_iocs ++;
	log_message(INFO,"WATCHDOG CONN UP for <%s> on %s state: %d\n", 
		ca_name(args.chid), iocname, ca_state(args.chid));

	// Find  and remove this pv from the list of pvs
	// with pending connections. Delete the node.
	tsSLIter<namenode> iter=pCAS->nameList.firstIter();
	int found = 0;
	namenode *pprevNN=0;
	while(iter.valid()) {
		pNN=iter.pointer();
		if(!strcmp(pNN->get_name(), pvname )) {
			//log_message(INFO,"Removed %s from pending\n", pvname);
			if (pprevNN) pCAS->nameList.remove(*pprevNN);
			else pCAS->nameList.get();
			delete pNN;
			found = 1;
			break;
		}
		pprevNN = pNN;
		iter++;
	}
	if(!found){
		log_message(ERROR, "Cannot remove from pending: <%s>\n", pvname);
		fflush(stdout);
	}

	if(pI) {
		// Set host name and port number.
		pI->set_addr(args.chid);

		// initial connection
		if (pI->get_status() == 0) { 
			pI->set_status(1);
		}
		// reconnection
		else if (pI->get_status() == 2) { 
#ifdef JS_FILEWAIT
			// Add this ioc to the list of those to check status of file
			// to prevent reading signal.list until writing by ioc is complete.
			pFW = new filewait(pI, 0);
			if(pFW == NULL) {
				log_message(ERROR,"Failed to create filewait node %s\n", iocname);
			}
			else {
				pCAS->addFW(pFW);
			}
#else
			remove_all_pvs(pI);
			add_all_pvs(pI); 
			pI->set_status(1);
#endif
		} 
		else {
		}
	}
	else { 
		log_message(ERROR,"WDCONN UP ERROR: Ioc %s NOT installed\n", iocname); 
		fflush(stderr);
	}
}


/*! \brief  CA callback after a search broadcast.
 *
 \verbatim  
	Handles connection events for pvs  which are not in any signal.list file.
 	On disconnect, sets ioc status to 2.
  	On connect or reconnect: 
        1. Get info
                isHeartbeat?
                is the ioc alreadyInIocTable?
                is iocUp?
        2. If(isHeartbeat && alreadyInIocTable)
                remove_all_pvs(pI);
        3. Install pv
        4. Remove pv from pending list
        5. If(isHeartbeat)
                if(alreadyInIocTable) setPort
                else install ioc
            set status to 1
            connected_iocs ++
        6. Append to signal.list file
                if necessary, create signal.list file 
        7. If(!isHeartbeat) ca_clear_channel
        8. If( !isHeartbeat && !alreadyInIocTable)
                if (heartbeat for this ioc is not on pending list)
                    broadcast for heartbeat
                    add heartbeat to pending list
\endverbatim
*/
extern "C" void processChangeConnectionEvent(struct connection_handler_args args)
{
	pIoc *pI;
    char pvNameStr[PV_NAME_SZ];
	char *ptr;
	int 	removed = 0;
	char iocname[HOST_NAME_SZ];
	ca_get_host_name(args.chid,iocname,HOST_NAME_SZ);
	pvE 	*pve;

    // We want to monitor all learned pvs because they may be from gateways
	int    monitorAll=1;

	if (args.op == CA_OP_CONN_DOWN) {

		if (monitorAll) {
			strncpy (pvNameStr, ca_name(args.chid),PV_NAME_SZ-1);
			stringId id2(pvNameStr, stringId::refString);
			pve = pCAS->stringResTbl.remove(id2);
			if(pve) {
				delete pve;
			}
			else {
				log_message(ERROR,"failed to remove %s\n", pvNameStr);fflush(stderr);
			}
		} else {
			connected_iocs --;
			pI = ( pIoc * ) ca_puser( args.chid );
			if(pI) {
				pI->set_status(2);
				log_message(INFO,"CONN DOWN for %s\n", pI->get_iocname()); fflush(stdout);
				// Remove all pvs now
				// On reconnect this ioc may have a different port
				removed = remove_all_pvs(pI);
				pI= pCAS->iocResTbl.remove(*pI);
				if(pI) delete pI;
	
			}
			else {
				log_message(ERROR,"CONN DOWN ERROR: IOC %s NOT INSTALLED \n", iocname);
			}
		}
		ca_clear_channel(args.chid);
	}
	else{
		// Got a response to UDP broadcast for a pv not already in the pv hash table. 
		int isHeartbeat = 0;


		stringId id(iocname, stringId::refString);
		pI = pCAS->iocResTbl.lookup(id);
		if(!pI) {
			pI = pCAS->installIocName(iocname,0);
			if (!pI) { log_message(ERROR,"error installing ioc: %s\n",
				iocname);
				return;
			}
			isHeartbeat = 1;
		    if (!monitorAll) log_message(INFO,"CONN UP for %s\n", iocname); fflush(stdout);
		}


		// 2.___________________________________________
		// This is a reconnect or initial connection.
		if(pI->get_status()==2 || pI->get_status()==0) {
	 		if (!monitorAll) connected_iocs ++;
			pI->set_addr(args.chid);
			ca_set_puser(args.chid,pI);
		}

		// 3.___________________________________________
		// Always install this pv into the pv hash table
		strncpy (pvNameStr, ca_name(args.chid),PV_NAME_SZ-1);
		ptr = strchr(pvNameStr,'.');
		if (ptr) *ptr = 0;
		pCAS->installPVName( pvNameStr, pI);

		// If monitorAll=1 we dont want ioc's pvEList so remove pv
		// just added by installPVName
		if (monitorAll && pI) pI->get_pvE();	

		// 4.___________________________________________
		// Always remove this pv from the list of pending conections.
		{
			tsSLIter<namenode> iter=pCAS->nameList.firstIter();
			namenode *pNN;
			int found = 0;
			namenode *pprevNN=0;
			while(iter.valid()) {
				pNN=iter.pointer();
				if(!strcmp(pNN->get_name(), pvNameStr )) {
					log_message(VERBOSE,"Removed %s from pending\n", pvNameStr);
					if (pprevNN) pCAS->nameList.remove(*pprevNN);
					else pCAS->nameList.get();
					delete pNN;
					found = 1;
					break;
				}
				pprevNN = pNN;
				iter++;
			}
			if(!found) log_message(ERROR, "Unable to purge %s\n", pvNameStr);
		}

		// 5.___________________________________________
		// Set pIoc status to UP
		pI->set_status(1);

		// 7.___________________________________________
		// If monitorAll=1 We want a connection handler on ALL pvs
		// else
		// Don't want monitors on anything except heartbeats.
		if( !monitorAll && !isHeartbeat) {
			int chid_status = ca_state(args.chid);
			if(chid_status != 3)
				ca_clear_channel(args.chid);
		}	
	}
	fflush(stderr);
	fflush(stdout);
}

/*! \brief Utility for CA monitor
*/
extern "C" void registerCA(void * /* pfdctx */,int fd, int condition)
{
    if (condition)
        add_CA_mon( fd);
    else
        remove_CA_mon( fd);
}

#ifndef WIN32
/*! \brief Callback for death of a child
 *
 * When run as a daemon, death of a child shakes the parent out of pause status.
 * A new child will be started.
*/
extern "C" void sig_chld(int )
{
#ifdef SOLARIS
    while(waitpid(-1,NULL,WNOHANG)>0);
#elif defined linux 
    while(waitpid(-1,NULL,WNOHANG)>0);
#elif defined CYGWIN32
    while(waitpid(-1,NULL,WNOHANG)>0);
#else
    while(wait3(NULL,WNOHANG,NULL)>0);
#endif
    signal(SIGCHLD,sig_chld);
}
#endif


/*! \brief Remove all pvs from one ioc from the pv hashtable
 *
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param iocName - name of the IOC
 */
static int remove_all_pvs(pIoc *pI)
{
	pvE 	*pve;
	const char    *pvNameStr;
	char checkStr[PV_NAME_SZ];
	int		ct=0;
	pvE		*pvEHeartbeat=0;
	const char	*iocName;
	int      removeAll=1;
	pvE 	*pve2;

	if(!pI) {
		log_message(ERROR, "NOT IN TABLE\n");fflush(stderr);
		return (-1);
	}
	iocName = pI->get_iocname();
	log_message(INFO,"Deleting all pvs for %s\n", iocName); fflush(stdout);

    // If ioc has a pvname list file
	// Don't remove the heartbeat so we know when the ioc comes back up.
    if ( pI->get_pathToList() && strlen(pI->get_pathToList()) ) {
        if (iocName) sprintf(checkStr,"%s%s",iocName, HEARTBEAT);
        removeAll=0;
    }

    // get removes first item from list
    while ( (pve = pI->get_pvE()) ) {
		pvNameStr = pve->get_name();
		if ( removeAll || strcmp(checkStr,pvNameStr)) {
//log_message(INFO,"Removing %s from %s\n", pvNameStr,iocName); fflush(stdout);
 			stringId id2(pvNameStr, stringId::refString);
 			pve2 = pCAS->stringResTbl.lookup(id2);
 			// if there is not a new pve (PV has not moved to another ioc)
 			if(pve == pve2) {
 				pve2 = pCAS->stringResTbl.remove(id2);
 			}
 			if(pve) {
 				delete pve;
 				ct--;
 			}
			else {
				log_message(ERROR,"failed to remove %s\n", pvNameStr);fflush(stderr);
			}
		}
		else {
		    if ( !strcmp(checkStr,pvNameStr)) {
//log_message(INFO, "heartbeat found while removing pvs %s from %s\n",checkStr,iocName); fflush(stdout);
				pvEHeartbeat = pve;
			}
		}
    }
	if (pvEHeartbeat) {
//log_message(INFO, "adding heartbeat back into %s pvEList\n",iocName); fflush(stdout);
		// Add heartbeat pvE back into pvEList
		pI->add(pvEHeartbeat);
	}
//log_message(INFO, "PV count is %d for ioc %s \n",ct,iocName); fflush(stdout);
	return ct;
}


/*! \brief Add all pvs from an ioc to the pv hashtable
 *
 * All pv's are inserted on nameserver startup
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param iocName - name of the IOC
 */
static int add_all_pvs(pIoc *pI)
{
	int count;
	const char *pathToList;
	FILE *pf;

	log_message(INFO,"Adding all pvs for %s\n", pI->get_iocname());
	fflush(stdout);

	pathToList = pI->get_pathToList();
	if (!pathToList) return -1;
	pf = reserve_fd_fopen(pathToList, "r");
	if (!pf) {
		log_message(ERROR, "file access problem with file=\"%s\" because \"%s\"\n",
			pathToList, strerror(errno));
		return (-1);
	}
	count =  parseDirectoryFP (pf, pI);
	reserve_fd_fclose (pf);
	//log_message(INFO,"File: %s:   PV count: %d\n", pathToList,count);

	return count;
}

bool identicalAddress ( struct sockaddr_in ipa1, struct sockaddr_in ipa2 )
{
    if ( ipa1.sin_addr.s_addr == ipa2.sin_addr.s_addr ) {
        if ( ipa1.sin_port == ipa2.sin_port) {
            if ( ipa1.sin_family == ipa2.sin_family ) {
                return 1;
            }
        }
    }
    return 0;
}

static char *basename_st(char *filename)
{
   char *pbackslash = strrchr(filename,'\\');
   char *pslash = strrchr(filename,'/');
   if (pslash ) return pslash+1;
   if (pbackslash ) return pbackslash+1;
   return (char *)filename;
}

static char *dirname_st(char *filename)
{
   char *pslash = strrchr(filename, '/');
   char *pbackslash = strrchr(filename, '\\');
   if (pslash ) *pslash = '\0';
   if (pbackslash ) *pbackslash = '\0';
   return (char *)filename;
}

static char *iocname(int isFilname,char *pPath) {
 return filenameIsIocname ? \
	basename_st((char *)pPath): basename_st(dirname_st((char *)pPath));
}

 
