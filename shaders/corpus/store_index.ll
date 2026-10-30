source_filename = "store_index.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write)
define void @store_index(ptr addrspace(1) nocapture noundef writeonly "air-buffer-no-alias" %0, i32 noundef %1) local_unnamed_addr #0 {
  %3 = zext i32 %1 to i64
  %4 = getelementptr inbounds i32, ptr addrspace(1) %0, i64 %3
  store i32 %1, ptr addrspace(1) %4, align 4, !tbaa !20, !alias.scope !24
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write) "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }

!air.kernel = !{!0}
!llvm.module.flags = !{!5, !6, !7, !8, !9, !10, !11, !12}
!air.compile_options = !{!13, !14, !15}
!llvm.ident = !{!16}
!air.version = !{!17}
!air.language_version = !{!18}
!air.source_file_name = !{!19}

!0 = !{ptr @store_index, !1, !2}
!1 = !{}
!2 = !{!3, !4}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!4 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"frame-pointer", i32 2}
!7 = !{i32 7, !"air.max_device_buffers", i32 31}
!8 = !{i32 7, !"air.max_constant_buffers", i32 31}
!9 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!10 = !{i32 7, !"air.max_textures", i32 128}
!11 = !{i32 7, !"air.max_read_write_textures", i32 8}
!12 = !{i32 7, !"air.max_samplers", i32 16}
!13 = !{!"air.compile.denorms_disable"}
!14 = !{!"air.compile.fast_math_enable"}
!15 = !{!"air.compile.framebuffer_fetch_enable"}
!16 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!17 = !{i32 2, i32 8, i32 0}
!18 = !{!"Metal", i32 4, i32 0, i32 0}
!19 = !{!"store_index.metal"}
!20 = !{!21, !21, i64 0}
!21 = !{!"int", !22, i64 0}
!22 = !{!"omnipotent char", !23, i64 0}
!23 = !{!"Simple C++ TBAA"}
!24 = !{!25}
!25 = distinct !{!25, !26, !"air-alias-scope-arg(0)"}
!26 = distinct !{!26, !"air-alias-scopes(store_index)"}
