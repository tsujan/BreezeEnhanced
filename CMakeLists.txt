
project(breezeenhanced)
set(PROJECT_VERSION "0.1")
set(PROJECT_VERSION_MAJOR 0)

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

include(WriteBasicConfigVersionFile)
include(FeatureSummary)

find_package(ECM 0.0.9 REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_SOURCE_DIR}/cmake)

set(QT_MIN_VERSION "5.15.0")
set(KF5_MIN_VERSION "5.82")

include(ECMInstallIcons)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(GenerateExportHeader)
# include(GtkUpdateIconCache)

find_package(KDecoration2 REQUIRED)


# old stuff
add_definitions(-DTRANSLATION_DOMAIN="breeze_kwin_deco")


#find_package(KF5 REQUIRED COMPONENTS CoreAddons GuiAddons ConfigWidgets WindowSystem I18n IconThemes)
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS CoreAddons GuiAddons ConfigWidgets WindowSystem I18n)
find_package(Qt5 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS DBus)

### XCB
find_package(XCB COMPONENTS XCB)
set_package_properties(XCB PROPERTIES
  DESCRIPTION "X protocol C-language Binding"
  URL "https://xcb.freedesktop.org"
  TYPE OPTIONAL
  PURPOSE "Required to pass style properties to native Windows on X11 Platform"
)

if(UNIX AND NOT APPLE)

  set(BREEZE_HAVE_X11 ${XCB_XCB_FOUND})
  if (XCB_XCB_FOUND)
    find_package(Qt5 REQUIRED CONFIG COMPONENTS X11Extras)
  endif()

else()

  set(BREEZE_HAVE_X11 FALSE)

endif()

################# configuration #################
configure_file(config-breeze.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-breeze.h )

################# includes #################
add_subdirectory(libbreezecommon)

################# newt target #################
### plugin classes
set(breezeenhanced_SRCS
    breezebutton.cpp
    breezedecoration.cpp
    breezeexceptionlist.cpp
    breezesettingsprovider.cpp
    breezesizegrip.cpp)

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
   config/ui/breezedetectwidget.ui
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
        Qt::Core
        Qt::Gui
        Qt::DBus
    PRIVATE
        breezeenhancedcommon5
        KDecoration2::KDecoration
        KF5::ConfigCore
        KF5::CoreAddons
        KF5::ConfigWidgets
        KF5::GuiAddons
        KF5::I18n
#        KF5::IconThemes
        KF5::WindowSystem)

if(BREEZE_HAVE_X11)
  target_link_libraries(breezeenhanced
    PUBLIC
      Qt::X11Extras
      XCB::XCB)
endif()


install(TARGETS breezeenhanced DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kdecoration2)
install(FILES config/breezeenhancedconfig.desktop DESTINATION  ${KDE_INSTALL_KSERVICES5DIR})
# install(TARGETS breezedecoration DESTINATION ${PLUGIN_INSTALL_DIR}/org.kde.kdecoration2)
# install(FILES config/breezedecorationconfig.desktop DESTINATION  ${KDE_INSTALL_KSERVICES5DIR})
