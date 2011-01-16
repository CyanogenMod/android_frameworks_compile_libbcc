; ModuleID = 'rslib.bc'

define <3 x float> @_Z15convert1_float3Dv3_h(<3 x i8> %u3) nounwind readnone {
  %conv = uitofp <3 x i8> %u3 to <3 x float>
  ret <3 x float> %conv
}

define <3 x i8> @_Z14convert2uchar3Dv3_f(<3 x float> %v) nounwind {
  %1 = extractelement <3 x float> %v, i32 0
  %2 = fptoui float %1 to i8
  %3 = insertelement <3 x i8> undef, i8 %2, i32 0
  %4 = extractelement <3 x float> %v, i32 1
  %5 = fptoui float %4 to i8
  %6 = insertelement <3 x i8> %3, i8 %5, i32 1
  %7 = extractelement <3 x float> %v, i32 2
  %8 = fptoui float %7 to i8
  %9 = insertelement <3 x i8> %6, i8 %8, i32 2
  ret <3 x i8> %9
}

declare float @llvm.powi.f32(float, i32) nounwind readonly

define <3 x float> @_Z4powiDv3_fi(<3 x float> %f3, i32 %exp) nounwind readnone {
  %x = extractelement <3 x float> %f3, i32 0
  %y = extractelement <3 x float> %f3, i32 1
  %z = extractelement <3 x float> %f3, i32 2
  %retx = tail call float @llvm.powi.f32(float %x, i32 %exp)
  %rety = tail call float @llvm.powi.f32(float %y, i32 %exp)
  %retz = tail call float @llvm.powi.f32(float %z, i32 %exp)
  %tmp1 = insertelement <3 x float> %f3, float %retx, i32 0
  %tmp2 = insertelement <3 x float> %tmp1, float %rety, i32 1
  %ret = insertelement <3 x float> %tmp2, float %retz, i32 2
  ret <3 x float> %ret
}

declare float @llvm.pow.f32(float, float) nounwind readonly

define <3 x float> @_Z4pow3Dv3_ff(<3 x float> %f3, float %exp) nounwind readnone {
  %x = extractelement <3 x float> %f3, i32 0
  %y = extractelement <3 x float> %f3, i32 1
  %z = extractelement <3 x float> %f3, i32 2
  %retx = tail call float @llvm.pow.f32(float %x, float %exp)
  %rety = tail call float @llvm.pow.f32(float %y, float %exp)
  %retz = tail call float @llvm.pow.f32(float %z, float %exp)
  %tmp1 = insertelement <3 x float> %f3, float %retx, i32 0
  %tmp2 = insertelement <3 x float> %tmp1, float %rety, i32 1
  %ret = insertelement <3 x float> %tmp2, float %retz, i32 2
  ret <3 x float> %ret
}
