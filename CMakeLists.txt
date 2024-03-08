cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(breezeenhanced)
set(PROJECT_VERSION "6.0.80")
set(PROJECT_VERSION_MAJOR 6)

include(WriteBasicConfigVersionFile)
include(FeatureSummary)

set(QT_MAJOR_VERSION 6)
set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.0.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.82")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CMakePackageConfigHelpers)
include(ECMInstallIcons)
include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(GenerateExportHeader)
include(KDEClangFormat)
# include(GtkUpdateIconCache)

find_package(KDecoration2 REQUIRED)

if(NOT WIN32 AND NOT APPLE)
    find_package(KF6KCMUtils ${KF6_MIN_VERSION})
    set_package_properties(KF6KCMUtils PROPERTIES
        TYPE REQUIRED
        DESCRIPTION "Helps create configuration modules"
        PURPOSE "KCMUtils used for the configuration modules or the decoration and Qt Style"
    )
endif()


# old stuff
add_definitions(-DTRANSLATION_DOMAIN="breeze_kwin_deco")


find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS CoreAddons GuiAddons ConfigWidgets WindowSystem I18n)
find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Widgets DBus)

################# includes #################
add_subdirectory(libbreezecommon)

################# newt target #################
### plugin classes
set(breezeenhanced_SRCS
    breezebutton.cpp
    breezedecoration.cpp
    breezeexceptionlist.cpp
    breezesettingsprovider.cpp)

kconfig_add_kcfg_files(breezeenhanced_SRCS breezesettings.kcfgc)

### config classes
### they are kept separately because they might move in a separate library in the future
set(breezeenhanced_config_SRCS
    config/breezeconfigwidget.cpp
    config/breezedetectwidget.cpp
    config/breezeexceptiondialog.cpp
    config/breezeexceptionlistwidget.cpp
    config/breezeexceptionmodel.cpp
    config/breezeitemmodel.cpp
)

set(breezeenhanced_config_PART_FORMS
   config/ui/breezeconfigurationui.ui
   config/ui/breezeexceptiondialog.ui
   config/ui/breezeexceptionlistwidget.ui
)

ki18n_wrap_ui(breezeenhanced_config_PART_FORMS_HEADERS ${breezeenhanced_config_PART_FORMS})

### build library
add_library(breezeenhanced MODULE
    ${breezeenhanced_SRCS}
    ${breezeenhanced_config_SRCS}
    ${breezeenhanced_config_PART_FORMS_HEADERS})

target_link_libraries(breezeenhanced
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
    PRIVATE
        breezeenhancedcommon6
        KDecoration2::KDecoration
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::ConfigWidgets
        KF6::GuiAddons
        KF6::I18n
        KF6::KCMUtils
        KF6::WindowSystem)

install(TARGETS breezeenhanced DESTINATION ${KDE_INSTALL_PLUGINDIR}/${KDECORATION_PLUGIN_DIR})
kcmutils_generate_desktop_file(breezeenhanced)