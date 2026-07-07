# AI dataset collection guide

Updated: 2026-07-06

This document defines how to collect prototype logs for later local/cloud AI work.

## 1. Source of truth

Use COM6 logs at `115200 8N1`.

The main machine-readable line is:

```text
[AI_SAMPLE] t=... session=... label=... temp=... hum=... env_valid=... gas_valid=... gas_mv=... gas_base=... gas_ppm=... gas_dbg_offset=... gas_delta=... presence=... pir=... rd03_ot2=... radar_valid=... radar_presence=... radar_cm=... zone=... peak_gate=... peak_cm=... peak_energy=... active_gates=... motion=... energy=... still=... occupied=... radar_age_ms=... state=... scenario=... risk=... risk_src=... scene_top=... scene_action=... scene_sev=... scene_conf=... scene_count=... scene_mask=... scene_ev1=... scene_ev2=... event_id=... event_type=... trigger=... flags=... ack_ms=... relay=... manual=... auto=...
```

Use `[INFO] app event ...` and `[INFO] app event detail ...` as event-boundary labels.

The current firmware also supports manual dataset labels from COM6:

```text
e = env_normal
w = walk_motion
f = fall_sim
j = long_still
g = gas_debug
v = voice_risk
x = idle / clear label
```

Every non-idle label increases `session`. The `session` and `label` fields are written into each following `[AI_SAMPLE]` line until `x` clears the label.

## 2. File naming

Use one file per scene and per physical layout:

```text
ai_YYYYMMDD_scene_subject_layout_run.log
```

Examples:

```text
ai_20260706_normal_walk_gary_v05_run01.log
ai_20260706_voice_risk3_simon_v05_run01.log
ai_20260706_long_still_gary_v05_run01.log
ai_20260706_gas_debug_350ppm_v05_run01.log
```

If the sensor angle, desk position, radar direction, or prototype base changes, increase the layout tag, for example `v05 -> v06`.

## 3. Labels

The first training labels come from firmware fields:

- `state`: current state-machine state.
- `session`: manual collection segment id. `0` means no active label.
- `label`: manual scene label from COM6, for example `fall_sim` or `voice_risk`.
- `env_valid / gas_valid`: whether BME280 and MQ data are currently valid.
- `gas_mv / gas_base / gas_delta`: MQ filtered voltage, learned baseline, and absolute delta in mV.
- `presence / pir / rd03_ot2 / radar_presence`: fused presence result and the three source votes.
- `peak_gate / peak_cm / peak_energy / active_gates / radar_age_ms`: Rd-03 V2 radar quality/detail fields useful for later sequence models and threshold tuning.
- `scenario`: firmware scenario.
- `risk`: final risk level after sensor rules, voice risk floor, and state logic.
- `risk_src`: human-readable reason for the current risk, for example `VOICE_ACK`, `GAS`, `RADAR_UART`, `RD03_OT2`, `NETWORK`, or `NONE`.
- `scene_top / scene_action / scene_sev / scene_conf`: multi-scene engine top signal, action hint, severity, and confidence.
- `scene_count / scene_mask`: number of active scene signals and a bitset of all active scenes.
- `scene_ev1 / scene_ev2`: compact evidence values for the top scene, such as ppm, still seconds, distance, or fault bits depending on `scene_top`.
- `event_type`: latest event label, for example `VOICE_RISK`, `GAS_RISK`, `LONG_STILL`.
- `trigger`: source modality, for example `VOICE`, `RADAR`, `SENSOR`, `BUTTON`, `REMOTE`.
- `flags`: event-specific extension bits.

For voice events, `flags` bit8-bit9 stores the original `RISK:0..3` level from Simon's ESP32-S3.

## 4. Clean data rules

- Real MQ calibration logs must have `gas_dbg_offset=0`.
- Gas demo logs using `D,request_id,6,4,value` or COM6 `4/5` must be named with `gas_debug`.
- Do not mix physical layouts in one dataset file.
- Start each run with 10 to 20 seconds of normal idle data before triggering the scene.
- End each run by ACK/clear and keep logging for 5 to 10 seconds after returning to normal.

## 5. Initial scene set

Collect these before training any model:

| Scene | Minimum runs | Main trigger |
| --- | ---: | --- |
| Normal idle | 3 | none |
| Normal walking near radar | 3 | radar |
| Long still | 3 | radar still seconds |
| Voice low risk | 3 | `RISK:1` |
| Voice high risk | 3 | `RISK:3` |
| SOS button | 3 | button |
| Gas debug warning | 3 | +150 ppm offset |
| Gas debug alarm | 3 | +350 ppm offset |
| Cloud relay control | 2 | remote |
| ACK/clear recovery | 3 | button or voice `RISK:0` |

## 6. First AI target

Do not start with a large black-box model.

First target:

```text
sensor sequence + event context -> risk_hint
```

The model output should be advisory. The state machine remains the final owner of relay, buzzer, TFT, MQTT, and alarm behavior.

## 7. Convert logs to CSV

Use the bundled lightweight parser:

```text
python tools/ai_sample_to_csv.py input.log output.csv
```

Example:

```text
python tools/ai_sample_to_csv.py ai_20260706_voice_risk3_simon_v05_run01.log ai_20260706_voice_risk3_simon_v05_run01.csv
```

## 8. Quick summary

Use this immediately after a test run:

```text
python tools/ai_dataset_summary.py input.log
python tools/ai_dataset_summary.py output.csv
```

It prints sample count, duration, label/state/event distribution, risk range, gas/radar ranges, and valid ratios for `env_valid`, `gas_valid`, `radar_valid`, `presence`, `pir`, `rd03_ot2`, and `radar_presence`.

## 9. Window features

After converting a log to CSV, build simple model-ready sliding-window features:

```text
python tools/ai_window_features.py input.csv windows.csv
python tools/ai_window_features.py input.csv windows.csv 10 5
```

The optional numbers are `window_seconds` and `stride_seconds`. The default is a 10 second window with 5 second stride.
