include(BuildPlugin)
BUILD_PLUGIN(vst3effect ../Vst3Effect.cpp ../Vst3Effect.h MOCFILES ../Vst3Effect.h)
target_link_libraries(vst3effect lmms_vst3host)
target_include_directories(vst3effect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
