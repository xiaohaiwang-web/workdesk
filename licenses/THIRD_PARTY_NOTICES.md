# Third-Party Notices

WorkDesk is distributed with third-party software. Keep these notices with the corresponding release artifacts.

## Tesseract OCR

The Tesseract OCR engine is licensed under Apache License 2.0. The included `chi_sim` and `eng` language data are from the `tessdata_fast` repository, also licensed under Apache License 2.0.

- Engine: https://github.com/tesseract-ocr/tesseract
- Language data: https://github.com/tesseract-ocr/tessdata_fast
- Windows distribution: https://github.com/UB-Mannheim/tesseract

See `Apache-2.0.txt`.

## Leptonica

Leptonica is licensed under the BSD 2-Clause license.

- https://github.com/DanBloomberg/leptonica

See `BSD-2-Clause-Leptonica.txt`.

## Other bundled OCR dependencies

The UB Mannheim Windows distribution may include separate DLLs for image codecs, compression, archive, networking, and cryptography libraries. The packaging script copies these DLLs as separate, unmodified files and preserves license and notice files found in the installed Tesseract distribution under `ocr/licenses/upstream/`.

The exact DLL set depends on the UB Mannheim build installed on the release machine. Review `ocr/OCR_RUNTIME_MANIFEST.txt` and `ocr/licenses/upstream/` for each release. Do not delete upstream notices.

## WorkDesk and other dependencies

- WorkDesk source: MIT License.
- Qt 6 modules and plugins: follow the Qt license applicable to the modules used and the way Qt was obtained. The release packaging script attempts to copy license and notice files from the Qt installation into `licenses/`.
- Aspose.PDF FOSS, libzip, zlib, and pugixml: retain the license and copyright material shipped with their source trees under `third_party/` and verify that the selected versions permit the intended distribution.
