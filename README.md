# WorkDesk - 本地智能文件管家与搜索引擎

WorkDesk 是一款基于 C++17 和 Qt 6 开发的轻量级本地文件管理与极速搜索工具。旨在打造一个纯粹、无感、毫秒级响应的个人知识库搜索引擎。

## ✨ 核心特性

- 📥 **拖拽收纳**：直接将文件拖入软件，自动剪切并按照文件后缀分类（文档、图片、表格等）。
- 📄 **深度摘要**：C++ 原生解析 `.pdf` (Aspose FOSS)、`.docx` (libzip+pugixml) 和 `.txt`，无需安装 Office 或 Python。
- 🔍 **极速检索**：依托 SQLite 数据库，支持文件名与内容的模糊匹配。
- 🎨 **桌面级 UI**：类似原生桌面的图标视图，支持拖拽、右键菜单、双击打开。
- 🛡️ **系统托盘与自启**：支持隐藏至右下角托盘运行，支持一键开机自启动。
- 🪶 **绝对轻量**：摒弃大模型依赖，纯 C++ 实现，打包后体积小巧，分发即用。

## 🛠️ 技术栈

- **语言/框架**: C++17, Qt 6.11 (Widgets, Sql)
- **构建工具**: CMake (MSVC 2022 编译器)
- **第三方依赖**:
  - `Aspose.PDF FOSS for C++` (PDF解析)
  - `libzip` + `pugixml` (Word文档解压与XML解析)
  - `zlib` (解压支持)

## 📦 如何编译

本项目使用 CMake 进行管理，所有第三方库均已作为源码放置在 `third_party` 目录中。

1. 确保已安装 Visual Studio 2022 及 CMake (>= 3.21)。
2. 确保已安装 Qt 6.11 环境。
3. 克隆仓库并初始化：
   ```bash
   git clone https://github.com/YourUsername/WorkDesk.git
   cd WorkDesk