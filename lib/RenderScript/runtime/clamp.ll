target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:64:128-a0:0:64-n32-S64"
target triple = "armv7-none-linux-gnueabi"

define i32 @_Z7rsClampjjj(i32 %amount, i32 %low, i32 %high) nounwind readnone alwaysinline {
  %1 = icmp ult i32 %amount, %low
  br i1 %1, label %5, label %2

; <label>:2                                       ; preds = %0
  %3 = icmp ugt i32 %amount, %high
  %4 = select i1 %3, i32 %high, i32 %amount
  br label %5

; <label>:5                                       ; preds = %2, %0
  %6 = phi i32 [ %4, %2 ], [ %low, %0 ]
  ret i32 %6
}
