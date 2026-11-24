# WorkDesk v0.2.0 Release Prep — Overlay Contents

This ZIP is an overlay for the existing WorkDesk repository, not a complete repository clone.

Included:
- Root build/version/docs metadata and `.gitignore`.
- Updated `src/app` and `src/core` sources based on the supplied project tree and the v0.3 preview changes.
- A `FileManager.h` signature correction so cancellation-token calls compile against the implementation.
- Windows Release and OCR runtime packaging scripts.
- Inno Setup configuration, licenses, release notes, and test plan.

Not included:
- `third_party/` (keep the user's existing `aspose_pdf`, `libzip`, `pugixml`, and `zlib` directories).
- `build/`, `dist/`, databases, user files, `.git/`, or backups.
- A prebuilt `workdesk.exe`, portable ZIP, or setup EXE. Those must be produced on the Windows MSVC/Qt build machine.

The archive includes the exact root `CMakeLists.txt`, `CMakePresets.json`, and `.gitignore` provided in the current conversation, with the CMake project version updated to 0.2.0 for this release candidate.
