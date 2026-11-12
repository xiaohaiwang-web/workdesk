qt_add_executable(workdesk
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(workdesk PRIVATE
    workdesk_core
    Qt6::Widgets
)

set_target_properties(workdesk PROPERTIES WIN32_EXECUTABLE OFF)
set_target_properties(workdesk PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)