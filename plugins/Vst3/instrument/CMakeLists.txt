include(BuildPlugin)
BUILD_PLUGIN(vst3instrument ../Vst3Instrument.cpp ../Vst3Instrument.h MOCFILES ../Vst3Instrument.h)
target_link_libraries(vst3instrument lmms_vst3host)
target_include_directories(vst3instrument PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
