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
if FMIESLIBS
IES_LIB_BASE = @IESLIBS@
IES_LIBS = -L$(IES_LIB_BASE) \
           -lFocalpointSDK -lLTStdPlatform -ldl
else
IES_LIBS = $(IESAPI_LIBS)
endif

if DCLIBONPINCS
LIBONP_INC_BASE = @LIBONPINCS@
LIBONP_CFLAGS = -I$(LIBONP_INC_BASE)
else
LIBONP_CFLAGS = $(LONP_CFLAGS)
endif

if DCLIBONPLIBS
LIBONP_LIB_BASE = @LIBONPLIBS@
LIBONP_LIBS = -Wl,-rpath $(LIBONP_LIB_BASE) -L$(LIBONP_LIB_BASE) \
              -lonp -lpthread
else
LIBONP_LIBS = $(LONP_LIBS)
endif

WARNING_FLAGS_GCC = -pedantic -Wall -Wextra -Wwrite-strings -Wformat=2 \
                    -Wlogical-op -Wpointer-arith -Wfloat-equal \
                    -Wnested-externs -Wbad-function-cast -Wconversion \
                    -Wcast-qual -Wcast-align -Wstrict-prototypes \
                    -Wmissing-declarations -Wmissing-include-dirs \
                    -Wdeclaration-after-statement -Wmissing-prototypes \
                    -Wold-style-definition

AM_CFLAGS = -std=c99 -O2 -D_GNU_SOURCE -Werror $(WARNING_FLAGS_GCC) \
            $(LIBNL_CFLAGS) $(LIBGVC_CFLAGS) -I$(abs_top_srcdir) \
            -I$(abs_top_srcdir)/include
AM_CFLAGS += $(LIBONP_CFLAGS)
CFLAGS += $(EXTRA_CFLAGS)

AM_LDFLAGS = $(LIBNL_LIBS) $(LIBGVC_LIBS)
AM_LDFLAGS += $(LIBONP_LIBS)

sbin_PROGRAMS = match
match_LDADD = $(abs_top_builddir)/lib/libmatch.la
match_SOURCES = match.c

sbin_PROGRAMS += matchd

matchd_SOURCES = matchd.c

matchd_LDADD = $(abs_top_builddir)/lib/libmatch.la \
             $(abs_top_builddir)/lib/libmatchies.la \
             $(abs_top_builddir)/lib/libmatchd.la \
             $(IES_LIBS)
matchd_CFLAGS = $(AM_CFLAGS) $(IES_CFLAGS) $(CFLAGS)

# Embedded Python shell support
if MATCHD_PYSH
matchd_CFLAGS += -DENABLE_MATCHD_PYSH
matchd_LDADD += libfmwrap.a

# Used by SWIG for extracting -I options and also required for compiling generated wrappers
AM_CFLAGS += $(IES_CFLAGS)

SWIG_INTERFACE_FILES = fm.i

BUILT_SOURCES = $(SWIG_INTERFACE_FILES:.i=.py)

SECONDARY_FILES = $(SWIG_INTERFACE_FILES:.i=.pywrap.c)

noinst_LIBRARIES = libfmwrap.a

libfmwrap_a_SOURCES = $(SWIG_INTERFACE_FILES:.i=.py) $(SWIG_INTERFACE_FILES:.i=.pywrap.c)
libfmwrap_a_CFLAGS = $(filter-out -Werror,$(AM_CFLAGS))
libfmwrap_a_CFLAGS += $(PYTHON_CPPFLAGS)

pyexec_LTLIBRARIES = _fm.la

nodist__fm_la_SOURCES = fm.py fm.pywrap.c
_fm_la_CFLAGS  = $(filter-out -Werror,$(AM_CFLAGS))
_fm_la_CFLAGS += $(PYTHON_CPPFLAGS)
_fm_la_LDFLAGS  = $(filter -L%,$(AM_LDFLAGS))
_fm_la_LDFLAGS += $(PYTHON_LDFLAGS)
_fm_la_LDFLAGS += -module -avoid-version

SWIG                = swig

VERB_SWIG    = $(verb__SWIG_$(V))
verb__SWIG_  = $(verb__SWIG_$(AM_DEFAULT_VERBOSITY))
verb__SWIG_0 = @echo "  SWIG  " $@;

.SECONDARY: $(SECONDARY_FILES)

CLEANFILES = $(BUILT_SOURCES) $(SECONDARY_FILES)

%.pywrap.c %.py: %.i
	$(VERB_SWIG)$(SWIG) -python $(filter -I%,$(AM_CFLAGS)) $(SWIG_FLAGS) -o "$*.pywrap.c" "$<"
	@$(if $(findstring -,$*),mv $(subst -,_,$*.py) $*.py,)

endif
