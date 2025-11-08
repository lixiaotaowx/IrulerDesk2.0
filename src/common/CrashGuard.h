#ifndef CRASHGUARD_H
 #define CRASHGUARD_H
 
 namespace CrashGuard {
     // 安装未处理异常捕获，打印带文件名与行号的调用栈
     void install();
 }
 
 #endif // CRASHGUARD_H