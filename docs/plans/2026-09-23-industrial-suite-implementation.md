# Industrial Suite Implementation Plan

> Work through the tasks in dependency order. Use test-first cycles and verify
> each claimed acceptance case with the named command.

**Goal:** Add optional, runnable UniStream, VisionReplay, UniMeasure and InspectFlow
modules that share frame and artifact contracts while preserving the Core-only build.

**Architecture:** Core remains unchanged. Extension targets live in `modules/` and
are enabled by explicit CMake options, each with a public header and tests.
Recording is the single persistence component. Headless tests and CLI use the
same APIs that a later workbench will call.

**Tech Stack:** C++20, CMake 3.24, CTest; pinned MCAP C++ header library for
recording; JSON library and OpenSSL only in enabled extensions as needed.

---

## Task 1: M0 baseline and design material

**Files:** `docs/design/industrial-suite/**`,
`docs/plans/2026-09-23-industrial-suite-design.md`.

1. Compare existing repository files with the attached package before copying.
2. Record HEAD, README blob, Core ABI, Frame ownership, CMake targets, current
   tests, license status and untested hardware conditions.
3. Run Core-only configure/build/CTest. Keep the full command and results in
   the design review. No extension should enter this default build.

## Task 2: Contract and Core frame adapter

**Files:** `modules/contracts/include/univision/industrial/contracts.h`,
`modules/contracts/src/contracts.cpp`, `tests/industrial-suite/contracts_tests.cpp`,
`CMakeLists.txt`.

1. Write tests for uint64 sequence, clock-domain distinction, malformed plane
   dimensions/offset/stride, depth unit mismatch, host-only copy, and byte budget.
2. Compile and observe the tests fail because the API is absent.
3. Implement immutable envelope and owned bytes. Copy the current Core lease
   only after validating size and budget. Never convert pixels implicitly.
4. Pass contract tests and the five existing Core tests. Add a default-OFF
   `UNIVISION_ENABLE_CONTRACTS` target and installation export.

## Task 3: UniMeasure offline circle diameter

**Files:** `modules/measure/include/univision/industrial/measure.h`,
`modules/measure/src/measure.cpp`, `tests/industrial-suite/measure_tests.cpp`.

1. Write failing tests for ideal/noisy synthetic circles, invalid raw units,
   missing calibration, fewer than 30 valid points, degenerate coverage and
   guard-band pass/fail/indeterminate examples from `04-unimeasure.md`.
2. Implement bounded geometric least-squares with finite-value checks,
   diagnostics and a versioned method profile. Keep validity distinct from
   decision; synthetic input yields `not-evaluated` for production.
3. Verify tests and record that mathematical tolerance is not physical accuracy.

## Task 4: Shared recording and recovery

**Files:** `modules/recording/include/univision/industrial/recording.h`,
`modules/recording/src/recording.cpp`, `tests/industrial-suite/recording_tests.cpp`.

1. Write failing round-trip, tampered payload, incomplete tail and no-overwrite
   tests using synthetic frames and a temporary workspace.
2. Pin the MCAP C++ dependency. Write uncompressed `univision.frame.v1`
   messages once, with metadata length + JSON + binary payload. Publish closed
   segments and manifest only after validation; retain partial tails.
3. Reader verifies length, hash, envelope and partial status before exposing a
   frame. Confirm original input hashes stay unchanged after derived runs.

## Task 5: UniStream delivery

**Files:** `modules/stream/include/univision/industrial/stream.h`,
`modules/stream/src/stream.cpp`, `tests/industrial-suite/stream_tests.cpp`.

1. Write failing tests for split/stuck UVS1 headers, over-limit lengths,
   duplicate key with same/different digest, isolated slow preview, and
   received versus durable receipts.
2. Implement bounded admission, frame chunking/reassembly and local loopback.
   Add TLS 1.3 mutual authentication and authorized stream identity before
   enabling non-loopback addresses. Never downgrade to plaintext.
3. Test reconnect with a bounded retained interval and explicit GAP for data
   outside it. Measure payload and wire bytes separately.

## Task 6: VisionReplay

**Files:** `modules/replay/include/univision/industrial/replay.h`,
`modules/replay/src/replay.cpp`, `tests/industrial-suite/replay_tests.cpp`.

1. Write failing tests for sequence/time seek, generation invalidation,
   missing frames, partial recording and mismatched run input digests.
2. Implement read-only session catalog and replay controls. Compare frozen
   runs only after checking input, units, calibration and coordinate frame.
3. Export per-frame differences with input/run provenance; never mutate source.

## Task 7: InspectFlow offline DAG

**Files:** `modules/flow/include/univision/industrial/flow.h`,
`modules/flow/src/flow.cpp`, `tests/industrial-suite/flow_tests.cpp`.

1. Write failing tests for cycles, unknown node/version, wrong port or unit,
   missing sink, live camera/script rejection, revision freeze, cancellation,
   invalid measurement and sink commit failure.
2. Register only `ProfileFile`, `CircleFitDiameter`, `VerificationRule`, and
   `ResultArchive` first. Topologically execute a frozen graph with bounded
   inputs and a persistent run manifest.
3. Ensure a run may succeed with a failing decision, but never when a required
   durable sink fails. Keep invalid measurement as `indeterminate`.

## Task 8: Headless integration and delivery evidence

**Files:** `apps/uv-cli/**`, `tests/industrial-suite/integration_tests.cpp`,
`README.md`, `.github/workflows/**`, `docs/design/industrial-suite/status.md`.

1. Create a CLI path for import/record/replay/measure/run with the synthetic
   fixture. Run it once end to end and compare produced hashes and IDs.
2. Run the design example validator, Core-only and extension build/CTest on
   Windows; add Linux CI. Verify install/export consumer paths.
3. Mark C/S/R/M/F acceptance IDs individually passed, failed or untested.
   Leave hardware, two-machine performance and physical metrology untested
   until the required equipment and datasets are available.

Each code task follows a red-green-refactor cycle. A passing synthetic test
supports only its specific acceptance condition, not L2/L3 certification.
