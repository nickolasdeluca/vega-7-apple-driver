source_filename = "atomic_count.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

%"struct.metal::_atomic" = type { i32 }

; Function Attrs: mustprogress nounwind willreturn
define void @atomic_count(ptr addrspace(1) nocapture noundef "air-buffer-no-alias" %0, i32 noundef %1) local_unnamed_addr #0 {
  %3 = getelementptr inbounds %"struct.metal::_atomic", ptr addrspace(1) %0, i64 0, i32 0
  %4 = tail call i32 @air.atomic.global.add.u.i32(ptr addrspace(1) nocapture %3, i32 1, i32 0, i32 2, i1 true) #2
  ret void
}

; Function Attrs: mustprogress nounwind willreturn
declare i32 @air.atomic.global.add.u.i32(ptr addrspace(1) nocapture, i32, i32, i32, i1) local_unnamed_addr #1

attributes #0 = { mustprogress nounwind willreturn "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }
attributes #1 = { mustprogress nounwind willreturn }
attributes #2 = { nounwind willreturn }

!air.kernel = !{!0}
!llvm.module.flags = !{!6, !7, !8, !9, !10, !11, !12, !13}
!air.compile_options = !{!14, !15, !16}
!llvm.ident = !{!17}
!air.version = !{!18}
!air.language_version = !{!19}
!air.source_file_name = !{!20}

!0 = !{ptr @atomic_count, !1, !2}
!1 = !{}
!2 = !{!3, !5}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.struct_type_info", !4, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"metal::_atomic", !"air.arg_name", !"counter"}
!4 = !{i32 0, i32 4, i32 0, !"uint", !"__s"}
!5 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id", !"air.arg_unused"}
!6 = !{i32 1, !"wchar_size", i32 4}
!7 = !{i32 7, !"frame-pointer", i32 2}
!8 = !{i32 7, !"air.max_device_buffers", i32 31}
!9 = !{i32 7, !"air.max_constant_buffers", i32 31}
!10 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!11 = !{i32 7, !"air.max_textures", i32 128}
!12 = !{i32 7, !"air.max_read_write_textures", i32 8}
!13 = !{i32 7, !"air.max_samplers", i32 16}
!14 = !{!"air.compile.denorms_disable"}
!15 = !{!"air.compile.fast_math_enable"}
!16 = !{!"air.compile.framebuffer_fetch_enable"}
!17 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!18 = !{i32 2, i32 8, i32 0}
!19 = !{!"Metal", i32 4, i32 0, i32 0}
!20 = !{!"atomic_count.metal"}
