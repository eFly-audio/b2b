cmake_minimum_required(VERSION 3.22)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
if(APPLE)
  project(b2b VERSION 0.7.2 LANGUAGES C CXX OBJC OBJCXX)
else()
  project(b2b VERSION 0.7.2 LANGUAGES C CXX)
endif()
set(CMAKE_CXX_STANDARD 20)

# Where JUCE and the prebuilt static libraries live (the build scripts set these)
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE7" CACHE PATH "JUCE 7 source tree")
set(B2B_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/deps-install" CACHE PATH "prefix holding include/ and lib/ of the dependencies")
set(JSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/deps/libdatachannel/deps/json/include" CACHE PATH "nlohmann json")

add_subdirectory(${JUCE_PATH} juce)

if(APPLE)
  set(B2B_FORMATS VST3 AU)
else()
  set(B2B_FORMATS VST3)
endif()

juce_add_plugin(SmashLink
  COMPANY_NAME "eFly Audio"
  PLUGIN_MANUFACTURER_CODE EFly
  PLUGIN_CODE B2bv
  FORMATS ${B2B_FORMATS}
  PRODUCT_NAME "b2b"
  BUNDLE_ID "com.eflyaudio.b2b"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Network
  VST3_AUTO_MANIFEST FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "b2b sends your voice to your collaborator."
  CAMERA_PERMISSION_ENABLED TRUE
  CAMERA_PERMISSION_TEXT "b2b shares your camera with your collaborator.")

target_sources(SmashLink PRIVATE src/Processor.cpp src/Editor.cpp src/LinkEngine.cpp src/AudioLink.cpp src/VideoLink.cpp src/AppCaptureWin.cpp)
if(APPLE)
  target_sources(SmashLink PRIVATE src/MacSupport.mm)
  set_source_files_properties(src/MacSupport.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

target_include_directories(SmashLink PRIVATE ${B2B_DEPS}/include ${B2B_DEPS}/include/opus ${JSON_INCLUDE})
target_compile_definitions(SmashLink PRIVATE RTC_STATIC)
target_compile_definitions(SmashLink PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_CAMERA=1)

set(B2B_LIBS opus vpx datachannel juice usrsctp mbedtls mbedx509 mbedcrypto everest p256m)
foreach(l ${B2B_LIBS})
  target_link_libraries(SmashLink PRIVATE ${B2B_DEPS}/lib/lib${l}.a)
endforeach()

target_link_libraries(SmashLink PRIVATE juce::juce_audio_utils juce::juce_video
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(WIN32)
  target_link_libraries(SmashLink PRIVATE ws2_32 iphlpapi bcrypt crypt32 d3d11 dxgi mmdevapi ole32 pthread)
  target_link_options(SmashLink PRIVATE -static)
  target_link_options(SmashLink_VST3 PRIVATE -static)
elseif(APPLE)
  target_link_libraries(SmashLink PRIVATE "-framework CoreGraphics" "-framework AVFoundation" "-framework CoreMedia"
    "-framework CoreVideo" "-framework Security" "-weak_framework ScreenCaptureKit")
endif()

# ---------------------------------------------------------------- b2b MIDI (instrument)
if(APPLE)
  set(B2BM_FORMATS VST3 AU)
else()
  set(B2BM_FORMATS VST3)
endif()
juce_add_plugin(B2BMidi
  COMPANY_NAME "eFly Audio"
  PLUGIN_MANUFACTURER_CODE EFly
  PLUGIN_CODE B2bm
  FORMATS ${B2BM_FORMATS}
  PRODUCT_NAME "b2b MIDI"
  BUNDLE_ID "com.eflyaudio.b2bmidi"
  IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Tools
  VST3_AUTO_MANIFEST FALSE)
target_sources(B2BMidi PRIVATE src/midi/MidiPlugin.cpp src/midi/MidiCapture.cpp)
target_compile_definitions(B2BMidi PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(B2BMidi PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(WIN32)
  target_link_options(B2BMidi PRIVATE -static)
  target_link_options(B2BMidi_VST3 PRIVATE -static)
endif()
