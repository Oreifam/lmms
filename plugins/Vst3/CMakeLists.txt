if(NOT WANT_VST3)
	return()
endif()

if(CMAKE_VERSION VERSION_LESS 3.18)
	message(FATAL_ERROR "WANT_VST3 requires CMake 3.18 or newer to download the pinned SDK")
endif()

# Steinberg VST 3 SDK 3.8.1 (MIT), tag v3.8.1_build_84.
# The superproject and its three required submodules are pinned by commit.
set(LMMS_VST3SDK_COMMIT 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96)
set(LMMS_VST3SDK_BASE_COMMIT fcf9da0bd27a16f7f03773a3a39822f28f5c8477)
set(LMMS_VST3SDK_INTERFACES_COMMIT 4f547e8e102b47de4a8b8aaf343c73b700786372)
set(LMMS_VST3SDK_PUBLIC_COMMIT 586dc5e6c8012c3e4b01c79389375cbe96bdb1da)

set(LMMS_VST3SDK_ROOT "${CMAKE_BINARY_DIR}/_deps/vst3sdk-src")

function(lmms_vst3_download url archive)
	if(EXISTS "${archive}")
		return()
	endif()
	message(STATUS "Downloading ${url}")
	file(DOWNLOAD "${url}" "${archive}" STATUS status TIMEOUT 180)
	list(GET status 0 code)
	if(NOT code EQUAL 0)
		list(GET status 1 message)
		file(REMOVE "${archive}")
		message(FATAL_ERROR "Failed to download ${url}: ${message}")
	endif()
endfunction()

function(lmms_vst3_overlay repo commit destination)
	set(archive "${CMAKE_BINARY_DIR}/_deps/${repo}-${commit}.tar.gz")
	set(unpack "${CMAKE_BINARY_DIR}/_deps/${repo}-${commit}")
	lmms_vst3_download("https://github.com/steinbergmedia/${repo}/archive/${commit}.tar.gz" "${archive}")
	file(MAKE_DIRECTORY "${unpack}")
	file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${unpack}")
	file(GLOB extracted "${unpack}/${repo}-*")
	list(LENGTH extracted count)
	if(NOT count EQUAL 1)
		message(FATAL_ERROR "Unexpected archive layout for ${repo}: ${extracted}")
	endif()
	list(GET extracted 0 extracted)
	if(destination STREQUAL ".")
		file(COPY "${extracted}/" DESTINATION "${LMMS_VST3SDK_ROOT}")
	else()
		file(COPY "${extracted}/" DESTINATION "${LMMS_VST3SDK_ROOT}/${destination}")
	endif()
endfunction()

if(NOT EXISTS "${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module.h")
	file(MAKE_DIRECTORY "${LMMS_VST3SDK_ROOT}")
	lmms_vst3_overlay(vst3sdk "${LMMS_VST3SDK_COMMIT}" ".")
	lmms_vst3_overlay(vst3_base "${LMMS_VST3SDK_BASE_COMMIT}" "base")
	lmms_vst3_overlay(vst3_pluginterfaces "${LMMS_VST3SDK_INTERFACES_COMMIT}" "pluginterfaces")
	lmms_vst3_overlay(vst3_public_sdk "${LMMS_VST3SDK_PUBLIC_COMMIT}" "public.sdk")
endif()

set(LMMS_VST3_SDK_SOURCES
	${LMMS_VST3SDK_ROOT}/base/source/baseiids.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fbuffer.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fdebug.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fdynlib.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fobject.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fstreamer.cpp
	${LMMS_VST3SDK_ROOT}/base/source/fstring.cpp
	${LMMS_VST3SDK_ROOT}/base/source/timer.cpp
	${LMMS_VST3SDK_ROOT}/base/source/updatehandler.cpp
	${LMMS_VST3SDK_ROOT}/base/thread/source/fcondition.cpp
	${LMMS_VST3SDK_ROOT}/base/thread/source/flock.cpp
	${LMMS_VST3SDK_ROOT}/pluginterfaces/base/conststringtable.cpp
	${LMMS_VST3SDK_ROOT}/pluginterfaces/base/coreiids.cpp
	${LMMS_VST3SDK_ROOT}/pluginterfaces/base/funknown.cpp
	${LMMS_VST3SDK_ROOT}/pluginterfaces/base/ustring.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/commoniids.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/memorystream.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/openurl.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/readfile.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/systemclipboard_linux.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/systemclipboard_win32.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/threadchecker_linux.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp
	${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp
)

if(WIN32)
	list(APPEND LMMS_VST3_SDK_SOURCES ${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp)
elseif(APPLE)
	message(FATAL_ERROR "WANT_VST3 is not implemented for macOS in this round")
else()
	list(APPEND LMMS_VST3_SDK_SOURCES ${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp)
endif()

add_library(lmms_vst3_sdk STATIC ${LMMS_VST3_SDK_SOURCES})
set_target_properties(lmms_vst3_sdk PROPERTIES
	SYSTEM TRUE
	POSITION_INDEPENDENT_CODE ON
)
target_include_directories(lmms_vst3_sdk PUBLIC "${LMMS_VST3SDK_ROOT}")
target_compile_definitions(lmms_vst3_sdk PRIVATE RELEASE=1)
target_compile_features(lmms_vst3_sdk PUBLIC cxx_std_17)
if(NOT WIN32)
	target_link_libraries(lmms_vst3_sdk PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(lmms_vst3host STATIC Vst3Instance.cpp Vst3Panel.cpp)
set_target_properties(lmms_vst3host PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(lmms_vst3host PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_BINARY_DIR}"
)
target_link_libraries(lmms_vst3host PUBLIC
	lmms_vst3_sdk
	Qt${QT_VERSION_MAJOR}::Core
	Qt${QT_VERSION_MAJOR}::Widgets
)

add_subdirectory(instrument)
add_subdirectory(effect)

set(LMMS_VST3SDK_ROOT "${LMMS_VST3SDK_ROOT}" CACHE INTERNAL "Pinned Steinberg VST3 SDK source root")
