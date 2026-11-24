# WorkDesk v0.2.0 — Local Search & OCR Release

WorkDesk v0.2.0 expands document extraction and introduces a bundled OCR option for scanned PDFs.

## Highlights

- Broader office/document format support, with explicit handling levels documented in `docs/SUPPORTED_FORMATS.md`.
- Legacy DOC/XLS/PPT extraction through locally installed Microsoft Office COM automation where available.
- Page-by-page PDF text extraction plus Tesseract OCR for likely scanned pages.
- Full installer defaults to the OCR component; a Compact type omits OCR. The portable ZIP includes OCR.
- Bundled OCR engine and `chi_sim` + `eng` language models are stored beside the app, not linked into `workdesk.exe`.
- One Tesseract C API engine is reused for pages in a single PDF where the bundled DLL exports are available; CLI fallback is retained.
- Local-time day/night theme blending and coordinated application shutdown.

## Downloads

- `WorkDesk-Setup-v0.2.0-x64.exe` — Windows x64 installer. Choose Full (recommended) for bundled Chinese/English OCR, or Compact to omit OCR.
- `WorkDesk-v0.2.0-win64-portable.zip` — portable folder archive with OCR included.

## Requirements

- Windows 10/11 x64.
- The installer requests administrator permission and includes the Microsoft Visual C++ x64 Redistributable when required.
- No Qt SDK or separately installed Tesseract is required for end users.
- Old binary Office formats still require a compatible local Microsoft Office COM server for content extraction.

## Known limitations

- Standalone image files are catalogued only; image OCR and visual search are not part of this release.
- OCR is limited by scan resolution, skew, handwriting, complex tables, formulas, and multi-column layouts.
- PDF extraction is bounded to 256 MiB input, 5,000 pages, 2 Mi characters of indexed text, up to 80 OCR pages, 20 seconds per OCR page, and 5 minutes total OCR time.
- Some formats are only catalogued and searched by filename; see `docs/SUPPORTED_FORMATS.md`.

## Verification status

This release package is prepared from the supplied source tree. Windows Release compilation, Tesseract runtime collection, installer compilation, and clean-machine installation must be completed on the Windows build machine before publishing this release.
