# Edge AI dashboard

Updated: 2026-07-08

`tools/edge_ai_dashboard/index.html` is a local upper-computer dashboard for the Caring Node prototype. It reads the STM32 serial log format directly and turns `[AI_SAMPLE]` lines into live visual panels.

## Purpose

The dashboard is designed for three jobs:

- Demonstration: show local AI, radar, gas, state machine, and relay context without staring at raw serial text.
- Debugging: see whether EdgeAI is stuck in `NORMAL`, whether `risk` is raised, whether `edge_ai_stale` appears, and whether gas/radar fields are valid.
- Data collection: load or stream logs, then export parsed AI samples as CSV for later model training.

## Start

Recommended on Windows:

```bat
tools\edge_ai_dashboard\start_dashboard.cmd
```

Then open:

```text
http://localhost:8765/
```

Chrome or Microsoft Edge is recommended because the live serial mode uses the Web Serial API.

## Live serial

1. Connect the NUCLEO board to the PC.
2. Close other serial tools using the same COM port.
3. Open the dashboard in Chrome or Edge.
4. Keep baud rate at `115200`.
5. Click `Connect Serial`.
6. Select the STM32 ST-LINK VCP port, usually COM6 in our current setup.

The dashboard parses:

- `[AI_SAMPLE] ...`
- `[INFO] status tx ...`
- `[INFO] console app ...`
- `[INFO] ai label ...`
- app event lines when present

## Log replay

If the board is not connected, click `Load Log` and choose a saved `.log` file from SuperCom. The page replays the file immediately and shows the same panels.

Useful files for current testing:

```text
C:\Users\0lour\Desktop\SuperCom-4.6\logs\2026-7M-8D\*.log
```

## Panels

### Runtime Snapshot

Shows the current high-level state:

- `state`, `scenario`, `risk`, `risk_src`
- `edge_ai_scene`, `edge_ai_raw`, `edge_ai_risk`, `edge_ai_conf`, `edge_ai_stab`
- radar distance, motion score, active gates, still seconds
- gas ppm, debug offset, voltage and delta
- inference timing fields

### Local Neural Network

Visualizes the current firmware model shape:

```text
12 inputs -> 8 hidden neurons -> 6 scene outputs
```

Input node brightness follows the normalized features used by `Modules/ai/edge_ai.c`:

1. High temperature
2. Low temperature
3. High humidity
4. Gas ppm
5. Gas delta
6. Fused presence
7. Radar presence
8. Radar nearness
9. Active gates
10. Motion score
11. Still duration
12. Offline context

The highlighted output is the fused `edge_ai_scene` reported by firmware. This is a visualization of runtime state, not a replacement for firmware inference.

### Radar and Room Activity

Displays a 2D explanatory beam:

- dot position: `radar_cm`
- dot size/color: `motion`
- bottom strip: active radar gates
- red gate: `peak_gate`

It is a demo visualization, not real 3D reconstruction.

### Trend Lines

Overlays normalized trends:

- `motion`
- `edge_ai_score`
- `gas_ppm`
- `radar_cm`

This helps spot spikes, gas debug changes, and whether fall-like activity is visible.

### Event Timeline

Shows recent event-like samples with state, scene, risk, and trigger fields.

## Current demo wording

Recommended wording:

```text
The dashboard is a local upper-computer console for the STM32 caring node. It visualizes the node's local neural inference path, sensor fusion evidence, radar activity, gas risk, and state-machine decisions in real time. During the contest demo, it proves that the device does not only upload raw data: it already performs local perception and risk reasoning before cloud verification.
```

## Notes

- If `Connect Serial` is unavailable, open the page from `http://localhost:8765/` rather than directly from the file system.
- If the serial port is busy, close SuperCom/CLion serial monitor first.
- If the dashboard shows `gas_dbg_offset=0` during a gas test, the gas debug command was not active.
- If fall tests still show only `NORMAL`, check `motion`, `active_gates`, and `radar_cm` in the trend panel before changing thresholds.
