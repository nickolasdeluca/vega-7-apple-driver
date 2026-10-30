source_filename = "group_sum.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

; Function Attrs: convergent mustprogress nounwind
define void @group_sum(ptr addrspace(1) nocapture noundef readonly "air-buffer-no-alias" %0, ptr addrspace(1) nocapture noundef writeonly "air-buffer-no-alias" %1, ptr addrspace(3) nocapture noundef "air-buffer-no-alias" %2, i32 noundef %3, i32 noundef %4, i32 noundef %5, i32 noundef %6) local_unnamed_addr #0 {
  %8 = zext i32 %3 to i64
  %9 = getelementptr inbounds i32, ptr addrspace(1) %0, i64 %8
  %10 = load i32, ptr addrspace(1) %9, align 4, !tbaa !25, !alias.scope !29, !noalias !32
  %11 = zext i32 %4 to i64
  %12 = getelementptr inbounds i32, ptr addrspace(3) %2, i64 %11
  store i32 %10, ptr addrspace(3) %12, align 4, !tbaa !25, !alias.scope !35, !noalias !36
  tail call void @air.wg.barrier(i32 2, i32 1) #2
  %13 = icmp ult i32 %6, 2
  br i1 %13, label %14, label %16

14:                                               ; preds = %27, %7
  %15 = icmp eq i32 %4, 0
  br i1 %15, label %29, label %33

16:                                               ; preds = %27, %7
  %17 = phi i32 [ %18, %27 ], [ %6, %7 ]
  %18 = lshr i32 %17, 1
  %19 = icmp ugt i32 %18, %4
  br i1 %19, label %20, label %27

20:                                               ; preds = %16
  %21 = add i32 %18, %4
  %22 = zext i32 %21 to i64
  %23 = getelementptr inbounds i32, ptr addrspace(3) %2, i64 %22
  %24 = load i32, ptr addrspace(3) %23, align 4, !tbaa !25, !alias.scope !35, !noalias !36
  %25 = load i32, ptr addrspace(3) %12, align 4, !tbaa !25, !alias.scope !35, !noalias !36
  %26 = add i32 %25, %24
  store i32 %26, ptr addrspace(3) %12, align 4, !tbaa !25, !alias.scope !35, !noalias !36
  br label %27

27:                                               ; preds = %20, %16
  tail call void @air.wg.barrier(i32 2, i32 1) #2
  %28 = icmp ult i32 %17, 4
  br i1 %28, label %14, label %16, !llvm.loop !37

29:                                               ; preds = %14
  %30 = zext i32 %5 to i64
  %31 = getelementptr inbounds i32, ptr addrspace(1) %1, i64 %30
  %32 = load i32, ptr addrspace(3) %2, align 4, !tbaa !25, !alias.scope !35, !noalias !36
  store i32 %32, ptr addrspace(1) %31, align 4, !tbaa !25, !alias.scope !39, !noalias !40
  br label %33

33:                                               ; preds = %29, %14
  ret void
}

; Function Attrs: convergent mustprogress nounwind willreturn
declare void @air.wg.barrier(i32, i32) local_unnamed_addr #1

attributes #0 = { convergent mustprogress nounwind "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }
attributes #1 = { convergent mustprogress nounwind willreturn }
attributes #2 = { convergent nounwind willreturn }

!air.kernel = !{!0}
!llvm.module.flags = !{!10, !11, !12, !13, !14, !15, !16, !17}
!air.compile_options = !{!18, !19, !20}
!llvm.ident = !{!21}
!air.version = !{!22}
!air.language_version = !{!23}
!air.source_file_name = !{!24}

!0 = !{ptr @group_sum, !1, !2}
!1 = !{}
!2 = !{!3, !4, !5, !6, !7, !8, !9}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"in"}
!4 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!5 = !{i32 2, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 3, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"partial"}
!6 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!7 = !{i32 4, !"air.thread_position_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"tid"}
!8 = !{i32 5, !"air.threadgroup_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"group"}
!9 = !{i32 6, !"air.threads_per_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"size"}
!10 = !{i32 1, !"wchar_size", i32 4}
!11 = !{i32 7, !"frame-pointer", i32 2}
!12 = !{i32 7, !"air.max_device_buffers", i32 31}
!13 = !{i32 7, !"air.max_constant_buffers", i32 31}
!14 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!15 = !{i32 7, !"air.max_textures", i32 128}
!16 = !{i32 7, !"air.max_read_write_textures", i32 8}
!17 = !{i32 7, !"air.max_samplers", i32 16}
!18 = !{!"air.compile.denorms_disable"}
!19 = !{!"air.compile.fast_math_enable"}
!20 = !{!"air.compile.framebuffer_fetch_enable"}
!21 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!22 = !{i32 2, i32 8, i32 0}
!23 = !{!"Metal", i32 4, i32 0, i32 0}
!24 = !{!"group_sum.metal"}
!25 = !{!26, !26, i64 0}
!26 = !{!"int", !27, i64 0}
!27 = !{!"omnipotent char", !28, i64 0}
!28 = !{!"Simple C++ TBAA"}
!29 = !{!30}
!30 = distinct !{!30, !31, !"air-alias-scope-arg(0)"}
!31 = distinct !{!31, !"air-alias-scopes(group_sum)"}
!32 = !{!33, !34}
!33 = distinct !{!33, !31, !"air-alias-scope-arg(1)"}
!34 = distinct !{!34, !31, !"air-alias-scope-arg(2)"}
!35 = !{!34}
!36 = !{!30, !33}
!37 = distinct !{!37, !38}
!38 = !{!"llvm.loop.mustprogress"}
!39 = !{!33}
!40 = !{!30, !34}
