# WorkDesk application icon
set(WORKDESK_ICON_FILE
    "${CMAKE_SOURCE_DIR}/resources/icons/workdesk.ico"
)

if(NOT EXISTS "${WORKDESK_ICON_FILE}")
    message(FATAL_ERROR "WorkDesk icon not found: ${WORKDESK_ICON_FILE}")
endif()

# IMPORTANT:
# Give Qt resource system a stable relative resource name.
# Runtime path will be :/icons/workdesk.ico
set_source_files_properties(
    "${WORKDESK_ICON_FILE}"
    PROPERTIES
        QT_RESOURCE_ALIAS "workdesk.ico"
)

# Windows executable icon resource
if(WIN32)
    file(TO_CMAKE_PATH
        "${WORKDESK_ICON_FILE}"
        WORKDESK_ICON_FILE_RC
    )

    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/workdesk.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/workdesk_icon.rc"
        @ONLY
    )

    set(WORKDESK_WIN_ICON_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/workdesk_icon.rc"
    )
endif()

qt_add_executable(workdesk
    main.cpp
    MainWindow.h
    MainWindow.cpp
    MainWindowUi.cpp
    FileListModel.h
    FileListModel.cpp
    ${WORKDESK_WIN_ICON_RESOURCE}
)

# Embed ICO into Qt resource system
qt_add_resources(workdesk workdesk_icon_resource
    PREFIX "/icons"
    FILES
        "${WORKDESK_ICON_FILE}"
)

target_link_libraries(workdesk PRIVATE
    workdesk_core
    Qt6::Widgets
)

# Preserve the existing application subsystem setting.
set_target_properties(workdesk PROPERTIES
    WIN32_EXECUTABLE OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)