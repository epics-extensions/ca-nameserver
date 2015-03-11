TOP=../..
include $(TOP)/configure/CONFIG

#USR_CXXFLAGS += -DPIOC
#USR_CXXFLAGS += -DJS_FILEWAIT
USR_CXXFLAGS += -DUSE_DENYFROM
#STATIC_BUILD=YES

#ifeq ($(OS_CLASS),solaris)
## Debugging options
#DEBUGCMD = purify -first-only -chain-length=30  $($(ANSI)_$(CMPLR))
#HOST_OPT=NO
#endif

# Purify
#PURIFY=YES
#ifeq ($(PURIFY),YES)
#ifeq ($(OS_CLASS),solaris)
#PURIFY_FLAGS = -first-only -chain-length=26 -max_threads=256
## Put the cache files in the appropriate bin directory
#PURIFY_FLAGS += -always-use-cache-dir -cache-dir=$(shell $(PERL) $(TOP)/config/fullPathName.pl .)
#DEBUGCMD = purify $(PURIFY_FLAGS)
#endif
#endif

# turns warnings into errors
#USR_CXXFLAGS = +p

# turns on all warnings
#HOST_WARN=YES
#CXXCMPLR = STRICT

#CXXCMPLR = NORMAL
#CCC_NORMAL = $(CCC)

PROD_LIBS	+= cas
#PROD_LIBS	+= cas_js
ifdef BASE_3_15
PROD_LIBS	+= dbCore
else
PROD_LIBS	+= asHost
endif
PROD_LIBS	+= ca
PROD_LIBS	+= gdd
PROD_LIBS	+= Com
PROD_LIBS	+= regex
regex_DIR  = $(INSTALL_LIB)
#cas_js_DIR  = $(EPICS_BASE_LIB)
cas_DIR  = $(EPICS_BASE_LIB)
ca_DIR  = $(EPICS_BASE_LIB)
asHost_DIR  = $(EPICS_BASE_LIB)
Com_DIR = $(EPICS_BASE_LIB)
gdd_DIR = $(EPICS_BASE_LIB)

SYS_PROD_LIBS_solaris := nsl
SYS_PROD_LIBS_WIN32 := ws2_32 advapi32 user32

SRCS += directoryServer.cc
SRCS += dirfmgr.cc
SRCS += gateAs.cc
SRCS += main.cc
SRCS += nsIO.cc
SRCS += nsPV.cc
SRCS += nsScalarPV.cc
SRCS += pvServer.cc
SRCS += reserve_fd.cc

PROD_HOST = caDirServ

include $(TOP)/configure/RULES
