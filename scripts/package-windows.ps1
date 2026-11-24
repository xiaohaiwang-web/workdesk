[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$Version = '0.2.0',

    [string]$QtRoot = 'C:\Qt\6.11.2\msvc2022_64',

    [string]$TesseractRoot = '',

    [switch]$SkipInstaller,

    [switch]$SkipVCRedistDownload
)

$ErrorActionPreference = 'Stop'
if ($Configuration -ne 'Release') { throw '此脚本用于正式发布，请使用 -Configuration Release。' }

function Write-Step([string]$Message) {
    Write-Host "`n[WorkDesk release] $Message" -ForegroundColor Cyan
}

function Resolve-Iscc {
    if ($env:INNO_SETUP_PATH -and (Test-Path -LiteralPath $env:INNO_SETUP_PATH)) {
        return (Resolve-Path -LiteralPath $env:INNO_SETUP_PATH).Path
    }

    $candidates = @(
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        'D:\Inno Setup 7\ISCC.exe',
        'D:\Inno Setup 6\ISCC.exe'
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    # Also inspect the uninstall registry so custom drive installs are found.
    $registryKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    $apps = Get-ItemProperty $registryKeys -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like '*Inno Setup*' }
    foreach ($app in $apps) {
        if ($app.InstallLocation) {
            $candidate = Join-Path $app.InstallLocation 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
        if ($app.DisplayIcon) {
            $iconPath = ($app.DisplayIcon -replace ',\s*-?\d+$', '').Trim('"')
            $folder = Split-Path -Parent $iconPath
            $candidate = Join-Path $folder 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}


function Assert-X64Pe([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "不是有效的 Windows PE 文件：$Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 64 -or $peOffset -gt ($stream.Length - 6)) {
            throw "PE 头偏移无效：$Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "PE 签名无效：$Path"
        }
        $machine = $reader.ReadUInt16()
        if ($machine -ne 0x8664) {
            throw ("运行时必须是 x64；文件 {0} 的 Machine 为 0x{1:X4}" -f $Path, $machine)
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Resolve-TesseractRoot([string]$RequestedRoot) {
    $candidates = @()
    if ($RequestedRoot) { $candidates += $RequestedRoot }
    if ($env:TESSERACT_ROOT) { $candidates += $env:TESSERACT_ROOT }
    $candidates += @(
        "$env:ProgramFiles\Tesseract-OCR",
        "${env:ProgramFiles(x86)}\Tesseract-OCR",
        'C:\Program Files\Tesseract-OCR',
        'C:\Program Files (x86)\Tesseract-OCR'
    )

    $command = Get-Command tesseract.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += (Split-Path -Parent $command.Source) }

    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $exe = Join-Path $candidate 'tesseract.exe'
        if (Test-Path -LiteralPath $exe) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw @'
未找到 Tesseract Windows x64 安装目录。
请先安装 UB Mannheim Tesseract，并确认有 chi_sim、eng 两个模型；
或者使用 -TesseractRoot 指向包含 tesseract.exe 和 tessdata 的目录。
例如：-TesseractRoot "C:\Program Files\Tesseract-OCR"
'@
}

function Copy-OcrRuntime([string]$SourceRoot, [string]$Destination) {
    Write-Step "收集应用私有 OCR 运行时：$SourceRoot"
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $sourceExe = Join-Path $SourceRoot 'tesseract.exe'
    $sourceTessdata = Join-Path $SourceRoot 'tessdata'
    if (-not (Test-Path -LiteralPath $sourceExe)) { throw "Tesseract 程序不存在：$sourceExe" }
    if (-not (Test-Path -LiteralPath $sourceTessdata -PathType Container)) { throw "Tesseract tessdata 目录不存在：$sourceTessdata" }

    foreach ($language in @('chi_sim', 'eng')) {
        $model = Join-Path $sourceTessdata "$language.traineddata"
        if (-not (Test-Path -LiteralPath $model)) {
            throw "缺少 OCR 语言模型：$model。发布版必须包含简体中文和英文模型。"
        }
    }

    Assert-X64Pe $sourceExe
    Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $Destination 'tesseract.exe') -Force

    # Keep every DLL from the same UB Mannheim distribution together in the
    # private OCR directory. This preserves the original dynamic dependency set
    # and lets Windows resolve dependent DLLs beside libtesseract/leptonica.
    $dllFiles = @(Get-ChildItem -LiteralPath $SourceRoot -Filter '*.dll' -File -Recurse -ErrorAction Stop)
    if ($dllFiles.Count -eq 0) { throw "Tesseract 安装目录没有 DLL：$SourceRoot" }
    foreach ($dll in $dllFiles) {
        $target = Join-Path $Destination $dll.Name
        if (Test-Path -LiteralPath $target) {
            $existingHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            $incomingHash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash
            if ($existingHash -ne $incomingHash) {
                throw "OCR 运行时存在同名但内容不同的 DLL，拒绝覆盖：$($dll.Name)"
            }
        } else {
            Copy-Item -LiteralPath $dll.FullName -Destination $target -Force
        }
    }

    $apiDll = @('libtesseract-5.dll', 'libtesseract-5.5.dll', 'libtesseract.dll') |
        ForEach-Object { Join-Path $Destination $_ } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $leptDll = @('libleptonica-6.dll', 'liblept-6.dll', 'liblept-5.dll', 'libleptonica-5.dll') |
        ForEach-Object { Join-Path $Destination $_ } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $apiDll) { throw '未找到 libtesseract C API DLL，当前安装包无法提供模型复用 OCR。' }
    if (-not $leptDll) { throw '未找到 Leptonica DLL，当前安装包无法提供模型复用 OCR。' }
    Assert-X64Pe $apiDll
    Assert-X64Pe $leptDll

    $targetTessdata = Join-Path $Destination 'tessdata'
    New-Item -ItemType Directory -Force -Path $targetTessdata | Out-Null
    foreach ($language in @('chi_sim', 'eng')) {
        Copy-Item -LiteralPath (Join-Path $sourceTessdata "$language.traineddata") `
            -Destination (Join-Path $targetTessdata "$language.traineddata") -Force
    }
    # OSD is optional for this release; include it when supplied by the source build.
    $osd = Join-Path $sourceTessdata 'osd.traineddata'
    if (Test-Path -LiteralPath $osd) {
        Copy-Item -LiteralPath $osd -Destination (Join-Path $targetTessdata 'osd.traineddata') -Force
    }

    foreach ($folderName in @('configs', 'tessconfigs')) {
        $optionalFolder = Join-Path $sourceTessdata $folderName
        if (Test-Path -LiteralPath $optionalFolder -PathType Container) {
            Copy-Item -LiteralPath $optionalFolder -Destination $targetTessdata -Recurse -Force
        }
    }

    # Preserve any license/notice material supplied by the upstream Windows build.
    $licenseDestination = Join-Path $Destination 'licenses\upstream'
    $licenseFiles = @(Get-ChildItem -LiteralPath $SourceRoot -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|COPYRIGHT|AUTHORS)(\.|$|_)' })
    foreach ($licenseFile in $licenseFiles) {
        $relative = $licenseFile.FullName.Substring($SourceRoot.Length).TrimStart([char[]]@([char]92, [char]47))
        $target = Join-Path $licenseDestination $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $licenseFile.FullName -Destination $target -Force
    }

    $notice = @"
WorkDesk v$Version - Bundled OCR runtime

This folder contains an unmodified copy of the Tesseract OCR Windows runtime
from the UB Mannheim distribution installed on the release build machine.

Tesseract OCR engine: Apache License 2.0
Language data (tessdata_fast models): Apache License 2.0
Leptonica: BSD 2-Clause license
Other DLLs are redistributed as separate, unmodified files from the same
Windows distribution and remain under their respective upstream licenses.
Any license/notice files found in the source installation are preserved under
licenses/upstream/. The project also ships the Apache-2.0 and Leptonica BSD-2
license texts in licenses/.

Upstream references:
- Tesseract: https://github.com/tesseract-ocr/tesseract
- tessdata_fast: https://github.com/tesseract-ocr/tessdata_fast
- Leptonica: https://github.com/DanBloomberg/leptonica
- UB Mannheim Windows build: https://github.com/UB-Mannheim/tesseract

Language models included: chi_sim, eng (plus osd when present).
OCR runs locally. WorkDesk does not send documents or OCR text to a server.
"@
    Set-Content -LiteralPath (Join-Path $Destination 'THIRD_PARTY_NOTICES.txt') -Value $notice -Encoding UTF8
    # Verify the staged executable and both models before producing any release archive.
    $stagedExe = Join-Path $Destination 'tesseract.exe'
    $versionOutput = (& $stagedExe --version 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "打包后的 Tesseract 无法运行：$versionOutput" }
    $languageOutput = (& $stagedExe --tessdata-dir $targetTessdata --list-langs 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $languageOutput -notmatch '(?m)^chi_sim\s*$' -or $languageOutput -notmatch '(?m)^eng\s*$') {
        throw "打包后的 Tesseract 语言验证失败：`n$languageOutput"
    }

    $versionFirstLine = ($versionOutput.Trim() -split '\r?\n' | Select-Object -First 1)
    $manifest = @(
        "Tesseract version: $versionFirstLine",
        "Tesseract API DLL: $(Split-Path -Leaf $apiDll)",
        "Leptonica DLL: $(Split-Path -Leaf $leptDll)",
        'Languages: chi_sim, eng'
    )
    $manifest += Get-ChildItem -LiteralPath $Destination -File -Recurse |
        Sort-Object FullName |
        ForEach-Object { "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.FullName.Substring($Destination.Length).TrimStart([char[]]@([char]92, [char]47)) }
    Set-Content -LiteralPath (Join-Path $Destination 'OCR_RUNTIME_MANIFEST.txt') -Value $manifest -Encoding UTF8
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$exeSource = Join-Path $projectRoot "build\bin\$Configuration\workdesk.exe"
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
$qsqliteSource = Join-Path $QtRoot 'plugins\sqldrivers\qsqlite.dll'
$platformSource = Join-Path $QtRoot 'plugins\platforms\qwindows.dll'
$distRoot = Join-Path $projectRoot 'dist'
$stageName = "WorkDesk-v$Version-win64"
$stageDir = Join-Path $distRoot $stageName
$portableZip = Join-Path $distRoot "$stageName-portable.zip"
$redistDir = Join-Path $distRoot 'redist'
$redistPath = Join-Path $redistDir 'vc_redist.x64.exe'
$issPath = Join-Path $projectRoot 'installer\WorkDesk.iss'

if (-not (Test-Path -LiteralPath $exeSource)) {
    throw "找不到程序：$exeSource。请先执行 cmake --build build --config $Configuration"
}
if (-not (Test-Path -LiteralPath $windeployqt)) { throw "找不到 windeployqt：$windeployqt。" }
if (-not (Test-Path -LiteralPath $qsqliteSource)) { throw "找不到 Qt SQLite 驱动：$qsqliteSource。" }
if (-not (Test-Path -LiteralPath $platformSource)) { throw "找不到 Qt Windows 平台插件：$platformSource。" }

$tesseractSourceRoot = Resolve-TesseractRoot $TesseractRoot

Write-Step "准备部署目录：$stageDir"
New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
Copy-Item -LiteralPath $exeSource -Destination (Join-Path $stageDir 'workdesk.exe') -Force
# Bundle WorkDesk application icon
$iconSource = Join-Path $projectRoot 'resources\icons\workdesk.ico'
if (-not (Test-Path -LiteralPath $iconSource -PathType Leaf)) {
    throw "缺少 WorkDesk 图标：$iconSource"
}
Copy-Item -LiteralPath $iconSource `
    -Destination (Join-Path $stageDir 'workdesk.ico') -Force

Write-Step '使用 Qt 6.11.2 / MSVC 2022 x64 的 windeployqt 收集 Qt 运行库'
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
& $windeployqt --release --no-translations --no-compiler-runtime --dir $stageDir (Join-Path $stageDir 'workdesk.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt 执行失败，退出代码：$LASTEXITCODE" }

Write-Step '显式部署 QSQLITE 和 Windows 平台插件'
$sqlPluginDir = Join-Path $stageDir 'sqldrivers'
$platformPluginDir = Join-Path $stageDir 'platforms'
New-Item -ItemType Directory -Force -Path $sqlPluginDir | Out-Null
New-Item -ItemType Directory -Force -Path $platformPluginDir | Out-Null
Copy-Item -LiteralPath $qsqliteSource -Destination (Join-Path $sqlPluginDir 'qsqlite.dll') -Force
if (-not (Test-Path -LiteralPath (Join-Path $platformPluginDir 'qwindows.dll'))) {
    Copy-Item -LiteralPath $platformSource -Destination (Join-Path $platformPluginDir 'qwindows.dll') -Force
}

$zlibCandidates = @(
    (Join-Path $projectRoot 'build\zlib_build\Release\libz.dll'),
    (Join-Path $projectRoot 'build\zlib_build\Release\zlib1.dll')
)
foreach ($zlib in $zlibCandidates) {
    if (Test-Path -LiteralPath $zlib) { Copy-Item -LiteralPath $zlib -Destination $stageDir -Force }
}

# Preserve Qt license/notice documents in the release artifacts. Qt DLLs remain
# separate files deployed by windeployqt; they are not statically linked.
$qtLicenseCandidates = @(
    (Join-Path $QtRoot 'Licenses'),
    (Join-Path $QtRoot 'licenses'),
    (Join-Path (Split-Path -Parent $QtRoot) 'Licenses'),
    (Join-Path (Split-Path -Parent (Split-Path -Parent $QtRoot)) 'Licenses')
)
foreach ($qtLicenseRoot in $qtLicenseCandidates) {
    if (Test-Path -LiteralPath $qtLicenseRoot -PathType Container) {
        $targetQtLicenses = Join-Path $stageDir 'licenses\Qt'
        New-Item -ItemType Directory -Force -Path $targetQtLicenses | Out-Null
        $selectedQtLicenses = Get-ChildItem -LiteralPath $qtLicenseRoot -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'LGPL|GPL|LICENSE|COPYING|NOTICE|THIRD' }
        foreach ($licenseFile in $selectedQtLicenses) {
            $relative = $licenseFile.FullName.Substring($qtLicenseRoot.Length).TrimStart([char[]]@([char]92, [char]47))
            $target = Join-Path $targetQtLicenses $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $licenseFile.FullName -Destination $target -Force
        }
        break
    }
}

# Full portable build always includes OCR. Installer lets users deselect OCR
# using the Compact setup type, while Full (default) installs it app-locally.
$ocrDestination = Join-Path $stageDir 'ocr'
Copy-OcrRuntime $tesseractSourceRoot $ocrDestination

Write-Step '复制 WorkDesk 与第三方源码许可证'
$stageLicenseDir = Join-Path $stageDir 'licenses'
New-Item -ItemType Directory -Force -Path $stageLicenseDir | Out-Null
$projectLicenseFile = Join-Path $projectRoot 'LICENSE'
if (Test-Path -LiteralPath $projectLicenseFile -PathType Leaf) {
    Copy-Item -LiteralPath $projectLicenseFile -Destination (Join-Path $stageDir 'LICENSE') -Force
}
$projectLicenseDir = Join-Path $projectRoot 'licenses'
if (Test-Path -LiteralPath $projectLicenseDir -PathType Container) {
    Get-ChildItem -LiteralPath $projectLicenseDir -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stageLicenseDir -Recurse -Force
    }
}
$thirdPartyRoot = Join-Path $projectRoot 'third_party'
if (Test-Path -LiteralPath $thirdPartyRoot -PathType Container) {
    $dependencyLicenses = Get-ChildItem -LiteralPath $thirdPartyRoot -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|COPYRIGHT|AUTHORS)(\.|$|_)' }
    foreach ($licenseFile in $dependencyLicenses) {
        $relative = $licenseFile.FullName.Substring($thirdPartyRoot.Length).TrimStart([char[]]@([char]92, [char]47))
        $target = Join-Path (Join-Path $stageLicenseDir 'third_party') $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $licenseFile.FullName -Destination $target -Force
    }
}

$runtimeReadme = @"
WorkDesk v$Version - Windows x64

Run workdesk.exe from this folder. Keep all DLL files and the platforms/sqldrivers
folders beside it. The portable package includes the OCR runtime and the chi_sim
(简体中文) + eng (English) language models under ocr\.

Do not copy workdesk.exe out of this folder: Qt runtime DLLs and plugins are required.
This package was built for Qt 6.11.2 / MSVC 2022 x64.

The portable ZIP does not install Microsoft Visual C++ Runtime. If Windows reports a
missing VCRUNTIME/MSVCP DLL, install the official x64 Visual C++ Redistributable:
https://aka.ms/vc14/vc_redist.x64.exe

OCR is application-private and local. WorkDesk does not require Tesseract on PATH.
"@
Set-Content -LiteralPath (Join-Path $stageDir '运行说明.txt') -Value $runtimeReadme -Encoding UTF8

$requiredFiles = @(
    'workdesk.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Sql.dll','workdesk.ico',
    'Qt6Network.dll', 'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll',
    'ocr\tesseract.exe', 'ocr\tessdata\chi_sim.traineddata', 'ocr\tessdata\eng.traineddata',
    'ocr\THIRD_PARTY_NOTICES.txt', 'LICENSE', 'licenses\THIRD_PARTY_NOTICES.md'
)
$missing = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $stageDir $_)) })
if ($missing.Count -gt 0) { throw "部署目录缺少必要文件：`n$($missing -join "`n")" }

Write-Step '验证便携目录并生成 ZIP'
if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
Compress-Archive -Path $stageDir -DestinationPath $portableZip -CompressionLevel Optimal
$stageBytes = (Get-ChildItem -LiteralPath $stageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$zipBytes = (Get-Item -LiteralPath $portableZip).Length
Write-Host ('便携目录大小：{0:N1} MB' -f ($stageBytes / 1MB))
Write-Host ('便携 ZIP 大小：{0:N1} MB' -f ($zipBytes / 1MB))
Write-Host "便携目录：$stageDir"
Write-Host "便携 ZIP：$portableZip"

if ($SkipInstaller) {
    Write-Host '已按参数跳过单文件安装包。' -ForegroundColor Yellow
    exit 0
}

$iscc = Resolve-Iscc
if (-not $iscc) {
    throw '未找到 Inno Setup 编译器。便携 ZIP 已生成，但正式发布还缺少安装程序。请设置 $env:INNO_SETUP_PATH 指向 ISCC.exe 后重新运行。'
}

if (-not $SkipVCRedistDownload) {
    Write-Step '下载 Microsoft 官方 Visual C++ x64 Redistributable'
    New-Item -ItemType Directory -Force -Path $redistDir | Out-Null
    if (-not (Test-Path -LiteralPath $redistPath)) {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $redistPath
    }
    $signature = Get-AuthenticodeSignature -FilePath $redistPath
    if ($signature.Status -ne 'Valid') { throw "VC++ Redistributable 数字签名验证失败：$($signature.Status)" }
} elseif (-not (Test-Path -LiteralPath $redistPath)) {
    throw "跳过 VC++ 运行库下载，但找不到：$redistPath"
}

if (-not (Test-Path -LiteralPath $issPath)) { throw "找不到 Inno Setup 脚本：$issPath" }
Write-Step '编译 WorkDesk 安装程序（完整安装默认包含 OCR，可选择精简安装）'
& $iscc "/DAppVersion=$Version" "/O$distRoot" $issPath
if ($LASTEXITCODE -ne 0) { throw "Inno Setup 编译失败，退出代码：$LASTEXITCODE" }

$setupExe = Join-Path $distRoot "WorkDesk-Setup-v$Version-x64.exe"
if (-not (Test-Path -LiteralPath $setupExe)) { throw "编译成功但没有找到安装包：$setupExe" }
$setupBytes = (Get-Item -LiteralPath $setupExe).Length
Write-Host ('单文件安装包大小：{0:N1} MB' -f ($setupBytes / 1MB)) -ForegroundColor Green
Write-Host "安装包：$setupExe"

$hashManifest = Join-Path $distRoot "WorkDesk-v$Version-SHA256.txt"
$hashLines = @(
    "WorkDesk v$Version release artifacts SHA-256",
    ("{0}  {1}" -f (Get-FileHash -LiteralPath $setupExe -Algorithm SHA256).Hash, (Split-Path -Leaf $setupExe)),
    ("{0}  {1}" -f (Get-FileHash -LiteralPath $portableZip -Algorithm SHA256).Hash, (Split-Path -Leaf $portableZip))
)
Set-Content -LiteralPath $hashManifest -Value $hashLines -Encoding UTF8
Write-Host "SHA256 清单：$hashManifest"
Write-Step 'WorkDesk 发布包已生成。请在干净 Windows 虚拟机/新用户环境完成验收后再创建 GitHub Release。'
