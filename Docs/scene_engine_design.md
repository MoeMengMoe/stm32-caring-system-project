# Scene engine design

Updated: 2026-07-06

This document defines the first version of the multi-scene layer.

## 1. Why this layer exists

The project should not grow into one huge state machine with every possible elderly-care case hardcoded inside it.

The intended architecture is:

```text
raw sensors
-> feature extraction
-> scene detectors
-> scene fusion
-> common app state machine
-> relay / buzzer / TFT / MQTT / cloud / AI
```

The state machine owns actions and safety. The scene engine owns interpretation.

## 2. Current integration level

Current firmware runs the scene engine as a parallel observer:

```text
SensorMvp_Update()
AppStateMachine_Update()
SceneEngine_Update()
```

It does not directly trigger relay, buzzer, ACK, or alarm yet. Existing gas and long-still rules still remain in `AppStateMachine_Update()`.

This is intentional. The scene layer must first produce stable logs before it becomes the only input to the action state machine.

## 3. Signal format

Each scene detector emits a normalized signal:

```text
id              scene type
action_hint     NONE / OBSERVE / REPORT / NOTICE / ACK / ALARM
severity        0-3
confidence      0-100
evidence_primary
evidence_secondary
```

The fusion stage selects one `top` signal and keeps a `scene_mask` bitset for all active scenes.

`AI_SAMPLE` now includes:

```text
scene_top
scene_action
scene_sev
scene_conf
scene_count
scene_mask
scene_ev1
scene_ev2
```

COM6 `p` also prints:

```text
[INFO] console scene top=... action=... severity=... conf=... count=... mask=... evidence=.../...
```

## 4. Current scenes

| Scene | Meaning | Current action hint |
| --- | --- | --- |
| `SENSOR_FAULT` | BME/MQ/radar data invalid or inconsistent | `REPORT` |
| `GAS_WARN` | MQ ppm estimate >= 100 | `ACK` |
| `GAS_ALARM` | MQ ppm estimate >= 300 | `ACK` |
| `LONG_STILL_WATCH` | Radar still time >= 8s | `REPORT` |
| `LONG_STILL_RISK` | Radar still time >= 20s | `ACK` |
| `RADAR_PRESENCE` | Any presence source says someone is present | `OBSERVE` |
| `PRESENCE_CONFLICT` | PIR/OT2/UART radar presence disagree | `REPORT` |
| `MOTION_BURST` | Radar motion score or active gate count is unusually high | `REPORT` |
| `HEAT_STRESS` | Temperature is high, especially when someone is present | `REPORT` / `NOTICE` |
| `COLD_RISK` | Temperature is very low | `REPORT` |
| `HUMIDITY_HIGH` | Humidity is high | `REPORT` |
| `HUMIDITY_LOW` | Humidity is low | `REPORT` |
| `NETWORK_OFFLINE` | Network is offline | `REPORT` |
| `ACTIVE_ACK` | The app state machine is already waiting for ACK or alarming | `ACK` / `ALARM` |

## 5. Product meaning

Not every scene is an alarm.

Use three classes:

```text
action scenes   -> may trigger ACK/ALARM after policy approval
report scenes   -> uploaded and displayed, useful for family/cloud review
learning scenes -> logged first, later used by AI or tuned rules
```

Examples:

- Gas risk and no-response are action scenes.
- Humidity, heat, sensor fault, and presence conflicts are report scenes.
- Motion burst and frequent movement are learning scenes until enough data exists.

## 6. AI role

The first local AI target should be:

```text
window features + scene context -> risk_hint / scene_hint
```

The AI result should not directly drive actuators in v1. It should become another scene signal or a risk hint, then the state machine and policy layer decide the final action.

## 7. Next migration step

After validating scene logs, migrate the direct rules in `AppStateMachine_Update()`:

```text
update_long_still()
update_gas_risk()
```

into scene-driven policy:

```text
SceneSignal(action=ACK, severity>=2)
-> policy allowlist
-> AppStateMachine_HandleSceneSignal()
```

This will keep the state machine small while allowing many future scenes.
