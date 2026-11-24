qt_add_library(workdesk_core STATIC
    # 头文件
    include/workdesk/core/FileRecord.h
    include/workdesk/core/FileManager.h
    include/workdesk/core/DatabaseManager.h
    
    # 源文件（AIEngine 已经被移除了）
    src/FileManager.cpp
    src/DatabaseManager.cpp
)

target_include_directories(workdesk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/aspose_pdf/include
    ${CMAKE_SOURCE_DIR}/third_party/libzip/include
    ${CMAKE_SOURCE_DIR}/third_party/pugixml/src
)

# 链接第三方库
target_link_libraries(workdesk_core PUBLIC 
    Qt6::Core 
    Qt6::Gui
    Qt6::Sql 
    aspose_pdf_foss
    zip
    zlib
    zlibstatic
    pugixml
)