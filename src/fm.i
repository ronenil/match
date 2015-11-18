%module fm
%include <typemaps.i>
%include <carrays.i>
%include <cmalloc.i>
%include <cdata.i>
%include <cpointer.i>
%{
#include <fm_sdk.h>
#include <fm_sdk_int.h>

/*
 * Stub for a function which is declared but not implemented
 */
fm_int fmFindUnusedGlorts(fm_int            sw,
                          fm_int            numGlorts,
                          fm_int            first,
                          fm_glortCamEntry *camEntry)
{
	return FM_ERR_UNSUPPORTED;
}

%}


%include <fm_std.h>
%include <common/fm_errno.h>
%include <common/fm_property.h>
%include <api/fm_api_attr.h>
%include <api/fm_api_port.h>
%include <api/fm_api_vlan.h>
%include <alos/fm_alos_logging.h>
%include <debug/fm_debug.h>
%include <api/internal/fm_api_lport_int.h>
%include <platforms/libertyTrail/platform_defines.h>
%include <platforms/libertyTrail/platform_debug.h>
%include <platforms/libertyTrail/platform_config.h>
%include <platforms/libertyTrail/platform_port.h>

%allocators(void);
%array_functions(fm_byte, byte);
%array_functions(fm_int, int);
%array_functions(fm_int16, int16);
%array_functions(fm_int32, int32);
%array_functions(fm_int64, int64);
%array_functions(fm_uint, uint);
%array_functions(fm_uint16, uint16);
%array_functions(fm_uint32, uint32);
%array_functions(fm_uint64, uint64);
%array_functions(fm_float, float);
%array_functions(fm_pciePortType, pciePortType);
%array_functions(fm_portType, portType);
