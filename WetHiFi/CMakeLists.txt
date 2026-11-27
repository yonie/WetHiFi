cmake_minimum_required(VERSION 3.14.0)

option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Enable VST 3 Plug-in Examples" OFF)
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Enable VST 3 Hosting Examples" OFF)

set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "")

set(vst3sdk_SOURCE_DIR "../vst3sdk")
if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Path to VST3 SDK is empty!")
endif()

project(WetHiFi
    # This is your plug-in version number. Change it here only.
    VERSION 1.0.0.0
    DESCRIPTION "WetHiFi VST 3 Plug-in"
)

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(WetHiFi
    source/version.h
    source/wethificids.h
    source/hifiengine.h
    source/hifiengine.cpp
    source/wethifiprocessor.h
    source/wethifiprocessor.cpp
    source/wethificontroller.h
    source/wethificontroller.cpp
    source/wethifientry.cpp
    source/hifiknob.h
    source/hifiknob.cpp
    source/lampview.h
    source/hifiscope.h
    source/peakled.h
    source/peakled.cpp
    source/meterlevel.h
    source/lampview.cpp
    source/hifieditor.h
    source/weteditor.h
    source/monobus.h
    source/customviewcreator.h
    source/customviewcreator.cpp
)

#- VSTGUI Wanted ----
if(SMTG_ENABLE_VSTGUI_SUPPORT)
    target_sources(WetHiFi
        PRIVATE
            resource/wethifieditor.uidesc
    )
    target_link_libraries(WetHiFi
        PRIVATE
            vstgui_support
    )
    # Everything the editor loads at runtime. The knob filmstrips and the
    # backplate are generated in the private toolkit from the panel art.
    smtg_target_add_plugin_resources(WetHiFi
        RESOURCES
            "resource/wethifieditor.uidesc"
            "resource/backplate.png"
            "resource/knob_highs.png"
            "resource/knob_mids.png"
            "resource/knob_bass.png"
            "resource/peak_led.png"
    )
endif(SMTG_ENABLE_VSTGUI_SUPPORT)
# -------------------

target_link_libraries(WetHiFi
    PRIVATE
        sdk
)

smtg_target_configure_version_file(WetHiFi)

if(SMTG_MAC)
    smtg_target_set_bundle(WetHiFi
        BUNDLE_IDENTIFIER org.yonie.vst3.wethifi
        COMPANY_NAME "WET VST"
    )
    smtg_target_set_debug_executable(WetHiFi
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
elseif(SMTG_WIN)
    target_sources(WetHiFi PRIVATE
        resource/win32resource.rc
    )
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT WetHiFi)

        smtg_target_set_debug_executable(WetHiFi
            "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
            "--pluginfolder \"$(OutDir)/\""
        )
    endif()
elseif(SMTG_LINUX)
    # Linux uses default settings
endif(SMTG_MAC)

# Audio Unit v2 wrapper for Logic. Only configured with the Xcode generator
# and SMTG_AUDIOUNIT_SDK_PATH pointing at Apple's AudioUnitSDK.
if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${vst3sdk_SOURCE_DIR}/cmake/modules")
    include(SMTG_AddVST3AuV2)
    # The AU version follows the project version; a host caches an AU by it.
    set(AU_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
    math(EXPR AU_VERSION_INT "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
    configure_file(resource/au-info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/au-info.plist @ONLY)
    smtg_target_add_auv2(WetHiFi-au
        BUNDLE_NAME WetHiFi
        BUNDLE_IDENTIFIER org.yonie.audiounit.wethifi
        INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_BINARY_DIR}/au-info.plist
        VST3_PLUGIN_TARGET WetHiFi)
endif(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)
