qt_add_library(workdesk_core STATIC
    include/workdesk/core/FileRecord.h
    include/workdesk/core/FileManager.h
    include/workdesk/core/DatabaseManager.h
    include/workdesk/core/AIEngine.h
    
    src/FileManager.cpp
    src/DatabaseManager.cpp
    src/AIEngine.cpp
)

target_include_directories(workdesk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # 把第三方库的头文件暴露出来
    ${CMAKE_SOURCE_DIR}/third_party/aspose_pdf/include
    ${CMAKE_SOURCE_DIR}/third_party/libzip/include
    ${CMAKE_SOURCE_DIR}/third_party/pugixml/src
)

target_link_libraries(workdesk_core PUBLIC 
    Qt6::Core 
    Qt6::Sql 
    llama 
    aspose_pdf_foss
    zip
    zlib        # 链接 zlib
    zlibstatic  # 链接 zlib 静态库
    pugixml
)