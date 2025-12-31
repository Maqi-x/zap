; ModuleID = 'zap_module'
source_filename = "zap_module"

@0 = private unnamed_addr constant [13 x i8] c"hello world\0A\00", align 1
@1 = private unnamed_addr constant [8 x i8] c"skibidi\00", align 1

declare i32 @puts(ptr)

define internal void @println(ptr %0) {
entry:
  %1 = call i32 @puts(ptr %0)
  ret void
}

define i32 @main() {
entry:
  call void @println(ptr @0)
  br i1 true, label %then, label %else

then:                                             ; preds = %entry
  call void @println(ptr @1)
  br label %ifcont

else:                                             ; preds = %entry
  br label %ifcont

ifcont:                                           ; preds = %else, %then
  ret i32 0
}
