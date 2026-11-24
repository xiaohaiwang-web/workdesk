# WorkDesk 文件格式支持清单

本清单描述当前版本实际接入的文件分类与正文提取路径。**被识别/分类不等于能够读取内部正文**。为避免误导，格式按三种处理状态标记：

- **直接提取**：WorkDesk 自己读取文本、XML、ZIP/XML 或现有 PDF 解析器，不要求安装 Office。
- **Office/WPS COM**：尝试通过本机 Microsoft Office 或兼容 WPS 只读打开。是否能成功还取决于对应组件、格式转换器和版本。
- **仅归档**：记录文件名、路径、类别、大小和修改时间；可以按文件名检索，但当前不读取内部正文。

所有内容提取均在后台任务中执行。普通正文输出上限约 8 Mi 字符；Office ZIP/XML 的单个 XML 部件上限为 64 MiB，Office/PDF 输入上限为 256 MiB。超过限制、加密、IRM、损坏或非标准文件可能只保留文件名索引。

## 1. Word / Writer / 文字处理

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.docx`, `.docm`, `.dotx`, `.dotm` | 直接提取 | 读取 Word Open XML 正文、页眉页脚、脚注/尾注和批注 XML；不执行 VBA 宏 |
| `.odt`, `.ott`, `.odm`, `.oth`, `.fodt`, `.stw` | 直接提取 | 读取 OpenDocument / StarOffice Writer 的 `content.xml` 或平面 XML（文件为对应 ZIP/XML 包时） |
| `.sxw` | 直接提取（ZIP/XML） | 读取旧版 StarOffice Writer 包中的 `content.xml` |
| `.abw` | 直接提取（XML） | 读取 AbiWord XML 正文 |
| `.rtf` | 直接提取 | 解析常见 RTF 控制字、Unicode 转义、段落和制表符 |
| `.doc`, `.dot`, `.wps`, `.wpt`, `.docb`, `.wpd`, `.wp`, `.lwp` | Office/WPS COM（可选） | 尝试由 Word/WPS Writer 打开；第三方格式是否可读取取决于本机转换器 |
| `.pages`, `.sdw` | 仅归档 | Apple Pages 与旧式 StarOffice Writer 不保证可解析 |

另外，`.txt`、`.xml`、`.html/.htm/.xhtml` 及常见代码、配置文件作为文本读取。HTML 会先转换为纯文本。

## 2. Excel / Calc / 表格数据

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.xlsx`, `.xlsm`, `.xltx`, `.xltm` | 直接提取 | 读取工作表、共享字符串、内联字符串、缓存值和公式文本；不执行 VBA 宏 |
| `.ods`, `.ots`, `.fods` | 直接提取 | 读取 OpenDocument 表格 `content.xml` / 平面 XML |
| `.sxc`, `.stc` | 直接提取（ZIP/XML） | 读取旧版 StarOffice Calc 包中的 `content.xml` |
| `.csv`, `.tsv`, `.dif`, `.slk`, `.prn`, `.odc` | 直接提取 | 按文本读取表格交换内容与 Office 数据连接 XML |
| `.dbf` | 直接提取（轻量 DBF 读取器） | 读取字段名和普通字段记录；memo/binary 字段及外部 `.dbt/.fpt` 文件暂不读取 |
| `.xls`, `.xlt`, `.xlsb`, `.xlw`, `.et`, `.ett` | Office/WPS COM（可选） | 尝试由 Excel/WPS 表格只读打开，提取使用区域单元格值；宏安全设置会尝试强制禁用 |
| `.xla`, `.xlam`, `.xll`, `.xlb`, `.xlc`, `.xlm`, `.numbers`, `.sdc` | 仅归档 | Excel 加载项/工具栏、Excel 4.0 宏工作表、旧式图表或 Apple Numbers 不保证有可提取工作表正文；为避免执行加载项或旧式宏表，不自动交给 Excel COM 处理 |
| `.wks`, `.wk1`, `.wk2`, `.wk3`, `.wk4`, `.123`, `.wq1`, `.qpw`, `.wb1`, `.wb2`, `.wb3` | 仅归档 | Lotus 1-2-3 / Quattro Pro 等旧式表格格式 |

## 3. PowerPoint / Impress / 演示文稿

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.pptx`, `.pptm`, `.ppsx`, `.ppsm`, `.potx`, `.potm`, `.sldx`, `.sldm` | 直接提取 | 读取幻灯片文字和演讲者备注 XML；不执行 VBA 宏 |
| `.odp`, `.otp`, `.fodp` | 直接提取 | 读取 OpenDocument Presentation `content.xml` / 平面 XML |
| `.sxi`, `.sti` | 直接提取（ZIP/XML） | 读取旧版 StarOffice Impress 包中的 `content.xml` |
| `.ppt`, `.pps`, `.pot`, `.dps`, `.dpt` | Office/WPS COM（可选） | 尝试由 PowerPoint/WPS 演示只读打开并提取形状文本 |
| `.ppa`, `.ppam`, `.thmx`, `.key`, `.sdd` | 仅归档 | PowerPoint 加载项/主题、Apple Keynote 和旧式 StarOffice 演示文件；不自动加载插件 |

## 4. Visio、图形、设计与 CAD

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.vsdx`, `.vsdm`, `.vssx`, `.vssm`, `.vstx`, `.vstm` | 直接提取 | 读取 Visio Open XML 页面和母版中的文本节点 |
| `.odg`, `.otg`, `.fodg`, `.sxd`, `.std` | 直接提取（ZIP/XML） | 读取 OpenDocument / StarOffice Drawing 包中的 `content.xml`（需为 ZIP/XML 包） |
| `.vsd`, `.vss`, `.vst`, `.sda` | 仅归档 | 旧版 Visio / StarOffice Draw 二进制格式 |
| `.ai`, `.eps`, `.psd`, `.psb`, `.xcf`, `.kra`, `.ora`, `.clip`, `.sai`, `.sai2`, `.procreate`, `.indd`, `.indt`, `.indb`, `.sketch`, `.fig`, `.afdesign`, `.afphoto`, `.afpub`, `.cdr` | 仅归档 | Adobe / Affinity / Corel / Krita / Clip Studio 等设计工程文件 |
| `.blend`, `.dwg`, `.dxf`, `.dgn`, `.ifc`, `.3dm`, `.3ds`, `.obj`, `.fbx`, `.stl`, `.step`, `.stp`, `.iges`, `.igs`, `.skp`, `.sldprt`, `.sldasm`, `.slddrw`, `.ipt`, `.iam`, `.idw`, `.f3d`, `.f3z`, `.sat`, `.sab`, `.3mf` | 仅归档 | 3D、CAD 与交换格式；当前不解析几何或图层信息 |
| `.gbr`, `.abr`, `.pat` | 仅归档 | GIMP/Photoshop 画笔与图案资源 |

## 5. PDF、网页归档、电子书与其他文档

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.pdf` | 直接提取 + 可选 OCR | 使用 Aspose.PDF FOSS 逐页提取文字；扫描页由随完整版分发的 Tesseract（chi_sim + eng）按需识别。精简安装不包含 OCR 运行时。 |
| `.xps`, `.oxps` | 直接提取 | 从 FixedPage XML 的 Glyphs 文本中提取 |
| `.mht`, `.mhtml` | 直接提取（有限 MIME） | 提取首个 HTML MIME 部分；复杂多语言编码、资源关联和多 HTML 部分不保证完整 |
| `.html`, `.htm`, `.xhtml` | 直接提取 | 通过 Qt QTextDocument 转换为纯文本 |
| `.epub` | 直接提取 | 读取 EPUB 包内 XHTML/HTML 正文 |
| `.fb2` | 直接提取（XML） | 读取 FictionBook XML 段落 |
| `.mobi`, `.azw`, `.azw3`, `.djvu`, `.cbz`, `.cbr` | 仅归档 | 电子书/漫画格式当前只收录文件信息 |
| `.xml`, `.json`, `.txt`, `.md` 以及常见代码/配置格式 | 直接提取 | 按文本读取并应用内容大小上限 |

## 6. 邮件、联系人、日历与协作文件

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.eml`, `.emlx`, `.mbox` | 基础文本 | 按原始文本读取；复杂 MIME 附件、部分传输编码尚未拆解 |
| `.ics`, `.vcs`, `.vcf` | 直接提取 | 日历事件、vCalendar 和联系人文本字段可检索 |
| `.msg`, `.oft`, `.pst`, `.ost`, `.olm` | 仅归档 | Outlook 邮件、模板和数据容器（含 macOS OLM 归档），暂不读取内部消息 |
| `.one`, `.onepkg`, `.onecache`, `.onetoc2` | 仅归档 | OneNote 页面和包结构暂不解析 |
| `.xsn`, `.xsf`, `.odf` | 仅归档 | InfoPath 表单模板/表单定义，以及旧式 OpenDocument Formula 文件 |

## 7. Access、Publisher、Project 与其他办公格式

| 扩展名 | 处理方式 | 说明 |
|---|---|---|
| `.accdb`, `.accde`, `.accdr`, `.accdt`, `.accdw`, `.mdb`, `.mde`, `.mda`, `.ade`, `.adp`, `.odb`, `.wdb` | 仅归档 | Access / OpenDocument / Microsoft Works 数据库；不读取内部表、查询、表单或记录 |
| `.pub` | 仅归档 | Microsoft Publisher 文档 |
| `.mpp`, `.mpt`, `.mpx` | 仅归档 | Microsoft Project 项目、模板和交换格式 |
| `.xml` | 直接提取 | 以普通 XML 文本索引；不会推断其专属应用语义 |

## 8. 图片与照片（本阶段只做文件收录）

识别并归入“图片”类别的常见扩展名包括：

- **常见位图**：`.jpg`, `.jpeg`, `.jpe`, `.jfif`, `.jif`, `.pjpeg`, `.pjp`, `.png`, `.apng`, `.gif`, `.bmp`, `.dib`, `.tif`, `.tiff`, `.webp`, `.ico`, `.icns`
- **其他/专业图像**：`.odi`（OpenDocument Image，仅归档）、`.tga`, `.exr`, `.hdr`, `.jp2`, `.j2k`, `.jpf`, `.jpx`, `.jpm`, `.mj2`, `.pcx`, `.ppm`, `.pgm`, `.pbm`, `.pnm`, `.wbmp`, `.xbm`, `.xpm`, `.mng`, `.dds`, `.sgi`, `.rgb`, `.rgba`, `.ras`, `.sun`, `.pic`, `.jng`, `.pcd`, `.pct`, `.pict`, `.jxr`, `.hdp`, `.wdp`, `.wmz`, `.emz`, `.qoi`, `.ktx`, `.ktx2`, `.basis`, `.astc`, `.pvr`
- **矢量/Windows 图形**：`.svg`, `.svgz`, `.wmf`, `.emf`
- **图标/光标**：`.ico`, `.icns`, `.cur`, `.ani`
- **手机/相机 RAW**：`.raw`, `.cr2`, `.cr3`, `.crw`, `.nef`, `.nrw`, `.arw`, `.dng`, `.orf`, `.rw2`, `.rwl`, `.pef`, `.srw`, `.kdc`, `.dcr`, `.mrw`, `.3fr`, `.ari`, `.bay`, `.cap`, `.dcs`, `.drf`, `.eip`, `.erf`, `.fff`, `.gpr`, `.iiq`, `.k25`, `.mdc`, `.mef`, `.mos`, `.ptx`, `.pxn`, `.raf`, `.rwz`, `.sr2`, `.srf`, `.x3f`, `.heic`, `.heif`, `.avif`, `.jxl`
- **医疗/科研影像容器**：`.dcm`, `.dicom`（只做文件归档，不解析影像内容）

以上图片当前只记录文件名、路径、类别、大小和修改时间，不读取像素、不生成缩略图、不做 OCR 或以图搜图。部分扩展名虽然被归档为图片，并不代表当前发行包已包含相应解码器。Qt 的实际图像解码格式由 `QImageReader::supportedImageFormats()` 和部署的 imageformats 插件决定。

设计工程文件（如 PSD/AI/INDD/Sketch/CAD/3D）归入“图形与设计”，不是普通图片分类。

## 9. 安全与资源边界

- Open XML、OpenDocument、EPUB、Visio 和 XPS 包由 libzip 只读打开，不解压到磁盘。
- 单个 ZIP 内 XML 部件最多读取 64 MiB；Office/PDF 文件大小上限为 256 MiB；普通正文最多约 8 Mi 字符，PDF 正文单独限制为 2 Mi 字符；PDF 最多检查 5,000 页。
- PDF OCR 最多识别 80 页、每页最多 20 秒、单文件 OCR 总时间最多 5 分钟，渲染分辨率为 240 DPI；达到限制时会标记内容截断。
- `.doc/.xls/.ppt` 等旧版二进制 Office 的 COM 提取为可选路径：只读打开、尝试关闭宏、应用级超时后终止 PowerShell 自动化进程。Excel `.xla/.xlam/.xll/.xlm` 与 PowerPoint `.ppa/.ppam` 加载项不自动交给 COM 处理。部署版不会自动安装 Office/WPS；Word COM 使用 90 秒超时，Excel/PowerPoint COM 使用 120 秒超时。
- 没有 Office/WPS COM 组件时，旧版二进制文件仍可以归档和按文件名检索。
- 密码保护、IRM、损坏、第三方转换器缺失或特殊方言可能导致正文无法提取。
- COM 解析仅用于 Windows。独立图片文件目前只做文件收录和分类，不进行图片内容 OCR 或以图搜图；PDF 内嵌扫描页可通过 Tesseract OCR 识别。

## 参考资料

- [Microsoft Office 文件格式参考（Word/Excel/PowerPoint）](https://learn.microsoft.com/en-us/office/compatibility/office-file-format-reference)
- [Microsoft Open XML 格式与扩展名](https://support.microsoft.com/en-us/office/vba/open-xml-formats-and-file-name-extensions)
- [Qt 6.11 QImageReader 支持格式](https://doc.qt.io/qt-6.11/qimagereader.html)
- [Qt 6.11 Image Formats 插件说明](https://doc.qt.io/qt-6/qtimageformats-index.html)
- [libzip 文档](https://libzip.org/documentation/libzip/)
