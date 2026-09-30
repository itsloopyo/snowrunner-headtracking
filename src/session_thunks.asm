; SPDX-License-Identifier: MIT
; Copyright (c) 2026 itsloopyo

EXTERN sr_session_calls:QWORD
EXTERN sr_session_original:QWORD

.code

; The session methods have different native signatures. Preserve every argument,
; including stack arguments and XMM registers, before entering the trampoline.
SessionThunk MACRO index
PUBLIC sr_session_thunk_&index
sr_session_thunk_&index PROC
    lock inc QWORD PTR [sr_session_calls]
    jmp QWORD PTR [sr_session_original + index * 8]
sr_session_thunk_&index ENDP
ENDM

SessionThunk 0
SessionThunk 1
SessionThunk 2
SessionThunk 3
SessionThunk 4
SessionThunk 5
SessionThunk 6
SessionThunk 7
SessionThunk 8
SessionThunk 9
SessionThunk 10
SessionThunk 11

END
