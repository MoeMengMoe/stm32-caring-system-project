.section .rodata
.balign 4

.global care_prompt_cancel_start
.global care_prompt_cancel_end
care_prompt_cancel_start:
.incbin "main/audio/care_prompt_cancel.pcm"
care_prompt_cancel_end:
.balign 4

.global care_prompt_command_received_start
.global care_prompt_command_received_end
care_prompt_command_received_start:
.incbin "main/audio/care_prompt_command_received.pcm"
care_prompt_command_received_end:
.balign 4

.global care_prompt_recorded_start
.global care_prompt_recorded_end
care_prompt_recorded_start:
.incbin "main/audio/care_prompt_recorded.pcm"
care_prompt_recorded_end:
.balign 4

.global care_prompt_sos_alarm_start
.global care_prompt_sos_alarm_end
care_prompt_sos_alarm_start:
.incbin "main/audio/care_prompt_sos_alarm.pcm"
care_prompt_sos_alarm_end:
.balign 4

.global care_prompt_timeout_escalate_start
.global care_prompt_timeout_escalate_end
care_prompt_timeout_escalate_start:
.incbin "main/audio/care_prompt_timeout_escalate.pcm"
care_prompt_timeout_escalate_end:
.balign 4

.global care_prompt_verify_start
.global care_prompt_verify_end
care_prompt_verify_start:
.incbin "main/audio/care_prompt_verify.pcm"
care_prompt_verify_end:
.balign 4

.global care_prompt_verify_short_start
.global care_prompt_verify_short_end
care_prompt_verify_short_start:
.incbin "main/audio/care_prompt_verify_short.pcm"
care_prompt_verify_short_end:
.balign 4

.global care_prompt_wakeup_start
.global care_prompt_wakeup_end
care_prompt_wakeup_start:
.incbin "main/audio/care_prompt_wakeup.pcm"
care_prompt_wakeup_end:
.balign 4
