;========= Copyright Valve Corporation, All rights reserved. ============;
; x64 Windows ABI. Native S:105B69-105B90 and C:109900 keep these
; operations on the x87 stack. Do not replace FSIN or fold intermediates.
OPTION CASEMAP:NONE

.const
ALIGN 4
glow_twenty DWORD 041A00000h
glow_three_tenths DWORD 03E99999Ah
glow_seven_tenths DWORD 03F333333h

.code
PUBLIC L4D2_GlowFlashScale
L4D2_GlowFlashScale PROC
	movss DWORD PTR [rsp+8], xmm0
	fld DWORD PTR [rsp+8]
	fmul DWORD PTR glow_twenty
	fsin
	fmul DWORD PTR glow_three_tenths
	fadd DWORD PTR glow_seven_tenths
	fstp DWORD PTR [rsp+8]
	movss xmm0, DWORD PTR [rsp+8]
	ret
L4D2_GlowFlashScale ENDP

PUBLIC L4D2_GlowFlashColor
L4D2_GlowFlashColor PROC
	; time=XMM0, red=RDX, green=R8, blue=R9.
	movss DWORD PTR [rsp+8], xmm0
	fld DWORD PTR [rsp+8]
	fmul DWORD PTR glow_twenty
	fsin
	fmul DWORD PTR glow_three_tenths
	fadd DWORD PTR glow_seven_tenths
	fld DWORD PTR [rdx]
	fmul st(0), st(1)
	fstp DWORD PTR [rdx]
	fld DWORD PTR [r8]
	fmul st(0), st(1)
	fstp DWORD PTR [r8]
	fmul DWORD PTR [r9]
	fstp DWORD PTR [r9]
	ret
L4D2_GlowFlashColor ENDP
END
