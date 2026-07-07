# Rd-03 V2 Official Client Calibration Plan

This document defines how we will use the official Rd-03 V2 upper-computer tool before trusting any long-still, distance, or room-scene rule.

## 1. Why This Matters

Our current STM32 firmware already reads:

- UART presence
- UART distance in cm
- 32 range-gate energy values
- OT2 digital presence

But the current long-still rule is still only a simple firmware heuristic. The official client is needed to answer three questions:

1. Is the module itself reporting a near-field minimum distance around 30 cm?
2. What does the empty room look like in range-gate energy?
3. Which thresholds does the official tool recommend for our actual fixture and room?

If the official client also never reports a distance below about 30 cm, then the value is probably a module algorithm / near-field behavior. If the official client does report closer distances but STM32 does not, then our report-mode configuration or parser needs deeper checking.

## 2. Tool Location

Simon provided:

```text
Simon6.4/xend101htool_1_.zip
```

The archive contains:

```text
XenD101HTool/ICL_XenD101H_Tool.exe
XenD101HTool/Firmware/XenD101H_V3.0.4.bin
XenD101HTool/SaveData/
XenD101HTool/Log/
```

Local extracted path prepared for this workspace:

```text
_incoming_zip/rd03_client/XenD101HTool/ICL_XenD101H_Tool.exe
```

`_incoming_zip/` is ignored by Git, so the Windows executable is available locally without being committed to the repository.

## 3. Recommended Wiring For Calibration

Use a USB-to-TTL adapter with 3.3 V logic.

```text
Rd-03 3V3 -> stable 3.3 V
Rd-03 GND -> USB-TTL GND
Rd-03 OT1/TX -> USB-TTL RX
Rd-03 RX -> USB-TTL TX
```

Do not connect the same Rd-03 UART to both STM32 USART3 and USB-TTL TX/RX at the same time during official-client calibration. Either unplug `PB10/PB11` from STM32 or use a dedicated second radar module.

## 4. Calibration Procedure

## 4.0 STM32 Calibration Log Mode

When using the STM32 firmware path instead of the official client, open the debug serial port and send:

```text
k
```

This toggles continuous radar calibration logs. When enabled, the firmware prints one line every 500 ms:

```text
[RADAR_CAL] t=... valid=1 presence=... ot2=... dist_cm=... zone=... peak_gate=... peak_cm=... peak_energy=... active=... motion=... energy=... still=... occupied=... rx_ovf=... gates=g00,g01,...,g31
```

Send `k` again to stop the calibration stream.

After saving the COM log, summarize it on the PC:

```text
python tools/rd03_calibration_summary.py COM6-115200.log
python tools/rd03_calibration_summary.py COM6-115200.log --csv rd03_empty_room.csv --label empty_room
```

Use different labels for different tests, for example:

```text
empty_room
dist_10cm
dist_20cm
dist_30cm
dist_50cm
dist_80cm
sitting_still
walking_center
walking_side
```

### 4.1 Empty-Room Baseline

1. Fix the radar in the same direction and height as the prototype.
2. Keep people away from the detection cone.
3. Run the official client for 2 to 5 minutes.
4. Save a screenshot and client log.
5. Record whether presence remains clear and which gates have background energy.

This becomes the background reference for false-presence and false-still suppression.

### 4.2 Distance Ruler Test

Put one person on the radar centerline at:

```text
10 cm, 20 cm, 30 cm, 50 cm, 80 cm, 120 cm, 180 cm, 250 cm, 300 cm
```

For each point, record:

- official client distance
- STM32 `radar_cm`
- STM32 `peak_gate`
- STM32 `peak_cm`
- STM32 `peak_energy`
- STM32 `active_gates`
- whether `OT2` is high

This test decides whether the 30 cm lower bound is a real radar behavior or our firmware issue.

### 4.3 Static Sitting Test

1. Person sits still inside the intended monitoring zone.
2. Record at least 3 minutes.
3. Record `motion`, `still`, `occupied`, `distance`, and gate energy.
4. Compare with empty-room baseline.

This decides the safe threshold for long-still detection.

### 4.4 Movement Test

Walk across the radar cone from left, center, and right.

Record:

- whether distance changes smoothly
- which gates activate
- whether motion score spikes
- whether presence clears after leaving

## 5. Firmware Rule After Calibration

Before calibration, automatic long-still ACK must stay conservative.

After calibration, the firmware should use:

- UART presence and OT2 cross-check
- active gate count
- peak energy above empty-room baseline
- stable distance zone
- occupied time
- still time
- cooldown after an ACK/clear

The official client is not a replacement for STM32 logic. It is the calibration and demonstration tool that makes the STM32 rules defensible.
