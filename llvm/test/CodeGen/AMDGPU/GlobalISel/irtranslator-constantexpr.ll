; NOTE: Assertions have been autogenerated by utils/update_mir_test_checks.py
; RUN: llc -global-isel -mtriple=amdgcn-amd-amdhsa -verify-machineinstrs -stop-after=irtranslator -o - %s | FileCheck %s

@var = global i32 undef

define i32 @test() {
  ; CHECK-LABEL: name: test
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK:   liveins: $sgpr30_sgpr31
  ; CHECK:   [[COPY:%[0-9]+]]:sgpr_64 = COPY $sgpr30_sgpr31
  ; CHECK:   [[C:%[0-9]+]]:_(s32) = G_CONSTANT i32 -1
  ; CHECK:   [[INTTOPTR:%[0-9]+]]:_(p0) = G_INTTOPTR [[C]](s32)
  ; CHECK:   [[GV:%[0-9]+]]:_(p0) = G_GLOBAL_VALUE @var
  ; CHECK:   [[ICMP:%[0-9]+]]:_(s1) = G_ICMP intpred(eq), [[INTTOPTR]](p0), [[GV]]
  ; CHECK:   [[ZEXT:%[0-9]+]]:_(s32) = G_ZEXT [[ICMP]](s1)
  ; CHECK:   [[COPY1:%[0-9]+]]:_(s32) = COPY [[ZEXT]](s32)
  ; CHECK:   [[COPY2:%[0-9]+]]:_(s32) = COPY [[COPY1]](s32)
  ; CHECK:   [[COPY3:%[0-9]+]]:_(s32) = COPY [[COPY2]](s32)
  ; CHECK:   [[COPY4:%[0-9]+]]:_(s32) = COPY [[COPY3]](s32)
  ; CHECK:   $vgpr0 = COPY [[COPY4]](s32)
  ; CHECK:   [[COPY5:%[0-9]+]]:ccr_sgpr_64 = COPY [[COPY]]
  ; CHECK:   S_SETPC_B64_return [[COPY5]], implicit $vgpr0
  ret i32 bitcast (<1 x i32> <i32 extractelement (<1 x i32> bitcast (i32 zext (i1 icmp eq (i32* @var, i32* inttoptr (i32 -1 to i32*)) to i32) to <1 x i32>), i64 0)> to i32)
}

@gint = external addrspace(1) global i8, align 4

; Technically we should be able to fold away the compare to true, but
; currently constexpr doesn't understand null in non-0 address spaces.
define amdgpu_kernel void @constantexpr_select_0() {
  ; CHECK-LABEL: name: constantexpr_select_0
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK:   [[GV:%[0-9]+]]:_(p1) = G_GLOBAL_VALUE @gint
  ; CHECK:   [[C:%[0-9]+]]:_(p1) = G_CONSTANT i64 0
  ; CHECK:   [[ICMP:%[0-9]+]]:_(s1) = G_ICMP intpred(eq), [[GV]](p1), [[C]]
  ; CHECK:   [[C1:%[0-9]+]]:_(s32) = G_CONSTANT i32 1
  ; CHECK:   [[C2:%[0-9]+]]:_(s32) = G_CONSTANT i32 0
  ; CHECK:   [[SELECT:%[0-9]+]]:_(s32) = G_SELECT [[ICMP]](s1), [[C1]], [[C2]]
  ; CHECK:   [[DEF:%[0-9]+]]:_(p1) = G_IMPLICIT_DEF
  ; CHECK:   G_STORE [[SELECT]](s32), [[DEF]](p1) :: (store (s32) into `i32 addrspace(1)* undef`, addrspace 1)
  ; CHECK:   S_ENDPGM 0
  store i32 select (i1 icmp eq (i8 addrspace(1)* @gint, i8 addrspace(1)* null), i32 1, i32 0), i32 addrspace(1)* undef, align 4
  ret void
}

define amdgpu_kernel void @constantexpr_select_1() {
  ; CHECK-LABEL: name: constantexpr_select_1
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK:   [[C:%[0-9]+]]:_(s64) = G_CONSTANT i64 1024
  ; CHECK:   [[INTTOPTR:%[0-9]+]]:_(p1) = G_INTTOPTR [[C]](s64)
  ; CHECK:   [[GV:%[0-9]+]]:_(p1) = G_GLOBAL_VALUE @gint
  ; CHECK:   [[ICMP:%[0-9]+]]:_(s1) = G_ICMP intpred(eq), [[INTTOPTR]](p1), [[GV]]
  ; CHECK:   [[C1:%[0-9]+]]:_(s32) = G_CONSTANT i32 1
  ; CHECK:   [[C2:%[0-9]+]]:_(s32) = G_CONSTANT i32 0
  ; CHECK:   [[SELECT:%[0-9]+]]:_(s32) = G_SELECT [[ICMP]](s1), [[C1]], [[C2]]
  ; CHECK:   [[DEF:%[0-9]+]]:_(p1) = G_IMPLICIT_DEF
  ; CHECK:   G_STORE [[SELECT]](s32), [[DEF]](p1) :: (store (s32) into `i32 addrspace(1)* undef`, addrspace 1)
  ; CHECK:   S_ENDPGM 0
  store i32 select (i1 icmp eq (i8 addrspace(1)* @gint, i8 addrspace(1)* inttoptr (i64 1024 to i8 addrspace(1)*)), i32 1, i32 0), i32 addrspace(1)* undef, align 4
  ret void
}

@a = external global [2 x i32], align 4

define i32 @test_fcmp_constexpr() {
  ; CHECK-LABEL: name: test_fcmp_constexpr
  ; CHECK: bb.1.entry:
  ; CHECK:   liveins: $sgpr30_sgpr31
  ; CHECK:   [[COPY:%[0-9]+]]:sgpr_64 = COPY $sgpr30_sgpr31
  ; CHECK:   [[GV:%[0-9]+]]:_(p0) = G_GLOBAL_VALUE @a
  ; CHECK:   [[C:%[0-9]+]]:_(s64) = G_CONSTANT i64 4
  ; CHECK:   [[PTR_ADD:%[0-9]+]]:_(p0) = G_PTR_ADD [[GV]], [[C]](s64)
  ; CHECK:   [[GV1:%[0-9]+]]:_(p0) = G_GLOBAL_VALUE @var
  ; CHECK:   [[ICMP:%[0-9]+]]:_(s1) = G_ICMP intpred(eq), [[PTR_ADD]](p0), [[GV1]]
  ; CHECK:   [[UITOFP:%[0-9]+]]:_(s32) = G_UITOFP [[ICMP]](s1)
  ; CHECK:   [[C1:%[0-9]+]]:_(s32) = G_FCONSTANT float 0.000000e+00
  ; CHECK:   [[FCMP:%[0-9]+]]:_(s1) = G_FCMP floatpred(oeq), [[UITOFP]](s32), [[C1]]
  ; CHECK:   [[ZEXT:%[0-9]+]]:_(s32) = G_ZEXT [[FCMP]](s1)
  ; CHECK:   $vgpr0 = COPY [[ZEXT]](s32)
  ; CHECK:   [[COPY1:%[0-9]+]]:ccr_sgpr_64 = COPY [[COPY]]
  ; CHECK:   S_SETPC_B64_return [[COPY1]], implicit $vgpr0
entry:
  ret i32 zext (i1 fcmp oeq (float uitofp (i1 icmp eq (i32* getelementptr inbounds ([2 x i32], [2 x i32]* @a, i64 0, i64 1), i32* @var) to float), float 0.000000e+00) to i32)
}
