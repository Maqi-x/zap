; ModuleID = 'zap_module'
source_filename = "zap_module"

@0 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1

define i32 @main() {
entry:
  %t = alloca ptr, align 8
  store ptr @0, ptr %t, align 8
  %a = alloca i32, align 4
  store i32 5, ptr %a, align 4
  %a1 = load i32, ptr %a, align 4
  %a2 = load i32, ptr %a, align 4
  %0 = mul i32 %a1, %a2
  store i32 %0, ptr %a, align 4
  %a3 = load i32, ptr %a, align 4
  %1 = add i32 %a3, 1
  ret i32 %1
}
