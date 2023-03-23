cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(breezeenhanced)
set(PROJECT_VERSION "5.27.0")
set(PROJECT_VERSION_MAJOR 5)

include(WriteBasicConfigVersionFile)
include(FeatureSummary)

find_package(ECM 0.0.9 REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_SOURCE_DIR}/cmake)

set(QT_MIN_VERSION "5.15.0")
set(KF5_MIN_VERSION "5.102.0")

include(ECMInstallIcons)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(GenerateExportHeader)
# include(GtkUpdateIconCache)

find_package(KDecoration2 REQUIRED)


# old stuff
add_definitions(-DTRANSLATION_DOMAIN="breeze_kwin_deco")


find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS CoreAddons GuiAddons ConfigWidgets WindowSystem I18n)
find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS DBus)

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
        KF5::WindowSystem)

install(TARGETS breezeenhanced DESTINATION ${KDE_INSTALL_PLUGINDIR}/org.kde.kdecoration2)
install(FILES config/breezeenhancedconfig.desktop DESTINATION  ${KDE_INSTALL_KSERVICESDIR})
