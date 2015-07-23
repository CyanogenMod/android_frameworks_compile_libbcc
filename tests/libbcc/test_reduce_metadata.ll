; Check that the #rs_export_reduce node is recognized.

; RUN: llvm-rs-as %s -o %t
; RUN: bcinfo %t | FileCheck %s

; CHECK: exportReduceCount: 1
; CHECK: func[0]: add

; ModuleID = 'reduce.bc'
target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-none-linux-gnueabi"

; Function Attrs: nounwind readnone
define i32 @add(i32 %a, i32 %b) #0 {
  %1 = add nsw i32 %b, %a
  ret i32 %1
}

attributes #0 = { nounwind readnone }

!llvm.ident = !{!0}
!\23pragma = !{!1, !2}
!\23rs_export_reduce = !{!3}

!0 = !{!"clang version 3.6 "}
!1 = !{!"version", !"1"}
!2 = !{!"java_package_name", !"com.android.rs.test"}
!3 = !{!"add"}
