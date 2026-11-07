; Pinned L4D2 client arithmetic: C:100D6900 and C:1035F4C0.
; Windows x64 ABI. No CRT replacement of the native x87 intermediates.
OPTION CASEMAP:NONE

.const
m7gc_one REAL4 1.0
m7gc_epsilon DWORD 034000000h
m3water_min_direction DQ 03F50624DD2F1A9FCh ; native dbl1053CBF0: exact double0.001
m3particle_aggregation_pad DQ 03FB999999999999Ah ; native dbl10540038: exact double0.1
m3laser_speed_scale DQ 03FC999999999999Ah ; native DOUBLE0.2
m3laser_distance_scale DD 03A83126Fh ; native FLOAT0.001
m3laser_half DD 03F000000h
m3laser_wave_x DD 0407EB852h ; native FLOAT3.98
m3laser_wave_y_sin DD 0400B851Fh ; native FLOAT2.18
m3laser_wave_y_cos DD 040551EB8h ; native FLOAT3.33
m3laser_wave_z DD 0408DC28Fh ; native FLOAT4.43
m3laser_unstarted_time DD 047C34FF3h ; native FLOAT99999.9
m3ssecos_inverse_sign DD 07FFFFFFFh
m3ssecos_one DD 03F800000h
m3ssecos_pi_over_two DD 03FC90FDBh
m3ssecos_two_over_pi DD 03F22F983h
m3ssecos_p0 DD 03FC90FDBh
m3ssecos_p1 DD 0BF255DE7h
m3ssecos_p2 DD 03DA335E3h
m3ssecos_p3 DD 0BB996966h

.code
PUBLIC L4D2_GlowDistanceWithinRange
L4D2_GlowDistanceWithinRange PROC
    mov DWORD PTR [rsp+8], r8d
    fld DWORD PTR [rcx]
    fsub DWORD PTR [rdx]
    fld DWORD PTR [rcx+4]
    fsub DWORD PTR [rdx+4]
    fld DWORD PTR [rcx+8]
    fsub DWORD PTR [rdx+8]
    fld st(1)
    fmulp st(2), st(0)
    fld st(2)
    fmulp st(3), st(0)
    fxch st(1)
    faddp st(2), st(0)
    fmul st(0), st(0)
    faddp st(1), st(0)
    fild DWORD PTR [rsp+8]
    fxch st(1)
    fcomip st(0), st(1)
    fstp st(0)
    setbe al
    ret
L4D2_GlowDistanceWithinRange ENDP

; Both entries implement the SAME native1035F4C0 normalization algorithm.
; KeepMagnitude is an ASSEMBLY-TIME specialization, not a runtime branch.
M7_VECTOR_NORMALIZE_BODY MACRO KeepMagnitude
    movss xmm2, DWORD PTR [rcx]
    movss xmm1, DWORD PTR [rcx+4]
    movss xmm3, DWORD PTR [rcx+8]
    fld DWORD PTR [rcx]
    fmul st(0), st(0)
    fld DWORD PTR [rcx+4]
    fmul st(0), st(0)
    faddp st(1), st(0)
    fld DWORD PTR [rcx+8]
    fmul st(0), st(0)
    faddp st(1), st(0)
    fsqrt
    IF KeepMagnitude
        fst DWORD PTR [rsp+8]
    ELSE
        fstp DWORD PTR [rsp+8]
    ENDIF
    movss xmm4, DWORD PTR [rsp+8]
    addss xmm4, m7gc_epsilon
    movss xmm0, m7gc_one
    divss xmm0, xmm4
    mulss xmm2, xmm0
    mulss xmm1, xmm0
    mulss xmm3, xmm0
    movss DWORD PTR [rcx], xmm2
    movss DWORD PTR [rcx+4], xmm1
    movss DWORD PTR [rcx+8], xmm3
ENDM

PUBLIC L4D2_GlowNormalize
L4D2_GlowNormalize PROC
    M7_VECTOR_NORMALIZE_BODY 1
    fstp QWORD PTR [rsp+8]
    movsd xmm0, QWORD PTR [rsp+8]
    ret
L4D2_GlowNormalize ENDP

; RCX=Vector*. Normalize IN PLACE and return exact native ordered magnitude<
; double0.001 in AL. No float/double return conversion of x87 magnitude.
PUBLIC L4D2_WaterWakeNormalizeAndIsShort
L4D2_WaterWakeNormalizeAndIsShort PROC
    M7_VECTOR_NORMALIZE_BODY 1
    fld QWORD PTR m3water_min_direction
    fcomip st(0), st(1)
    fstp st(0)
    seta al
    ret
L4D2_WaterWakeNormalizeAndIsShort ENDP

; Native15C812..826: x87 radius-square + DOUBLE0.1, one final FLOAT spill.
PUBLIC L4D2_ParticleAggregationRadiusSquared
L4D2_ParticleAggregationRadiusSquared PROC
    movss DWORD PTR [rsp+8], xmm0
    fld DWORD PTR [rsp+8]
    fmul st(0), st(0)
    fadd QWORD PTR m3particle_aggregation_pad
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_ParticleAggregationRadiusSquared ENDP

; Native15C90C..920 stores each center component through x87 float format.
; RCX=destination Vector*, RDX=source Vector*. Preserve SNaN/exception behavior.
PUBLIC L4D2_StoreParticleAggregationCenter
L4D2_StoreParticleAggregationCenter PROC
    fld DWORD PTR [rdx]
    fstp DWORD PTR [rcx]
    fld DWORD PTR [rdx+4]
    fstp DWORD PTR [rcx+4]
    fld DWORD PTR [rdx+8]
    fstp DWORD PTR [rcx+8]
    ret
L4D2_StoreParticleAggregationCenter ENDP

; Native25E511..523: load/store actual gun3340 through x87 FLOAT format.
; Preserve the SNaN/exception/spill phase BEFORE the fresh gun-owner query.
PUBLIC L4D2_LaserSpreadFloat
L4D2_LaserSpreadFloat PROC
    movss DWORD PTR [rsp+8], xmm0
    fld DWORD PTR [rsp+8]
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_LaserSpreadFloat ENDP

; Fold actual2543D0 into its ONLY25DD80 caller without an x64 double-return
; rounding boundary. XMM0=compare time,1=last RNG time,2=timer Now,3=fire start.
; Native timestamp COMISS/JBE includes unordered -> sentinel; final FCOMIP/
; JBE includes unordered -> DECAY. No timer HasStarted float elapsed spill.
PUBLIC L4D2_LaserJitterShouldDecay
L4D2_LaserJitterShouldDecay PROC
    movss DWORD PTR [rsp+8], xmm0
    movss DWORD PTR [rsp+12], xmm1
    movss DWORD PTR [rsp+16], xmm2
    movss DWORD PTR [rsp+20], xmm3
    xor eax, eax
    xorps xmm4, xmm4
    comiss xmm3, xmm4
    jbe m3laser_fire_unstarted
    fld DWORD PTR [rsp+16]
    fsub DWORD PTR [rsp+20]
    jmp m3laser_fire_elapsed
m3laser_fire_unstarted:
    fld DWORD PTR m3laser_unstarted_time
m3laser_fire_elapsed:
    fadd DWORD PTR [rsp+12]
    fld DWORD PTR [rsp+8]
    fcomip st(0), st(1)
    fstp st(0)
    setbe al
    ret
L4D2_LaserJitterShouldDecay ENDP

; Native25E5CE..677, EXACT x87 stack/FSIN/FCOS sequence and FLOAT stores.
; RCX=endpoint Vector*, XMM1=owner local speed,2=gun spread,3=curtime;
; fifth argument FLOAT distance is at[rsp+40]. Native out-of-range trig C2
; behavior is retained; no CRT sin/cos approximation or forced range reduction.
PUBLIC L4D2_ApplyLaserSightWobble
L4D2_ApplyLaserSightWobble PROC
    movss DWORD PTR [rsp+8], xmm1
    movss DWORD PTR [rsp+12], xmm2
    movss DWORD PTR [rsp+16], xmm3
    fld DWORD PTR [rsp+8]
    fmul QWORD PTR m3laser_speed_scale
    fadd DWORD PTR [rsp+12]
    fld DWORD PTR [rsp+16]
    fld DWORD PTR [rsp+40]
    fmul DWORD PTR m3laser_distance_scale
    fld st(1)
    fmul DWORD PTR m3laser_wave_x
    fsin
    fmul st(0), st(3)
    fmul st(0), st(1)
    fadd DWORD PTR [rcx]
    fstp DWORD PTR [rcx]
    fld st(1)
    fmul DWORD PTR m3laser_wave_y_sin
    fsin
    fld st(2)
    fmul DWORD PTR m3laser_wave_y_cos
    fcos
    faddp st(1), st(0)
    fmul DWORD PTR m3laser_half
    fmul st(0), st(3)
    fmul st(0), st(1)
    fadd DWORD PTR [rcx+4]
    fstp DWORD PTR [rcx+4]
    fxch st(1)
    fmul DWORD PTR m3laser_wave_z
    fcos
    fmulp st(2), st(0)
    fmulp st(1), st(0)
    fadd DWORD PTR [rcx+8]
    fstp DWORD PTR [rcx+8]
    ret
L4D2_ApplyLaserSightWobble ENDP
.const
m3afov_one DD 03F800000h
m3afov_three DD 040400000h
m3afov_angle180 DD 043340000h
m3afov_angleNeg180 DD 0C3340000h
m3afov_angle360f DD 043B40000h
m3afov_angle360q DQ 04076800000000000h

.code
EXTERN fmod:PROC

; XMM0=current angle f32, XMM1=previous f32, XMM2=reciprocal time f32.
; Returns exact caller f32 speed spill from native26CA5D..65 /26CA78..99.
PUBLIC L4D2_EyeAngularVelocityComponent
L4D2_EyeAngularVelocityComponent PROC FRAME
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    movss DWORD PTR [rsp+20h], xmm0
    movss DWORD PTR [rsp+24h], xmm1
    movss DWORD PTR [rsp+28h], xmm2
    fld DWORD PTR [rsp+20h]
    fsub DWORD PTR [rsp+24h]
    fxam
    fnstsw ax
    test ax, 0100h
    jnz m3afov_nonfinite_remainder
    fnstcw WORD PTR [rsp+38h]
    mov WORD PTR [rsp+3Ah], 133Fh
    fldcw WORD PTR [rsp+3Ah]
    fld m3afov_angle360q
    fxch st(1)
m3afov_remloop:
    fprem
    fnstsw ax
    test ah, 4
    jnz m3afov_remloop
    fstp st(1)
    fldcw WORD PTR [rsp+38h]
    jmp m3afov_remainder_ready
m3afov_nonfinite_remainder:
    fstp QWORD PTR [rsp+30h]
    movsd xmm0, QWORD PTR [rsp+30h]
    movsd xmm1, m3afov_angle360q
    call fmod
    movsd QWORD PTR [rsp+30h], xmm0
    fld QWORD PTR [rsp+30h]
m3afov_remainder_ready:
    fst DWORD PTR [rsp+2Ch]
    movss xmm0, DWORD PTR [rsp+20h]
    comiss xmm0, DWORD PTR [rsp+24h]
    movss xmm0, DWORD PTR [rsp+2Ch]
    jbe m3afov_not_increasing
    comiss xmm0, m3afov_angle180
    jb m3afov_angle_ready
    subss xmm0, m3afov_angle360f
    jmp m3afov_reload_wrapped
m3afov_not_increasing:
    movss xmm1, m3afov_angleNeg180
    comiss xmm1, xmm0
    jb m3afov_angle_ready
    addss xmm0, m3afov_angle360f
m3afov_reload_wrapped:
    fstp st(0)
    movss DWORD PTR [rsp+2Ch], xmm0
    fld DWORD PTR [rsp+2Ch]
m3afov_angle_ready:
    fmul DWORD PTR [rsp+28h]
    fstp DWORD PTR [rsp+2Ch]
    movss xmm0, DWORD PTR [rsp+2Ch]
    add rsp, 58h
    ret
L4D2_EyeAngularVelocityComponent ENDP

; ECX=int defaultFOV, XMM1=f32 acceleration magnitude. Native FILD/FMULT/
; FSTP32, NOT pre-rounding a general int32 into SSE f32 before multiply.
PUBLIC L4D2_ScaleViewAcceleration
L4D2_ScaleViewAcceleration PROC
    mov DWORD PTR [rsp+8], ecx
    movss DWORD PTR [rsp+12], xmm1
    fild DWORD PTR [rsp+8]
    fmul DWORD PTR [rsp+12]
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_ScaleViewAcceleration ENDP

; XMM0=numerator f32 spill, XMM1=actual GetFOV F32 return, XMM2=limit f32.
; Native GetFOV's value is proven f32 on EVERY reached return path. Keep
; caller DIV/FCOMIP unspilled and JBE (unordered also invalidates timer).
PUBLIC L4D2_IsViewAccelerationUnsteady
L4D2_IsViewAccelerationUnsteady PROC
    movss DWORD PTR [rsp+8], xmm0
    movss DWORD PTR [rsp+12], xmm1
    movss DWORD PTR [rsp+16], xmm2
    fld DWORD PTR [rsp+12]
    fdivr DWORD PTR [rsp+8]
    fld DWORD PTR [rsp+16]
    fcomip st(0), st(1)
    fstp st(0)
    setbe al
    ret
L4D2_IsViewAccelerationUnsteady ENDP

; XMM0=time f32, XMM1=start f32, XMM2=target f32. Real632A0 specialization
; for genuine674E0 call A=0/B=1, preserving native SSE normalize/clamp,
; x87 cubic/remap and the OUTER674E0 FSTP32 spill before return.
PUBLIC L4D2_FOVTransitionFloat
L4D2_FOVTransitionFloat PROC
    movss DWORD PTR [rsp+16], xmm1
    movss DWORD PTR [rsp+20], xmm2
    xorps xmm3, xmm3
    subss xmm0, xmm3
    divss xmm0, m3afov_one
    movss DWORD PTR [rsp+8], xmm0
    movss xmm1, m3afov_one
    comiss xmm0, xmm1
    ja m3afov_spline_clamp
    xorps xmm1, xmm1
    comiss xmm1, xmm0
    jbe m3afov_spline_ready
m3afov_spline_clamp:
    movss DWORD PTR [rsp+8], xmm1
m3afov_spline_ready:
    fld DWORD PTR [rsp+8]
    fld st(0)
    fmul st(0), st(1)
    fld m3afov_three
    fmul st(0), st(1)
    fxch st(1)
    fadd st(0), st(0)
    fmulp st(2), st(0)
    fsubrp st(1), st(0)
    fld DWORD PTR [rsp+20]
    fld DWORD PTR [rsp+16]
    fsub st(1), st(0)
    fxch st(2)
    fmulp st(1), st(0)
    faddp st(1), st(0)
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_FOVTransitionFloat ENDP

.const
m3pview_bob70 DQ 03FE6666666666666h
m3pview_bob30 DQ 03FD3333333333333h

.code
PUBLIC L4D2_ComputePlayerViewBob
L4D2_ComputePlayerViewBob PROC
    mov rax, QWORD PTR [rsp+28h]
    movsd QWORD PTR [rsp+8], xmm0
    movss DWORD PTR [rsp+10h], xmm1
    movss DWORD PTR [rsp+18h], xmm2
    movss DWORD PTR [rsp+20h], xmm3
    fld QWORD PTR [rsp+8]
    fld DWORD PTR [rsp+18h]
    fmul st(0), st(0)
    fld DWORD PTR [rsp+10h]
    fmul st(0), st(0)
    faddp st(1), st(0)
    fsqrt
    fmul DWORD PTR [rsp+20h]
    fst DWORD PTR [rax]
    fxch st(1)
    fsin
    fld st(1)
    fmul m3pview_bob70
    fmulp st(1), st(0)
    fxch st(1)
    fmul m3pview_bob30
    faddp st(1), st(0)
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_ComputePlayerViewBob ENDP

.const
m3yl_180 REAL4 180.0
m3yl_360 REAL4 360.0
m3yl_90 REAL4 90.0
m3yl_270 REAL4 270.0
m3yl_invpi REAL8 0.3183098861837907
m3yl_pi REAL8 3.141592653589793
m3yl_tau REAL8 6.283185307179586

.code
; ONE shared real FSINCOS leaf, agreed with M3MoveParameters.
; float xmm0, sine RDX, cosine R8. Cosine is popped first, then sine.
PUBLIC L4D2_SinCosFloat
L4D2_SinCosFloat PROC
    movss DWORD PTR [rsp+8], xmm0
    fld DWORD PTR [rsp+8]
    fsincos
    fstp DWORD PTR [r8]
    fstp DWORD PTR [rdx]
    ret
L4D2_SinCosFloat ENDP

; Native10360340: RCX forward, RDX angles. Retain the unspilled x87
; sign comparisons against the degree products, then SSE +360 after F32 spill.
PUBLIC L4D2_ScrimmageVectorAngles
L4D2_ScrimmageVectorAngles PROC
    xorps xmm5, xmm5
    movss xmm0, DWORD PTR [rcx+4]
    ucomiss xmm0, xmm5
    jp m3yl_va_general
    jne m3yl_va_general
    movss xmm0, DWORD PTR [rcx]
    ucomiss xmm0, xmm5
    jp m3yl_va_general
    jne m3yl_va_general
    movaps xmm4, xmm5
    movss xmm0, DWORD PTR [rcx+8]
    comiss xmm0, xmm5
    jbe m3yl_va_vertical90
    movss xmm0, m3yl_270
    jmp m3yl_va_store
m3yl_va_vertical90:
    movss xmm0, m3yl_90
    jmp m3yl_va_store
m3yl_va_general:
    fld DWORD PTR [rcx+4]
    fld DWORD PTR [rcx]
    fpatan
    fld m3yl_180
    fmul st(1), st(0)
    fld m3yl_invpi
    fmul st(2), st(0)
    fxch st(2)
    fst DWORD PTR [rsp+8]
    fldz
    movss xmm4, DWORD PTR [rsp+8]
    fcomi st(0), st(1)
    fstp st(1)
    jbe m3yl_va_pitch
    addss xmm4, m3yl_360
m3yl_va_pitch:
    movss xmm1, DWORD PTR [rcx+4]
    fld DWORD PTR [rcx+8]
    fchs
    movss xmm0, DWORD PTR [rcx]
    mulss xmm1, xmm1
    mulss xmm0, xmm0
    addss xmm0, xmm1
    sqrtss xmm0, xmm0
    movss DWORD PTR [rsp+8], xmm0
    fld DWORD PTR [rsp+8]
    fpatan
    fmulp st(2), st(0)
    fxch st(1)
    fmulp st(2), st(0)
    fxch st(1)
    fst DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    fxch st(1)
    fcomip st(0), st(1)
    fstp st(0)
    jbe m3yl_va_store
    addss xmm0, m3yl_360
m3yl_va_store:
    movss DWORD PTR [rdx], xmm0
    movss DWORD PTR [rdx+4], xmm4
    movss DWORD PTR [rdx+8], xmm5
    ret
L4D2_ScrimmageVectorAngles ENDP

; Native103E29AD..2A30. radius XMM0, rowF32 XMM1, phiDenominator XMM2, output R9.
; Row transport offsets: horizontalF32=0,zF32=4,firstHorizontal80=8.
PUBLIC L4D2_ScrimmageSphereRow
L4D2_ScrimmageSphereRow PROC
    movss DWORD PTR [rsp+8], xmm0
    movss DWORD PTR [rsp+16], xmm1
    movss DWORD PTR [rsp+24], xmm2
    fld DWORD PTR [rsp+8]
    fld DWORD PTR [rsp+16]
    fdiv DWORD PTR [rsp+24]
    fmul m3yl_pi
    fld st(0)
    fsin
    fmul st(0), st(2)
    fst DWORD PTR [r9]
    fxch st(1)
    fcos
    fmulp st(2), st(0)
    fxch st(1)
    fstp DWORD PTR [r9+4]
    fstp TBYTE PTR [r9+8]
    ret
L4D2_ScrimmageSphereRow ENDP

; Native103E2A32..2A95. row RCX, column EDX, thetaDenominator XMM2, output R9.
; First column retains original unspilled horizontal; subsequent columns
; reload its F32 spill. Both theta division and tau multiplication are x87.
PUBLIC L4D2_ScrimmageSphereVertex
L4D2_ScrimmageSphereVertex PROC
    test edx, edx
    jnz m3yl_sphere_spilled
    fld TBYTE PTR [rcx+8]
    jmp m3yl_sphere_theta
m3yl_sphere_spilled:
    fld DWORD PTR [rcx]
m3yl_sphere_theta:
    mov DWORD PTR [rsp+8], edx
    movss DWORD PTR [rsp+16], xmm2
    fild DWORD PTR [rsp+8]
    fdiv DWORD PTR [rsp+16]
    fmul m3yl_tau
    fld st(0)
    fcos
    fmul st(0), st(2)
    fstp DWORD PTR [r9]
    fsin
    fmulp st(1), st(0)
    fstp DWORD PTR [r9+4]
    mov eax, DWORD PTR [rcx+4]
    mov DWORD PTR [r9+8], eax
    ret
L4D2_ScrimmageSphereVertex ENDP

; C361500 / C362120+C3653F0 normalization / C35F600.
; Windows x64 scalar ABI. Preserve x87 FSINCOS/FPATAN/FSQRT and F32 spills.
; The existing sole L4D2_GlowNormalize remains the vector-normalization owner.
.const
m3oc_half REAL4 0.5
m3oc_zero REAL4 0.0
m3oc_small DWORD 03A83126Fh
m3oc_degrees DWORD 042652EE1h
.code

PUBLIC L4D2_ObserverAngleQuaternion
L4D2_ObserverAngleQuaternion PROC
    ; RCX = RadianEuler(roll,pitch,yaw), RDX = Quaternion output.
    ; All locals fit in the caller's 32-byte home area.
    movss xmm0, DWORD PTR [rcx+8]
    mulss xmm0, m3oc_half
    movss DWORD PTR [rsp+32], xmm0
    fld DWORD PTR [rsp+32]
    fsincos
    fstp DWORD PTR [rsp+8]
    fstp DWORD PTR [rsp+12]
    movss xmm0, DWORD PTR [rcx+4]
    mulss xmm0, m3oc_half
    movss DWORD PTR [rsp+32], xmm0
    fld DWORD PTR [rsp+32]
    fsincos
    fstp DWORD PTR [rsp+16]
    fstp DWORD PTR [rsp+20]
    movss xmm0, DWORD PTR [rcx]
    mulss xmm0, m3oc_half
    movss DWORD PTR [rsp+32], xmm0
    fld DWORD PTR [rsp+32]
    fsincos
    fstp DWORD PTR [rsp+24]
    fstp DWORD PTR [rsp+28]
    movss xmm0, DWORD PTR [rsp+28]
    mulss xmm0, DWORD PTR [rsp+16]
    movss xmm1, DWORD PTR [rsp+24]
    mulss xmm1, DWORD PTR [rsp+20]
    movaps xmm2, xmm0
    mulss xmm2, DWORD PTR [rsp+12]
    movaps xmm3, xmm1
    mulss xmm3, DWORD PTR [rsp+8]
    addss xmm2, xmm3
    movss DWORD PTR [rdx+4], xmm2
    mulss xmm0, DWORD PTR [rsp+8]
    mulss xmm1, DWORD PTR [rsp+12]
    subss xmm0, xmm1
    movss DWORD PTR [rdx], xmm0
    movss xmm0, DWORD PTR [rsp+24]
    mulss xmm0, DWORD PTR [rsp+16]
    movss xmm1, DWORD PTR [rsp+28]
    mulss xmm1, DWORD PTR [rsp+20]
    movaps xmm2, xmm0
    mulss xmm2, DWORD PTR [rsp+12]
    movaps xmm3, xmm1
    mulss xmm3, DWORD PTR [rsp+8]
    subss xmm2, xmm3
    movss DWORD PTR [rdx+8], xmm2
    mulss xmm1, DWORD PTR [rsp+12]
    mulss xmm0, DWORD PTR [rsp+8]
    addss xmm1, xmm0
    movss DWORD PTR [rdx+12], xmm1
    ret
L4D2_ObserverAngleQuaternion ENDP

PUBLIC L4D2_ObserverNormalizeQuaternion
L4D2_ObserverNormalizeQuaternion PROC
    ; RCX = Quaternion. Native squared radius spills to F32 before FSQRT.
    movss xmm0, DWORD PTR [rcx]
    movss xmm1, DWORD PTR [rcx+4]
    movss xmm2, DWORD PTR [rcx+8]
    movss xmm3, DWORD PTR [rcx+12]
    mulss xmm0, xmm0
    mulss xmm1, xmm1
    addss xmm0, xmm1
    mulss xmm2, xmm2
    addss xmm0, xmm2
    mulss xmm3, xmm3
    addss xmm0, xmm3
    movss DWORD PTR [rsp+8], xmm0
    ucomiss xmm0, m3oc_zero
    jp m3oc_qnormalize
    je m3oc_qdone
m3oc_qnormalize:
    fld DWORD PTR [rsp+8]
    fsqrt
    fld1
    fdivrp st(1), st(0)
    fld DWORD PTR [rcx+12]
    fmul st(0), st(1)
    fstp DWORD PTR [rcx+12]
    fld DWORD PTR [rcx+8]
    fmul st(0), st(1)
    fstp DWORD PTR [rcx+8]
    fld DWORD PTR [rcx+4]
    fmul st(0), st(1)
    fstp DWORD PTR [rcx+4]
    fmul DWORD PTR [rcx]
    fstp DWORD PTR [rcx]
m3oc_qdone:
    ret
L4D2_ObserverNormalizeQuaternion ENDP

PUBLIC L4D2_ObserverMatrixAngles
L4D2_ObserverMatrixAngles PROC
    ; RCX = matrix3x4_t; RDX = QAngle. Only the seven native inputs are read.
    fld DWORD PTR [rcx]
    fld DWORD PTR [rcx+16]
    fld st(0)
    fmul st(0), st(1)
    fld st(2)
    fmul st(0), st(3)
    faddp st(1), st(0)
    fsqrt
    fld m3oc_small
    fxch st(1)
    fcomi st(0), st(1)
    fstp st(1)
    jbe m3oc_singular
    fxch st(2)
    fpatan
    fld m3oc_degrees
    fmul st(1), st(0)
    fxch st(1)
    fstp DWORD PTR [rdx+4]
    fld DWORD PTR [rcx+32]
    fchs
    fxch st(1)
    fxch st(2)
    fpatan
    fmul st(0), st(1)
    fstp DWORD PTR [rdx]
    fld DWORD PTR [rcx+36]
    fld DWORD PTR [rcx+40]
    fpatan
    fmulp st(1), st(0)
    fstp DWORD PTR [rdx+8]
    ret
m3oc_singular:
    fstp st(2)
    fstp st(0)
    mov DWORD PTR [rdx+8], 0
    fld DWORD PTR [rcx+4]
    fchs
    fld DWORD PTR [rcx+20]
    fpatan
    fld m3oc_degrees
    fmul st(1), st(0)
    fxch st(1)
    fstp DWORD PTR [rdx+4]
    fld DWORD PTR [rcx+32]
    fchs
    fxch st(1)
    fxch st(2)
    fpatan
    fmulp st(1), st(0)
    fstp DWORD PTR [rdx]
    ret
L4D2_ObserverMatrixAngles ENDP
; Missing nativeA6560 caller specialization only. Existing canonical macro,
; all landed math, and the single assembly TU/END remain unchanged.
; RCX=direction in/out, RDX=F32 distance out. Return SSE2 CRT-style int count.
PUBLIC L4D2_NormalizeWaterBulletAndCount
L4D2_NormalizeWaterBulletAndCount PROC
    M7_VECTOR_NORMALIZE_BODY 1
    fst DWORD PTR [rdx]
    fmul QWORD PTR m3laser_speed_scale ; Same exact native DOUBLE0.2.
    fstp QWORD PTR [rsp+16]
    cvttsd2si eax, QWORD PTR [rsp+16]
    ret
L4D2_NormalizeWaterBulletAndCount ENDP

; C949D0: original unspilled x87 degrees * float-radians, FSIN,
; ABS, float-scale, then the original single-precision sample spill.
PUBLIC L4D2_RumbleSine
L4D2_RumbleSine PROC
    movss DWORD PTR [rsp+8], xmm0
    movss DWORD PTR [rsp+12], xmm1
    mov DWORD PTR [rsp+16], 03C8EFA35h
    fld DWORD PTR [rsp+8]
    fmul DWORD PTR [rsp+16]
    fsin
    fabs
    fmul DWORD PTR [rsp+12]
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2_RumbleSine ENDP

; M7TutorA68C missing entries, not a second ASM TU/normalization algorithm.
.const
tutor_two_hundred      DWORD 043480000h
tutor_one_eighty       DWORD 043340000h
tutor_reciprocal_pi    DWORD 03EA2F986h
.code
PUBLIC L4D2InstructorNormalizeDirection
L4D2InstructorNormalizeDirection PROC
    M7_VECTOR_NORMALIZE_BODY 0
    ret
L4D2InstructorNormalizeDirection ENDP

; C28A79F..C28A7C2. XMM0 = rounded F32 distance; return F32 FOV in XMM0.
; This is FPATAN(200 / distance, 1), not a CRT atan2(double,double) call.
L4D2TutorFramingFOV PROC
    movss DWORD PTR [rsp+8], xmm0
    fld DWORD PTR [tutor_two_hundred]
    fdiv DWORD PTR [rsp+8]
    fld1
    fpatan
    fmul DWORD PTR [tutor_one_eighty]
    fmul DWORD PTR [tutor_reciprocal_pi]
    fstp DWORD PTR [rsp+8]
    movss xmm0, DWORD PTR [rsp+8]
    ret
L4D2TutorFramingFOV ENDP

; Whole reached native51F20 numerical body, target Win64 calling convention.
; RCX = real origin Vector*, RDX = real QAngle*. Owner tests its real root bool.
; FISTP respects the caller's CURRENT x87 rounding mode. Do not replace with
; CVTT, MXCSR rounding, floor/roundf, or change either control word.
; No original address, public engine interface, allocation or new policy.
OPTION CASEMAP:NONE

.const
m3pq_origin_encode DWORD 042000000h
m3pq_origin_decode DWORD 03D000000h
m3pq_angle16_encode DWORD 043360B61h
m3pq_angle16_decode DWORD 03BB40000h
m3pq_angle13_encode DWORD 041B60B61h
m3pq_unsigned_dword DWORD 04F800000h
m3pq_angle13_decode DWORD 03D340000h

.code
PUBLIC L4D2_NetworkQuantizePose
L4D2_NetworkQuantizePose PROC
    mov r9, rdx
    movss xmm0, m3pq_origin_encode
    movss xmm1, m3pq_origin_decode
    xor r8d, r8d
m3pq_origin_loop:
    movss xmm2, DWORD PTR [rcx+r8*4]
    mulss xmm2, xmm0
    movss DWORD PTR [rsp+8], xmm2
    fld DWORD PTR [rsp+8]
    fistp DWORD PTR [rsp+12]
    xorps xmm2, xmm2
    cvtsi2ss xmm2, DWORD PTR [rsp+12]
    mulss xmm2, xmm1
    movss DWORD PTR [rcx+r8*4], xmm2
    inc r8d
    cmp r8d, 3
    jl m3pq_origin_loop

    movss xmm0, m3pq_angle16_encode
    movss xmm1, m3pq_angle16_decode
    movss xmm2, m3pq_angle13_encode
    xor r8d, r8d
m3pq_angle_loop:
    movss xmm3, DWORD PTR [r9+r8*4]
    mulss xmm3, xmm0
    cvttss2si edx, xmm3
    and edx, 0FFFFh
    xorps xmm3, xmm3
    cvtsi2ss xmm3, edx
    mulss xmm3, xmm1
    mulss xmm3, xmm2
    movss DWORD PTR [rsp+8], xmm3
    fld DWORD PTR [rsp+8]
    fistp QWORD PTR [rsp+16]
    fild DWORD PTR [rsp+16]
    mov edx, DWORD PTR [rsp+16]
    test edx, edx
    jns m3pq_angle_nonnegative
    fadd m3pq_unsigned_dword
m3pq_angle_nonnegative:
    fmul m3pq_angle13_decode
    fstp DWORD PTR [r9+r8*4]
    inc r8d
    cmp r8d, 3
    jl m3pq_angle_loop
    ret
L4D2_NetworkQuantizePose ENDP

; Native S4F11E0/C368700, identical to mathlib/sse.cpp _SSE_cos scalar polynomial.
; Windows x64 float ABI; the paired game callers supply their own F32 remap spills.
PUBLIC _SSE_cos
_SSE_cos PROC
    movss xmm1, m3ssecos_inverse_sign
    andps xmm0, xmm1
    addss xmm0, m3ssecos_pi_over_two
    mulss xmm0, m3ssecos_two_over_pi
    cvttss2si ecx, xmm0
    movss xmm5, m3ssecos_one
    mov edx, ecx
    shl edx, 30
    cvtsi2ss xmm1, ecx
    and edx, 080000000h
    and ecx, 1
    subss xmm0, xmm1
    neg ecx
    movd xmm3, ecx
    minss xmm0, xmm5
    movss xmm1, m3ssecos_p3
    subss xmm5, xmm0
    andps xmm5, xmm3
    movss xmm4, m3ssecos_p2
    andnps xmm3, xmm0
    orps xmm5, xmm3
    movss xmm0, xmm5
    mulss xmm5, xmm5
    movss xmm3, m3ssecos_p1
    movss xmm2, xmm5
    mulss xmm5, xmm1
    movss xmm1, m3ssecos_p0
    addss xmm5, xmm4
    mulss xmm5, xmm2
    movd xmm4, edx
    addss xmm5, xmm3
    mulss xmm5, xmm2
    orps xmm0, xmm4
    addss xmm5, xmm1
    mulss xmm0, xmm5
    movss DWORD PTR [rsp+8], xmm0
    movss xmm0, DWORD PTR [rsp+8]
    ret
_SSE_cos ENDP

END
