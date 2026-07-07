# Report Delivery Plan

Updated: 2026-07-07

This file converts the official delivery package under `交付要求/` into a working checklist for the competition report and video. It is not the final report.

## 1. Hard Delivery Rules

- Video: MP4, total length <= 3 minutes.
- Video cover: use the official cover image; cover duration <= 2 seconds.
- Video file size: <= 300 MB.
- Video content: focus on real product demonstration, not PPT narration.
- Report: PDF format, <= 50 MB.
- Important code: RAR archive, <= 100 MB.
- Upload deadline: 2026-07-09 18:00.
- Critical anonymity rule: the report and video must not contain school name, instructor name, or equivalent identifying text.
- Authorization form: print, all team members sign, scan and submit.

## 2. Official Report Template

Title page:
- Work name.
- Do not include school name, instructor name, lab name, or other identity clues.

Abstract:
- <= 800 Chinese characters.
- Explain target problem, system architecture, main functions, verified results, and innovation.

Part 1: Work Overview
- Functions and features: <= 400 characters, important figure allowed.
- Application fields: <= 400 characters, important figure allowed.
- Main technical characteristics: <= 400 characters.
- Main performance indicators: <= 200 characters, table recommended.
- Main innovations: <= 200 characters, list points directly.
- Design flow: <= 200 characters, important figure allowed.

Part 2: System Composition and Function Description
- Overall introduction: system block diagram, submodules, and module relationships.
- Hardware system:
  - Hardware overall introduction.
  - Mechanical design, from whole to local, with CAD or photos if available.
  - Circuit modules, from whole to local, marking key input/output signals.
- Software system:
  - Software overall introduction, including PC/cloud if applicable.
  - Module-level explanation, top-down flowcharts, key inputs and outputs.

Part 3: Completion Status and Performance Parameters
- Overall introduction: front and 45-degree global photos of the real system.
- Engineering results:
  - Mechanical result photos.
  - Circuit result photos.
  - Software interface screenshots.
- Feature results:
  - Show each function and measured/observable performance.
  - Use real test photos, logs, dashboards, or instrument screenshots where possible.

Part 4: Summary
- Extensibility: <= 300 characters.
- Experience and reflection: <= 1000 characters.

Part 5: References
- Standard format.
- <= 20 references.

## 3. Current Material Sources

Existing text drafts:
- `Docs/competition_report_draft.md`: long persuasive draft, rich but needs claim verification.
- `Docs/work_report_text_draft.md`: more conservative draft, closer to current implemented state.
- `Docs/system_schematic.md`: system architecture and wiring/data-flow source.

Implementation evidence:
- `Docs/pinmap.md`: authoritative pin/wiring source.
- `Docs/protocol.md`: UART/MQTT protocol source.
- `Docs/comm_wifi_interface.md`: ESP8266/cloud bridge source.
- `Docs/state_machine_acceptance.md`: local state machine acceptance source.
- `Docs/rd03_upper_client_calibration.md`: radar upper-tool and calibration source.
- `Docs/ai_dataset_collection.md`: AI dataset collection plan.
- `Docs/scene_engine_design.md`: multi-scene reasoning design.
- `Docs/prototype_fixture_design.md`: prototype base and fixture design.
- `Docs/cad/single_board_v05_layout.svg`: current base layout drawing.

Official delivery files:
- `交付要求/2026嵌入式大赛应用赛道作品上传要求.docx`
- `交付要求/2026嵌入式大赛应用赛道作品报告模板(1) (1).docx`
- `交付要求/2026嵌入式大赛应用赛道作品视频封面.png`
- `交付要求/授权书.pdf`

## 4. Claim Hygiene Rules

Use three evidence levels in the report draft:

- Verified: tested on real hardware, supported by serial logs, screenshots, or photos.
- Integrated: code/interface exists and basic path is connected, but full scenario evidence is incomplete.
- Planned/Extensible: architecture is reserved, but it must be written as future work or extension.

Avoid overstating these items unless we have fresh proof:
- Radar distance accuracy below 0.7 m.
- Fall detection accuracy.
- Voice recognition accuracy percentage.
- Offline cache and recovery upload.
- Real Mi Home production device linkage.
- Medical/community/hospital automatic dispatch.
- Gas ppm absolute accuracy, because MQ ppm is estimated and not calibrated with standard gas.

## 5. Recommended Report Positioning

Suggested work name:
- Privacy-Preserving Edge-Autonomous Elderly Care and Smart Home Linkage System

Chinese title candidate:
- 隐私优先型边缘自治独居看护与智能家居联动系统

Core story:
- The work is not just a sensor demo.
- It is a local-first elderly-care IoT node that combines environmental sensing, non-imaging presence sensing, local state machine safety closure, voice/remote command interfaces, cloud analysis, and smart-home actuation.
- AI should be presented as a two-layer plan:
  - Cloud AI: summary, risk explanation, notification recommendation.
  - Local AI: integrated lightweight STM32-side MLP based on radar/environment/gas/presence/network features, with `AI_SAMPLE` logs reserved for later weight replacement and validation.

## 6. Evidence We Still Need

Real photos:
- Full system front photo.
- Full system 45-degree photo.
- Base layout photo.
- STM32 and main wiring close-up.
- Radar module direction and placement close-up.
- MQ divider/wiring close-up.
- TFT interface photo.
- Relay and low-voltage LED load photo.
- ESP8266/ESP32 voice module close-up if used in final video.

Screenshots/logs:
- Serial log with BME/MQ/radar/status lines.
- Serial log or dashboard showing SOS -> ACK_WAIT -> ACK/CLEARED.
- Serial log or dashboard showing SOS -> timeout -> ALARM.
- MQTT messages or Home Assistant dashboard.
- PushPlus notification if available.
- Radar upper-tool screenshot if used as evidence.
- AI_SAMPLE log or CSV summary if AI data collection is mentioned.

Demonstration clips for the 3-minute video:
- Normal monitoring state.
- Local SOS/ACK closed loop.
- Gas ppm debug injection or real MQ trend demonstration.
- Relay/LED actuation.
- Cloud dashboard/HA update.
- Voice command path if stable.

## 7. Writing Strategy

Use `Docs/work_report_text_draft.md` as the conservative base.

Merge selected high-level language from `Docs/competition_report_draft.md`, but downgrade any claim without proof into:
- "已预留"
- "可扩展"
- "当前演示版支持"
- "基于估算"
- "后续可通过标定/训练完善"

Keep the final report compact:
- Use fewer, stronger figures.
- Prefer real photos and screenshots over abstract explanation.
- Put exact pin tables and long protocol details in appendix or short summarized tables.

## 8. Immediate Next Steps

1. Build the official report outline in the official template order.
2. Fill each section with conservative text from `work_report_text_draft.md`.
3. Add a "claim verification pass" before converting to PDF.
4. Prepare the figure list and assign photo/screenshot capture tasks.
5. Render/verify the final DOCX or PDF before submission.
