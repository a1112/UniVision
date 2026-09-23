# Industrial suite design review and repository mapping

Status: implementation baseline for an optional, runnable headless slice. The attached
v0.1 package remains a draft; its example validation report is not product evidence.

## M0: checked repository baseline

- HEAD: `d0eeb092c0833af9dfa8b74531e9f8d9919d4103`.
- The current `README.md` blob is `898303b541ad7466370957a1582a4b0492d40144`,
  matching the snapshot named by the design package.
- Core is C++20, version 0.3.0, with `UniVision::Core` and C ABI version 1. The
  public `Frame` contains a `FrameBuffer` lease; GenTL returns the buffer to its
  producer when the lease is released. A slow extension must take an explicit,
  bounded owned copy before releasing that lease.
- Default CMake config builds Core, examples and tests. Qt debugger is optional.
  No recording, replay, measurement or flow implementation exists in this tree.
- CI covers Windows and Linux. No license file is present in this checkout; this
  needs maintainer review before adding distributable third-party packages.
- Baseline verified on Windows with MinGW 13.1 and Ninja: 5/5 CTest tests pass.
  The existing GenTL compile emits a `NOMINMAX` redefinition warning.

## Chosen architecture

Keep Core's public ABI unchanged. Add independently selectable headless targets:

1. `contracts`: immutable frame identity, clock, unit, payload layout and
   artifact references; bounded Core-frame copy and semantic validation.
2. `recording`: the single MCAP writer/reader and session manifest, with a
   recoverable incomplete tail and content checks. It owns persistent bytes.
3. `stream`: explicit local/remote publisher and subscriber, with bounded
   admission, full-frame integrity, received/durable distinction, and no
   automatic camera control. Remote access requires TLS and authorization.
4. `replay`: session catalog, bounded seek/playback generation and comparison
   of frozen runs; it only reads original recording data.
5. `measure`: offline circle-fit diameter, diagnostic validity, calibration
   version and guarded decision. Synthetic input is never a production pass.
6. `flow`: registered offline DAG nodes, typed ports, preflight, frozen run
   revision, cancellation and durable sink. Live device and arbitrary script
   nodes are rejected by the executor.
7. A CLI exercises the same engines without a GUI. A workbench can follow after
   the headless contracts and process API are stable.

Dependencies point downward: stream/replay/measure/flow may depend on contracts;
recording depends on contracts; replay depends on recording; flow may use replay,
measure and recording. Core never links any extension. New options default OFF.

## Alternatives considered

- Extend Core directly: fewer targets, but it would impose storage/network and
  processing responsibilities on all existing camera consumers.
- Use a Python-only service: quick to prototype and easy to inspect, but it
  diverges from the repository's C++20 runtime and complicates frame ownership.
- Optional C++ targets (chosen): more setup work, but preserves the current
  package boundary and shares the existing simulator/test infrastructure.

## Data and failure rules

The imported schemas and synthetic examples under `docs/design/industrial-suite`
are the source of field names. C++ validators also enforce arithmetic overflow,
plane bounds, unit compatibility, uint64 identity, workspace-relative paths and
content hashes. A received frame is not durable until the recording sink commits.
Missing bytes become a gap or partial session. An invalid measurement cannot become
a pass, and a flow run cannot succeed before all required sinks commit.

## Verification layers

- L0: validate imported schema examples and provenance.
- L1: unit and synthetic integration tests for every engine, including negative
  cases, crash/partial recovery, and unchanged Core-only build/install behavior.
- L2/L3: platform, real camera, two-machine TLS/throughput and independent
  metrology evidence require equipment and representative data; record as
  untested until executed.

See the design package's `07-delivery-plan.md` and `08-acceptance.md` for the full
M0-M5 sequence and C/S/R/M/F acceptance IDs.
