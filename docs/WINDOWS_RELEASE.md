# Windows Release Packaging

## OCR packaging model

Tesseract is **not** statically linked into `workdesk.exe`. The release script copies an app-private runtime into `dist/WorkDesk-v0.2.0-win64/ocr/`:

- `tesseract.exe` (CLI fallback)
- the Tesseract C API DLL
- the Leptonica DLL
- DLL dependencies from the same UB Mannheim install directory
- `tessdata/chi_sim.traineddata` and `tessdata/eng.traineddata` (plus `osd` when present)
- upstream license/notice files when supplied by the installed distribution

The app checks `WORKDESK_TESSERACT_PATH` first, then the bundled `ocr/tesseract.exe`, then PATH/common system install locations. The bundled engine uses the sibling `ocr/tessdata` directory, so end users do not need PATH or TESSDATA_PREFIX changes.

The installer requests administrator permission because it installs to Program Files and may install the Microsoft Visual C++ Redistributable. It offers two types:

- **Full** (default): installs OCR as an application component.
- **Compact**: installs the main application without OCR. The single setup download still contains the compressed OCR payload so the user can choose Full or Compact during setup; Compact simply does not install the OCR folder.

The portable ZIP is the full build and includes OCR. The setup EXE embeds the OCR payload so the user can choose Full or Compact during installation; the Compact type omits copying the OCR folder but does not reduce the downloaded setup EXE size.

## Build and package

From the repository root, run the one-command release script (it builds Release first, then packages):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-release.ps1
```

If you have already built Release and only want to regenerate the deployment folder and installers, run `scripts/package-windows.ps1` directly.

Optional custom roots:

```powershell
.\scripts\package-windows.ps1 `
  -QtRoot "C:\Qt\6.11.2\msvc2022_64" `
  -TesseractRoot "C:\Program Files\Tesseract-OCR"
```

The script fails instead of producing a misleading OCR release if it cannot find `tesseract.exe`, the C API DLL, the Leptonica DLL, or either `chi_sim`/`eng` model. It validates the staged CLI version and available languages before zipping.

The script also:

- deploys Qt DLLs and plugins using `windeployqt`;
- explicitly copies `qsqlite.dll` and `qwindows.dll`;
- validates core runtime files;
- copies the WorkDesk license, root third-party notices, and matching license files from the four vendored source dependency directories into the installed `licenses/` folder;
- downloads and Authenticode-verifies Microsoft's official VC++ x64 Redistributable;
- compiles Inno Setup when `ISCC.exe` is found (including custom D-drive installs discovered from the uninstall registry);
- generates a SHA-256 manifest for the final installer and portable ZIP.
- copies the project's MIT license and matching license/notice files from `third_party/` into the installed `licenses/` directory.

## Release artifacts

Expected files:

```text
dist/
  WorkDesk-Setup-v0.2.0-x64.exe
  WorkDesk-v0.2.0-win64-portable.zip
  WorkDesk-v0.2.0-SHA256.txt
  WorkDesk-v0.2.0-win64/
```

A setup EXE is a self-contained installer, not a statically linked single-file application. It expands Qt and OCR runtime files into the installation directory.

## Legal and redistribution notes

Tesseract is Apache-2.0; Leptonica is BSD-2-Clause; tessdata_fast model files are Apache-2.0. Tesseract's own project notes that dependencies may carry separate licenses. The release script preserves license/notice files found under the UB Mannheim installation and writes a third-party notice into `ocr/`. Review the exact DLL set and its notices before public distribution. Qt, Aspose.PDF FOSS, libzip, zlib, and pugixml license obligations also apply; do not remove their license notices/source-offer requirements where applicable.
