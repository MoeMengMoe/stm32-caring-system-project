# Local Edge AI Prototype

This module is the STM32-side lightweight AI path used by the caring node. It is intentionally small enough to run in the main loop without RTOS, heap allocation, or external memory.

## Runtime role

`Modules/ai/edge_ai.c` reads the current sensor snapshot and app context, then produces:

- `edge_ai_scene`: one of normal, environment comfort, gas risk, stillness risk, activity anomaly, or system context.
- `edge_ai_risk`: 0-3 risk hint.
- `edge_ai_conf`: model confidence percentage.
- `edge_ai_score`: anomaly score for logging and later comparison.

The app state machine treats this output as a risk floor. It can raise risk when confidence is high, but it does not suppress hard safety rules such as SOS, gas warning, voice risk, or ACK handling.

## Input vector

The first firmware version uses 12 normalized inputs:

1. High-temperature deviation.
2. Low-temperature deviation.
3. High-humidity deviation.
4. Gas delta from baseline.
5. Fused presence.
6. Radar validity.
7. Radar distance.
8. Radar active gate count.
9. Radar motion score.
10. Radar still duration.
11. Network offline state.
12. Network offline state.

## Model shape

The prototype model is a compact MLP:

```text
12 inputs -> 8 hidden neurons -> 6 scene classes
```

The current weights are bootstrapped from domain rules so the full inference path exists on the board before the real dataset is large enough. After enough `AI_SAMPLE` logs are collected, the same interface can keep the exported trained weights while the rest of the firmware stays unchanged.

## Data loop

Every `[AI_SAMPLE]` log now includes both raw sensor features and edge AI outputs. The offline tools convert logs to CSV and build sliding-window features:

```text
python tools/ai_sample_to_csv.py input.log output.csv
python tools/ai_window_features.py output.csv windows.csv 10 5
```

This lets us evaluate whether the board-side model agrees with human labels such as `walk`, `still`, `fall`, `gas`, and `voice_risk`.

## Report wording

Recommended wording for delivery documents:

```text
The node integrates a local lightweight neural inference path on STM32U5. It fuses environmental, radar, PIR, gas, voice-risk, network, and relay context into scene and risk outputs, then cooperates with the cloud AI layer for semantic verification and remote automation.
```
