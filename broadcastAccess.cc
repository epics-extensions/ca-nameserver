static char RcsId[] = "@(#)$Id$";

/*+*********************************************************************
 *
 * File:       broadcastAccess.cc
 * Project:    CA Nameserver
 * Descr.:     Host broadcast access
 * Author(s):  J. Anderson (APS)
 *
 *********************************************************************-*/

#include "broadcastAccess.h"

#define MAX_LINE_LENGTH 2000

broadcastAccess::broadcastAccess(const char* lfile,unsigned hostCount) :
	allowFromList(false)
{
	this->hostTbl.setTableSize(hostCount);
	if(readHostList(lfile))
		fprintf(stderr,"Failed to install host broadcast access file %s\n",lfile);
}

broadcastAccess::~broadcastAccess(void)
{
  tsSLList < hostId > tmpIdList;

  this->hostTbl.removeAll(tmpIdList);
  while ( hostId * pId = tmpIdList.get() ) {
    pId->~hostId ();
  }
}

int broadcastAccess::readHostList(const char* lfile)
{
	FILE* fd;
	char inbuf[MAX_LINE_LENGTH];
	char *ptr,*cmd, *hname;
	hostId *pH;

	this->allowFromList=false;

	if (!lfile)	return 0;
	if((fd=fopen(lfile,"r"))==NULL)
	{
		fprintf(stderr,"Failed to open host broadcast access file %s\n",lfile);
		return -1;
	}

	// Read all host broadcast access file lines (only one command allowed)
	while(fgets(inbuf,sizeof(inbuf),fd))
	{
		if((ptr=strchr(inbuf,'#'))) *ptr='\0'; // Take care of comments

		hname=NULL;

		if(!(cmd=strtok(inbuf," \t\n"))) continue;

		if(strcasecmp(cmd,"DENY")==0) this->allowFromList=false;
		else if(strcasecmp(cmd,"ALLOW")==0) this->allowFromList=true;
			else
			{
				fprintf(stderr,"Error in broadcast host list file:"
					" invalid command `%s`\n",cmd);
			}

		// Arbitrary number of arguments: [from] host names
		if((hname=strtok(NULL,", \t\n")) && strcasecmp(hname,"FROM")==0)
		  hname=strtok(NULL,", \t\n");
		if(hname)           // host name(s) present
		  do
		  {
			  pH = new hostId(hname);
			  this->hostTbl.add(*pH);
		  }
		  while((hname=strtok(NULL,", \t\n")));

        // Only one command line from broadcast file allowed
		break;
	}
		
	fclose(fd);
	return 0;
}

