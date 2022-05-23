#include <stdlib.h>
#include "Vtop.h"
#include "Vtop_top.h"
#include "Vtop_execution.h"
#include "Vtop_bus_interface.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "testing_x86.h"

extern Vtop *tb;

//	AFFECT S AS SIGN, Z AS ZERO, A AS CARRY(3), O AS OVERFLOW(dst,src,7), C AS CARRY(7), P AS PARITYEVEN { (dst + src)+carry }->res;
int CheckAddFlagsW(int a,int b, int flagMask)
{
    unsigned long ret;

    __asm__ volatile (
        "add %w2, %w0\n"
        "pushfq\n"
        "pop %0\n"
        : "=q" (ret)
        : "0" (a), "r" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask);
}

int CheckSubFlagsW(int a,int b, int flagMask)
{
    unsigned long ret;

    __asm__ volatile (
        "sub %w2, %w0\n"
        "pushfq\n"
        "pop %0\n"
        : "=q" (ret)
        : "0" (a), "r" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask);
}

int CheckAddResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "add %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckAddResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "add %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckOrResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "or %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckOrResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "or %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckAdcResultW(int a,int b, int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "adc %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "adc %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckAdcResultB(int a,int b, int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "adc %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "adc %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckSbbResultW(int a,int b, int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sbb %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sbb %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckSbbResultB(int a,int b, int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sbb %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sbb %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckAndResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "and %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckAndResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "and %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckSubResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "sub %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckSubResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "sub %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckXorResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "xor %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckXorResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "xor %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckCmpResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "cmp %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckCmpResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "cmp %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckShlResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shl $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shl $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckShlResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shl $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shl $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckShrResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shr $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shr $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckShrResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shr $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shr $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckSarResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sar $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sar $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckSarResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sar $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sar $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRorResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "ror $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "ror $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRorResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "ror $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "ror $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRolResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rol $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rol $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRolResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rol $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rol $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRclResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcl $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcl $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRclResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcl $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcl $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}
int CheckRcrResultW(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcr $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcr $1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRcrResultB(int a,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcr $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcr $1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckNegResultW(int a,int flagMask,int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "neg %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckNegResultB(int a,int flagMask,int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "neg %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckTestResultW(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "test %w1, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckTestResultB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "test %b1, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "1" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckShlClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shl %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shl %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckShlClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shl %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shl %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckShrClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shr %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shr %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckShrClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "shr %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "shr %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckSarClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sar %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sar %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckSarClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "sar %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "sar %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRorClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "ror %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "ror %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRorClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "ror %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "ror %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRolClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rol %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rol %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRolClResultB(int a,int cl, int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rol %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rol %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckRclClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcl %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcl %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRclClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcl %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcl %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}
int CheckRcrClResultW(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcr %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcr %b3, %w0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckRcrClResultB(int a,int cl,int carryIn, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    if (carryIn)
    {
    __asm__ volatile (
        "stc\n"
        "rcr %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }
    else
    {
    __asm__ volatile (
        "clc\n"
        "rcr %b3, %b0\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=q" (ret), "=q" (res)
        : "0" (a), "cJ" (cl)
        );
    }

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFF)==(expected&0xFF));
}

int CheckMulB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "mul %b2\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res)
        : "q" (a), "0" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckMulW(int a,int b, int flagMask, int expected, int expected2)
{
    unsigned long ret;
    unsigned long res;
    unsigned long res2;

    __asm__ volatile (
        "mul %w3\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res), "=d" (res2)
        : "q" (a), "0" (b)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF) && ((res2&0xFFFF)==(expected2&0xFFFF)));
}

int CheckIMulB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "imul %b2\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res)
        : "q" (b), "0" (a)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckIMulW(int a,int b, int flagMask, int expected, int expected2)
{
    unsigned long ret;
    unsigned long res;
    unsigned long res2;

    __asm__ volatile (
        "imul %w3\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res), "=d" (res2)
        : "q" (b), "0" (a)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF) && ((res2&0xFFFF)==(expected2&0xFFFF)));
}

int CheckDivB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "div %b2\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res)
        : "q" (b), "0" (a)
        );

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckDivW(int al,int ah,int b, int flagMask, int expected, int expected2)
{
    unsigned long ret;
    unsigned long res;
    unsigned long res2;
    
    __asm__ volatile (
        "div %w3\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res), "=d" (res2)
        : "q" (b), "0" (al), "2" (ah)
        );
    
    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF) && ((res2&0xFFFF)==(expected2&0xFFFF)));
}

int CheckIDivB(int a,int b, int flagMask, int expected)
{
    unsigned long ret;
    unsigned long res;

    __asm__ volatile (
        "idiv %b2\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res)
        : "q" (b), "0" (a)
        );

    if ((res&0xFFFF)!=(expected&0xFFFF))
        printf("\n%04X / %02X = %04X vs %04X\n",a&0xFFFF,b&0xFF,res&0xFFFF,expected&0xFFFF);

    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF));
}

int CheckIDivW(int al,int ah,int b, int flagMask, int expected, int expected2)
{
    unsigned long ret;
    unsigned long res;
    unsigned long res2;
    
    __asm__ volatile (
        "idiv %w3\n"
        "push %0\n"
        "pushfq\n"
        "pop %0\n"
        "pop %1\n"
        : "=a" (ret), "=q" (res), "=d" (res2)
        : "q" (b), "0" (al), "2" (ah)
        );
    
    return TestFlags(tb->top->eu->FLAGS,ret, flagMask) && ((res&0xFFFF)==(expected&0xFFFF) && ((res2&0xFFFF)==(expected2&0xFFFF)));
}


#define topBit(ppp) ((ppp & (_wordSize?0x8000:0x80))!=0)
#define sizeMask() (_wordSize?0xFFFF:0xFF)
#define wait(n) {}//
#define interrupt(n) {}//

#define ax() ax
#define dx() dx

//// HACK
bool div(uint16_t l, uint16_t h,uint16_t _source,int _wordSize, int _signed,int intrpt, uint16_t expectedL,uint16_t expectedH, int& result)
{
    result=0;
    int bitCount = 8;
    if (_wordSize) {
        //l = ax();
        //h = dx();
        bitCount = 16;
    }
    bool negative = false;
    bool dividendNegative = false;
    if (_signed) 
    {
        if (topBit(h)) {
            h = ~h;
            l = (~l + 1) & sizeMask();
            if (l == 0)
                ++h;
            h &= sizeMask();
            negative = true;
            dividendNegative = true;
            wait(4);
        }
        if (topBit(_source)) {
            _source = ~_source + 1;
            negative = !negative;
        }
        else
            wait(1);
        wait(9);
        wait(3);
    }
    wait(8);
    _source &= sizeMask();
    if (h >= _source) {
        wait(1); //2); //3);
        if (_signed)
            wait(1);
        interrupt(0);
/*        if (!intrpt)
            printf("\nINT0 (early)\n");*/
        result=2;
        return false;
    }
    if (_signed)
        wait(1);
    wait(2);
    bool carry = true;
    for (int b = 0; b < bitCount; ++b) {
        //printf("L : %04X\n",l);
        //printf("carryInToShift : %d\n",carry);
        uint16_t r = (l << 1) + (carry ? 1 : 0);
        carry = topBit(l);
        l = r;//&0xFF;
        r = (h << 1) + (carry ? 1 : 0);
        carry = topBit(h);
        h = r;//&0xFF;
        wait(8);
        if (carry) {
            carry = false;
            h -= _source;
            if (b == bitCount - 1)
                wait(2);
        }
        else {
            carry = _source > h;
            if (!carry) {
                h -= _source;
                wait(1);
                if (b == bitCount - 1)
                    wait(2);
            }
        }
    }
    //printf("carryOut : %d\n",carry);
    //printf("Pre Final L : %04X\n",l);
    l = ~((l << 1) + (carry ? 1 : 0));
    //printf("Final L : %04X\n",l);
    if (_signed) {
        wait(4);
        if (topBit(l)) {
            wait(1);
            wait(1); //2);
            interrupt(0);
/*            if (!intrpt)
                printf("\nINT0 (late)\n");*/
            result=1;
            return false;
        }
        wait(7);
        if (negative)
            l = ~l + 1;
        if (dividendNegative)
            h = ~h + 1;
    }
    //ah() = h & 0xff;
    //al() = l & 0xff;
    if (_wordSize) {
        
        if ((expectedL != l) || (expectedH != h))
        {
            printf("\nREM: %04X, QUO: %04X   vs    %04X %04X\n",h,l,expectedH,expectedL);
            result = 3;
        }
        //dx() = h;
        //ax() = l;
    }
    else
    {
        if ((expectedL != (l&0xFF)) || (expectedH != (h&0xFF)))
        {
            printf("\nREM: %02X, QUO: %02X   vs   %02X %02X\n",h&0xFF,l&0xFF,expectedH,expectedL);
            result=3;
        }
    }
    if (intrpt)
        printf("\nFAILED\n");
    return true;
}
