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
// All code in pvServer.h pvServer.cc, nsPV.cc, nsScalarPV.cc taken from
// exServer template in EPICS base R3.14.7


//
// Example EPICS CA server
//
#include "pvServer.h"
#include "gddApps.h"

//
// static data for nsPV
//
char nsPV::hasBeenInitialized = 0;
gddAppFuncTable<nsPV> nsPV::ft;
epicsTime nsPV::currentTime;

//
// special gddDestructor guarantees same form of new and delete
//
class nsFixedStringDestructor: public gddDestructor {
    virtual void run (void *);
};

//
// nsPV::nsPV()
//
nsPV::nsPV ( pvServer & casIn, pvInfo & setup, 
            bool preCreateFlag ) : 
    cas ( casIn ),
    timer ( cas.createTimer() ),
    info ( setup ),
    interest ( false ),
    preCreate ( preCreateFlag )
{

    //
    // start a very slow background scan
    // (we will speed this up to the normal rate when
    // someone is watching the PV)
    //
    if ( this->info.getScanPeriod () > 0.0 ) {
        this->timer.start ( *this, this->getScanPeriod() );
    }
}

//
// nsPV::~nsPV()
//
nsPV::~nsPV() 
{
    this->timer.destroy ();
    this->info.unlinkPV();
}

//
// nsPV::destroy()
//
// this is replaced by a noop since we are 
// pre-creating most of the PVs during init in this simple server
//
void nsPV::destroy()
{
    if ( ! this->preCreate ) {
        delete this;
    }
}

//
// nsPV::update()
//
caStatus nsPV::update ( const gdd & valueIn )
{
#   if DEBUG
        printf("Setting %s to:\n", this->info.getName().string());
        valueIn.dump();
#   endif

    caStatus status = this->updateValue ( valueIn );
    if ( status || ( ! this->pValue.valid() ) ) {
        return status;
    }

    //
    // post a value change event
    //
    caServer * pCAS = this->getCAS();
    if ( this->interest == true && pCAS != NULL ) {
        casEventMask select ( pCAS->valueEventMask() | pCAS->logEventMask() );
        this->postEvent ( select, *this->pValue );
    }

    return S_casApp_success;
}

// nsScanTimer::expire ()
//
epicsTimerNotify::expireStatus
nsPV::expire ( const epicsTime & /*currentTime*/ ) // X aCC 361
{
    this->scan();
    if ( this->getScanPeriod() > 0.0 ) {
        return expireStatus ( restart, this->getScanPeriod() );
    }
    else {
        return noRestart;
    }
}

// nsPV::bestExternalType()
//
aitEnum nsPV::bestExternalType () const
{
    return this->info.getType ();
}

//
// nsPV::interestRegister()
//
caStatus nsPV::interestRegister ()
{
    if ( ! this->getCAS() ) {
        return S_casApp_success;
    }

    this->interest = true;
    if ( this->getScanPeriod() > 0.0 &&
            this->getScanPeriod() < this->timer.getExpireDelay() ) {
        this->timer.start ( *this, this->getScanPeriod() );
    }

    return S_casApp_success;
}

//
// nsPV::interestDelete()
//
void nsPV::interestDelete()
{
    this->interest = false;
}

//
// nsPV::show()
//
void nsPV::show ( unsigned level ) const
{
    if (level>1u) {
        if ( this->pValue.valid () ) {
            printf ( "nsPV: cond=%d\n", this->pValue->getStat () );
            printf ( "nsPV: sevr=%d\n", this->pValue->getSevr () );
            printf ( "nsPV: value=%f\n", static_cast < double > ( * this->pValue ) );
        }
        printf ( "nsPV: interest=%d\n", this->interest );
        this->timer.show ( level - 1u );
    }
}

//
// nsPV::initFT()
//
void nsPV::initFT ()
{
    if ( nsPV::hasBeenInitialized ) {
            return;
    }

    //
    // time stamp, status, and severity are extracted from the
    // GDD associated with the "value" application type.
    //
    nsPV::ft.installReadFunc ("value", &nsPV::getValue);
    nsPV::ft.installReadFunc ("precision", &nsPV::getPrecision);
    nsPV::ft.installReadFunc ("graphicHigh", &nsPV::getHighLimit);
    nsPV::ft.installReadFunc ("graphicLow", &nsPV::getLowLimit);
    nsPV::ft.installReadFunc ("controlHigh", &nsPV::getHighLimit);
    nsPV::ft.installReadFunc ("controlLow", &nsPV::getLowLimit);
    nsPV::ft.installReadFunc ("alarmHigh", &nsPV::getHighLimit);
    nsPV::ft.installReadFunc ("alarmLow", &nsPV::getLowLimit);
    nsPV::ft.installReadFunc ("alarmHighWarning", &nsPV::getHighLimit);
    nsPV::ft.installReadFunc ("alarmLowWarning", &nsPV::getLowLimit);
    nsPV::ft.installReadFunc ("units", &nsPV::getUnits);

    nsPV::hasBeenInitialized = 1;
}

//
// nsPV::getPrecision()
//
caStatus nsPV::getPrecision ( gdd & prec )
{
    prec.put(4u);
    return S_cas_success;
}

//
// nsPV::getHighLimit()
//
caStatus nsPV::getHighLimit ( gdd & value )
{
    value.put(info.getHopr());
    return S_cas_success;
}

//
// nsPV::getLowLimit()
//
caStatus nsPV::getLowLimit ( gdd & value )
{
    value.put(info.getLopr());
    return S_cas_success;
}

//
// nsPV::getUnits()
//
caStatus nsPV::getUnits( gdd & units )
{
    aitString str("furlongs", aitStrRefConstImortal);
    units.put(str);
    return S_cas_success;
}

//
// nsPV::getValue()
//
caStatus nsPV::getValue ( gdd & value )
{
    caStatus status;

    if ( this->pValue.valid () ) {
        gddStatus gdds;

        gdds = gddApplicationTypeTable::
            app_table.smartCopy ( &value, & (*this->pValue) );
        if (gdds) {
            status = S_cas_noConvert;   
        }
        else {
            status = S_cas_success;
        }
    }
    else {
        status = S_casApp_undefined;
    }
    return status;
}

//
// nsPV::write()
// (synchronous default)
//
caStatus nsPV::write ( const casCtx &, const gdd & valueIn )
{
    return this->update ( valueIn );
}
 
//
// nsPV::read()
// (synchronous default)
//
caStatus nsPV::read ( const casCtx &, gdd & protoIn )
{
    return this->ft.read ( *this, protoIn );
}

//
// nxFixedStringDestructor::run()
//
// special gddDestructor guarantees same form of new and delete
//
void nsFixedStringDestructor::run ( void * pUntyped )
{
    aitFixedString *ps = (aitFixedString *) pUntyped;
    delete [] ps;
}

