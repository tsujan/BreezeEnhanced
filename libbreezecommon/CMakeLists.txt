################# dependencies #################
### Qt/KDE
find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} REQUIRED CONFIG COMPONENTS Widgets)

################# breezestyle target #################
set(breezeenhancedcommon_LIB_SRCS
    breezeboxshadowrenderer.cpp
)

add_library(breezeenhancedcommon6 ${breezeenhancedcommon_LIB_SRCS})

generate_export_header(breezeenhancedcommon6
    BASE_NAME breezecommon
    EXPORT_FILE_NAME breezecommon_export.h)

target_link_libraries(breezeenhancedcommon6
    PUBLIC
        Qt::Core
        Qt::Gui)

set_target_properties(breezeenhancedcommon6 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS breezeenhancedcommon6 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
