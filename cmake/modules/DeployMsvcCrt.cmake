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

	set(version_roots "")
	if(DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
		file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" redist_dir)
		string(REGEX REPLACE "[/\\\\]+$" "" redist_dir "${redist_dir}")
		list(APPEND version_roots "${redist_dir}")
		get_filename_component(msvc_redist "${redist_dir}" DIRECTORY)
		file(GLOB version_dirs LIST_DIRECTORIES true "${msvc_redist}/*")
		list(SORT version_dirs COMPARE NATURAL ORDER DESCENDING)
		foreach(dir ${version_dirs})
			if(IS_DIRECTORY "${dir}")
				list(APPEND version_roots "${dir}")
			endif()
		endforeach()
	endif()

	# cl.exe lives at .../VC/Tools/MSVC/<ver>/bin/Host<arch>/<target>/cl.exe.
	# The matching redist is .../VC/Redist/MSVC/<ver>, which may not equal
	# VCToolsRedistDir on newer Visual Studio layouts.
	if(CMAKE_CXX_COMPILER)
		get_filename_component(_target_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
		get_filename_component(_host_dir "${_target_dir}" DIRECTORY)
		get_filename_component(_bin_dir "${_host_dir}" DIRECTORY)
		get_filename_component(_ver_dir "${_bin_dir}" DIRECTORY)
		get_filename_component(_ver_name "${_ver_dir}" NAME)
		get_filename_component(_msvc_dir "${_ver_dir}" DIRECTORY)
		get_filename_component(_tools_dir "${_msvc_dir}" DIRECTORY)
		get_filename_component(_vc_dir "${_tools_dir}" DIRECTORY)
		file(GLOB compiler_versions LIST_DIRECTORIES true "${_vc_dir}/Redist/MSVC/*")
		list(SORT compiler_versions COMPARE NATURAL ORDER DESCENDING)
		list(INSERT compiler_versions 0 "${_vc_dir}/Redist/MSVC/${_ver_name}")
		foreach(dir ${compiler_versions})
			if(IS_DIRECTORY "${dir}")
				list(APPEND version_roots "${dir}")
			endif()
		endforeach()
	endif()
	list(REMOVE_DUPLICATES version_roots)

	# Release CRT first, then the onecore variant. Skip debug_nonredist.
	set(dlls "")
	foreach(root ${version_roots})
		set(found "")
		foreach(layout
				"${root}/${arch}/Microsoft.VC*.CRT"
				"${root}/onecore/${arch}/Microsoft.VC*.CRT")
			file(GLOB layout_dlls "${layout}/*.dll")
			list(APPEND found ${layout_dlls})
		endforeach()
		list(FILTER found EXCLUDE REGEX "[Dd]\\.dll$")
		if(found)
			set(dlls "${found}")
			break()
		endif()
	endforeach()

	if(NOT dlls)
		set(fallback_dirs "")
		if(CMAKE_CXX_COMPILER)
			get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
			list(APPEND fallback_dirs "${_compiler_dir}")
		endif()
		if(bitness EQUAL 32)
			list(APPEND fallback_dirs "C:/Windows/SysWOW64")
		else()
			list(APPEND fallback_dirs "C:/Windows/System32")
		endif()
		foreach(dir ${fallback_dirs})
			foreach(name msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
				if(EXISTS "${dir}/${name}")
					list(APPEND dlls "${dir}/${name}")
				endif()
			endforeach()
		endforeach()
		list(REMOVE_DUPLICATES dlls)
	endif()

	if(NOT dlls)
		message(WARNING
			"No ${arch} MSVC CRT DLLs found for RemoteVstPlugin${bitness}. "
			"The installer will not ship them, so 32-bit VST plugins will fail to load "
			"on a PC that does not already have the x86 Visual C++ runtime. "
			"VCToolsRedistDir='$ENV{VCToolsRedistDir}' compiler='${CMAKE_CXX_COMPILER}'")
		return()
	endif()

	install(FILES ${dlls} DESTINATION "${destination}")
endfunction()
