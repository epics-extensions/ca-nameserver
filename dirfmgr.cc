/*! \file dirfmgr.cc
 * \brief TBD
 *
 * \author Joan Sage
 * \Revision History:
 * Initial release September 2001
*/
static char *rcsid="$Header$";


#include <fdManager.h>
#include <osiTimer.h>
#include <cadef.h>
#include <assert.h>

class CA_monFDReg : public fdReg {
public:
	CA_monFDReg (const SOCKET fdIn, const fdRegType typ,
            const unsigned onceOnlyIn=0) :
		fdReg (fdIn, typ, onceOnlyIn) {}

private:
	virtual void callBack ();
};


void CA_monFDReg::callBack ()
{
	/* 
 	 * We can call this N times for N active IOC's or
	 * only once after dropping out of fdManager::process()
	 *
	 * ca_pend_event (1e-12);
	 */
}

void add_CA_mon(int fd)
{
		fdReg *p;

		p = new CA_monFDReg (fd,fdrRead);
		assert (p!=NULL);
}

void remove_CA_mon(int fd)
{
		fdReg *p;

		p = fileDescriptorManager.lookUpFD(fd, fdrRead);
		if(p)
			delete p;
}
