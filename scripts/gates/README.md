# Phase gate contract

> 2026-09-10: the old Loop / control-hash / Phase queue was deprecated by the user; the text below is a historical contract. Execute builds, relevant regressions and the necessary `delivery.ps1` directly today, without `loop-gate.ps1`, and not bound by the old STOP or CONTROL_HASHES rules. A single test is at most 300 seconds; runtime identity and source-isolation checks keep working.

> 2026-09-06 user-authorized handoff revision: current progress, five-minute short tests and user hardware acceptance follow `../docs/ACTIVE_DELIVERY_PLAN.md`, replacing the old strict serial build / 30-minute test / unconnected-device-block-all-delivery rules below. Historical records are not a current pass proof.

Every `scripts/gates/phaseN.ps1` must:

1. Accept the required parameter `param([string]$Root)`;
2. Use `Set-StrictMode -Version Latest`, exit non-zero on failure;
3. Not modify source code, control plane, STATE/BACKLOG/EVIDENCE/WORKLOG; may run the tested program so it writes this run-id's new log, counter and capture under the gitignored `logs/captures`;
4. Fail closed: a missing command, log, capture, counter, hardware capability or parsed field all fail;
5. Actually run the current-phase program/test; cannot only search old logs for "PASS";
6. Check the Debug/Release and Playbook-required run matrix;
7. Print each check, actual value, threshold, artifact path and final exit code;
8. Not access the renodx add-on, not download replacement DLSSNR files, not upload artifacts;
9. Be re-runnable; every run generates a unique run-id, and the evidence's run-id, time, input hash and current executable hash are verified; cannot mistake an old leftover for this run's result;
10. Exit 0 only means this phase's machine-verifiable conditions pass; it does not replace an independent Reviewer.

Launch V1 Phase 5-7 additionally require:

- `phase5.ps1` must parse the real SR->NR graph, NVOF non-zero motion, confidence, DAV2/Auto fallback, scene/reset, and 1080p60/native-4K60 30-minute endurance each; the old Zero-only gate must be rewritten and cannot keep being used;
- `phase6.ps1` must run a real DLSSG + 4K present/audio probe, proving the generated frame is not equal to the surrounding frames or a 50/50 blend, and check A/B, A/B/C, PTS/cadence/latency/queue/VRAM;
- `phase7.ps1` is joint gate: 4K Player, real 4K60 DirectShow/UVC Capture, Image Export, D3D12 NVENC 4K H.264/HEVC Video Export, and the product UI/recovery five sets of evidence, missing one fails;
- Hardware/SDK unavailability is an honest gate failure, not a skip. Do not rewrite capability-unavailable into product-complete.
- The gate must prove the product library linked to Player/Capture/Export actually executed real work; rewriting a similar flow only in `*_probe/main.cpp` or the harness does not pass.
- Every run JSON must bind this run-id, exe/input/config/runtime hash. For 4K checks, must also record the actual source/working/output extent and resource format; cannot infer from file name, window size or final resize.
- Do not shorten a Product Spec 30-minute requirement to 5 minutes. A 5-minute smoke may be run separately during development, but the field and gate name must both clearly say `smoke`, and cannot satisfy the endurance check.
- A manifest may only pass the dependency-identity check; cannot pass provider-created/evaluated/output-valid/quality check alone.
- Debug layer/GBV/InfoQueue must record stored/retrieved/failure/saturation/error/corruption; incomplete retrieval or descriptor-uninitialized both fail; cannot filter messages to manufacture a 0-error.

The base `scripts/loop-gate.ps1` hashes all Git-tracked and not-ignored
project files before and after the phase gate; the phase gate may only make the tested program write into the ignored `logs/`, `captures/` or build directory. Any source, gate, STATE/record or control-plane change makes the outer gate fail.

Suggested skeleton:

~~~powershell
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Resolve known executables and inputs from $Root.
# Run the actual test/probe and capture its exit code.
# Parse quantitative output and fail if any required field/artifact is absent.

if (-not $allChecksPassed) {
    exit 1
}
exit 0
~~~

Forbidden fake gates:

- Only checking that the exe or log "exists";
- Printing PASS and then exiting 0 unconditionally;
- The gate itself fabricating/writing result files or using a placeholder in place of the tested program's real output;
- Not checking run-id, input/exe hash, and directly reusing any old log or capture;
- Using the current date, line count or successful compile to stand in for Create/Evaluate/present evidence;
- Capturing a black image, constant image or duplicated frames that still pass because the API returned 0.
- Using window/desktop capture to pass off a physical capture card, or only verifying device enumeration without a sustained frame stream;
- Only checking that the output file exists without using ffprobe/its own decoder to check frame count, size, duration, audio track and start/end content;
- Motion/depth textures that exist but are all-zero, constant, stale or wrong direction still passing;
- Treating a simple blend, repeated present or a theoretical 2X counter as a DLSSG-generated frame.
- Using a single 1080p->4K SR harness to pass off native 4K Player/Capture/Export;
- Running the same 1080p material twice and naming JSON fields 4K/endurance;
- `EnhanceGraph` only counting, copying packets while the real GPU work still runs in harness-private code;
- Marking depth/NVENC/NVOF providers complete merely because the model/SDK manifest exists;
- Using whole-frame GPU->CPU readback/raw pipe to pass off D3D12 NVENC export;
- Capturing A/B/C windows beyond the Product Spec limit, or failing to record the deliberate lookahead frame count/milliseconds.
