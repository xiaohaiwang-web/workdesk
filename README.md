# WorkDesk

**WorkDesk v0.1.0** 是一款面向 Windows 的本地文件整理与检索桌面工具，基于 C++17、Qt 6.11 Widgets、SQLite 和 CMake 构建。

它希望给用户一种进入独立桌面工作区的感觉：文件以图标网格或紧凑列表呈现，左侧提供分类导航，右侧提供文件检查器。界面参考 Finder / macOS 的空间布局，但 WorkDesk 本身仍是 Windows 上运行的 Qt Widgets 程序，并不是 macOS 移植版。

## v0.1.0 功能

- **Finder 风格工作区**：自绘系统栏、红黄绿窗口控制点、侧边分类导航、文件图标网格、紧凑列表和右侧文件检查器。
- **文件导入**：支持拖放文件和文件选择器导入；按扩展名分类，并移动到 WorkDesk 本地文件库。
- **分类浏览**：支持 PDF、Word、Excel、图片、文本与代码以及其他文件类别。
- **文件操作**：双击打开文件；右键菜单提供打开、打开所在文件夹、复制路径、重新提取摘要和删除文件等操作。
- **本地搜索**：通过现有 `DatabaseManager` 访问 SQLite 搜索能力。UI 同时兼容旧版 `searchFiles()` 和 Search Core v2 的 `searchFilesPage()` 接口；使用分页 API 时，当前工作区加载首批最多 200 条结果。
- **文档摘要**：对当前支持的文本、PDF、DOCX 和 DOC 文件提取摘要。DOC 格式需要本机 Word 或 WPS 的 COM 自动化能力。
- **后台任务**：搜索输入防抖；文件移动和摘要提取由后台任务队列处理。
- **系统托盘**：支持最小化到托盘、从托盘恢复，以及 Windows 开机启动选项。
- **快捷操作**：`Ctrl + K` 聚焦搜索框；底部 Dock 提供常用入口。

## 搜索能力与当前边界

v0.1.0 保留 SQLite 本地检索，不依赖云服务或大模型 API。需要区分“当前 UI 已接入的检索”与“后续完整搜索能力”：

- 当前 UI 会调用项目中已有的 `DatabaseManager` 搜索接口。旧接口按原有数据库实现检索；Search Core v2 接口可返回有界结果页。
- 本次 Finder UI 改造没有重新定义数据库 Schema，也没有把 AI 搜索、Embedding 向量检索或模型重排序作为已交付功能。
- Search Core v2 虽然预留了全文字段、trigram 检索和按需内容分块 API，但完整的文件内容提取/历史文件补索引流程仍需要在 UI 导入与后台索引流程中进一步联调。因此，v0.1.0 不承诺所有文件正文都已建立全文索引。
- 1–2 个字符的查询可能走较慢的兼容扫描路径；实际搜索速度取决于当前仓库采用的 `src/core` 版本、SQLite/FTS5 能力、索引完成情况和文件库规模。
- 当前 UI 使用 Search Core v2 接口时只加载首批最多 200 条结果，界面尚未提供完整的翻页控件。

因此，本版本适合作为 WorkDesk 的首个桌面体验预览版，不应宣传为已经完成大规模中文全文搜索或 AI 语义搜索的最终产品。

## 支持的文件类型

文件分类当前覆盖：

- PDF 文档
- Word 文档（DOC、DOCX）
- Excel 表格（XLS、XLSX，当前按类型归档）
- 常见图片格式（JPG、JPEG、PNG、GIF，当前按类型归档）
- 常见文本、代码和配置文件
- 其他文件

正文提取能力不等于分类能力。Excel、图片和扫描版 PDF 当前不保证可以提取可搜索正文；本版本不包含 OCR。

## 技术栈

- C++17
- Qt 6.11 Widgets / Gui / Sql
- SQLite / FTS5（具体索引能力取决于仓库当前 `src/core` 实现）
- CMake 3.21+
- Visual Studio 2022 / MSVC x64
- Aspose.PDF FOSS for C++、libzip、pugixml、zlib

## 构建环境

- Windows 10 / Windows 11 x64
- Visual Studio 2022，安装“使用 C++ 的桌面开发”工作负载
- CMake 3.21 或更新版本
- Qt 6.11 MSVC 2022 x64
- Windows SDK

仓库中的 `CMakePresets.json` 默认 Qt 路径为：

```text
C:/Qt/6.11.2/msvc2022_64
```

如 Qt 安装位置不同，请修改 `CMAKE_PREFIX_PATH`。

## 编译

Debug：

```powershell
cmake --preset msvc2022-debug
cmake --build --preset msvc2022-debug
```

Release：

```powershell
cmake --build build --config Release
```

本 Release 源码覆盖包不包含完整 `third_party/` 目录，也不包含预编译 EXE/DLL。请在完整项目仓库中构建。

## 数据位置与文件行为

默认数据目录为：

```text
%ProgramData%\WorkDesk\
```

数据库文件：

```text
%ProgramData%\WorkDesk\workdesk.db
```

程序会在该目录下按类别保存导入文件。导入不是创建快捷方式：WorkDesk 会将源文件移动到文件库；如果目标位置已有同名文件，会尝试使用带时间戳的文件名避免覆盖。

右键菜单中的删除操作会删除文件库中的实际文件，而不只是从界面隐藏。执行删除前请核对提示中的路径，并在升级或迁移数据库前做好备份。

## v0.1.0 尚未包含

- 完整且已验证的历史文件全文补索引流程。
- 中文拼音检索、OCR 和 Excel 单元格内容解析。
- 面向超大文件库的端到端压力测试与性能承诺。
- AI Provider 配置页、API Key 安全保存、自然语言搜索和 AI 重排序。
- 完整安装程序、自动更新和便携版依赖部署。

## 开源协议

项目当前 README 声明采用 MIT License。对外发布时，请确保仓库根目录包含与该声明对应的 `LICENSE` 文件及正确的版权信息。
