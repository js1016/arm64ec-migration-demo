;=============================================================================
; FastMath.asm —— x64 手写汇编（MASM / ml64.exe）
;
; 迁移阻断点（本示例中最“硬”的一处）：
;   ml64.exe 只能产出 x64 目标文件，**无法**为 ARM64EC 编译。
;   把工程切换到 ARM64EC 平台时，MSBuild 会直接报错或产出架构不匹配的 .obj。
;
;   迁移方案：
;     (a) 用 C/C++ 重写（最省事，编译器通常能自动向量化）；
;     (b) 用 armasm64.exe 写一份 ARM64 汇编，并按平台条件编译选择；
;     (c) 若该函数不在热路径上，直接删除汇编版本。
;
; 函数原型（x64 调用约定）：
;   extern "C" uint64_t FastMath_SumU32(const uint32_t* data, size_t count);
;     RCX = data   （第 1 个整型参数）
;     RDX = count  （第 2 个整型参数）
;     RAX = 返回值
;=============================================================================

.code

FastMath_SumU32 PROC

    xor     rax, rax                ; 累加器清零
    test    rdx, rdx                ; count == 0 ?
    jz      FM_Done

FM_Loop:
    mov     r8d, dword ptr [rcx]    ; 读取一个 32 位元素（写 r8d 会自动零扩展到 r8）
    add     rax, r8                 ; 以 64 位累加，避免溢出
    add     rcx, 4                  ; 指针前进 4 字节
    dec     rdx                      ; 计数减一
    jnz     FM_Loop

FM_Done:
    ret

FastMath_SumU32 ENDP

END
