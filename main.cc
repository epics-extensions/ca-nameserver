/*! \file main.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/

#ifdef linux
#include <sys/wait.h>
#include <time.h>
#endif
#ifdef SOLARIS
#include <sys/wait.h>
#endif

#include <libgen.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
//
#include <unistd.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <fdmgr.h>
#include <fdManager.h>
#include <envDefs.h>
#include "directoryServer.h"

#ifndef TRUE
#define TRUE 1
#endif

//prototypes
int parseDirectoryFile (const char *pFileName);
int parseDirectoryFP (FILE *pf, const char *pFileName, int first);
void start_ca_monitor();
extern "C" void registerCA(void *pfdctx,int fd,int condition);
extern void remove_CA_mon(int fd);	//!< CA utility
extern void add_CA_mon(int fd);		//!< CA utility
extern "C" void processChangeConnectionEvent( struct connection_handler_args args);
extern "C" void WDprocessChangeConnectionEvent( struct connection_handler_args args);
extern "C" void sig_chld(int);
extern "C" void kill_the_kid(int);
extern "C" void sig_dont_dump(int);
int start_daemon();
void setup_logging(char *log_file);
int remove_all_pvs(const char *hostname);
int add_all_pvs(const char *hostName);

// globals
directoryServer	*pCAS;
pid_t child_pid;		//!< pid of the child process
pid_t parent_pid;		//!< pid of the parent process
int outta_here;			//!< flag indicating kill signal has been received
int start_new_log;		//!< flag indicating user wants to start a new log
int connected_iocs;		//!< count of iocs which are currently connected
int requested_iocs;		//!< count of iocs which we would like to be connected
int verbose = 0;		//!< verbose mode off = 0, on = 1
FILE *never_ptr;
int FileWait = 0;		//!< count of hosts writing signal.list files
int filenameIsIocname = 0;  //!< Signal list filename is iocname (else basename dirname)


/*! \brief Initialization and main loop
 *
*/
extern int main (int argc, char *argv[])
{
	// Runtime option args:
	unsigned 	debugLevel = 0u;
	char		fileName[128];					//!< default input filename
	char		log_file[128];					//!< default log filename
	char		home_dir[128];					//!< default home directory
	char*		pvlist_file=0;					//!< default pvlist filename
	char*		access_file=0;					//!< default access security filename
	int			server_mode = 0;				//!< running as a daemon = 1
	unsigned 	hash_table_size = 0;			//!< user requested size
	aitBool		forever = aitTrue;
	osiTime		begin(osiTime::getCurrent());
	int			logging_to_file = 0;			//!< default is logging to terminal
	//int			nPV = DEFAULT_HASH_SIZE;		//!< default pv hash table size
	int			pv_count;						//!< count of pv's in startup lists
	int			daemon_status;					//!< did the forks work?
	struct 		timeval first;					//!< time loading begins
	struct 		timeval second;					//!< time loading ends
	struct 		timeval lapsed;					//!< loading time
	struct 		timezone tzp;

	strcpy(fileName, "pvDirectory.txt");
	strcpy(home_dir, "./");
	strcpy(log_file, "log.file");

	// Parse command line args.
	//extern char *optarg;
	//char *errstr;
	int c;
    while ((c = getopt(argc, argv, "a:p:d:h:f:l:c:nsv")) != -1){
        switch (c) {
           case 'v':
                verbose = 1;
                break;
           case 'd':
                debugLevel = atoi(optarg);
                break;
           case 'f':
                strncpy(fileName, optarg, 127);
                break;
           case 'c':
                strncpy(home_dir, optarg, 127);
				break;
           case 'l':
                strncpy(log_file, optarg,127);
				fprintf(stdout,"logging to file: %s\n",log_file);
				logging_to_file = 1;
                break;
           case 'p':
                pvlist_file=optarg;
                fprintf(stdout,"pvlist file: %s\n",pvlist_file);
                break;
           case 'a':
                access_file=optarg;
                fprintf(stdout,"access file: %s\n",access_file);
                break;
           case 's':
                server_mode = 1;
				logging_to_file = 1;
                break;
           case 'h':
                hash_table_size = atoi(optarg);;
                break;
           case 'n':
                filenameIsIocname = 1;
                break;
           case '?':
				fprintf (stderr, "usage: %s [-d<debug level> -f<PV directory file> -a<access security file> -p<pv list file> -l<log file> -s -h<hash table size>] [-c cd to -v]\n", argv[0]);
        		exit(-1);
        }
    }

	if(server_mode) {
		fprintf(stdout, "Starting daemon\n");
		daemon_status = start_daemon();
		if(daemon_status) {
			exit(0);
		}
	}
	else{
		parent_pid=getpid();
	}

    // Go to nameserver's home directory now
    if(home_dir) {
        if(chdir(home_dir)<0) {
            perror("Change to home directory failed");
            fprintf(stderr,"-->Bad home <%s>\n",home_dir); fflush(stderr);
            return -1;
        }
    }


	// Create the kill and restart scripts
#define NS_SCRIPT_FILE "nameserver.killer"
#define NS_RESTART_FILE "nameserver.restart"
	FILE *fd;
    if((fd=fopen(NS_SCRIPT_FILE,"w"))==(FILE*)NULL) {
        fprintf(stderr,"open of script file %s failed\n", NS_SCRIPT_FILE);
        fd=stderr;
    }

	int sid=getpid();
    fprintf(fd,"\n");
    fprintf(fd,"# options:\n");
    fprintf(fd,"# home=<%s>\n",home_dir);
    if (pvlist_file) fprintf(fd,"# pvlist file=<%s>\n",pvlist_file);
    if (access_file) fprintf(fd,"# access file=<%s>\n",access_file);
    fprintf(fd,"# log file=<%s>\n",log_file);
    fprintf(fd,"# list file=<%s>\n",fileName);
    fprintf(fd,"# \n");
    fprintf(fd,"# use the following the get a PV summary report in log:\n");
    fprintf(fd,"#    kill -USR1 %d\n",sid);
    fprintf(fd,"# use the following to clear PIOC pvs from the nameserver:\n");
    fprintf(fd,"# \t (valid pvs will automatically reconnect)\n");
    fprintf(fd,"#    kill -USR2 %d\n",sid);

    fprintf(fd,"# \n");
    //fprintf(fd,"# EPICS_CA_ADDR_LIST=%s\n",getenv("EPICS_CA_ADDR_LIST"));
    //fprintf(fd,"# EPICS_CA_AUTO_ADDR_LIST=%s\n",getenv("EPICS_CA_AUTO_ADDR_LIST"));

	const char *ptr;
     ptr = envGetConfigParamPtr(&EPICS_CA_ADDR_LIST);
     if (ptr != NULL) 
		fprintf(fd,"# EPICS_CA_ADDR_LIST=%s\n",ptr);
     else 
		fprintf(fd,"# EPICS_CA_ADDR_LIST is undefined\n");
     ptr = envGetConfigParamPtr(&EPICS_CA_AUTO_ADDR_LIST);
     if (ptr != NULL) 
		fprintf(fd,"# EPICS_CA_AUTO_ADDR_LIST=%s\n",ptr);
     else 
		fprintf(fd,"# EPICS_CA_AUTO_ADDR_LIST is undefined\n");

    fprintf(fd,"\nkill %d # to kill everything\n\n",parent_pid);
    fprintf(fd,"\n# kill %d # to kill off this server\n\n",sid);
    fflush(fd);

    if(fd!=stderr) fclose(fd);
    chmod(NS_SCRIPT_FILE,00755);

    if((fd=fopen(NS_RESTART_FILE,"w"))==(FILE*)NULL)
    {
        fprintf(stderr,"open of restart file %s failed\n",
            NS_RESTART_FILE);
        fd=stderr;
    }
    fprintf(fd,"\nkill %d # to kill off this gateway\n\n",sid);
    fflush(fd);

    if(fd!=stderr) fclose(fd);
    chmod(NS_RESTART_FILE,00755);

	gettimeofday(&first, &tzp);

	if(logging_to_file) {
		setup_logging(log_file);
	}

	fprintf(stdout,"Start time: %s\n", ctime(&first.tv_sec));
	fflush(stdout);
	fflush(stderr);

	// Initialize the server and hash tables
	if(hash_table_size) {
		pCAS = new directoryServer(hash_table_size);
	}
	else {
		pCAS = new directoryServer(DEFAULT_HASH_SIZE);
	}
	if (!pCAS) {
		return (-1);
	}

	// Setup Access Security
	pCAS->as= new gateAs(pvlist_file,access_file);

	// Enable watchdog monitoring
	start_ca_monitor();

	outta_here = 0;
	start_new_log = 0;


	// Read the signal lists and fill the hash tables
	pv_count = parseDirectoryFile (fileName);

	fprintf(stdout, "Total PVs in signal lists: %d\n", pv_count);

	pCAS->setDebugLevel(debugLevel);

	// Get startup timing information.
	gettimeofday(&second, &tzp);
	fprintf(stdout,"START time: %s\n", ctime(&second.tv_sec));
    if (first.tv_usec > second.tv_usec) {
              second.tv_usec += 1000000;
              second.tv_sec--;
    }
    lapsed.tv_usec = second.tv_usec - first.tv_usec;
    lapsed.tv_sec  = second.tv_sec  - first.tv_sec;
	fprintf(stdout,"Load time: %ld sec %ld usec\n", lapsed.tv_sec, lapsed.tv_usec);
	fflush(stdout);
	fflush(stderr);


	// Main loop
	if (forever) {
		osiTime	delay(1000u,0u);
		//osiTime delay(0u,10000000u);     // (sec, nsec) (10 ms)
		osiTime	purgeTime(30u,0u);	// set interval between purge checks to 30 seconds 
		osiTime	tooLong(300*1u,0u);	// remove from pending list after 5 minutes
		while (!outta_here) {
			fileDescriptorManager.process(delay);
			ca_pend_event(1e-12);
			//ca_poll();

			// If an ioc is reconnecting,
			// see if the ioc finished writing the signal.list file?
#ifdef JS_FILEWAIT
			if(FileWait){
				struct stat sbuf;			
				tsSLIterRm<filewait> iter2(pCAS->fileList);
				filewait *pFW;
				int size = 0;
				// For each ioc on the linked list of reconnecting iocs
				while((pFW=iter2()))  {
					char file_to_wait_for[100];
					sprintf(file_to_wait_for,"./iocs/%s",pFW->get_hostname());
					// We're waiting to get the filesize the same twice in a row.
					// This is at best a poor test to see if the ioc has finished writing signal.list.
					// Better way would be to find a token at the end of the file but signal.list
					// files are also used by the old nameserver and can't be changed.
					if(stat(file_to_wait_for, &sbuf) == 0) {
						// size will be 0 first time thru
						// So at least we will have a delay in having to get here a second time.
						size = pFW->get_size();
						if((sbuf.st_size == size) && (size != 0)) {
							printf("SIZE UNEQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
							add_all_pvs(pFW->get_hostname()); 
							iter2.remove();
							delete pFW;
							FileWait--;
							pFW->set_size(0);
						}
						else{
							pFW->set_size(sbuf.st_size);
							printf("UNEQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
						}
					}
					else {
						printf("Stat failed for %s because %s\n", file_to_wait_for, strerror(errno));
					}
				}
			}
#endif

			if(start_new_log) {
#ifdef PIOC
				remove_all_pvs("opbat1");
				remove_all_pvs("opbat2");
				remove_all_pvs("devsys01");
				system("rm -r /usr/local/bin/nameserver/piocs");
				start_new_log = 0;
				printf("REMOVE DONE\n");
#else
				start_new_log = 0;
				setup_logging(log_file);
#endif
			}
			// Is it time to purge the list of pending pv connections?
			osiTime		now(osiTime::getCurrent());
			if((now - begin) > purgeTime) {
				// reset the timer.
				begin = osiTime::getCurrent();

				// Look for purgable pv's.
				tsSLIterRm<namenode> iter1(pCAS->nameList);
				namenode *pNN;
				while((pNN=iter1()))  {
					if((pNN->get_otime() + tooLong < now )) {
						if(pNN->get_rebroadcast()) {

							// Rebroadcast for heartbeat
							chid tchid;
							int chid_status = ca_state(pNN->get_chid());
							//fprintf(stdout, "1. chid_status: %d name: %s\n", 
							//	chid_status, pNN->get_name());fflush(stdout);
							if(chid_status != 3) {
								int stat = ca_clear_channel(pNN->get_chid());
								ca_flush_io();
								if(stat != ECA_NORMAL) {
									fprintf(stderr, "Purge: Can't clear channel for %s\n", pNN->get_name());
									fprintf(stderr, "Error: %s\n", ca_message(stat));
								}
								else {
									stat = ca_search_and_connect((char *)pNN->get_name(), &tchid, 
										WDprocessChangeConnectionEvent, 0);
									if(stat != ECA_NORMAL) {
										fprintf(stderr, "Purge: Can't search for %s\n", pNN->get_name());
									} else {
										pNN->set_chid(tchid);
										pNN->set_otime();
										//fprintf(stdout, "Purge: New search for %s\n", pNN->get_name());
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
						//fprintf(stdout, "2. chid_status: %d name: %s\n", 
						//	chid_status, pNN->get_name());fflush(stdout);
						if(chid_status != 3) {
							int stat = ca_clear_channel(pNN->get_chid());
							ca_flush_io();
							if(stat != ECA_NORMAL) 
								fprintf(stdout,"Channel clear error for %s\n", pNN->get_name());
						}
						fflush(stdout);
						iter1.remove();
						delete pNN;
					} // end purge
				} // end iter looking for things to purge
			} // end if time to purge
		} // end while(!outta_here)
	}
	pCAS->show(2u);
	delete pCAS;
	return (0);
}

/*! \brief logging code shamelessly stolen from gateway code!
 *
 * \param log_file - name to use for log file
*/
void setup_logging(char *log_file)
{
	struct		stat sbuf;			//old logfile info
	char 		cur_time[200];
	time_t t;

	if(stat(log_file, &sbuf) == 0) {
		if(sbuf.st_size > 0) {
			time(&t);
			sprintf(cur_time,"%s.%lu",log_file,(unsigned long)t);
			if(link(log_file,cur_time)<0) {
				fprintf(stderr,"Failure to move old log to new name %s",
					cur_time);
			}
			else{
				unlink(log_file);
			}
		}
	}
	if( (freopen(log_file,"a",stderr))==NULL ) {
		fprintf(stderr,"Redirect of stderr to file %s failed\n",log_file);
			fflush(stderr);
	}
	if( (freopen(log_file,"a",stdout))==NULL ) {
		fprintf(stderr,"Redirect of stdout to file %s failed\n",log_file);
		fflush(stderr);
	}
}

/*! \brief Fork twice to create a daemon process with no associated terminal
 *
*/
int start_daemon()
{
	signal(SIGCHLD, sig_chld);
	switch(fork()) {
		case -1:	//error
			perror("Cannot create daemon process");
			return -1;
		case 0:		//child
			setpgrp();
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
			fprintf(stderr, "Abort on SIGBUS\n"); fflush(stderr);
			break;
		case SIGSEGV:
			fprintf(stderr, "Abort on SIGSEGV\n"); fflush(stderr);
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

/*! \brief Process the input directory file.
 *
 * Open the specified input file which contains a list of pathnames
 * to pv list files called signal.list. The  parent directories of list files
 * must be the host name for all pvs in the list.
 * For example, "/xxx/xxx/iocmag/signal.list" contains all pvs on iocmag.
 *
 * \param  pFileName - name of the input directory file.
*/
int parseDirectoryFile (const char *pFileName)
{

	FILE	*pf, *pf2;
	int	count = 0;
	char input[200];
struct 		timeval second;					//!< time loading ends
struct 		timezone tzp;

	// Open the user specified input file or the default input file(pvDirectory.txt).
	pf = fopen(pFileName, "r");
	if (!pf) {
		fprintf(stderr, "file access problems with file=\"%s\" because \"%s\"\n",
			pFileName, strerror(errno));
		return (-1);
	}

	// Open each of the files listed in the specified file.
	while(TRUE) {
		if (fscanf(pf, " %s ", input) == 1) {
			if(input[0] == '#') {
				continue;
			}
			pf2 = fopen(input, "r");
    		if (!pf2) { 
				fprintf(stderr, "file access problems %s %s\n", 
					input, strerror(errno));
        		continue;
    		}
			count =  count + parseDirectoryFP (pf2, input, 1);
			fclose (pf2);
		}
		else break;
	}
	fclose (pf);
	return count;
}

/*! \brief Process the 'signal.list' files
 *
 * Add host to ioc hash table.
 * Create watchdog channels.
 * Read the PV name entries from the signal.list file.
 * Populate the pv hash table.
 *
 * \param pf - pointer to the open 'signal.list' file
 * \param pFileName - full path name to the file
 * \param startup_flag - 1=initialization, 0=reconnect
*/
int parseDirectoryFP (FILE *pf, const char *pFileName, int startup_flag)
{
	namenode *pNN;
	pHost 	*pHcheck;
	char 	pvNameStr[PV_NAME_SZ];
	char 	tStr[PV_NAME_SZ];
	struct 	sockaddr_in ipa;
	char 	shortHN[HOST_NAME_SZ];
	chid 	chd;
	int 	count = 0;
	int 	status;
	int 	have_a_heart = 0;
	int 	removed = 0;

	// Set network info 
	if( filenameIsIocname) {
		strncpy(shortHN,  basename((char *)pFileName), PV_NAME_SZ-1);
	} else {
		if( strcmp(shortHN,SIG_LIST)==0 || !filenameIsIocname) {
			strncpy(shortHN,  basename(dirname((char *)pFileName)), PV_NAME_SZ-1);
		}
	}
	memset((char *)&ipa,0,sizeof(ipa));
	ipa.sin_family = AF_INET;
	ipa.sin_port = 0u; // use the default CA server port
	status = aToIPAddr (shortHN, 0u, &ipa);
	if (status) {
		fprintf (stderr, "Unknown host %s in %s \n", shortHN, pFileName);
		return 0;
	}
	requested_iocs++;

	// Install the heartbeat signal as a watchdog to get disconnect
	// signals to invalidate the IOC. DON'T do this on a reconnect
	// because we won't have deleted the heartbeat signal.
	if( startup_flag) {
		stringId id(shortHN, stringId::refString);
		pHcheck = pCAS->hostResTbl.lookup(id);
		if(pHcheck) {
			fprintf(stderr,"Duplicate in list: %s\n", pFileName);
			return(0);
		}
		sprintf(tStr,"%s%s", shortHN,HEARTBEAT);
		status = ca_search_and_connect(tStr,&chd, WDprocessChangeConnectionEvent, 0);
		ca_flush_io();
		if (status != ECA_NORMAL) {
			fprintf(stderr,"1 ca_search failed on channel name: [%s]\n",tStr);
			return(0);
		}
		else {
			pCAS->installHostName(shortHN,pFileName, ipa);
			pCAS->installPVName( tStr, shortHN);

            // Add this pv to the list of pending pv connections
			pNN = new namenode(tStr, chd, 1);
            pNN->set_otime();
            if(pNN == NULL) {
                    fprintf(stderr,"Failed to create namenode %s\n", tStr);
            }
			else {
            	pCAS->addNN(pNN);
			}
		}
		//ca_flush_io();
	}


	while (TRUE) {
		// Read the PV name entries until end of file.
		if (fscanf(pf, " %127s ", pvNameStr) != 1) {
			break;
		}
		// Add the pv to the pv hash table. This will always fail for 
		// xxx:heartbeat. On first_connect, we've already installed it. 
		// On reconect, the heartbeat is still in the hashtable.
		// This is faster than doing a strcmp on every signal read from the file.
		int failed = pCAS->installPVName( pvNameStr, shortHN);
		if(failed == 1 ) {
			have_a_heart ++;
		}
		else if(failed == -1){
			fprintf(stderr,"HB ERROR! FAILED TO INSTALL %s\n", pvNameStr);
		}
		count++;
	}

	printf("%s ct: %d\n", shortHN, count);

	// If the signal.list file did not contain a heartbeat, clean up.
	// This seems like too much work, but it does avoid having to do strncmp's
	// during startup. Also, in practice, this should rarely be called.
	if(startup_flag && !have_a_heart) {
		requested_iocs--;
		fprintf(stderr, "NO HEARTBEAT in %s\n", pFileName);
		tsSLIterRm<namenode> iter(pCAS->nameList);
		while((pNN=iter()))  {
			if(!strcmp(pNN->get_name(), tStr )) {
				iter.remove();
				delete pNN;
				break;
			}
		}

		int chid_status = ca_state(chd);
		fprintf(stdout, "3. chid_status: %d \n", chid_status);fflush(stdout);
		if(chid_status != 3)
			ca_clear_channel(chd);
    	stringId id(shortHN, stringId::refString);
		pHost *pH;
        pH= pCAS->hostResTbl.lookup(id);

		// Janet Anderson found problem on startup with missing heartbeat:
		// At this point, if we call remove_all_pvs, it will fail because
		// we haven't copied signal.list to ./iocs. It is normally copied
		// when an ioc goes down. We'd rather not copy all signal.list files
		// on startup so we can keep startup time as short as possible,
		// we'll copy just the ones we need now.

		mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
        mkdir ("./iocs", mode);

        char ctmp[100];
        sprintf(ctmp, "./iocs/%s",  shortHN);
        mkdir (ctmp, mode);

        char cmd[100];
        sprintf(cmd, "cp %s ./iocs/%s/%s", pH->get_pathToList(), shortHN, SIG_LIST);
        fprintf(stdout, "cmd:<%s>\n",  cmd);fflush(stdout);
        system(cmd);
		// end bug fix


        if(pH) {
            removed = remove_all_pvs(shortHN);
			delete pH; 
        }
		else { printf("lookup failed for %s\n", shortHN);}
	}
	return count - removed; 

}

/*! \brief Get ready to monitor channel access signals
 *
*/
void start_ca_monitor()
{
    void *pfdctx;

    SEVCHK(ca_task_initialize(),
        "initializeCA: error in ca_task_initialize");
    pfdctx = (void *) fdmgr_init();
    SEVCHK(ca_add_fd_registration(registerCA,pfdctx),
        "initializeCA: error adding CA's fd to X");
}

/*! \brief Callback for watchdog connection events
 *
 * These are events which will cause change to the IOC hash table.
*/
extern "C" void WDprocessChangeConnectionEvent(struct connection_handler_args args)
{

	char shortHN[HOST_NAME_SZ];
	char pvname[PV_NAME_SZ];
	pHost *pH;
	namenode *pNN;
#ifdef JS_FILEWAIT
	filewait *pFW;
#endif
	int len, i;
	struct      timeval first;
    struct      timezone tzp;

	strncpy(pvname, ca_name(args.chid),PV_NAME_SZ-1);

	strncpy(shortHN, ca_host_name(args.chid),HOST_NAME_SZ-1);
	len = strlen(shortHN);
	for(i=0; i<len; i++){
		if(shortHN[i] == HN_DELIM || shortHN[i] == HN_DELIM2 ){
			shortHN[i] = 0x0;
			break;
		}
	}

	stringId id(shortHN, stringId::refString);
	if (args.op == CA_OP_CONN_DOWN) {
		pH= pCAS->hostResTbl.lookup(id);
		if(pH) {
				pH->set_status(2);
		}
		connected_iocs --;
		fprintf(stdout, "WATCHDOG CONN DOWN for %s\n", shortHN);
		gettimeofday(&first, &tzp);
    	fprintf(stdout,"*********DOWN time: %s\n", ctime(&first.tv_sec));

		// Copy old signal.list file to a safe place so it can be used later to
		// clean up the PV hashtable.
		// ex: cp /cs/op/iocs/iocnl1/signal.list ./iocs/iocnl1
		mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
		mkdir ("./iocs", mode);

		char ctmp[100];
		sprintf(ctmp, "./iocs/%s",  shortHN);
		mkdir (ctmp, mode);

		char cmd[100];
		sprintf(cmd, "cp %s ./iocs/%s/%s", pH->get_pathToList(), shortHN, SIG_LIST);
        fprintf(stdout, "cmd:<%s>\n",  cmd);fflush(stdout);
        system(cmd);


		// Add this heartbeat pv back to the list of pending pv connections
		pNN = new namenode(pvname, args.chid, 1);
		pNN->set_otime();
		if(pNN == NULL) {
			fprintf(stderr,"Failed to create namenode %s\n", pvname);
		}
		pCAS->addNN(pNN);
	}
	else{
		connected_iocs ++;
		fprintf(stdout,"WATCHDOG CONN UP for <%s> on %s state: %d\n", 
			ca_name(args.chid), ca_host_name(args.chid), ca_state(args.chid));
		gettimeofday(&first, &tzp);
    	fprintf(stdout,"*********UP time: %s", ctime(&first.tv_sec));
		fflush(stdout);

		// Find  and remove this pv from the list of pvs
		// with pending connections. Delete the node.
		tsSLIterRm<namenode> iter(pCAS->nameList);
		int found = 0;
		while((pNN=iter()))  {
			if(!strcmp(pNN->get_name(), pvname )) {
				//printf("Removed %s from pending\n", pvname);
				iter.remove();
				delete pNN;
				found = 1;
				break;
			}
		}
		if(!found){
			fprintf(stdout, "Cannot remove from pending: <%s>\n", pvname);
			fflush(stdout);
		}
		pH= pCAS->hostResTbl.lookup(id);
		if(pH) {
			// Confirm port number.
			char tstr[60];
			strcpy(tstr,ca_host_name(args.chid));
			len = strlen(tstr);
			char cport[8];
			int port;
			cport[0] = tstr[len-4];
			cport[1] = tstr[len-3];
			cport[2] = tstr[len-2];
			cport[3] = tstr[len-1];
			cport[4] = 0x0;
			port = atoi(cport);
			if(port != 0) {
				fprintf(stdout, "setting host %s to port %d\n",
					ca_host_name(args.chid), port);
				pH->setPort(port);
			}


			// initial connection
			if (pH->get_status() == 0) { 
				pH->set_status(1);
			}
			// reconnection
			else if (pH->get_status() == 2) { 
				remove_all_pvs(shortHN);
				add_all_pvs(shortHN); 
#ifdef JS_FILEWAIT
				// Add this host to the list of those to check status of file
				// to prevent reading signal.list until writing by ioc is complete.
				pFW = new filewait(shortHN, 0);
				if(pFW == NULL) {
					fprintf(stderr,"Failed to create filewait node %s\n", shortHN);
				}
				else {
					pCAS->addFW(pFW);
				}
				FileWait++;
#endif
			} 
		}
		else { 
			fprintf(stderr,"WDCONN UP ERROR: Host %s NOT installed\n", shortHN); 
			fflush(stderr);
		}
	}
}


/*! \brief  CA callback after a search broadcast.
 *
 \verbatim  
	Handles connection events for pvs  which are not in any signal.list file.
 	On disconnect, sets host status to 2.
  	On connect or reconnect: 
        1. Get info
                isHeartbeat?
                is the host alreadyInHostTable?
                is hostUp?
        2. If(isHeartbeat && alreadyInHostTable)
                remove_all_pvs(shortHN);
        3. Install pv
        4. Remove pv from pending list
        5. If(isHeartbeat)
                if(alreadyInHostTable) setPort
                else install host
            set status to 1
            connected_iocs ++
        6. Append to signal.list file
                if necessary, create signal.list file 
        7. If(!isHeartbeat) ca_clear_channel
        8. If( !isHeartbeat && !alreadyInHostTable)
                if (heartbeat for this host is not on pending list)
                    broadcast for heartbeat
                    add heartbeat to pending list
\endverbatim
*/
extern "C" void processChangeConnectionEvent(struct connection_handler_args args)
{
	int status;
	struct sockaddr_in ipa;
	pHost *pHost;
    char pvNameStr[PV_NAME_SZ];
	char shortHN[HOST_NAME_SZ];
	char portStr[8];
	char checkStr[PV_NAME_SZ];
	char *find;
	unsigned int portNumber;

	strncpy(shortHN, ca_host_name(args.chid),HOST_NAME_SZ-1);
	int len = strlen(shortHN);
	for(int i=0; i<len; i++){
		if(shortHN[i] == HN_DELIM || shortHN[i] == HN_DELIM2 ){
			shortHN[i] = 0x0;
			break;
		}
	}
	stringId id(shortHN, stringId::refString);
	sprintf(checkStr,"%s%s",shortHN,HEARTBEAT);

	if (args.op == CA_OP_CONN_DOWN) {
		if(strcmp(checkStr,ca_name(args.chid))) {
			return;
		}
		fprintf(stdout,"CONN DOWN for %s\n", ca_name(args.chid)); fflush(stdout);
		connected_iocs --;
		pHost = pCAS->hostResTbl.lookup(id);
		if(pHost) { 
			pHost->set_status(2); 
		}
		else { 
			fprintf(stderr,"CONN DOWN ERROR: HOST %s NOT INSTALLED \n", shortHN); 
			fflush(stderr);
		}
	}
	else{
		// Got a response to UDP broadcast for a pv not already in the pv hash table. 
		fprintf(stdout,"CONN UP for <%s> on <%s> state: %d\n", 
			ca_name(args.chid), ca_host_name(args.chid), ca_state(args.chid));

		// 1.___________________________________________
		int isHeartbeat = 0;
		int alreadyInHostTable = 0;
		int hostUp = 0;
		if(!strcmp(checkStr,ca_name(args.chid))) {
			isHeartbeat = 1;
		}
		pHost = pCAS->hostResTbl.lookup(id);
		if(pHost) {
			alreadyInHostTable = 1;
			if (pHost->get_status() == 1) 
				hostUp = 1;
		}
		fprintf(stdout,"isHeartbeat: %d alreadyInHostTable: %d hostUp: %d\n",
			isHeartbeat, alreadyInHostTable, hostUp);fflush(stdout);

		// 2.___________________________________________
		// This is a reconnect.
		if(isHeartbeat && alreadyInHostTable)
			remove_all_pvs(shortHN);

		// 3.___________________________________________
		// Always install this pv into the pv hash table
		memset((char *)&ipa,0,sizeof(ipa));
		ipa.sin_family = AF_INET;
		find = index(ca_host_name(args.chid),':');
		if(find){
			strncpy(portStr, find+1, 4);
			portStr[4] = 0x0;
			portNumber = atoi(portStr);
			ipa.sin_port = htons((aitUint16) portNumber);
			printf("portNumber: %d\n", portNumber);
		} else {
			ipa.sin_port = 0u;
			printf("portNumber: 0u\n");
		}
		status = aToIPAddr (shortHN, ipa.sin_port, &ipa);
		if (status) {
			fprintf (stderr, "Unknown host name: %s with PV: %s\n",
					shortHN, ca_name(args.chid));
		}
		strncpy (pvNameStr, ca_name(args.chid),PV_NAME_SZ-1);
		pCAS->installPVName( pvNameStr, shortHN);

		// 4.___________________________________________
		// Always remove this pv from the list of pending conections.
		{
			tsSLIterRm<namenode> iter(pCAS->nameList);
			namenode *pNN;
			int found = 0;
			while((pNN=iter()))  {
				if(!strcmp(pNN->get_name(), pvNameStr )) {
					if(verbose)printf("Removed %s from pending\n", pvNameStr);
					iter.remove();
					delete pNN;
					found = 1;
					break;
				}
			}
			if(!found) fprintf(stderr, "Unable to purge %s\n", pvNameStr);
		}

		// 5.___________________________________________
		if(isHeartbeat) {
			// If host is in the host table, this is a reconnect.
			if(alreadyInHostTable) {
				// The port number may have changed on reconnect.
				char tstr[60];
				strcpy(tstr,ca_host_name(args.chid));
				len = strlen(tstr);
				char cport[8];
				int port;
				cport[0] = tstr[len-4];
				cport[1] = tstr[len-3];
				cport[2] = tstr[len-2];
				cport[3] = tstr[len-1];
				cport[4] = 0x0;
				port = atoi(cport);
				if(port != 0) {
					fprintf(stdout, "setting host %s to port %d\n",
						ca_host_name(args.chid), port);
					pHost->setPort(port);
				}
			}
			// This is an initial connection
			else if(!alreadyInHostTable) {
				char ctmp[100];
				sprintf(ctmp, "./iocs/%s/%s",  shortHN, SIG_LIST);
				pCAS->installHostName(shortHN, ctmp, ipa);

				// Verify that host was installed.
				pHost = pCAS->hostResTbl.lookup(id);
				if(!pHost) {
					fprintf(stderr,"error installing host: %s\n", shortHN);
					pHost->set_status(2);
				}
			}
			if(pHost) {
				pHost->set_status(1);
				connected_iocs ++;
			}
		}
#ifdef PIOC
		// 6.___________________________________________
		// Create or append to signal.list
		// Since we ALWAYS install pv in host table, we must ALWAYS
		// add to signal.list file. Create it if it doesn't exist.
		char pathToList[PATH_NAME_SZ];
		FILE *pf;
		sprintf(pathToList, "./piocs/%s/%s",shortHN,SIG_LIST);
		pf = fopen(pathToList, "a");
		if(pf == NULL) {
			//Create a signal.list file in ./piocs/shortHN
			mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
			mkdir ("./piocs", mode);
			char ctmp[100];
			sprintf(ctmp, "./piocs/%s",  shortHN);
			mkdir (ctmp, mode);
			if(verbose)printf("created %s\n", pathToList);
			pf = fopen(pathToList, "a");
		}
		fprintf(pf, "%s\n", ca_name(args.chid));
		fclose(pf);
		if(verbose) printf("Appended %s to SIG_LIST\n", ca_name(args.chid));
#endif

		// 7.___________________________________________
		// Don't want monitors on anything except heartbeats.
		if( !isHeartbeat) {
			int chid_status = ca_state(args.chid);
			fprintf(stdout, "4. chid_status: %d name: %s\n", chid_status, pvNameStr);fflush(stdout);
			if(chid_status != 3)
				ca_clear_channel(args.chid);
		}

		// 8.___________________________________________
		// Check pending list and broadcast only ONCE for heartbeat.
		if( !isHeartbeat && !alreadyInHostTable) {
			// Is the heartbeat for the ioc on which this pv resides already on the pending list?
			tsSLIter<namenode> iter2(pCAS->nameList);
			namenode *pNN;
			int found = 0;
			while((pNN=iter2()))  {
				if(!strcmp(pNN->get_name(), checkStr )) {
					if(verbose)printf("%s already on b'cast list\n", checkStr);
					found = 1;
					break;
				}
			}
			if(!found) {
				chid chd;
				if(verbose)fprintf(stdout, "b'casting for %s\n", checkStr);
				status = ca_search_and_connect(checkStr,&chd,
					processChangeConnectionEvent, 0);
				if (status != ECA_NORMAL) 
					fprintf(stderr,"ca_search failed on channel name: [%s]\n",checkStr);
				else {
					// Add this pv to the list of pending pv connections
					//namenode *pNN;
					pNN = new namenode(checkStr, chd, 0);
					pNN->set_otime();
					if(pNN == NULL)  fprintf(stderr,"Failed to create namenode %s\n", checkStr);
					else pCAS->addNN(pNN);
				}
			}
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

/*! \brief Callback for death of a child
 *
 * When run as a daemon, death of a child shakes the parent out of pause status.
 * A new child will be started.
*/
extern "C" void sig_chld(int )
{
#ifdef SOLARIS
    while(waitpid(-1,NULL,WNOHANG)>0);
#elseifdef linux
    while(waitpid(-1,NULL,WNOHANG)>0);
#else
    while(wait3(NULL,WNOHANG,NULL)>0);
#endif
    signal(SIGCHLD,sig_chld);
}


/*! \brief Remove all pvs from one host from the pv hashtable
 *
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param hostName - name of the IOC
 */
int remove_all_pvs(const char *hostName)
{
	char 	tname[PATH_NAME_SZ];
	char 	pathToList[PATH_NAME_SZ];
	pHost 	*pH;
	pvE 	*pve;
	FILE	*pf;
	char    pvNameStr[PV_NAME_SZ];
	char    checkStr[PV_NAME_SZ];
#ifdef PIOC
	int		pioc_removal = 0;
	char 	cmd[100];
#endif

	fprintf(stdout,"Deleting all pvs for %s\n", hostName); fflush(stdout);
	stringId id(hostName, stringId::refString);
	pH = pCAS->hostResTbl.lookup(id);
	if(!pH) {
		fprintf(stderr, "NOT IN TABLE\n");fflush(stderr);
		return (-1);
	}

	// use the copy of signal.list we stored in ./iocs when we got the disconnect signal

	strncpy(tname, pH->get_pathToList(),PATH_NAME_SZ-1);
	sprintf(pathToList, "./iocs/%s/%s",basename(dirname(tname)), SIG_LIST);
	printf("remove_all file: %s\n", pathToList);
	pf = fopen(pathToList, "r");

#ifdef PIOC
	if (!pf) {
		// Try the pioc directory
		strncpy(tname, pH->get_pathToList(),PATH_NAME_SZ-1);
		sprintf(pathToList, "./piocs/%s/%s",basename(dirname(tname)), SIG_LIST);
		printf("remove_all file: %s\n", pathToList);
		pf = fopen(pathToList, "r");
		if (!pf) {
			fprintf(stderr, "File access problems with file=\"%s\" because \"%s\"\n",
				pathToList, strerror(errno));fflush(stderr);
			return (-1);
		}
		pioc_removal = 1;
	}
#else
	if (!pf) {
		fprintf(stderr, "File access problems with file=\"%s\" because \"%s\"\n",
			pathToList, strerror(errno));fflush(stderr);
		return (-1);
	}
#endif
	
	int ct = 0;
	while (TRUE) {
		if (fscanf(pf, "%127s", pvNameStr) != 1) {
			fprintf(stdout,"Deleted %d pvs from  %s \n", ct, hostName);fflush(stdout);
			fclose (pf);
#ifdef PIOC
			if(pioc_removal) {
				sprintf(cmd, "rm -r %s", pathToList);
				system(cmd);
				printf("cmd: %s\n", cmd);
			}
#endif
			return ct; // end of file
		}
		// Don't remove the heartbeat so we know when the ioc comes back up.
		sprintf(checkStr,"%s%s",hostName, HEARTBEAT);
		if(strcmp(checkStr,pvNameStr)) {
			stringId id2(pvNameStr, stringId::refString);
			pve = pCAS->stringResTbl.remove(id2);
			if(pve) {
				delete pve;
				ct++;
			}
			else {
				fprintf(stderr,"failed to remove %s\n", pvNameStr);fflush(stderr);
			}
		}
	}
}

/*! \brief Add all pvs from a host to the pv hashtable
 *
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param hostName - name of the IOC
 */
int add_all_pvs(const char *hostName)
{
	int count;
	char pathToList[PATH_NAME_SZ];
	FILE	*pf;
	pHost *pH;

	fprintf(stdout,"Adding all pvs for %s\n", hostName);
	fflush(stdout);

	stringId id(hostName, stringId::refString);
	pH = pCAS->hostResTbl.lookup(id);
	if(!pH) {
		fprintf(stderr,"%s NOT IN TABLE\n", hostName);
		return (-1);
	}

	strncpy(pathToList, pH->get_pathToList(),PATH_NAME_SZ-1);
	pf = fopen(pathToList, "r");
	if (!pf) {
		fprintf(stderr, "file access problem with file=\"%s\" because \"%s\"\n",
			pH->get_pathToList(), strerror(errno));
		return (-1);
	}
	count =  parseDirectoryFP (pf, pathToList, 0);
	//fprintf(stdout,"File: %s PVs: %d\n", basename(dirname(pathToList)),count);

   	pH->set_status(1);
	fclose (pf);
	return 0;
}

