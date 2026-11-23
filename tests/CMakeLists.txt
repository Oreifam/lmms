include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/HearingGuardTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/tracks/AutomationTrackTest.cpp
)

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME})

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
endforeach()

if(WANT_VST3 AND TARGET lmms_vst3host)
	set(LMMS_VST3_PROBE_SOURCES
		src/vst3/Probe.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/common/pluginview.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/main/pluginfactory.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/main/moduleinit.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstaudioeffect.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstbus.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstcomponent.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstcomponentbase.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vsteditcontroller.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstnoteexpressiontypes.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstparameters.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstrepresentation.cpp
		${LMMS_VST3SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
	)
	if(WIN32)
		list(APPEND LMMS_VST3_PROBE_SOURCES ${LMMS_VST3SDK_ROOT}/public.sdk/source/main/dllmain.cpp)
	else()
		list(APPEND LMMS_VST3_PROBE_SOURCES ${LMMS_VST3SDK_ROOT}/public.sdk/source/main/linuxmain.cpp)
	endif()
	add_library(lmms_vst3_probe MODULE ${LMMS_VST3_PROBE_SOURCES})
	set_target_properties(lmms_vst3_probe PROPERTIES
		PREFIX ""
		OUTPUT_NAME "LmmsVst3Probe"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		SYSTEM TRUE
	)
	target_link_libraries(lmms_vst3_probe PRIVATE lmms_vst3_sdk)
	target_compile_definitions(lmms_vst3_probe PRIVATE RELEASE=1 PROJECT_INCLUDES_VSTEDITCONTROLLER=1)

	add_executable(Vst3HostTest src/core/Vst3HostTest.cpp)
	add_test(NAME Vst3HostTest COMMAND Vst3HostTest)
	set_tests_properties(Vst3HostTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	target_include_directories(Vst3HostTest PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES> ${CMAKE_SOURCE_DIR}/plugins/Vst3)
	target_static_libraries(Vst3HostTest PRIVATE lmmsobjs lmms_vst3host)
	target_link_libraries(Vst3HostTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY} lmms_vst3host)
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3HostTest PRIVATE
		LMMS_TESTING
		"LMMS_VST3_PROBE_PATH=\"$<TARGET_FILE:lmms_vst3_probe>\""
	)
	add_dependencies(Vst3HostTest lmms_vst3_probe)
endif()
