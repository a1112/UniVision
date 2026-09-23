# Repository implementation status

Updated: 2026-09-23. This file describes the current checkout; the imported v0.1
design package remains a draft and its `validation-report.json` records only
design-example validation.

| Area | Implemented in this checkout | Still open |
| --- | --- | --- |
| M0/Core boundary | Core ABI unchanged; optional targets default OFF; Core-only build, five tests and install checked on Windows MinGW 13.1 | Maintainer license review; real camera and Linux CI execution |
| Shared contracts | Frame identity, unit and clock-domain fields, bounded Mono8 Core-frame copy, plane/budget validation, relative artifact path check, SHA-256 | Full PFNC/multipart adapter, versioned wire JSON parser and cross-process ownership |
| UniStream | UVS1 fixed header codec with split/sticky-message tests; bounded local inspection/preview queue, duplicate-key conflict and received receipts | TCP/TLS 1.3 transport, mutual authentication, credit, durable/processed receipts, spool/resume, two-machine load tests |
| Shared recording | Optional pinned MCAP 2.1.3 writer/reader, one uncompressed segment, manifest and payload/segment hash verification; truncated tails and published segments without a manifest remain partial | Segment rollover, general crash-prefix recovery, fsync/power-loss verification and independent MCAP compatibility testing |
| VisionReplay | Read-only session loading, sequence seek, generation invalidation, frozen-run numeric comparison with input/geometry gates | Playback clock controls, indexed large-session seek, state-node warm-up, UI and report export |
| UniMeasure | Synthetic/metric circle diameter fit, input/calibration/coverage/residual guards, guarded error decision | Reference uncertainty budget, independent gauge data, physical accuracy validation, additional measurands |
| InspectFlow | Four registered offline nodes, saved JSON definition loader, typed preflight, cycle/unknown node rejection, workspace path confinement, synthetic pipeline and immutable run artifact | General DAG scheduler, persistent failure/cancel manifests, isolated workers, cache/checkpoints and GUI editor |
| CLI | `univision_uv_cli demo <new-workspace>` exercises simulator → stream → MCAP → replay plus profile → measure → flow archive; `run-synthetic-flow` loads a saved graph | General import/configuration commands and workbench process API |

The current 12 CTest cases pass on Windows MinGW 13.1 and MSVC 19.44 and support
an L1 synthetic headless slice. They do not establish
L2 real-device operation, L3 physical metrology, L4 production integration or
the throughput numbers in `08-acceptance.md`.

## Acceptance alignment

The following IDs from `08-acceptance.md` have direct L1 test evidence in this
checkout, limited to the named local behavior: C01 (Core-only build/install), C03
(uint64 MCAP round trip), C04 (bounded layout validation), S02 (local duplicate
key handling), R01 (record/read payload and identity), M01 (synthetic circle fit),
M02 (missing calibration/raw-unit rejection), M05 (guard-band arithmetic), F02
(unknown node/version rejection), F07 (offline node allowlist), and F08 (invalid
measurement cannot pass). C05, C08, S01, S03, S04, R02–R05, M03, F01, F03 and
F04 have only partial L1 evidence; their full acceptance conditions remain open.
All other IDs remain untested or unimplemented, including hardware throughput,
two-machine security, uncertainty evidence, and CLI/UI equivalence. This is not
an acceptance sign-off for any ID at L2 or above.

`graph_sha256` in a run manifest hashes the normalized in-memory execution graph,
including the explicit synthetic provenance inserted by the CLI. It is not the
byte hash of the source JSON file. A failed archive write may leave a `.tmp` file;
there is no crash recovery protocol for flow runs yet.

## Local verification

```text
cmake --preset industrial
cmake --build --preset industrial
ctest --preset industrial
cmake --install build/industrial --prefix build/industrial/install
```

The `industrial` preset downloads the pinned MCAP and nlohmann/json source
archives only when recording is enabled. The default presets keep all extension
options off. On Windows, run `build/industrial/univision_uv_cli.exe demo
build/demo-01` with a new output path. The demo produces
`demo-session/session.json` and `synthetic-results/demo-run.json`.
