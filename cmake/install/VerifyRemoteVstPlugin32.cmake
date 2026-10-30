# Runs at install/package time. Fails the package if RemoteVstPlugin32.exe
# depends on a non-system DLL that was not installed beside it.
if(NOT DEFINED REMOTE_VST32_DIR)
	message(FATAL_ERROR "REMOTE_VST32_DIR is not set")
endif()

set(_exe "${REMOTE_VST32_DIR}/RemoteVstPlugin32.exe")
if(NOT EXISTS "${_exe}")
	message(FATAL_ERROR "RemoteVstPlugin32.exe was not installed in ${REMOTE_VST32_DIR}")
endif()

find_program(_DUMPBIN dumpbin)
if(NOT _DUMPBIN)
	message(FATAL_ERROR "dumpbin was not found; cannot verify RemoteVstPlugin32 dependencies")
endif()

execute_process(
	COMMAND "${_DUMPBIN}" /dependents "${_exe}"
	OUTPUT_VARIABLE _deps
	ERROR_VARIABLE _deps_err
	RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "dumpbin /dependents failed for ${_exe}: ${_deps_err}")
endif()

set(_system_prefixes
	"api-ms-win"
	"ext-ms-win"
	"advapi32"
	"comctl32"
	"comdlg32"
	"gdi32"
	"imm32"
	"kernel32"
	"msvcrt"
	"ntdll"
	"ole32"
	"oleaut32"
	"rpcrt4"
	"setupapi"
	"shell32"
	"shlwapi"
	"ucrtbase"
	"user32"
	"version"
	"winmm"
	"ws2_32"
)

string(REGEX MATCHALL "[A-Za-z0-9_+]+\\.dll" _dlls "${_deps}")
if(NOT _dlls)
	message(FATAL_ERROR "dumpbin reported no DLL dependencies for ${_exe}:\n${_deps}")
endif()

set(_missing "")
foreach(_dll IN LISTS _dlls)
	string(TOLOWER "${_dll}" _low)
	string(LENGTH "${_low}" _low_len)
	set(_system FALSE)
	foreach(_prefix IN LISTS _system_prefixes)
		string(LENGTH "${_prefix}" _prefix_len)
		if(_low_len GREATER_EQUAL _prefix_len)
			string(SUBSTRING "${_low}" 0 ${_prefix_len} _head)
			if(_head STREQUAL _prefix)
				set(_system TRUE)
			endif()
		endif()
	endforeach()
	if(NOT _system AND NOT EXISTS "${REMOTE_VST32_DIR}/${_dll}")
		list(APPEND _missing "${_dll}")
	endif()
endforeach()

if(_missing)
	message(FATAL_ERROR
		"RemoteVstPlugin32.exe is missing non-system DLLs in ${REMOTE_VST32_DIR}: ${_missing}\n${_deps}")
endif()

message(STATUS "RemoteVstPlugin32 dependency check passed (${REMOTE_VST32_DIR})")
