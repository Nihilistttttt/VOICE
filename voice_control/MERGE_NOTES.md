# Doctor merged project

## Merge basis

- Main project: newer `doctor` project.
- SSVEP source: legacy mode-1 doctor project.
- The newer activity entry, home page, nine-page `ViewPager2` structure, monitoring, focus, comparison, MI, inference, posture and configuration functions are retained.

## Integrated SSVEP functions

- Complete doctor-side SSVEP control page with 11/13/15/17 Hz target selection.
- Start, stop and no-device self-test controls.
- Patient stimulus-start acknowledgement.
- Single-channel FBCCA analysis pipeline, notch filtering, sliding window, voting and result display.
- SSVEP result delivery to the patient.

## Communication

- 41002: acquisition device.
- 41003: generic forwarding.
- 41004: patient data channel only.
- 41005: patient-to-doctor command channel.
- 41006: bidirectional page/SSVEP control channel.
- 41007: UDP discovery.
- 41006 uses `PATIENT_READY` / `CONTROL_READY`, PING/PONG heartbeat, generation-based stale-socket protection, reconnect and page/SSVEP state snapshots.

## MI / SSVEP mutual exclusion

`TrainingModeCoordinator` owns the active training mode. Switching pages or returning home stops the previous mode before the next one becomes active:

- SSVEP stop clears FBCCA state, synthetic source thread, patient stimulus and device SSVEP command state.
- MI stop clears timer callbacks, waiting/trial state and sends device `STOP` only when MI was active.

Doctor page mapping:

- Page 4: SSVEP.
- Page 5: MI.
- Other pages: no active training mode.

## Validation completed

- Java syntax parsing for all source files.
- XML well-formedness and resource-reference checks.
- Duplicate class and duplicate layout-ID checks.
- Pure-Java FBCCA smoke test for all four target frequencies.
- Doctor-side 41006 integration test: handshake, heartbeat, live command delivery, state snapshot and reconnect recovery.

`local.properties` retains the original SDK path and may need to be updated by Android Studio on another computer.
