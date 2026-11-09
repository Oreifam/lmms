# INSTALL_EXTERNAL_PROJECT: install a project created with ExternalProject_Add in the
# parent project's install time.
#
# Description:
# In a regular scenario, cmake will install external projects
# BEFORE actually building the parent project. Since the building
# process may use installed components from the project.
# We want to give the external project the ability to install
# files directly to the parent's install. Therefore, we have to
# manually trigger the install stage with the parent's INSTALL_PREFIX.
MACRO(INSTALL_EXTERNAL_PROJECT name)
	ExternalProject_Get_Property(${name} BINARY_DIR)

	install(CODE "include(\"${BINARY_DIR}/cmake_install.cmake\")")
ENDMACRO()

IF(LMMS_BUILD_WIN32 AND NOT LMMS_BUILD_WIN64)
	ADD_SUBDIRECTORY(RemoteVstPlugin)
ELSEIF(LMMS_BUILD_WIN64 AND MSVC)
	# The parent build is single-config (Ninja + CMAKE_BUILD_TYPE), but this
	# external project uses the Visual Studio generator. Without --config,
	# that generator builds and installs Debug, and the bridge then depends
	# on the debug CRT (MSVCP140D.dll / VCRUNTIME140D.dll).
	if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "None")
		set(_remote_vst32_cfg "${CMAKE_BUILD_TYPE}")
	elseif(CMAKE_CONFIGURATION_TYPES)
		set(_remote_vst32_cfg "$<CONFIG>")
	else()
		set(_remote_vst32_cfg "RelWithDebInfo")
	endif()

	ExternalProject_Add(RemoteVstPlugin32
		"${EXTERNALPROJECT_ARGS}"
		CMAKE_GENERATOR "${LMMS_MSVC_GENERATOR}"
		CMAKE_GENERATOR_PLATFORM Win32
		#CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
		CMAKE_ARGS
			"${EXTERNALPROJECT_CMAKE_ARGS}"
			"-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
		BUILD_COMMAND "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --config "${_remote_vst32_cfg}"
	)

	ExternalProject_Get_Property(RemoteVstPlugin32 BINARY_DIR)
	install(CODE "
		set(CMAKE_INSTALL_CONFIG_NAME \"${_remote_vst32_cfg}\")
		set(BUILD_TYPE \"${_remote_vst32_cfg}\")
		include(\"${BINARY_DIR}/cmake_install.cmake\")
	")
ELSEIF(LMMS_BUILD_LINUX)
	ExternalProject_Add(RemoteVstPlugin32
		"${EXTERNALPROJECT_ARGS}"
		CMAKE_ARGS
			"${EXTERNALPROJECT_CMAKE_ARGS}"
			"-DCMAKE_CXX_COMPILER=${WINEGCC}"
			"-DCMAKE_CXX_FLAGS=-m32"
	)

	INSTALL(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/../32/RemoteVstPlugin32" "${CMAKE_CURRENT_BINARY_DIR}/../32/RemoteVstPlugin32.exe.so" DESTINATION "${PLUGIN_DIR}/32")
ELSEIF(CMAKE_TOOLCHAIN_FILE_32)
	ExternalProject_Add(RemoteVstPlugin32
		"${EXTERNALPROJECT_ARGS}"
		CMAKE_ARGS
			"${EXTERNALPROJECT_CMAKE_ARGS}"
			"-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_32}"
			"-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE_32}"
	)
	INSTALL_EXTERNAL_PROJECT(RemoteVstPlugin32)
ELSE()
	MESSAGE(WARNING "Can't build RemoteVstPlugin32, unknown environment. Please supply CMAKE_TOOLCHAIN_FILE_32 and optionally CMAKE_PREFIX_PATH_32")
	RETURN()
ENDIF()

