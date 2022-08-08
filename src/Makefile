TOP = ..
include $(TOP)/configure/CONFIG

ifeq ($(USE_DENYFROM), YES)
  USR_CXXFLAGS += -DUSE_DENYFROM
endif
ifeq ($(USE_NEG_REGEXP), YES)
  USR_CXXFLAGS += -DUSE_NEG_REGEXP
endif

# Build with PCRE or Regex
ifeq ($(SYS_PCRE), YES)
  # Use system-supplied PCRE library
  USR_CXXFLAGS += -DUSE_PCRE
  PROD_SYS_LIBS += pcre
else ifeq ($(USE_PCRE), YES)
  # Use external PCRE library
  USR_CXXFLAGS += -DUSE_PCRE
  USR_INCLUDES += -I$(PCRE)/include
  PROD_LIBS += pcre
  pcre_DIR = $(firstword $(wildcard $(PCRE)/lib/$(T_A) $(PCRE)/lib))
else ifneq ($(strip $(REGEX)),)
  # Use external GNU Regex library
  USR_INCLUDES += -I$(REGEX)/include
  PROD_LIBS += regex
  regex_DIR = $(firstword $(wildcard $(REGEX)/lib/$(T_A) $(REGEX)/lib))
else ifneq ($(OS_CLASS), Linux)
  # Must be a system-supplied GNU Regex library,
  # except on Linux where it's part of libc
  PROD_SYS_LIBS += regex
endif

# From PCAS
PROD_LIBS += cas gdd

ifdef EPICS_BASE_HOST_LIBS
  PROD_LIBS += $(EPICS_BASE_HOST_LIBS)
else
  PROD_LIBS += dbCore ca Com
endif

PROD_SYS_LIBS_WIN32 = ws2_32 advapi32 user32

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
