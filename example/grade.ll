; ModuleID = 'zap_module'
source_filename = "zap_module"

@0 = private unnamed_addr constant [9 x i8] c"Grade: A\00", align 1
@1 = private unnamed_addr constant [9 x i8] c"Grade: B\00", align 1
@2 = private unnamed_addr constant [9 x i8] c"Grade: C\00", align 1
@3 = private unnamed_addr constant [9 x i8] c"Grade: D\00", align 1
@4 = private unnamed_addr constant [9 x i8] c"Grade: F\00", align 1

declare i32 @puts(ptr)

define internal void @println(ptr %0) {
entry:
  %1 = call i32 @puts(ptr %0)
  ret void
}

define i32 @main() {
entry:
  %score = alloca i32, align 4
  store i32 75, ptr %score, align 4
  %score1 = load i32, ptr %score, align 4
  %0 = icmp sge i32 %score1, 90
  %ifcond = icmp ne i1 %0, false
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  call void @println(ptr @0)
  br label %ifcont16

else:                                             ; preds = %entry
  %score2 = load i32, ptr %score, align 4
  %1 = icmp sge i32 %score2, 80
  %ifcond3 = icmp ne i1 %1, false
  br i1 %ifcond3, label %then4, label %else5

then4:                                            ; preds = %else
  call void @println(ptr @1)
  br label %ifcont15

else5:                                            ; preds = %else
  %score6 = load i32, ptr %score, align 4
  %2 = icmp sge i32 %score6, 70
  %ifcond7 = icmp ne i1 %2, false
  br i1 %ifcond7, label %then8, label %else9

then8:                                            ; preds = %else5
  call void @println(ptr @2)
  br label %ifcont14

else9:                                            ; preds = %else5
  %score10 = load i32, ptr %score, align 4
  %3 = icmp sge i32 %score10, 60
  %ifcond11 = icmp ne i1 %3, false
  br i1 %ifcond11, label %then12, label %else13

then12:                                           ; preds = %else9
  call void @println(ptr @3)
  br label %ifcont

else13:                                           ; preds = %else9
  call void @println(ptr @4)
  br label %ifcont

ifcont:                                           ; preds = %else13, %then12
  br label %ifcont14

ifcont14:                                         ; preds = %ifcont, %then8
  br label %ifcont15

ifcont15:                                         ; preds = %ifcont14, %then4
  br label %ifcont16

ifcont16:                                         ; preds = %ifcont15, %then
  ret i32 0
}
