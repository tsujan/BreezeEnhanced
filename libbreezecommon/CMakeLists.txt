################# dependencies #################
### Qt/KDE
find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} REQUIRED CONFIG COMPONENTS Widgets)

################# breezestyle target #################
set(breezeenhancedcommon_LIB_SRCS
    breezeboxshadowrenderer.cpp
)

add_library(breezeenhancedcommon5 ${breezeenhancedcommon_LIB_SRCS})

generate_export_header(breezeenhancedcommon5
    BASE_NAME breezecommon
    EXPORT_FILE_NAME breezecommon_export.h)

target_link_libraries(breezeenhancedcommon5
    PUBLIC
        Qt::Core
        Qt::Gui)

set_target_properties(breezeenhancedcommon5 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS breezeenhancedcommon5 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
