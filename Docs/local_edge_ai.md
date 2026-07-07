# Local Edge AI Prototype

This module is the STM32-side lightweight AI path used by the caring node. It is intentionally small enough to run in the main loop without RTOS, heap allocation, or external memory.

## Runtime role

`Modules/ai/edge_ai.c` reads the current sensor snapshot and app context, then produces:

- `edge_ai_scene`: one of normal, environment comfort, gas risk, stillness risk, activity anomaly, or system context.
- `edge_ai_raw`: the unsmoothed MLP top class before temporal fusion.
- `edge_ai_risk`: 0-3 risk hint.
- `edge_ai_conf`: model confidence percentage.
- `edge_ai_stab`: how many consecutive inference ticks support the same fused scene, capped for logging.
- `edge_ai_ev`: evidence bitmask showing which modalities supported the decision.
- `edge_ai_trend`: short-term trend/anomaly pressure from gas, radar, stillness, offline, and fault context.
- `edge_ai_score`: anomaly score for logging and later comparison.

The app state machine treats this output as an advisory local-AI layer. It can raise risk when confidence and stability are high, but it does not suppress hard safety rules such as SOS, gas warning, voice risk, or ACK handling.

## Runtime fusion

The board-side AI path is deliberately split into three layers:

1. The compact MLP creates a fast raw scene estimate from the current sensor vector.
2. Temporal fusion smooths class scores and tracks scene stability so a single noisy radar or MQ sample does not immediately become an alarm.
3. Safety fusion applies deterministic guardrails for known critical patterns such as high gas ppm, fast gas rise, long stillness with radar evidence, motion bursts, network offline, and sensor health issues.

The state machine consumes the fused result:

- Risk 1 with enough stability becomes a local `NOTICE` hint.
- Stable risk 2+ scenes such as gas risk or long stillness can enter `ACK_WAIT`, which drives buzzer/relay/remote event handling.
- Existing hard events still win over AI hints.

## Timing model

The edge AI path is scheduled cooperatively in the bare-metal super-loop. The main loop calls `EdgeAi_UpdateIfDue(...)`; inference runs only every 250 ms, while other loop iterations immediately reuse the latest result. This prevents the AI layer from running on every loop turn and keeps UART, radar DMA processing, relay control, buzzer, and display work responsive.

Timing fields are exposed for validation:

- `edge_ai_ms`: last measured inference time in milliseconds.
- `edge_ai_max_ms`: worst measured inference time since boot.
- `edge_ai_age_ms`: age of the current AI result.
- `edge_ai_skip`: number of loop calls skipped by the scheduler.
- `edge_ai_ran`: whether this sample was taken on an inference tick.
- `edge_ai_stale`: result age exceeded the stale threshold.

Expected prototype behavior: `edge_ai_ms` is usually 0-1 ms on STM32U5 for the current 12-8-6 MLP, `edge_ai_age_ms` stays below about 250-500 ms during normal operation, and `edge_ai_stale` stays 0.

## Input vector

The first firmware version uses 12 normalized inputs:

1. High-temperature deviation.
2. Low-temperature deviation.
3. High-humidity deviation.
4. Gas ppm estimate.
5. Gas delta from baseline.
6. Fused presence.
7. Radar valid-presence flag.
8. Radar distance.
9. Radar active gate count.
10. Radar motion score.
11. Radar still duration.
12. Network offline state.

## Model shape

The prototype model is a compact MLP:

```text
12 inputs -> 8 hidden neurons -> 6 scene classes
```

The current weights are bootstrapped from domain rules so the full inference path exists on the board before the real dataset is large enough. After enough `AI_SAMPLE` logs are collected, the same interface can keep the exported trained weights while the rest of the firmware stays unchanged.

## Evidence mask

`edge_ai_ev` is logged as a hex bitmask:

| Bit | Meaning |
| --- | --- |
| 0 | Environment data valid |
| 1 | Gas data valid |
| 2 | Any presence evidence |
| 3 | Radar frame valid |
| 4 | Motion evidence |
| 5 | Stillness evidence |
| 6 | Network offline/context evidence |
| 7 | Sensor health warning |

## Data loop

Every `[AI_SAMPLE]` log now includes both raw sensor features and edge AI outputs. The offline tools convert logs to CSV and build sliding-window features:

```text
python tools/ai_sample_to_csv.py input.log output.csv
python tools/ai_window_features.py output.csv windows.csv 10 5
```

This lets us evaluate whether the board-side model agrees with human labels such as `walk`, `still`, `fall`, `gas`, and `voice_risk`.

For quick serial verification, send `p` on COM6 and check that the debug line contains:

```text
edge_ai=<scene> raw=<scene> risk=<0..3> conf=<0..100> stab=<n> score=<n> trend=<n> ev=0xNN
```

The same line also includes:

```text
ai_ms=<n> ai_max=<n> ai_age=<n> ai_skip=<n> ai_stale=<0|1>
```

## Report wording

Recommended wording for delivery documents:

```text
The node integrates a local lightweight neural inference path on STM32U5. It fuses environmental, radar, PIR, gas, voice-risk, network, and relay context into scene and risk outputs, then cooperates with the cloud AI layer for semantic verification and remote automation.
```
