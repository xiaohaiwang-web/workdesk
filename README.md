# WorkDesk - 本地文件管家与极速搜索引擎

WorkDesk 是一个基于 C++17 和 Qt 6 开发的桌面级本地文件管理与搜索工具。项目旨在打造一个无感收纳、毫秒级响应的个人专属文件搜索引擎。在最新迭代中，已移除臃肿的本地 AI 依赖，全面转向轻量化与高性能的本地检索方案。

## 主要特性

- **拖拽即归档**：直接将文件拖入程序窗口，自动剪切并按文件类型分类保存。
- **毫秒级全文检索**：底层集成 SQLite FTS5（倒排索引）虚拟表，支持多关键词匹配，搜索速度极快。
- **原生文档摘要提取**：无需外部运行环境（如 Python），纯 C++ 解析文档内容。
  - PDF：基于 Aspose.PDF FOSS 库解析。
  - DOCX：基于 libzip 解压与 pugixml 解析 XML 提取文本。
  - DOC：静默调用本地 Word/WPS COM 接口进行转换读取。
  - 纯文本及代码文件：直接流式读取。
- **现代极简 UI**：无边框窗口、阴影支持、自定义窗口控制按钮及原生系统图标展示。

## 技术架构

- 开发语言：C++17
- 界面框架：Qt 6.11 (Widgets, Sql)
- 构建系统：CMake (基于 MSVC 2022 编译)
- 搜索引擎：SQLite FTS5
- 第三方依赖：
  - Aspose.PDF FOSS for C++
  - libzip + pugixml
  - zlib

## 构建指南

项目采用 CMake 管理，所有第三方依赖均已以源码形式内嵌在 `third_party` 目录中。

### 环境要求
- Visual Studio 2022
- CMake 3.21 及以上版本
- Qt 6.11 开发环境

### 编译步骤

1. 克隆代码仓库
   ```bash
   git clone https://github.com/xiaohaiwang-web/workdesk.git
   cd workdesk
生成构建目录（请将 CMAKE_PREFIX_PATH 替换为你本地实际的 Qt 路径）

bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
执行编译

bash
cmake --build build --config Release
开发计划
☑ 拖拽收纳与文件分类系统
☑ 本地文档摘要自动提取
☑ SQLite FTS5 全文搜索引擎集成
☑ 无边框窗口界面与系统托盘支持
□ 拼音首字母检索（例如输入 syal 匹配“实验案例”）
□ 多词 OR 逻辑与搜索权重排序
□ 关键词高亮与多维度过滤
开源协议
本项目采用 MIT License 协议开源。