# WorkDesk v0.2.0 Release Test Plan

Run these checks on Windows 10/11 x64. At least one final pass should use a clean VM or Windows account without Qt or Tesseract installed.

## 1. Build and package

- [ ] `cmake --build build --config Release` succeeds.
- [ ] `scripts/package-windows.ps1` completes without missing runtime/model warnings.
- [ ] `dist/WorkDesk-v0.2.0-win64/ocr/` contains `tesseract.exe`, Tesseract/Leptonica DLLs, `chi_sim.traineddata`, and `eng.traineddata`.
- [ ] `tesseract.exe --version` and `--list-langs` succeed from the staged OCR directory.
- [ ] Inno Setup produces `WorkDesk-Setup-v0.2.0-x64.exe`.
- [ ] SHA-256 manifest exists and matches both artifacts.

## 2. Clean-machine installation

- [ ] Install Full type. No Qt SDK, Tesseract install, PATH edit, or manual model copy is needed.
- [ ] Verify Program Files contains `ocr/` with both models and `licenses/` contains WorkDesk/Qt/third-party notices.
- [ ] Install Compact type and verify the `ocr/` folder is omitted; note that the downloaded setup EXE itself still embeds the optional payload.
- [ ] Import a Chinese scanned PDF and search a unique phrase from the scan.
- [ ] Confirm Debug log contains `Tesseract C API initialized once for this PDF` once per PDF, not once per page.
- [ ] Install Compact type. The app starts and ordinary text/PDF full-text search works; scanned PDF shows OCR-unavailable behavior without crashing.
- [ ] Extract portable ZIP and run `workdesk.exe`; OCR works from the `ocr/` sibling folder.

## 3. File-format regression

- [ ] DOCX, XLSX, PPTX: search a unique phrase placed beyond the first 1,000 characters.
- [ ] DOC, XLS, PPT: where Microsoft Office is installed, verify read-only COM extraction and search.
- [ ] Text PDF: search a phrase near the end; verify no OCR initialization for normal text pages.
- [ ] Mixed/scanned PDF: verify native text and OCR text are both searchable.
- [ ] Password-protected or damaged PDF: import does not crash; failure is reported/recorded safely.
- [ ] Image file: classified and filename-searchable; do not expect image OCR.

## 4. OCR performance and memory

- [ ] Reprocess the same scanned PDF used in the previous benchmark and record elapsed time.
- [ ] Compare with the earlier ~2-minute command-line-per-page run; do not claim a speedup until measured.
- [ ] Observe WorkDesk private memory before OCR, during a multi-page OCR run, and after completion.
- [ ] Confirm OCR model memory is released after the PDF task finishes.
- [ ] Verify 80-page and 5-minute OCR limits are respected.

## 5. Shutdown regression

- [ ] Start a multi-page scan PDF re-extraction, then choose tray > Exit WorkDesk.
- [ ] Tray tooltip indicates safe shutdown while a page is finishing.
- [ ] WorkDesk exits after active work reaches a cancellation point.
- [ ] Task Manager has no leftover WorkDesk process or `tesseract.exe` child.
- [ ] Close window with tray available: it minimizes to tray. Use tray Exit to close completely.
- [ ] Close window when no tray is available: application performs a real shutdown.

## 6. Adaptive theme regression

- [ ] Left sidebar displays sun/moon indicator.
- [ ] Confirm theme weight progresses at 30-minute timer intervals.
- [ ] In a test VM, temporarily adjust local time across 05:00–07:00 and 17:00–19:00 to verify dawn/dusk blends.
- [ ] Check text, selection, buttons, list rows, detail panel, and status labels remain readable through intermediate weights.

## 7. Search and data safety

- [ ] FTS5/trigram is enabled on first launch.
- [ ] Search remains responsive while imports/OCR run in the background.
- [ ] Existing v0.1.0 database opens without a destructive migration.
- [ ] Re-extraction updates summary and full text; it does not create duplicate file records.
- [ ] Original files remain accessible and deleting a record still follows the explicit confirmation flow.
