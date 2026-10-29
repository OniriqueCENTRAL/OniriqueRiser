cmake_minimum_required(VERSION 3.22)

# Mac : binaire universel (Intel + Apple Silicon), à définir avant project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(OniriqueRiser VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# AU (Logic, GarageBand) uniquement sur Mac
# AAX (Pro Tools) mis de côté pour l'instant : seulement si on le demande (cmake -DONIRIQUE_AAX=ON)
option(ONIRIQUE_AAX "Compiler aussi le format AAX" OFF)
set(ONIRIQUE_FORMATS VST3 Standalone)
if(ONIRIQUE_AAX)
    list(APPEND ONIRIQUE_FORMATS AAX)
endif()
if(APPLE)
    list(APPEND ONIRIQUE_FORMATS AU)
endif()

# AAX (Pro Tools) : SDK Avid téléchargé sur developer.avid.com. Sans lui, JUCE prend le SDK AAX qu'il embarque.
set(ONIRIQUE_AAX_SDK "E:/DEV/aax-sdk-2-8-0" CACHE PATH "Dossier du SDK AAX d'Avid")
if(EXISTS "${ONIRIQUE_AAX_SDK}/Interfaces")
    juce_set_aax_sdk_path("${ONIRIQUE_AAX_SDK}")
endif()

juce_add_plugin(OniriqueRiser
    COMPANY_NAME                Onirique
    PLUGIN_MANUFACTURER_CODE    Onir
    PLUGIN_CODE                 Orsr
    FORMATS                     ${ONIRIQUE_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    PRODUCT_NAME                "ONIRIQUE RISER"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Fx
    AAX_IDENTIFIER              store.onirique.riser
    AAX_CATEGORY                AAX_ePlugInCategory_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueRiser PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UIKit.cpp
    Source/DirectPreview.cpp)

# logo du site (sigil blanc) et polices de la charte (Unbounded, Hanken Grotesk, JetBrains Mono), intégrés au binaire
juce_add_binary_data(OniriqueAssets SOURCES Resources/logo.png
    Resources/Unbounded800.ttf Resources/HankenGrotesk600.ttf Resources/JetBrainsMono500.ttf Resources/JetBrainsMono600.ttf)

target_include_directories(OniriqueRiser PRIVATE Source)

target_compile_definitions(OniriqueRiser PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Mac : sans cette option, Core Graphics fusionne toutes les petites zones animées (étoiles, logo, mètres,
# analyseur) en une seule grande zone et redessine presque toute la fenêtre à chaque image. Avec elle, JUCE
# passe par un rendu Metal qui ne redessine que les zones demandées.
if(APPLE)
    target_compile_definitions(OniriqueRiser PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

target_link_libraries(OniriqueRiser
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        OniriqueAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
