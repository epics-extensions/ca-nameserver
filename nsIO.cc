/*! \file nsIO.cc
 * \brief nameserver I/O routines
 *
 * \author Janet Anderson
 * \Revision History:
 * Initial release May 2005
*/

#include <stdlib.h>
#include <sys/stat.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#ifdef linux
#include <sys/wait.h>
#endif

#include "reserve_fd.h"
#include "nsIO.h"

#define NS_SCRIPT_FILE "nameserver.killer"
#define NS_RESTART_FILE "nameserver.restart"

#define MAX_MESSAGE_CHARS 1024
#define MAX_TIME_CHARS 30

static int log_level = 2;

#ifndef WIN32

/*! \brief Change to home directory 
 *
 * \param homeDir - home directory
 *
*/
int cd_home_dir(const char* const home_dir)
{
	// Go to nameserver's home directory now
	if(!home_dir || chdir(home_dir)<0) {
			perror("Change to home directory failed");
			fprintf(stderr,"-->Bad home <%s>\n",home_dir); fflush(stderr);
			return -1;
	} 
    return 0;
}

/*! \brief Create file containing a killer script
*/
void create_killer_script(pid_t parent_pid,const char* const home_dir,const char* const pvlist_file,const char* const log_file,const char* const fileName)
{
	FILE *fd;
	if((fd=reserve_fd_fopen(NS_SCRIPT_FILE,"w"))==(FILE*)NULL) {
        fprintf(stderr,"open of script file %s failed\n", NS_SCRIPT_FILE);
        fd=stderr;
    }

	int sid=getpid();
    fprintf(fd,"\n");
    fprintf(fd,"# options:\n");
    if (home_dir) fprintf(fd,"# home=<%s>\n",home_dir);
    if (pvlist_file) fprintf(fd,"# pvlist file=<%s>\n",pvlist_file);
    if (log_file) fprintf(fd,"# log file=<%s>\n",log_file);
    if (fileName) fprintf(fd,"# list file=<%s>\n",fileName);
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
    fprintf(fd,"\nkill %d # to kill everything\n\n",parent_pid);
    fprintf(fd,"\n# kill %d # to kill off this server\n\n",sid);
    fflush(fd);

    if(fd!=stderr) reserve_fd_fclose(fd);
    chmod(NS_SCRIPT_FILE,00755);
}


/*! \brief Create file containing a restart script
*/
void create_restart_script()
{
	FILE *fd;
	if((fd=reserve_fd_fopen(NS_RESTART_FILE,"w"))==(FILE*)NULL)
    {
        fprintf(stderr,"open of restart file %s failed\n",
            NS_RESTART_FILE);
        fd=stderr;
    }
	int sid=getpid();
    fprintf(fd,"\nkill %d # to kill off this nameserver\n\n",sid);
    fflush(fd);

    if(fd!=stderr) reserve_fd_fclose(fd);
    chmod(NS_RESTART_FILE,00755);

}

void increase_process_limits()
{
	struct rlimit lim;

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

}

#endif

/*! \brief logging code shamelessly stolen from gateway code!
 *
 * \param log_file - name to use for log file
*/
void setup_logging(char *log_file)
{
#ifndef WIN32
	struct		stat sbuf;			//old logfile info
	char 		cur_time[200];
	time_t t;

	if (!log_file) return;

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
	FILE *fp=reserve_fd_fopen(log_file,"w");
	if(fp == NULL) {
		fprintf(stderr,"Cannot open %s\n",log_file);
		fflush(stderr);
	} else {
		reserve_fd_fclose(fp);
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

/*! \brief print an environmant variable stolen from gateway code!
 *
 * \param fp - pointer to an open log file
 * \param var - environment variable name
*/
void print_env(FILE *fp, const char *var)
{
    if(!fp || !var) return;

    char *value=getenv(var);
    fprintf(fp,"%s=%s\n", var, value?value:"Not specified");
}


/*! \brief print environment variables
 *
 * \param fp - pointer to an open log file
*/
void print_env_vars(FILE *fp)
{
   	print_env(fp,"EPICS_CA_ADDR_LIST");
   	print_env(fp,"EPICS_CA_AUTO_ADDR_LIST");
   	print_env(fp,"EPICS_CA_SERVER_PORT");

   	print_env(fp,"EPICS_CAS_BEACON_PERIOD");
   	print_env(fp,"EPICS_CAS_BEACON_PORT");
   	print_env(fp,"EPICS_CAS_BEACON_ADDR_LIST");
   	print_env(fp,"EPICS_CAS_AUTO_BEACON_ADDR_LIST");

   	print_env(fp,"EPICS_CAS_SERVER_PORT");
   	print_env(fp,"EPICS_CAS_INTF_ADDR_LIST");
   	print_env(fp,"EPICS_CAS_IGNORE_ADDR_LIST");
}


/***********************************************************************
 * set the log message level
 ***********************************************************************/
void set_log_level(int level)
{
   log_level = level;
}


/***********************************************************************
 * log a message to the logfile
 ***********************************************************************/
void log_message(int level,const char* fmt,...)
{
    va_list vargs;
    static char text[MAX_MESSAGE_CHARS];
    int status=0;
    char buf[MAX_TIME_CHARS];

    fflush(stdout);
    fflush(stderr);

    if (level > log_level) return;

    va_start(vargs,fmt);
    vsprintf(text,fmt,vargs);
    va_end(vargs);

    epicsTime time = epicsTime::getCurrent ();
    time.strftime(buf,30,"%Y-%m-%d %H:%M:%S.%06f");

    if (buf)
      status=fprintf(stdout,"%s  %s",buf,text);
    else
      status=fprintf(stdout,"                            %s",text);

    if (status<0 )  {
      fprintf(stderr,"Can't write to log file: '%s' \n", text );
    }

    fflush(stdout);
    fflush(stderr);

    return;
}

