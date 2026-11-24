# WorkDesk v0.2.0

WorkDesk 是一款面向 Windows 的本地文件管理与全文搜索工具。文件整理、正文提取、OCR 和检索均在本机完成；普通文件搜索不依赖网络或大模型服务。

## v0.2.0 本次发布重点

- **更广的办公格式覆盖**：Word、Excel、PowerPoint、OpenDocument、Visio、RTF、CSV、EPUB、XPS、MHTML、DBF 等。支持边界按 `docs/SUPPORTED_FORMATS.md` 区分为直接提取、Office/WPS COM 和仅归档。
- **旧版 Office 正文提取**：`.doc/.xls/.ppt` 在本机对应 Microsoft Office COM 可用时尝试只读提取；不要求 WorkDesk 安装 Office。
- **PDF 正文与扫描件 OCR**：文字型 PDF 使用 Aspose.PDF 逐页提取；扫描页按需使用 Tesseract。完整版随包提供 `chi_sim` 简体中文和 `eng` 英文模型。
- **OCR 模型复用**：优先动态加载 Tesseract C API，同一份 PDF 只初始化一次 OCR 引擎和语言模型，页面间复用；C API 不可用时回退命令行 OCR。
- **全文与摘要分离**：摘要约 1,000 字用于结果预览；可提取正文独立进入 SQLite/FTS5，PDF 正文有单独资源上限。
- **日夜自适应主题**：本地时间 05:00–07:00 渐亮、17:00–19:00 渐暗，每 30 分钟更新一次，不运行高频动画。
- **安全退出**：窗口关闭可最小化到系统托盘；托盘“退出 WorkDesk”会取消后台任务、等待 OCR/解析任务到达安全退出点后结束进程。

## 文件处理边界

文件扩展名被识别或归类，不代表软件一定能够提取其内部正文。加密文件、损坏文件、复杂格式、缺少本机 Office 转换器的旧版文件可能只保留文件名索引。独立图片本阶段只做收录和分类，不进行图片 OCR 或以图搜图。

完整矩阵见 [`docs/SUPPORTED_FORMATS.md`](docs/SUPPORTED_FORMATS.md)。

## Windows 运行与分发

目标平台：Windows 10/11 x64。

- **完整安装（默认）**：管理员安装到 Program Files；包含 WorkDesk、Qt 运行库、Tesseract OCR DLL、`chi_sim`/`eng` 模型和 Microsoft VC++ Redistributable（系统缺少时安装）。
- **精简安装**：安装程序可选择不安装 OCR 组件；普通文件搜索和文字型 PDF 解析仍可使用。安装文件本身仍包含压缩后的可选 OCR 载荷。
- **便携 ZIP**：包含完整 OCR 运行时，解压后运行 `workdesk.exe`；不要单独拷贝 EXE。

OCR 是应用私有组件，WorkDesk 优先使用程序目录下的 `ocr/tesseract.exe` 和 `ocr/tessdata`，不要求终端用户安装 Tesseract、修改 PATH 或配置环境变量。

## 开发环境

- Visual Studio 2022（MSVC 2022 x64）
- CMake 3.21+
- Qt 6.11.2 MSVC 2022 x64
- Inno Setup 6/7（用于生成单文件安装包）
- 开发机安装 UB Mannheim Tesseract Windows x64，且 `tessdata` 至少包含 `chi_sim.traineddata` 和 `eng.traineddata`

CMake Preset 默认 Qt 路径：`C:/Qt/6.11.2/msvc2022_64`。

### 编译

```powershell
cmake --preset msvc2022-debug
cmake --build --preset msvc2022-debug
cmake --build build --config Release
```

### 生成 Release 包

在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-release.ps1
```

如 Qt 安装位置不同：

```powershell
.\scripts\package-windows.ps1 -QtRoot "C:\Qt\6.11.2\msvc2022_64"
```

如 Tesseract 不在默认安装路径：

```powershell
.\scripts\package-windows.ps1 -TesseractRoot "C:\Program Files\Tesseract-OCR"
```

脚本先完成 Release 编译，再检查 OCR DLL 和语言模型、运行 `tesseract.exe --version/--list-langs`，最后生成便携 ZIP、Inno Setup 安装程序和 SHA-256 清单。

详细打包流程见 [`docs/WINDOWS_RELEASE.md`](docs/WINDOWS_RELEASE.md)。发布前验收见 [`docs/RELEASE_TEST_PLAN.md`](docs/RELEASE_TEST_PLAN.md)。

## 第三方组件

WorkDesk 使用 Qt 6、Aspose.PDF FOSS、libzip、zlib、pugixml，以及可选/随包分发的 Tesseract、Leptonica 和语言模型。运行时以独立文件形式分发，不静态合并进 `workdesk.exe`。请保留 `licenses/` 与安装包内 `ocr/licenses/` 的许可和声明文件。Tesseract 使用 Apache 2.0 许可，Leptonica 使用 BSD 2-Clause；其余运行时依赖分别遵循各自许可。<https://github.com/tesseract-ocr/tesseract> <https://github.com/DanBloomberg/leptonica>

## 项目

https://github.com/xiaohaiwang-web/workdesk
