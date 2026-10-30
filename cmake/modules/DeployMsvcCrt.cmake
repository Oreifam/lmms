# Copy the non-debug MSVC CRT DLLs for one architecture into an install destination.
# The 32-bit VST bridge is a separate process. Windows will not load the 64-bit
# CRT that sits beside lmms.exe, and a build machine that has the x86 CRT in
# SysWOW64 makes GetPrerequisites treat those DLLs as system libraries.
function(lmms_deploy_msvc_crt bitness destination)
	if(NOT MSVC)
		return()
	endif()

	if(bitness EQUAL 32)
		set(arch x86)
	elseif(bitness EQUAL 64)
		set(arch x64)
	else()
		message(FATAL_ERROR "lmms_deploy_msvc_crt: unsupported bitness ${bitness}")
	endif()

	set(roots "")
	if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
		list(APPEND roots "$ENV{VCToolsRedistDir}")
	endif()

	# cl.exe lives at .../VC/Tools/MSVC/<ver>/bin/Host<arch>/<target>/cl.exe
	if(CMAKE_CXX_COMPILER)
		get_filename_component(_target_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
		get_filename_component(_host_dir "${_target_dir}" DIRECTORY)
		get_filename_component(_bin_dir "${_host_dir}" DIRECTORY)
		get_filename_component(_ver_dir "${_bin_dir}" DIRECTORY)
		get_filename_component(_ver_name "${_ver_dir}" NAME)
		get_filename_component(_msvc_dir "${_ver_dir}" DIRECTORY)
		get_filename_component(_tools_dir "${_msvc_dir}" DIRECTORY)
		get_filename_component(_vc_dir "${_tools_dir}" DIRECTORY)
		list(APPEND roots "${_vc_dir}/Redist/MSVC/${_ver_name}")
	endif()

	set(dlls "")
	foreach(root ${roots})
		file(GLOB found "${root}/${arch}/Microsoft.VC*.CRT/*.dll")
		list(APPEND dlls ${found})
	endforeach()
	# Drop the debug CRT (*d.dll) without dropping vcruntime140_1.dll.
	list(FILTER dlls EXCLUDE REGEX "[Dd]\\.dll$")
	list(REMOVE_DUPLICATES dlls)

	if(NOT dlls)
		message(FATAL_ERROR
			"No ${arch} MSVC CRT DLLs found for RemoteVstPlugin${bitness}. "
			"VCToolsRedistDir='$ENV{VCToolsRedistDir}' compiler='${CMAKE_CXX_COMPILER}'")
	endif()

	install(FILES ${dlls} DESTINATION "${destination}")
endfunction()
