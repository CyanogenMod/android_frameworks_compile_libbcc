target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:64:128-a0:0:64-n32-S64"
target triple = "armv7-none-linux-gnueabi"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;               INTRINSICS               ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

declare <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float>, <4 x float>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vqshiftns.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqshiftns.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqshiftns.v2i32(<2 x i64>, <2 x i64>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vqshiftnu.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqshiftnu.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqshiftnu.v2i32(<2 x i64>, <2 x i64>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vqshiftnsu.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqshiftnsu.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqshiftnsu.v2i32(<2 x i64>, <2 x i64>) nounwind readnone

declare <4 x i32> @llvm.arm.neon.vmins.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vmaxs.v4i32(<4 x i32>, <4 x i32>) nounwind readnone


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;                HELPERS                 ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

define internal <4 x float> @smear_4f(float %in) nounwind readnone alwaysinline {
  %1 = insertelement <4 x float> undef, float %in, i32 0
  %2 = insertelement <4 x float> %1, float %in, i32 1
  %3 = insertelement <4 x float> %2, float %in, i32 2
  %4 = insertelement <4 x float> %3, float %in, i32 3
  ret <4 x float> %4
}

define internal <2 x float> @smear_2f(float %in) nounwind readnone alwaysinline {
  %1 = insertelement <2 x float> undef, float %in, i32 0
  %2 = insertelement <2 x float> %1, float %in, i32 1
  ret <2 x float> %2
}

define internal <4 x i32> @smear_4i32(i32 %in) nounwind readnone alwaysinline {
  %1 = insertelement <4 x i32> undef, i32 %in, i32 0
  %2 = insertelement <4 x i32> %1, i32 %in, i32 1
  %3 = insertelement <4 x i32> %2, i32 %in, i32 2
  %4 = insertelement <4 x i32> %3, i32 %in, i32 3
  ret <4 x i32> %4
}



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;                 CLAMP                  ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

define <4 x float> @_Z5clampDv4_fS_S_(<4 x float> %value, <4 x float> %low, <4 x float> %high) nounwind readonly {
  %1 = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %value, <4 x float> %high) nounwind readnone
  %2 = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %1, <4 x float> %low) nounwind readnone
  ret <4 x float> %2
}

define <4 x float> @_Z5clampDv4_fff(<4 x float> %value, float %low, float %high) nounwind readonly {
  %_high = tail call <4 x float> @smear_4f(float %high) nounwind readnone
  %_low = tail call <4 x float> @smear_4f(float %low) nounwind readnone
  %out = tail call <4 x float> @_Z5clampDv4_fS_S_(<4 x float> %value, <4 x float> %_low, <4 x float> %_high) nounwind readonly
  ret <4 x float> %out
}

define <3 x float> @_Z5clampDv3_fS_S_(<3 x float> %value, <3 x float> %low, <3 x float> %high) nounwind readonly {
  %_value = shufflevector <3 x float> %value, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %_low = shufflevector <3 x float> %low, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %_high = shufflevector <3 x float> %high, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %a = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %_value, <4 x float> %_high) nounwind readnone
  %b = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %a, <4 x float> %_low) nounwind readnone
  %c = shufflevector <4 x float> %b, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %c
}

define <3 x float> @_Z5clampDv3_fff(<3 x float> %value, float %low, float %high) nounwind readonly {
  %_value = shufflevector <3 x float> %value, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %_high = tail call <4 x float> @smear_4f(float %high) nounwind readnone
  %_low = tail call <4 x float> @smear_4f(float %low) nounwind readnone
  %a = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %_value, <4 x float> %_high) nounwind readnone
  %b = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %a, <4 x float> %_low) nounwind readnone
  %c = shufflevector <4 x float> %b, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %c
}

define <2 x float> @_Z5clampDv2_fS_S_(<2 x float> %value, <2 x float> %low, <2 x float> %high) nounwind readonly {
  %1 = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %value, <2 x float> %high) nounwind readnone
  %2 = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %1, <2 x float> %low) nounwind readnone
  ret <2 x float> %2
}

define <2 x float> @_Z5clampDv2_fff(<2 x float> %value, float %low, float %high) nounwind readonly {
  %_high = tail call <2 x float> @smear_2f(float %high) nounwind readnone
  %_low = tail call <2 x float> @smear_2f(float %low) nounwind readnone
  %a = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %value, <2 x float> %_high) nounwind readnone
  %b = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %a, <2 x float> %_low) nounwind readnone
  ret <2 x float> %b
}


define float @_Z5clampfff(float %value, float %low, float %high) nounwind readonly {
  %_value = tail call <2 x float> @smear_2f(float %value) nounwind readnone
  %_low = tail call <2 x float> @smear_2f(float %low) nounwind readnone
  %_high = tail call <2 x float> @smear_2f(float %high) nounwind readnone
  %a = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %_value, <2 x float> %_high) nounwind readnone
  %b = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %a, <2 x float> %_low) nounwind readnone
  %c = extractelement <2 x float> %b, i32 0
  ret float %c
}


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;                  FMAX                  ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

define <4 x float> @_Z4fmaxDv4_fS_(<4 x float> %v1, <4 x float> %v2) nounwind readonly {
  %1 = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %v1, <4 x float> %v2) nounwind readnone
  ret <4 x float> %1
}

define <4 x float> @_Z4fmaxDv4_ff(<4 x float> %v1, float %v2) nounwind readonly {
  %1 = tail call <4 x float> @smear_4f(float %v2) nounwind readnone
  %2 = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %v1, <4 x float> %1) nounwind readnone
  ret <4 x float> %2
}

define <3 x float> @_Z4fmaxDv3_fS_(<3 x float> %v1, <3 x float> %v2) nounwind readonly {
  %1 = shufflevector <3 x float> %v1, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <3 x float> %v2, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %3 = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %1, <4 x float> %2) nounwind readnone
  %4 = shufflevector <4 x float> %3, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %4
}

define <3 x float> @_Z4fmaxDv3_ff(<3 x float> %v1, float %v2) nounwind readonly {
  %1 = shufflevector <3 x float> %v1, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <4 x float> @smear_4f(float %v2) nounwind readnone
  %3 = tail call <4 x float> @llvm.arm.neon.vmaxs.v4f32(<4 x float> %1, <4 x float> %2) nounwind readnone
  %c = shufflevector <4 x float> %3, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %c
}

define <2 x float> @_Z4fmaxDv2_fS_(<2 x float> %v1, <2 x float> %v2) nounwind readonly {
  %1 = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %v1, <2 x float> %v2) nounwind readnone
  ret <2 x float> %1
}

define <2 x float> @_Z4fmaxDv2_ff(<2 x float> %v1, float %v2) nounwind readonly {
  %1 = tail call <2 x float> @smear_2f(float %v2) nounwind readnone
  %2 = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %v1, <2 x float> %1) nounwind readnone
  ret <2 x float> %2
}

define float @_Z4fmaxff(float %v1, float %v2) nounwind readonly {
  %1 = tail call <2 x float> @smear_2f(float %v1) nounwind readnone
  %2 = tail call <2 x float> @smear_2f(float %v2) nounwind readnone
  %3 = tail call <2 x float> @llvm.arm.neon.vmaxs.v2f32(<2 x float> %1, <2 x float> %2) nounwind readnone
  %4 = extractelement <2 x float> %3, i32 0
  ret float %4
}


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;                  FMIN                  ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

define <4 x float> @_Z4fminDv4_fS_(<4 x float> %v1, <4 x float> %v2) nounwind readonly {
  %1 = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %v1, <4 x float> %v2) nounwind readnone
  ret <4 x float> %1
}

define <4 x float> @_Z4fminDv4_ff(<4 x float> %v1, float %v2) nounwind readonly {
  %1 = tail call <4 x float> @smear_4f(float %v2) nounwind readnone
  %2 = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %v1, <4 x float> %1) nounwind readnone
  ret <4 x float> %2
}

define <3 x float> @_Z4fminDv3_fS_(<3 x float> %v1, <3 x float> %v2) nounwind readonly {
  %1 = shufflevector <3 x float> %v1, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <3 x float> %v2, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %3 = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %1, <4 x float> %2) nounwind readnone
  %4 = shufflevector <4 x float> %3, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %4
}

define <3 x float> @_Z4fminDv3_ff(<3 x float> %v1, float %v2) nounwind readonly {
  %1 = shufflevector <3 x float> %v1, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <4 x float> @smear_4f(float %v2) nounwind readnone
  %3 = tail call <4 x float> @llvm.arm.neon.vmins.v4f32(<4 x float> %1, <4 x float> %2) nounwind readnone
  %c = shufflevector <4 x float> %3, <4 x float> undef, <3 x i32> <i32 0, i32 1, i32 2>
  ret <3 x float> %c
}

define <2 x float> @_Z4fminDv2_fS_(<2 x float> %v1, <2 x float> %v2) nounwind readonly {
  %1 = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %v1, <2 x float> %v2) nounwind readnone
  ret <2 x float> %1
}

define <2 x float> @_Z4fminDv2_ff(<2 x float> %v1, float %v2) nounwind readonly {
  %1 = tail call <2 x float> @smear_2f(float %v2) nounwind readnone
  %2 = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %v1, <2 x float> %1) nounwind readnone
  ret <2 x float> %2
}

define float @_Z4fminff(float %v1, float %v2) nounwind readonly {
  %1 = tail call <2 x float> @smear_2f(float %v1) nounwind readnone
  %2 = tail call <2 x float> @smear_2f(float %v2) nounwind readnone
  %3 = tail call <2 x float> @llvm.arm.neon.vmins.v2f32(<2 x float> %1, <2 x float> %2) nounwind readnone
  %4 = extractelement <2 x float> %3, i32 0
  ret float %4
}

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;                  YUV                   ;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


@yuv_U = internal constant <4 x i32> <i32 0, i32 -100, i32 516, i32 0>, align 16
@yuv_V = internal constant <4 x i32> <i32 409, i32 -208, i32 0, i32 0>, align 16
@yuv_0 = internal constant <4 x i32> <i32 0, i32 0, i32 0, i32 0>, align 16
@yuv_255 = internal constant <4 x i32> <i32 65535, i32 65535, i32 65535, i32 65535>, align 16


define <4 x i8> @_Z18rsYuvToRGBA_uchar4hhh(i8 %pY, i8 %pU, i8 %pV) nounwind readnone alwaysinline {
  %_sy = zext i8 %pY to i32
  %_su = zext i8 %pU to i32
  %_sv = zext i8 %pV to i32

  %_sy2 = add i32 -16, %_sy
  %_sy3 = mul i32 298, %_sy2
  %_su2 = add i32 -128, %_su
  %_sv2 = add i32 -128, %_sv
  %_y = tail call <4 x i32> @smear_4i32(i32 %_sy3) nounwind readnone
  %_u = tail call <4 x i32> @smear_4i32(i32 %_su2) nounwind readnone
  %_v = tail call <4 x i32> @smear_4i32(i32 %_sv2) nounwind readnone

  %mu = load <4 x i32>* @yuv_U, align 8
  %mv = load <4 x i32>* @yuv_V, align 8
  %_u2 = mul <4 x i32> %_u, %mu
  %_v2 = mul <4 x i32> %_v, %mv
  %_y2 = add <4 x i32> %_y, %_u2
  %_y3 = add <4 x i32> %_y2, %_v2

 ; %r1 = tail call <4 x i16> @llvm.arm.neon.vqshiftnsu.v4i16(<4 x i32> %_y3, <4 x i32> <i32 8, i32 8, i32 8, i32 8>) nounwind readnone
;  %r2 = trunc <4 x i16> %r1 to <4 x i8>
;  ret <4 x i8> %r2

  %c0 = load <4 x i32>* @yuv_0, align 8
  %c255 = load <4 x i32>* @yuv_255, align 8
  %r1 = tail call <4 x i32> @llvm.arm.neon.vmaxs.v4i32(<4 x i32> %_y3, <4 x i32> %c0) nounwind readnone
  %r2 = tail call <4 x i32> @llvm.arm.neon.vmins.v4i32(<4 x i32> %r1, <4 x i32> %c255) nounwind readnone
  %r3 = lshr <4 x i32> %r2, <i32 8, i32 8, i32 8, i32 8>
  %r4 = trunc <4 x i32> %r3 to <4 x i8>
  ret <4 x i8> %r4
}

