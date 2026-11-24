# Apply WorkDesk v0.2.0 Release Preparation

This is a source overlay for the existing Windows repository. It does **not** include `third_party/` or a prebuilt EXE; keep the project's existing four dependency folders intact.

## 1. Protect the current working tree

From `D:\workdesk`:

```powershell
git status -sb
# Recommended: create a release-prep branch before applying the overlay.
git switch -c release/workdesk-v0.2.0
```

Back up the current working tree or create a commit before replacing files.

## 2. Apply the overlay

Extract this ZIP into `D:\workdesk` and allow the listed files to replace matching files. The archive preserves the correct `src/app` and `src/core` paths. It does not contain `build/`, `dist/`, `third_party/`, `.git/`, or your existing database.

The `.gitignore` is included unchanged. The existing `third_party/aspose_pdf`, `third_party/libzip`, `third_party/pugixml`, and `third_party/zlib` folders must remain in place.

## 3. Build and package

The user's previously verified development environment is expected:

- Qt 6.11.2 MSVC 2022 x64 at `C:\Qt\6.11.2\msvc2022_64`
- Visual Studio 2022 / MSVC x64
- Tesseract 5.x x64 installed at `C:\Program Files\Tesseract-OCR` (or provide `-TesseractRoot`)
- `chi_sim.traineddata` and `eng.traineddata` in the Tesseract `tessdata` directory
- Inno Setup 6/7 (the script searches the registry and the previously used `D:\Inno Setup 6` path)

Run one command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-release.ps1
```

Or specify non-default locations:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-release.ps1 `
  -QtRoot "C:\Qt\6.11.2\msvc2022_64" `
  -TesseractRoot "C:\Program Files\Tesseract-OCR"
```

The script performs a Release build, collects Qt runtime files, copies the local Tesseract engine/DLLs/models into the private `ocr/` folder, validates `chi_sim` and `eng`, creates a portable ZIP, downloads and verifies Microsoft's VC++ x64 runtime, compiles the Inno Setup installer, and writes SHA-256 hashes.

## 4. Expected release artifacts

```text
dist/WorkDesk-Setup-v0.2.0-x64.exe
dist/WorkDesk-v0.2.0-win64-portable.zip
dist/WorkDesk-v0.2.0-SHA256.txt
dist/WorkDesk-v0.2.0-win64/
```

The Full installer type is selected by default and installs OCR. Compact omits the `ocr/` folder. The portable ZIP always includes OCR. The downloaded setup EXE still contains the compressed OCR payload even when a user later chooses Compact.

## 5. Do not publish until verified

Follow `docs/RELEASE_TEST_PLAN.md`, especially clean-machine Full/Compact install, Chinese scanned-PDF OCR, OCR model initialization once per PDF, shutdown during OCR, and database upgrade from v0.1.0.

This source overlay has not been compiled with the user's Windows Qt/MSVC toolchain in this environment. The Windows build and generated binary artifacts must be verified on the development PC before creating the GitHub Release.
