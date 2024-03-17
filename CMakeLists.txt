cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(breezeenhanced)
set(PROJECT_VERSION "6.0.0")
set(PROJECT_VERSION_MAJOR 6)

include(WriteBasicConfigVersionFile)
include(FeatureSummary)
set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.0.0")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_SOURCE_DIR}/cmake)

include(ECMInstallIcons)
include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(GenerateExportHeader)
# include(GtkUpdateIconCache)

find_package(KDecoration2 REQUIRED)

find_package(KF6KCMUtils ${KF6_MIN_VERSION})
set_package_properties(KF6KCMUtils PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Helps create configuration modules"
    PURPOSE "KCMUtils used for the configuration modules or the decoration and Qt Style"
)

# old stuff
add_definitions(-DTRANSLATION_DOMAIN="breeze_kwin_deco")


find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS GuiAddons WindowSystem I18n)
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Widgets DBus)

################# includes #################
add_subdirectory(libbreezecommon)

################# newt target #################
### plugin classes
set(breezeenhanced_SRCS
    breezebutton.cpp
    breezedecoration.cpp
    breezesettingsprovider.cpp)

### config classes
set(breezeenhanced_config_SRCS
    breezeexceptionlist.cpp
    config/breezeconfigwidget.cpp
    config/breezedetectwidget.cpp
    config/breezeexceptiondialog.cpp
    config/breezeexceptionlistwidget.cpp
    config/breezeexceptionmodel.cpp
    config/breezeitemmodel.cpp
)
ki18n_wrap_ui(breezeenhanced_config_SRCS
   config/ui/breezeconfigurationui.ui
   config/ui/breezeexceptiondialog.ui
   config/ui/breezeexceptionlistwidget.ui
)
add_library(breezeenhanced_STATIC STATIC ${breezeenhanced_config_SRCS})
# Needed to link this static lib to shared libs
set_property(TARGET breezeenhanced_STATIC PROPERTY POSITION_INDEPENDENT_CODE ON)
kconfig_add_kcfg_files(breezeenhanced_STATIC breezesettings.kcfgc)
target_link_libraries(breezeenhanced_STATIC
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        KDecoration2::KDecoration
    PRIVATE
        KF6::I18n
        KF6::WindowSystem
        KF6::KCMUtils
)

### build library
add_library(breezeenhanced MODULE
    ${breezeenhanced_SRCS}
    ${breezeenhanced_config_PART_FORMS_HEADERS})

target_link_libraries(breezeenhanced
    PRIVATE
        breezeenhancedcommon6
        breezeenhanced_STATIC
        KF6::GuiAddons
        KF6::I18n
        KF6::WindowSystem
        KF6::KCMUtils)


install(TARGETS breezeenhanced DESTINATION ${KDE_INSTALL_PLUGINDIR}/${KDECORATION_PLUGIN_DIR})

add_subdirectory(config)
