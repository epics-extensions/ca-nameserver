/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
//
// fileDescriptorManager.process(delay);
// (the name of the global symbol has leaked in here)
//

//
// Example EPICS CA server
//
#include "pvServer.h"

//
// static list of pre-created PVs
//
pvInfo pvServer::pvList[] = {
    pvInfo (1.0, "heartbeat", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "request", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "broadcast", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "broadcast_denied", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "pending", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "ioc_error", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "hit", 0.0f, 0.0f, aitEnumFloat64),
    pvInfo (5.0, "ioc_down", 0.0f, 0.0f, aitEnumFloat64)
};

const unsigned pvServer::pvListNElem = NELEMENTS (pvServer::pvList);

//
// pvServer::pvServer()
//
pvServer::pvServer ( const char * const pvPrefix )
{
    nsPV *pPV;
    pvInfo *pPVI;
    pvInfo *pPVAfter = &pvServer::pvList[pvListNElem];
    char name[256];
    const char * const pNameFmtStr = "%.100s%.20s";

    fprintf(stdout, "\npvPrefix = %s \n", pvPrefix);
    nsPV::initFT();

    //
    // pre-create all of the simple PVs that this server will export
    //
    for (pPVI = pvServer::pvList; pPVI < pPVAfter; pPVI++) {
        pPV = pPVI->createPV (*this, true);
        if (!pPV) {
            fprintf(stderr, "Unable to create new PV \"%s\"\n",
                pPVI->getName());
        }

        //
        // Install canonical (root) name
        //
        sprintf(name, pNameFmtStr, pvPrefix, pPVI->getName());
        this->installName(*pPVI, name);
        fprintf(stdout, "Installed PV: %s in ns hash table\n", name);
    }
    fprintf(stdout, "Total nameserver PVs: %d\n\n",this->pvListNElem);
    fflush(stdout);

}

//
// pvServer::~pvServer()
//
pvServer::~pvServer()
{
    //printf ("NELEMENTS=%d\n",NELEMENTS(pvServer::pvList));
    for ( unsigned i = 0;
            i < NELEMENTS(pvServer::pvList); i++ ) {
        pvServer::pvList[i].deletePV ();
    }
    //printf ("nsResTbl numEntriesInstall=%d\n",nsResTbl.numEntriesInstalled());
    this->nsResTbl.traverse ( &pvEntry::destroy );
}

//
// pvServer::installName()
//
void pvServer::installName(pvInfo &info, const char *pName)
{
    pvEntry *pEntry;

    pEntry = new pvEntry(info, *this, pName);
    if (pEntry) {
        int resLibStatus;
        resLibStatus = this->nsResTbl.add(*pEntry);
        if (resLibStatus==0) {
            return;
        }
        else {
            delete pEntry;
        }
    }
    fprintf ( stderr, 
"Unable to enter PV=\"%s\" Name=\"%s\" in PV name hash table\n",
        info.getName(), pName );
    fflush(stderr);
}

//
// More advanced pvExistTest() isnt needed so we forward to
// original version. This avoids sun pro warnings and speeds 
// up execution.
//
pvExistReturn pvServer::pvExistTest
	( const casCtx & ctx, const caNetAddr &, const char * pPVName )
{
//fprintf (stdout,"pvServer::pvExistTest  3PARAM name=%s\n",pPVName);fflush(stdout);
	return this->pvExistTest ( ctx, pPVName );
}

//
// pvServer::pvExistTest()
//
pvExistReturn pvServer::pvExistTest
    ( const casCtx& ctxIn, const char * pPVName )
{
//fprintf (stdout,"pvServer::pvExistTest name=%s\n",pPVName);fflush(stdout);
    //
    // lifetime of id is shorter than lifetime of pName
    //
    stringId id ( pPVName, stringId::refString );
    pvEntry *pPVE;

    //
    // Look in hash table for PV name
    //
    pPVE = this->nsResTbl.lookup ( id );
    if ( ! pPVE ) {
        return pverDoesNotExistHere;
    }

    pvInfo & pvi = pPVE->getInfo();
//printf ("PV exists here: %s\n",pPVName);
    return pverExistsHere;
}

//
// pvServer::pvAttach()
//
pvAttachReturn pvServer::pvAttach // X aCC 361
    (const casCtx &ctx, const char *pName)
{
    //
    // lifetime of id is shorter than lifetime of pName
    //
    stringId id(pName, stringId::refString); 
    nsPV *pPV;
    pvEntry *pPVE;

    pPVE = this->nsResTbl.lookup(id);
    if (!pPVE) {
        return S_casApp_pvNotFound;
    }

    pvInfo &pvi = pPVE->getInfo();

    //
    //  create the synchronous PV now 
    //
    pPV = pvi.createPV(*this, false);
    if (pPV) {
        return *pPV;
    }
    else {
        return S_casApp_noMemory;
    }
}

//
// pvServer::setDebugLevel ()
//
void pvServer::setDebugLevel ( unsigned level )
{
    this->caServer::setDebugLevel ( level );
}

//
// pvServer::generateBeaconAnomaly ()
//
void pvServer::generateBeaconAnomaly ()
{
    this->caServer::generateBeaconAnomaly ();
}

//
// pvServer::createTimer ()
//
class epicsTimer & pvServer::createTimer ()
{
    if ( this->pTimerQueue ) {
        return this->pTimerQueue->createTimer ();
    }
    else {
        return this->caServer::createTimer ();
    }
}


//
// pvInfo::createPV()
//
nsPV *pvInfo::createPV ( pvServer & cas,
                         bool preCreateFlag)
{
    if (this->pPV) {
        return this->pPV;
    }

    nsPV *pNewPV;

    pNewPV = new nsScalarPV ( cas, *this, preCreateFlag );
    
    //
    // load initial value (this is not done in
    // the constructor because the base class's
    // pure virtual function would be called)
    //
    // We always perform this step even if
    // scanning is disable so that there will
    // always be an initial value
    //
    if (pNewPV) {
        this->pPV = pNewPV;
        pNewPV->scan();
    }

    return pNewPV;
}

//
// pvServer::show() 
//
void pvServer::show (unsigned level) const
{
    //
    // server tool specific show code goes here
    //
    this->nsResTbl.show(level);

    //
    // print information about ca server libarary
    // internals
    //
    //this->caServer::show(level);
}

