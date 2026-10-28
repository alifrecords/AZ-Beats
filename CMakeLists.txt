cmake_minimum_required(VERSION 3.22)

project(AZBetas VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

if(MSVC)
    add_compile_options(/bigobj)
endif()

# JUCE: the JUCE folder next to this file, or -DJUCE_DIR=/path/to/JUCE
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

set(AZ_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AZ_FORMATS AU)
endif()

set(AZ_NEEDS_CURL FALSE)
if(UNIX AND NOT APPLE)
    set(AZ_NEEDS_CURL TRUE)
endif()

# Public key for licence verification (written by admin/admin.html).
juce_add_binary_data(AZBetasData SOURCES config/license_public_key.txt)
set_target_properties(AZBetasData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

set(AZ_CORE_SOURCES
    Source/License.cpp
    Source/Catalog.cpp
    Source/LoopRender.cpp
    Source/Modernize.cpp)

set(AZ_SOURCES
    ${AZ_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LibraryView.cpp
    Source/ModernizeView.cpp
    Source/PadView.cpp
    Source/AccountView.cpp)

set(AZ_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

set(AZ_INCLUDES Source)
set(AZ_SYS_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/signalsmith-stretch/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/signalsmith-linear/include)

juce_add_plugin(AZBetas
    COMPANY_NAME                "Azimxodjayev Music"
    COMPANY_WEBSITE             "https://t.me/Azimxodjayev_music_bot"
    BUNDLE_ID                   "com.azimxodjayevmusic.azbetas"
    PLUGIN_MANUFACTURER_CODE    Azmx
    PLUGIN_CODE                 Azbt
    PRODUCT_NAME                "AZ BETAS"
    FORMATS                     ${AZ_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    NEEDS_CURL                  ${AZ_NEEDS_CURL}
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(AZBetas PRIVATE ${AZ_SOURCES})
target_include_directories(AZBetas PRIVATE ${AZ_INCLUDES})
target_include_directories(AZBetas SYSTEM PRIVATE ${AZ_SYS_INCLUDES})
target_compile_definitions(AZBetas PUBLIC ${AZ_DEFS})
if(AZ_NEEDS_CURL)
    target_compile_definitions(AZBetas PUBLIC JUCE_USE_CURL=1)
else()
    target_compile_definitions(AZBetas PUBLIC JUCE_USE_CURL=0)
endif()

target_link_libraries(AZBetas
    PRIVATE
        AZBetasData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------
# Offline tests (no DAW needed):  cmake -DAZ_BUILD_TESTS=ON
# ---------------------------------------------------------------
option(AZ_BUILD_TESTS "Build offline tests and screenshot tool" OFF)
if(AZ_BUILD_TESTS)
    juce_add_console_app(AZBetasTest PRODUCT_NAME "AZBetasTest")
    target_sources(AZBetasTest PRIVATE Tests/OfflineTest.cpp ${AZ_CORE_SOURCES})
    target_include_directories(AZBetasTest PRIVATE ${AZ_INCLUDES})
    target_include_directories(AZBetasTest SYSTEM PRIVATE ${AZ_SYS_INCLUDES})
    target_compile_definitions(AZBetasTest PRIVATE ${AZ_DEFS} JUCE_USE_CURL=1
        JucePlugin_Name="AZ BETAS" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0 JucePlugin_VersionString="1.0.0")
    target_link_libraries(AZBetasTest PRIVATE AZBetasData juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography juce::juce_recommended_config_flags)
    if(AZ_NEEDS_CURL)
        find_package(CURL REQUIRED)
        target_link_libraries(AZBetasTest PRIVATE CURL::libcurl)
    endif()
endif()

if(AZ_BUILD_TESTS)
    juce_add_console_app(AZBetasScreenshot PRODUCT_NAME "AZBetasScreenshot")
    target_sources(AZBetasScreenshot PRIVATE Tests/Screenshot.cpp ${AZ_SOURCES})
    target_include_directories(AZBetasScreenshot PRIVATE ${AZ_INCLUDES})
    target_include_directories(AZBetasScreenshot SYSTEM PRIVATE ${AZ_SYS_INCLUDES})
    target_compile_definitions(AZBetasScreenshot PRIVATE ${AZ_DEFS} JUCE_USE_CURL=1
        JucePlugin_Name="AZ BETAS" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0 JucePlugin_VersionString="1.0.0"
        JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AZBetasScreenshot PRIVATE AZBetasData juce::juce_audio_utils juce::juce_dsp
        juce::juce_cryptography juce::juce_recommended_config_flags)
    if(AZ_NEEDS_CURL)
        target_link_libraries(AZBetasScreenshot PRIVATE CURL::libcurl)
    endif()
endif()
