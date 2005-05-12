TOP=../..
include $(TOP)/configure/CONFIG

#USR_CXXFLAGS += -DPIOC
USR_CXXFLAGS += -DJS_FILEWAIT
USR_CXXFLAGS += -DUSE_DENYFROM
#STATIC_BUILD=YES

#ifeq ($(OS_CLASS),solaris)
## Debugging options
#DEBUGCMD = purify -first-only -chain-length=30  $($(ANSI)_$(CMPLR))
#HOST_OPT=NO
#endif

# turns warnings into errors
#USR_CXXFLAGS = +p

# turns on all warnings
#HOST_WARN=YES
#CXXCMPLR = STRICT

#CXXCMPLR = NORMAL
#CCC_NORMAL = $(CCC)

PROD_LIBS	+= regex
PROD_LIBS	+= cas
#PROD_LIBS	+= cas_js
PROD_LIBS	+= ca
PROD_LIBS	+= asHost
PROD_LIBS	+= Com
PROD_LIBS	+= gdd
regex_DIR  = $(INSTALL_LIB)
#cas_js_DIR  = $(EPICS_BASE_LIB)
cas_DIR  = $(EPICS_BASE_LIB)
ca_DIR  = $(EPICS_BASE_LIB)
asHost_DIR  = $(EPICS_BASE_LIB)
Com_DIR = $(EPICS_BASE_LIB)
gdd_DIR = $(EPICS_BASE_LIB)

SYS_PROD_LIBS_solaris := nsl
SYS_PROD_LIBS_WIN32 := ws2_32 advapi32 user32

SRCS +=main.cc
SRCS += reserve_fd.cc
SRCS += nsIO.cc
SRCS += dirfmgr.cc
SRCS += directoryServer.cc
SRCS += gateAs.cc

PROD_HOST = caDirServ

include $(TOP)/configure/RULES
