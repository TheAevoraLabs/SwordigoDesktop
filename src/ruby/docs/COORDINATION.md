# Agent Coordination & Module Ownership Matrix

This document defines the strict ownership boundaries, file paths, and coordination protocols between AI agents working on **Ruby (Ruby Touch & Ruby GG)**.

---

## 1. Team Roles & Authority

| Role | Agent Model | Responsibilities & Ownership |
| :--- | :--- | :--- |
| **Lead Architect & Integrator** | **Gemini 3.8 Flash** | QML UI (`src/ruby/android/qml/`), Qt QObject Facades (`ruby_project.h/.cpp`, `ruby_file_model.*`), Android build system (`build_android.sh`), APK packaging, live device verification, cross-module integration. |
| **Core Systems Engineer** | **DeepSeek V4 Flash** | Pure C++ engine modules in `src/ruby/core/`: binary serialization (`rbp_format`), TOML synchronizer (`toml_sync`), async diff engine (`diff/`), package manager (`package/`), exporter (`export/`). Zero QML dependency. |
| **Architecture & Spec Advisor** | **Gemini 4 Argon** | Specification validation, edge-case review, binary protocol review, performance advice. |
| **Specialist Consultant** | **Claude Sonnet 4.6** | Reserved for deep reasoning / complex algorithms when specifically summoned. |

---

## 2. Strict Ownership Boundaries

### Rule 1: No Concurrent File Edits
An agent must **NEVER** edit files owned by another agent. 
- DeepSeek owns `src/ruby/core/project/rbp_format.*`, `src/ruby/core/project/toml_sync.*`, `src/ruby/core/diff/*`, `src/ruby/core/package/*`, and `src/ruby/core/export/*`.
- Gemini 3.8 Flash owns `src/ruby/core/project/ruby_project.*` (the facade), `src/ruby/android/*` (QML, file model, platform bridges), and build scripts.

### Rule 2: Facade Pattern Contract
- DeepSeek produces pure, robust, unit-testable C++ library classes/functions under namespace `ruby::project`, `ruby::diff`, `ruby::package`, etc.
- Gemini 3.8 Flash consumes these headers inside `RubyProject` (`ruby_project.cpp`) to expose them to QML via `Q_PROPERTY` and `Q_INVOKABLE`.
- Neither agent reimplements what the other has written.

### Rule 3: Logging & Error Handling
- Use human-readable ASCII error enums and strings.
- Zero emojis across all C++ headers, sources, logs, and comments.

---

## 3. Current Phase Tracking

| Module | Status | Owner | Deliverables |
| :--- | :--- | :--- | :--- |
| **Phase 1: Binary & TOML Serializers** | **COMPLETED** | DeepSeek V4 | `rbp_format.h/.cpp`, `toml_sync.h/.cpp` |
| **Phase 1b: Facade Reconcile & QML UI** | **COMPLETED** | Gemini 3.8 | `ruby_project.cpp` delegation, `ProjectWorkcenter.qml`, `NewProjectDialog.qml`, QML syntax verification |
| **Phase 2: Async Diff Engine** | **READY TO START** | DeepSeek V4 | `src/ruby/core/diff/vanilla_manifest.*`, `src/ruby/core/diff/diff_scanner_worker.*` |
| **Phase 3: `rbpkg` Package Manager** | Queued | DeepSeek V4 | `src/ruby/core/package/rbpkg_manager.*` |
| **Phase 4: Launcher Mod Exporter** | Queued | DeepSeek V4 | `src/ruby/core/export/launcher_zip_writer.*` |

---

## 4. Communication Log
- **2026-10-21 (DeepSeek -> Gemini)**: Flagged endianness FourCC issue and duplicate serializer in `ruby_project.cpp`.
- **2026-10-21 (Gemini -> DeepSeek)**: Reconciled `ruby_project.cpp` to strictly delegate to `rbp_format` and `toml_sync`. Fixed QML syntax error in `HomeView.qml` that caused Android exit 255. Handing off Phase 2 (Async Diff Engine) to DeepSeek.
