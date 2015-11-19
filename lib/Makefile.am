if FMIESINCS
IES_INC_BASE = @IESINCS@
IES_CFLAGS = -DFM_SUPPORT_FM10000 -D_FM_ARCH_X86_64 \
             -I$(IES_INC_BASE) \
             -I$(IES_INC_BASE)/alos \
             -I$(IES_INC_BASE)/alos/linux \
             -I$(IES_INC_BASE)/std/intel \
             -I$(IES_INC_BASE)/common \
             -I$(IES_INC_BASE)/platforms \
             -I$(IES_INC_BASE)/platforms/libertyTrail
else
IES_CFLAGS = -DFM_SUPPORT_FM10000 -D_FM_ARCH_X86_64 \
             $(IESAPI_CFLAGS)
endif

WARNING_FLAGS_GCC = -pedantic -Wall -Wextra -Wwrite-strings -Wformat=2 \
                    -Wlogical-op -Wpointer-arith -Wfloat-equal \
                    -Wnested-externs -Wbad-function-cast -Wconversion \
                    -Wcast-qual -Wcast-align -Wstrict-prototypes \
                    -Wmissing-declarations -Wmissing-include-dirs \
                    -Wdeclaration-after-statement -Wmissing-prototypes \
                    -Wold-style-definition

if DEBUG
OPTFLAGS = -g3 -O0
else
OPTFLAGS = -g -O2
endif

AM_CFLAGS = -std=c99 $(OPTFLAGS) -D_GNU_SOURCE -Werror $(WARNING_FLAGS_GCC) \
            $(LIBNL_CFLAGS) $(LIBGVC_CFLAGS) -I$(abs_top_srcdir) \
            -I$(abs_top_srcdir)/include
AM_CFLAGS += -DMAT_LOG_WITH_LOCATION
CFLAGS += $(EXTRA_CFLAGS)

AM_LDFLAGS = -Wl,--no-as-needed

LIBTOOL_DEPS = @LIBTOOL_DEPS@

lib_LTLIBRARIES = libmatchies.la
libmatchies_la_SOURCES = ieslib.c
libmatchies_la_CFLAGS = $(AM_CFLAGS) $(IES_CFLAGS)
libmatchies_la_LDFLAGS = $(AM_LDFLAGS) -release @MATCH_INTERFACE_VERSION@

lib_LTLIBRARIES += libmatch.la
libmatch_la_SOURCES = matchlib_nl.c matchlib.c matlog.c matstream.c
libmatch_la_LDFLAGS = $(AM_LDFLAGS) -release @MATCH_INTERFACE_VERSION@

lib_LTLIBRARIES += libmatchd.la
libmatchd_la_SOURCES = matchd_lib.c backend.c
libmatchd_la_CFLAGS = $(AM_CFLAGS) $(IES_CFLAGS)
libmatchd_la_LDFLAGS = $(AM_LDFLAGS) -release @MATCH_INTERFACE_VERSION@
