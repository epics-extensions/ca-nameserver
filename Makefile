# Top of Nameserver tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure
DIRS += src

# Directory dependencies
src_DEPEND_DIRS += configure

include $(TOP)/configure/RULES_TOP
