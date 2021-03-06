#
# C Library including string, locale
#
LIBS   = libc-string libc-locale libc-stdlib libc-stdio libc-gen libc-gdtoa \
         libc-inet libc-stdtime libc-regex libc-compat libc-setjmp

LIBS  += base config

#
# Back end
#
SRC_CC = atexit.cc dummies.cc rlimit.cc sysctl.cc \
         issetugid.cc errno.cc gai_strerror.cc clock_gettime.cc \
         gettimeofday.cc malloc.cc progname.cc fd_alloc.cc file_operations.cc \
         plugin.cc plugin_registry.cc select.cc exit.cc environ.cc nanosleep.cc \
         libc_mem_alloc.cc pread_pwrite.cc readv_writev.cc poll.cc \
         libc_pdbg.cc vfs_plugin.cc

INC_DIR += $(REP_DIR)/src/lib/libc

#
# Files from string library that are not included in libc-raw_string because
# they depend on the locale library.
#
SRC_C += strcoll.c strxfrm.c wcscoll.c wcsxfrm.c

include $(REP_DIR)/lib/mk/libc-common.inc

vpath % $(REP_DIR)/src/lib/libc
vpath % $(LIBC_DIR)/libc/string

#
# Shared library, for libc we need symbol versioning
#
SHARED_LIB  = yes
LD_OPT     += --version-script=$(REP_DIR)/src/lib/libc/Version.def
