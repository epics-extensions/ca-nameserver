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
//  Example EPICS CA server
//
//
//  caServer
//  |
//  pvServer
//
//  casPV
//  |
//  nsPV
//  |
//  nsScalarPV
//  |
//


//
// ANSI C
//
#include <string.h>
#include <stdio.h>

//
// EPICS
//
//#define epicsAssertAuthor "Jeff Hill johill@lanl.gov"
#include "gddAppFuncTable.h"
#include "smartGDDPointer.h"
#include "epicsTimer.h"
#include "casdef.h"
#include "epicsAssert.h"
#include "resourceLib.h"
#include "tsMinMax.h"

// pvExistTest counts
struct nsStats {
    double request;
    double broadcast;
    double broadcast_denied;
    double pending;
    double ioc_error;
    double hit;
    double ioc_down;
};

#ifndef NELEMENTS
#   define NELEMENTS(A) (sizeof(A)/sizeof(A[0]))
#endif

//
// info about all pv in this server
//

class pvServer;
class nsPV;

//
// pvInfo
//
class pvInfo {
public:
    pvInfo ( double scanPeriodIn, const char *pNameIn, 
            aitFloat32 hoprIn, aitFloat32 loprIn,
            aitEnum typeIn);
    pvInfo ( const pvInfo & copyIn );
    ~pvInfo ();
    double getScanPeriod () const;
    const char *getName () const;
    double getHopr () const;
    double getLopr () const;
    aitEnum getType () const;
    void unlinkPV ();
    nsPV *createPV ( pvServer & nsCAS, 
        bool preCreateFlag );
    void deletePV ();

private:
    const double scanPeriod;
    const char * pName;
    const double hopr;
    const double lopr;
    aitEnum type;
    nsPV * pPV;
    pvInfo & operator = ( const pvInfo & );
};

//
// pvEntry 
//
// o entry in the string hash table for the pvInfo
//
class pvEntry // X aCC 655
    : public stringId, public tsSLNode < pvEntry > {
public:
    pvEntry ( pvInfo  &infoIn, pvServer & casIn, const char * pName );
    ~pvEntry();
    pvInfo & getInfo() const { return this->info; }
    void destroy ();

private:
    pvInfo & info;
    pvServer & cas;
    pvEntry & operator = ( const pvEntry & );
    pvEntry ( const pvEntry & );
};


//
// nsPV
//
class nsPV : public casPV, public epicsTimerNotify,
    public tsSLNode < nsPV > {
public:
    nsPV ( pvServer & cas, pvInfo & setup, 
        bool preCreateFlag );
    virtual ~nsPV();

    void show ( unsigned level ) const;

    //
    // Called by the server libary each time that it wishes to
    // subscribe for PV the server tool via postEvent() below.
    //
    caStatus interestRegister ();

    //
    // called by the server library each time that it wishes to
    // remove its subscription for PV value change events
    // from the server tool via caServerPostEvents()
    //
    void interestDelete ();

    aitEnum bestExternalType () const;

    //
    // chCreate() is called each time that a PV is attached to
    // by a client. The server tool must create a casChannel object
    // (or a derived class) each time that this routine is called
    //
    // If the operation must complete asynchronously then return
    // the status code S_casApp_asyncCompletion and then
    // create the casChannel object at some time in the future
    //
    //casChannel *createChannel ();

    //
    // This gets called when the pv gets a new value
    //
    caStatus update ( const gdd & );

    //
    // Gets called when we add noise to the current value
    //
    virtual void scan () = 0;
    
    //
    // If no one is watching scan the PV with 10.0
    // times the specified period
    //
    double getScanPeriod ();

    caStatus read ( const casCtx &, gdd & protoIn );

    caStatus readNoCtx ( smartGDDPointer pProtoIn );

    caStatus write ( const casCtx &, const gdd & value );

    void destroy ();

//    const pvInfo & getPVInfo ();

    const char * getName() const;

    static void initFT();

protected:
    smartGDDPointer pValue;
    pvServer & cas;
    epicsTimer & timer;
    pvInfo & info; 
    bool interest;
    bool preCreate;
    static epicsTime currentTime;

    virtual caStatus updateValue ( const gdd & ) = 0;

private:

    //
    // scan timer expire
    //
    expireStatus expire ( const epicsTime & currentTime );

    //
    // Std PV Attribute fetch support
    //
    gddAppFuncTableStatus getPrecision(gdd &value);
    gddAppFuncTableStatus getHighLimit(gdd &value);
    gddAppFuncTableStatus getLowLimit(gdd &value);
    gddAppFuncTableStatus getUnits(gdd &value);
    gddAppFuncTableStatus getValue(gdd &value);
    gddAppFuncTableStatus getEnums(gdd &value);

    nsPV & operator = ( const nsPV & );
    nsPV ( const nsPV & );

    //
    // static
    //
    static gddAppFuncTable<nsPV> ft;
    static char hasBeenInitialized;
};

//
// nsScalarPV
//
class nsScalarPV : public nsPV {
public:
    nsScalarPV ( pvServer & cas, pvInfo &setup, 
        bool preCreateFlag ) :
        nsPV ( cas, setup, 
            preCreateFlag) {}
    void scan();
private:
    caStatus updateValue ( const gdd & );
//    nsScalarPV & operator = ( const nsScalarPV & );
//    nsScalarPV ( const nsScalarPV & );
};

//
// pvServer
//
class pvServer : private caServer {
public:
    pvServer ( const char * const pvPrefix );
    ~pvServer ();
    void show ( unsigned level ) const;
    void removeName ( pvEntry & entry );

    class epicsTimer & createTimer ();
	void setDebugLevel ( unsigned level );
	void generateBeaconAnomaly ();

protected:
    pvExistReturn pvExistTest ( const casCtx &, 
        const caNetAddr &, const char * pPVName );
    pvExistReturn pvExistTest ( const casCtx &, 
        const char * pPVName );
    pvAttachReturn pvAttach ( const casCtx &, 
        const char * pPVName );

private:
    resTable < pvEntry, stringId > nsResTbl;
    epicsTimerQueueActive * pTimerQueue;

    void installName ( pvInfo & info, const char * pName );

    pvServer & operator = ( const pvServer & );
    pvServer ( const pvServer & );

    //
    // list of pre-created PVs
    //
    static pvInfo pvList[];
    static const unsigned pvListNElem;
};


//
// for use when MSVC++ will not build a default copy constructor 
// for this class
//
inline pvInfo::pvInfo ( const pvInfo & copyIn ) :

    scanPeriod ( copyIn.scanPeriod ), pName ( copyIn.pName ),
    hopr ( copyIn.hopr ), lopr ( copyIn.lopr ), type ( copyIn.type ),
    pPV ( copyIn.pPV )
{
}

inline pvInfo::~pvInfo ()
{
    //
    // GDD cleanup gets rid of GDD's that are in use 
    // by the PV before the file scope destructer for 
    // this class runs here so this does not seem to 
    // be a good idea
    //
    //if ( this->pPV != NULL ) {
    //   delete this->pPV;
    //}
}

inline void pvInfo::deletePV ()
{
    if ( this->pPV != NULL ) {
        delete this->pPV;
    }
}

inline double pvInfo::getScanPeriod () const
{
    return this->scanPeriod;
}

inline const char *pvInfo::getName () const 
{ 
    return this->pName; 
}

inline double pvInfo::getHopr () const 
{ 
    return this->hopr; 
}

inline double pvInfo::getLopr () const 
{ 
    return this->lopr; 
}

inline aitEnum pvInfo::getType () const 
{ 
    return this->type;
}

inline void pvInfo::unlinkPV () 
{ 
    this->pPV = NULL; 
}

inline pvEntry::pvEntry ( pvInfo  & infoIn, pvServer & casIn, 
        const char * pName ) : 
    stringId ( pName ), info ( infoIn ), cas ( casIn ) 
{
    assert ( this->stringId::resourceName() != NULL );
}

inline pvEntry::~pvEntry ()
{
    this->cas.removeName ( *this );
}

inline void pvEntry::destroy ()
{
    delete this;
}

inline void pvServer::removeName ( pvEntry & entry )
{
    pvEntry * pE;
    pE = this->nsResTbl.remove ( entry );
    assert ( pE == &entry );
}

inline double nsPV::getScanPeriod ()
{
    double curPeriod = this->info.getScanPeriod ();
    if ( ! this->interest ) {
        curPeriod *= 10.0L;
    }
    return curPeriod;
}

inline caStatus nsPV::readNoCtx ( smartGDDPointer pProtoIn )
{
    return this->ft.read ( *this, *pProtoIn );
}

//inline const pvInfo & nsPV::getPVInfo ()
//{
//    return this->info;
//}

inline const char * nsPV::getName () const
{
    return this->info.getName();
}

inline pvInfo::pvInfo ( double scanPeriodIn, const char *pNameIn,
    aitFloat32 hoprIn, aitFloat32 loprIn,
    aitEnum typeIn) :

    scanPeriod ( scanPeriodIn ), pName ( pNameIn ),
    hopr ( hoprIn ), lopr ( loprIn ), type ( typeIn ),
    pPV ( 0 )
{
}

