/*! \file main.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/
static char *rcsid="$Header$";


#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef linux
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif
#ifdef SOLARIS
#include <libgen.h>
#include <sys/wait.h>
#endif


#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define WIN32_MAXSTDIO 2048
#else
#include <unistd.h>
#include <sys/resource.h>
#endif

#include <time.h>

#if 0
#include <sys/time.h>
#include <sys/utsname.h>
#include "tsSLList.h"
#endif

#include <envDefs.h>
#include <fdmgr.h>
#include <fdManager.h>

#include "directoryServer.h"

#ifndef TRUE
#define TRUE 1
#endif

#define NS_RESERVE_FILE  "nameserver.reserve"
#define NS_PVLIST_FILE  "nameserver.pvlist"

//prototypes
int parseDirectoryFile (const char *pFileName);
int parseDirectoryFP (FILE *pf, const char *pFileName, int first, pHost* pH);
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
int remove_all_pvs(pHost *pH);
int add_all_pvs(pHost *pH);
FILE *reserve_fd_fopen(const char *filename, const char *mode);
int reserve_fd_fclose(FILE *stream);
FILE *reserve_fd_openReserveFile(void);

// globals
directoryServer	*pCAS;
#ifndef _WIN32
pid_t child_pid;		//!< pid of the child process
pid_t parent_pid;		//!< pid of the parent process
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
int verbose = 0;		//!< verbose mode off = 0, on = 1
FILE *never_ptr;
int filenameIsIocname = 0;  //!< Signal list filename is iocname (else basename dirname)
FILE *reserveFp = NULL;

char *iocname(int isFilname,char *pPath);

/*! \brief Initialization and main loop
 *
*/
extern int main (int argc, char *argv[])
{
	// Runtime option args:
	unsigned 	debugLevel = 0u;
	char		*fileName;					//!< default input filename
	char		*log_file;					//!< default log filename
	char		*home_dir;					//!< default home directory
	char		defaultFileName[] = "pvDirectory.txt";
	char		defaultLog_file[] = "log.file";
	char		defaultHome_dir[] = "./";
	char*		pvlist_file=0;				//!< default pvlist filename
	int			server_mode = 0;				//!< running as a daemon = 1
	unsigned 	hash_table_size = 0;			//!< user requested size
	aitBool		forever = aitTrue;
 	epicsTime	begin(epicsTime::getCurrent());
	int			logging_to_file = 0;			//!< default is logging to terminal
	//int			nPV = DEFAULT_HASH_SIZE;		//!< default pv hash table size
	int			pv_count;						//!< count of pv's in startup lists
#ifndef _WIN32
	int			daemon_status;					//!< did the forks work?
#endif
	epicsTime	first;					//!< time loading begins
	local_tm_nano_sec	ansiDate;					//!< time loading begins
	epicsTime	second;					//!< time loading begins
	double 		lapsed;					//!< loading time
	pHost 		*pH;
	int 		parm_error=0;
	int 		i;
	int c;
#ifndef WIN32
    struct rlimit lim;
#endif

    fileName = defaultFileName;
    log_file = defaultLog_file;
    home_dir = defaultHome_dir;

//printf ("argc=%d\n",argc);

	// Parse command line args.
	for (i = 1; i<argc && !parm_error; i++) {
//printf ("i=%d  argv=%s\n",i,argv[i]);
		switch(c = argv[i][1]) {
           case 'v':
                verbose = 1;
                break;
           case 'd':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
						debugLevel=atoi(argv[i]);
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
           case 'l':
				if(++i>=argc) parm_error=1;
				else {
					if(argv[i][0]=='-') parm_error=2;
					else {
                        log_file = argv[i];
						fprintf(stdout,"logging to file: %s\n",log_file);
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
                		fprintf(stdout,"pvlist file: %s\n",pvlist_file);
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
           "-p<pvlist file> "
           "-l<log file> -s -h<hash table size>] [-c cd to -v]\n", argv[0]);
       	exit(-1);
    }


#ifndef _WIN32
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
#endif

    // Go to nameserver's home directory now
    if(home_dir) {
        if(chdir(home_dir)<0) {
            perror("Change to home directory failed");
            fprintf(stderr,"-->Bad home <%s>\n",home_dir); fflush(stderr);
            return -1;
        }
    }

#ifndef _WIN32
	// Create the kill and restart scripts
#define NS_SCRIPT_FILE "nameserver.killer"
#define NS_RESTART_FILE "nameserver.restart"
	FILE *fd;
	if((fd=reserve_fd_fopen(NS_SCRIPT_FILE,"w"))==(FILE*)NULL) {
        fprintf(stderr,"open of script file %s failed\n", NS_SCRIPT_FILE);
        fd=stderr;
    }

	int sid=getpid();
    fprintf(fd,"\n");
    fprintf(fd,"# options:\n");
    fprintf(fd,"# home=<%s>\n",home_dir);
    if (pvlist_file) fprintf(fd,"# pvlist file=<%s>\n",pvlist_file);
    fprintf(fd,"# log file=<%s>\n",log_file);
    fprintf(fd,"# list file=<%s>\n",fileName);
    fprintf(fd,"# \n");
    fprintf(fd,"# use the following the get a PV summary report in log:\n");
    fprintf(fd,"#    kill -USR1 %d\n",sid);
#ifdef PIOC
    fprintf(fd,"# use the following to clear PIOC pvs from the nameserver:\n");
    fprintf(fd,"# \t (valid pvs will automatically reconnect)\n");
#else
    fprintf(fd,"# use the following to start a new logfile\n");
#endif
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

    if(fd!=stderr) reserve_fd_fclose(fd);
    chmod(NS_SCRIPT_FILE,00755);

	if((fd=reserve_fd_fopen(NS_RESTART_FILE,"w"))==(FILE*)NULL)
    {
        fprintf(stderr,"open of restart file %s failed\n",
            NS_RESTART_FILE);
        fd=stderr;
    }
    fprintf(fd,"\nkill %d # to kill off this nameserver\n\n",sid);
    fflush(fd);

    if(fd!=stderr) reserve_fd_fclose(fd);
    chmod(NS_RESTART_FILE,00755);
#endif

    // Increase process limits to max
#ifdef WIN32
    // Set open file limit (512 by default, 2048 is max)
#if DEBUG_OPENFILES
    int maxstdio=_getmaxstdio();
    printf("Permitted open files: %d\n",maxstdio);
    printf("\nSetting limits to %d...\n",WIN32_MAXSTDIO);
#endif
  // This will fail and not do anything if WIN32_MAXSTDIO > 2048
    int status=_setmaxstdio(WIN32_MAXSTDIO);
    if(!status) {
        printf("Failed to set STDIO limit\n");
    }
#if DEBUG_OPENFILES
    maxstdio=_getmaxstdio();
    printf("Permitted open files (after): %d\n",maxstdio);
#endif
#else  //#ifdef WIN32
    // Set process limits
    if(getrlimit(RLIMIT_NOFILE,&lim)<0) {
        fprintf(stderr,"Cannot retrieve the process FD limits\n");
    } else  {
#if DEBUG_OPENFILES
        printf("RLIMIT_NOFILE (before): rlim_cur=%d rlim_rlim_max=%d "
          "OPEN_MAX=%d SC_OPEN_MAX=%d FOPEN_MAX=%d\n",
          lim.rlim_cur,lim.rlim_max,
          OPEN_MAX,_SC_OPEN_MAX,FOPEN_MAX);
        printf("  sysconf: _SC_OPEN_MAX %d _SC_STREAM_MAX %d\n",
          sysconf(_SC_OPEN_MAX), sysconf(_SC_STREAM_MAX));
#endif
        if(lim.rlim_cur<lim.rlim_max) {
            lim.rlim_cur=lim.rlim_max;
            if(setrlimit(RLIMIT_NOFILE,&lim)<0)
              fprintf(stderr,"Failed to set FD limit %d\n",
                (int)lim.rlim_cur);
        }
#if DEBUG_OPENFILES
        if(getrlimit(RLIMIT_NOFILE,&lim)<0) {
            printf("RLIMIT_NOFILE (after): Failed\n");
        } else {
            printf("RLIMIT_NOFILE (after): rlim_cur=%d rlim_rlim_max=%d "
              "OPEN_MAX=%d SC_OPEN_MAX=%d FOPEN_MAX=%d\n",
              lim.rlim_cur,lim.rlim_max,
              OPEN_MAX,_SC_OPEN_MAX,FOPEN_MAX);
            printf("  sysconf: _SC_OPEN_MAX %d _SC_STREAM_MAX %d\n",
              sysconf(_SC_OPEN_MAX), sysconf(_SC_STREAM_MAX));
        }
#endif
    }

    if(getrlimit(RLIMIT_CORE,&lim)<0) {
        fprintf(stderr,"Cannot retrieve the process FD limits\n");
    }
#endif

	//time  in  seconds since 00:00:00 UTC, January 1, 1970.
	first = epicsTime::getCurrent ();

	if(logging_to_file) {
		setup_logging(log_file);
	}

	ansiDate = first;
	fprintf(stdout,"Start time: %s\n", asctime(&ansiDate.ansi_tm));
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

	// Setup broadcast access security
	if (pvlist_file) pCAS->pgateAs = new gateAs(pvlist_file);

	// Enable watchdog monitoring
	start_ca_monitor();

	outta_here = 0;
	start_new_log = 0;


	// Read the signal lists and fill the hash tables
	pv_count = parseDirectoryFile (fileName);

	fprintf(stdout, "Total PVs in signal lists: %d\n", pv_count);

	pCAS->setDebugLevel(debugLevel);

	// Get startup timing information.
	second = epicsTime::getCurrent ();
	ansiDate = second;
	fprintf(stdout,"START time: %s\n", asctime(&ansiDate.ansi_tm));
    lapsed  = second - first;
	fprintf(stdout,"Load time: %f sec\n", lapsed);
	fflush(stdout);
	fflush(stderr);

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
			ca_pend_event(1e-12);
			//ca_poll();

#ifdef JS_FILEWAIT
			// For each ioc on the linked list of reconnecting iocs
			// see if the ioc finished writing the signal.list file?
			struct stat sbuf;			
			filewait *pFW;
			filewait *pprevFW=0;
			int size = 0;
			tsSLIter<filewait> iter2=pCAS->fileList.firstIter();
			while(iter2.valid()) {
					tsSLIter<filewait> tmp = iter2;
					tmp++;
					pFW=iter2.pointer();
					char *file_to_wait_for;
					file_to_wait_for = pFW->get_pHost()->get_pathToList();
					// We're waiting to get the filesize the same twice in a row.
					// This is at best a poor test to see if the ioc has finished writing signal.list.
					// Better way would be to find a token at the end of the file but signal.list
					// files are also used by the old nameserver and can't be changed.
					if(stat(file_to_wait_for, &sbuf) == 0) {
						// size will be 0 first time thru
						// So at least we will have a delay in having to get here a second time.
						size = pFW->get_size();
						if((sbuf.st_size == size) && (size != 0)) {
							//printf("SIZE EQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
							add_all_pvs(pFW->get_pHost()); 
                            if(pprevFW) pCAS->fileList.remove(*pprevFW);
							else pCAS->fileList.get();
							delete pFW;
							pFW =0;
						}
						else{
							pFW->set_size(sbuf.st_size);
							//printf("UNEQUAL %s %d %d\n", file_to_wait_for, size, (int)sbuf.st_size);
						}
					}
					else {
						printf("Stat failed for %s because %s\n", file_to_wait_for, strerror(errno));
					}
					if (pFW) pprevFW = pFW;
					iter2 = tmp;
			}
#endif

			if(start_new_log) {
#ifdef PIOC
				stringId id("opbat1", stringId::refString);
				pH = pCAS->hostResTbl.lookup(id);
				remove_all_pvs(pH);
				stringId id2("opbat2", stringId::refString);
				pH = pCAS->hostResTbl.lookup(id2);
				remove_all_pvs(pH);
				stringId id3("devsys01", stringId::refString);
				pH = pCAS->hostResTbl.lookup(id3);
				remove_all_pvs(pH);
				system("rm -r /usr/local/bin/nameserver/piocs");
				start_new_log = 0;
				printf("REMOVE DONE\n");
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

				// Look for purgable pv's.
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
							pH = (pHost*)ca_puser(pNN->get_chid());
							//fprintf(stdout, "1. chid_status: %d name: %s\n", 
							//	chid_status, pNN->get_name());fflush(stdout);
							if(chid_status != 3) {
							    //enum channel_state {cs_never_conn, cs_prev_conn, cs_conn, cs_closed};
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
										ca_set_puser (tchid, pH);
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
						if (pprevNN) pCAS->nameList.remove(*pprevNN);
						else pCAS->nameList.get();
						delete pNN;
						pNN = 0;
					} // end purge
					if (pNN) pprevNN = pNN;
					iter1 = tmp;
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
#ifndef _WIN32
	struct		stat sbuf;			//old logfile info
	char 		cur_time[200];
	time_t t;

	// Save log file if it exists
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
#endif
	// Redirect stdout and stderr
	// Open it and close it to empty it (Necessary on WIN32,
	// apparently not necessary on Solaris)
	FILE *fp=fopen(log_file,"w");
	if(fp == NULL) {
		fprintf(stderr,"Cannot open %s\n",log_file);
		fflush(stderr);
	} else {
		fclose(fp);
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
#ifndef _WIN32
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
#endif


#ifndef _WIN32
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
#endif
	

/*! \brief  Allow termination of parent process to terminate the child.
 *
*/
#ifndef _WIN32
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
			pf2 = reserve_fd_fopen(input, "r");
    		if (!pf2) { 
				fprintf(stderr, "file access problems %s %s\n", 
					input, strerror(errno));
        		continue;
    		}
			count =  count + parseDirectoryFP (pf2, input, 1, 0);
			reserve_fd_fclose (pf2);
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
int parseDirectoryFP (FILE *pf, const char *pFileName, int startup_flag, pHost *pHostIn)
{
	namenode *pNN;
	pHost 	*pHcheck;
	pHost 	*pH = pHostIn;
	char 	pvNameStr[PV_NAME_SZ];
	char 	tStr[PV_NAME_SZ];
	char 	shortHN[HOST_NAME_SZ];
	chid 	chd;
	int 	count = 0;
	int 	status;
	int 	have_a_heart = 0;
	int 	removed = 0;
	char 	*fileNameCopy;
	char 	*pIocname;

	if (!pFileName) return 0;
	fileNameCopy = strdup(pFileName); 
	if (!fileNameCopy) return 0;
	pIocname = iocname(filenameIsIocname, fileNameCopy);
    strncpy (shortHN,pIocname,HOST_NAME_SZ-1);
    shortHN[HOST_NAME_SZ-1]='\0';

	free(fileNameCopy);
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
		//ca_flush_io();  // connect event may occur before installPVName completes
		if (status != ECA_NORMAL) {
			fprintf(stderr,"1 ca_search failed on channel name: [%s]\n",tStr);
			if (chd) ca_clear_channel(chd);
			return(0);
		}
		else {
			pH = pCAS->installHostName(shortHN,pFileName);
			ca_set_puser (chd, pH);
			pCAS->installPVName( tStr, pH);

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
		ca_flush_io();
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
		int failed = pCAS->installPVName( pvNameStr, pH);
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

		int chid_status = ca_state(chd);
		//fprintf(stdout, "3. chid_status: %d \n", chid_status);fflush(stdout);
		if(chid_status != 3)
			ca_clear_channel(chd);
    	stringId id(shortHN, stringId::refString);

        if(pH) {
            removed = remove_all_pvs(pH);
			pH= pCAS->hostResTbl.remove(*pH);
			if(pH) delete pH; 
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

    SEVCHK(ca_context_create(ca_enable_preemptive_callback),
        "initializeCA: error in ca_context_create");
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
	char pvname[PV_NAME_SZ];
	char *hostname;
	pHost *pH;
	namenode *pNN;
#ifdef JS_FILEWAIT
	filewait *pFW;
#endif
	epicsTime	first;
	local_tm_nano_sec	ansiDate;

	strncpy(pvname, ca_name(args.chid),PV_NAME_SZ-1);
	pH = (pHost*)ca_puser(args.chid);
    hostname = pH->get_hostname();  // this is iocname

	if (args.op == CA_OP_CONN_DOWN) {
		if(pH) pH->set_status(2);
		connected_iocs --;
		fprintf(stdout, "WATCHDOG CONN DOWN for %s\n", hostname);
		first = epicsTime::getCurrent();
		ansiDate = first;
		fprintf(stdout,"*********DOWN time: %s\n", asctime(&ansiDate.ansi_tm));
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
			ca_name(args.chid), hostname, ca_state(args.chid));
//fprintf(stdout,"WATCHDOG CONN UP host=%s \n", pH->get_hostname());
		first = epicsTime::getCurrent();
		ansiDate = first;
		fprintf(stdout,"*********UP time: %s", asctime(&ansiDate.ansi_tm));
		fflush(stdout);

		// Find  and remove this pv from the list of pvs
		// with pending connections. Delete the node.
		tsSLIter<namenode> iter=pCAS->nameList.firstIter();
		int found = 0;
		namenode *pprevNN=0;
		while(iter.valid()) {
			pNN=iter.pointer();
			if(!strcmp(pNN->get_name(), pvname )) {
				//printf("Removed %s from pending\n", pvname);
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
			fprintf(stdout, "Cannot remove from pending: <%s>\n", pvname);
			fflush(stdout);
		}

		if(pH) {
			// Confirm port number.
			pH->set_addr(args.chid);

			// initial connection
			if (pH->get_status() == 0) { 
				pH->set_status(1);
			}
			// reconnection
			else if (pH->get_status() == 2) { 
				remove_all_pvs(pH);
#ifdef JS_FILEWAIT
				// Add this host to the list of those to check status of file
				// to prevent reading signal.list until writing by ioc is complete.
				pFW = new filewait(pH, 0);
				if(pFW == NULL) {
					fprintf(stderr,"Failed to create filewait node %s\n", hostname);
				}
				else {
					pCAS->addFW(pFW);
				}
#else
				add_all_pvs(pH); 
//Begin PROPOSED CHANGE on reconnection ONLY, copy signal.list file to ./iocs
//End PROPOSED CHANGE
#endif
			} 
			else {
			}
		}
		else { 
			fprintf(stderr,"WDCONN UP ERROR: Host %s NOT installed\n", hostname); 
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
                remove_all_pvs(pH);
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
	pHost *pH;
    char pvNameStr[PV_NAME_SZ];
	char *ptr;
	int 	removed = 0;
	char hostname[HOST_NAME_SZ];
	ca_get_host_name(args.chid,hostname,HOST_NAME_SZ);

	if (args.op == CA_OP_CONN_DOWN) {
		connected_iocs --;

		pH = ( pHost * ) ca_puser( args.chid );
		if(pH) {
			pH->set_status(2);
		    fprintf(stdout,"CONN DOWN for %s\n", pH->get_hostname()); fflush(stdout);
			// Remove all pvs now
			// On reconnect this ioc may have a different port
            removed = remove_all_pvs(pH);
            pH= pCAS->hostResTbl.remove(*pH);
            if(pH) delete pH;
		}
		else {
			fprintf(stderr,"CONN DOWN ERROR: HOST %s NOT INSTALLED \n", hostname);
			fflush(stderr);
		}
	    ca_set_puser(args.chid,0);

        // Add this pv to the list of pending pv connections
	    namenode *pNN;
		pNN = new namenode(ca_name(args.chid), args.chid, 1);
        pNN->set_otime();
        if(pNN == NULL) {
            fprintf(stderr,"Failed to create namenode %s\n", ca_name(args.chid));
        }
		else {
           	pCAS->addNN(pNN);
		}
	}
	else{
		// Got a response to UDP broadcast for a pv not already in the pv hash table. 

		int isHeartbeat = 0;

		stringId id(hostname, stringId::refString);
		pH = pCAS->hostResTbl.lookup(id);
		if(!pH) {
			pH = pCAS->installHostName(hostname,0);
			if (!pH) { fprintf(stderr,"error installing host: %s\n",
				hostname);
				return;
			}
			isHeartbeat = 1;
		    fprintf(stdout,"CONN UP for %s\n", hostname); fflush(stdout);
		}


		// 2.___________________________________________
		// This is a reconnect or initial connection.
		if(pH->get_status()==2 || pH->get_status()==0) {
	 		connected_iocs ++;
			pH->set_addr(args.chid);
			ca_set_puser(args.chid,pH);
		}

		// 3.___________________________________________
		// Always install this pv into the pv hash table
		strncpy (pvNameStr, ca_name(args.chid),PV_NAME_SZ-1);
		ptr = strchr(pvNameStr,'.');
		if (ptr) *ptr = 0;
		pCAS->installPVName( pvNameStr, pH);

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
					if(verbose)printf("Removed %s from pending\n", pvNameStr);
					if (pprevNN) pCAS->nameList.remove(*pprevNN);
					else pCAS->nameList.get();
					delete pNN;
					found = 1;
					break;
				}
				pprevNN = pNN;
				iter++;
			}
			if(!found) fprintf(stderr, "Unable to purge %s\n", pvNameStr);
		}

		// 5.___________________________________________
		// Set pHost status to UP
		pH->set_status(1);

		// 7.___________________________________________
		// Don't want monitors on anything except heartbeats.
		if( !isHeartbeat) {
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

#ifndef _WIN32
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
#else
    while(wait3(NULL,WNOHANG,NULL)>0);
#endif
    signal(SIGCHLD,sig_chld);
}
#endif


/*! \brief Remove all pvs from one host from the pv hashtable
 *
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param hostName - name of the IOC
 */
int remove_all_pvs(pHost *pH)
{
	pvE 	*pve;
	const char    *pvNameStr;
	char checkStr[PV_NAME_SZ];
	int		ct=0;
	pvEHost		*pvEHostHeartbeat=0;
	char	*hostName;
    int      removeAll=1;

	if(!pH) {
		fprintf(stderr, "NOT IN TABLE\n");fflush(stderr);
		return (-1);
	}
	hostName = pH->get_hostname();
	fprintf(stdout,"Deleting all pvs for %s\n", hostName); fflush(stdout);

    // If ioc has a pvname list file
	// Don't remove the heartbeat so we know when the ioc comes back up.
    if ( pH->get_pathToList() && strlen(pH->get_pathToList()) ) {
        if (hostName) sprintf(checkStr,"%s%s",hostName, HEARTBEAT);
        removeAll=0;
    }

    // get removes first item from list
    while ( pvEHost *pveh = pH->pvEList.get()) {
		pvNameStr = pveh->get_pvE()->get_name();
//fprintf(stdout,"Removing %s \n", pvNameStr); fflush(stdout);
		if ( removeAll || strcmp(checkStr,pvNameStr)) {

			stringId id2(pvNameStr, stringId::refString);
			pve = pCAS->stringResTbl.remove(id2);
			if(pve) {
				delete pve;
				ct--;
			}
			else {
				fprintf(stderr,"failed to remove %s\n", pvNameStr);fflush(stderr);
			}
			delete pveh;
		}
		else {
//fprintf(stdout, "heartbeat found while removing pvs %s\n",checkStr); fflush(stdout);
			pvEHostHeartbeat = pveh;
		}
    }
	if (pvEHostHeartbeat) {
//fprintf(stdout, "adding heartbeat back into %s pvEList\n",hostName); fflush(stdout);
		// Add heartbeat pvEHost back into pvEList
		pH->pvEList.add(*pvEHostHeartbeat);
	}
	return ct;
}


/*! \brief Add all pvs from a host to the pv hashtable
 *
 * All pv's are removed (and then the new set inserted) on reconnection of an IOC.
 *
 * \param hostName - name of the IOC
 */
int add_all_pvs(pHost *pH)
{
	int count;
	char *pathToList;
	FILE *pf;

	fprintf(stdout,"Adding all pvs for %s\n", pH->get_hostname());
	fflush(stdout);

	pathToList = pH->get_pathToList();
	pf = reserve_fd_fopen(pathToList, "r");
	if (!pf) {
		fprintf(stderr, "file access problem with file=\"%s\" because \"%s\"\n",
			pathToList, strerror(errno));
		return (-1);
	}
	count =  parseDirectoryFP (pf, pathToList, 0, pH);
	//fprintf(stdout,"File: %s PVs: %d\n", basename(dirname(pathToList)),count);

   	pH->set_status(1);
	reserve_fd_fclose (pf);
	return 0;
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

#ifdef _WIN32
char *basename(char *filename)
{
   char *pbackslash = strrchr(filename,'\\');
   char *pslash = strrchr(filename,'/');
   if (pslash ) return pslash+1;
   if (pbackslash ) return pbackslash+1;
   return (char *)filename;
}

char *dirname(char *filename)
{
   char *pslash = strrchr(filename, '/');
   char *pbackslash = strrchr(filename, '\\');
   if (pslash ) *pslash = '\0';
   if (pbackslash ) *pbackslash = '\0';
   return (char *)filename;
}
#endif

char *iocname(int isFilname,char *pPath) {
 return filenameIsIocname ? \
	basename((char *)pPath): basename(dirname((char *)pPath));
}

// Functions to try to reserve a file descriptor to use for fopen.  On
// Solaris, at least, fopen is limited to FDs < 256.  These could all
// be used by CA and CAS sockets if there are connections to enough
// IOCs  These functions try to reserve a FD < 256.
FILE *reserve_fd_fopen(const char *filename, const char *mode)
{
    // Close the dummy file holding the FD open
    if(reserveFp) ::fclose(reserveFp);
    reserveFp=NULL;

    // Open the file.  It should use the lowest available FD, that is,
    // the one we just made available.
    FILE *fp=::fopen(filename,mode);
    if(!fp) {
        // Try to get the reserved one back
        reserveFp=::fopen(NS_RESERVE_FILE,"w");
    }

    return fp;
}

int reserve_fd_fclose(FILE *stream)
{
    // Close the file
    int ret=::fclose(stream);

    // Open the dummy file to reserve the FD just made available
    reserveFp=::fopen(NS_RESERVE_FILE,"w");

    return ret;
}

FILE *reserve_fd_openReserveFile(void)
{
    reserveFp=::fopen(NS_RESERVE_FILE,"w");
    return reserveFp;
}

