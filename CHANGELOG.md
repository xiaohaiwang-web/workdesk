# Changelog

## [0.2.0] - 2026-11-24

### Added
- Expanded document classification and extraction coverage across common office, open-document, ebook, email, diagram, design, CAD, and image extensions.
- Added legacy Word, Excel, and PowerPoint COM extraction paths for compatible locally installed Office applications.
- Added page-by-page PDF text extraction and optional scan-page OCR.
- Added Tesseract C API reuse per PDF with CLI fallback.
- Added app-private OCR runtime packaging with Simplified Chinese and English models.
- Added local-time day/night theme blending and a low-frequency 30-minute update timer.
- Added coordinated shutdown for import/search workers and OCR child processes.

### Changed
- Separated summary preview from full-text indexing.
- Updated Windows release packaging to produce a full OCR portable ZIP and a setup program with Full/Compact install types.
- Updated version metadata to 0.2.0.

### Notes
- OCR of standalone image files and image-based visual search are not included in this release.
- OCR accuracy depends on scan quality, language model, layout, and document structure.
- The release build still requires Windows compilation and clean-machine smoke tests before publication.
