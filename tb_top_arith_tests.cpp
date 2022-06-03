#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include "Vtop.h"
#include "Vtop_top.h"
#include "Vtop_execution.h"
#include "Vtop_bus_interface.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "testing_x86.h"

#define TRACE 0

#define RAMSIZE 1024*1024
static unsigned char RAM[RAMSIZE];

#define CLK_DIVISOR 8

#define TICK_LIMIT  0 && 300000

static int internalClock=CLK_DIVISOR;
static int ck=0;

static uint32_t latchedAddress;
static int lastWrite=1,lastRead=1;
extern Vtop *tb;

int exitStatus = EXIT_SUCCESS;

#define TEST_MULT 6

int GuardCheckDivB(int a,int b, int flagMask, int expected)
{
    if (b==0)
    {
        int vector = 0x100;

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        return ip==vector;
    }
    else
    {
        uint32_t divA,divB;
        divA= a;
        divB= b;

        if (divA/divB > 255)
        {
            int vector = 0x100;

            int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
            return ip==vector;
        }

        return CheckDivB(a, b, flagMask, expected);
    }
}

bool div(uint16_t l, uint16_t h,uint16_t _source,int _wordSize, int _signed, int intrpt,uint16_t expectedL, uint16_t expectedH, int& res);

static sigjmp_buf fpe_env;


volatile int interrupt=0;
static void handler(int signal, siginfo_t* w, void* a)
{
    siglongjmp(fpe_env, w->si_code);
}

int GuardCheckIDivB(int a,int b, int flagMask, int expected)
{
/*    struct sigaction act,old;
    interrupt=0;
    int ok=0;

    int code=sigsetjmp(fpe_env,1);
    if (code==0)
    {
        act.sa_sigaction = handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags=SA_SIGINFO;
        if (sigaction(SIGFPE,&act,&old)<0)
            exit(1);
        ok= CheckIDivB(a, b, flagMask, expected);
        if (sigaction(SIGFPE,&old,NULL)<0)
            exit(1);
    }
    else
    {
        if (sigaction(SIGFPE,&old,NULL)<0)
            exit(1);

        interrupt=1;
        //printf("\n\n\nEXCEPTED\n");
    }
*/
    int ok=0;
    int answer=div(a&0xFF,a>>8,b,0,1,interrupt,tb->top->eu->AX&0xFF,tb->top->eu->AX>>8,ok);

    //if (interrupt)
    if (!answer)
    {
        int vector = 0x100;

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        if (ip!=vector)
            printf("\nDIDN'T CATCH OVERFLOW\n");
        return ip==vector;
    }
    return ok==0;
}

int GuardCheckDivW(int al,int ah,int b, int flagMask, int expected, int expected2)
{
    if (b==0)
    {
        int vector = 0x100;

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        return ip==vector;
    }
    else
    {
        uint32_t divA,divB;
        divA= (ah<<16)|(al);
        divB= b;

        if (divA/divB > 65535)
        {
            int vector = 0x100;

            int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
            return ip==vector;
        }

        return CheckDivW(al,ah, b, flagMask, expected, expected2);
    }
}

int GuardCheckIDivW(int al,int ah,int b, int flagMask, int expected, int expected2)
{
    int ok=0;
    int answer=div(al,ah,b,1,1,interrupt,tb->top->eu->AX,tb->top->eu->DX,ok);

    if (!answer)
    {
        int vector = 0x100;

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        if (ip!=vector)
            printf("\nDIDN'T CATCH OVERFLOW\n");
        return ip==vector;
    }
    return ok==0;
}

// 0 = opA,opB,flags,result
// 1 = opA,opB,cIn,flags,result
// 2 = opA, flags, result
// 3 = opA,opB,flags,result  (but result in AX)
// 4 = opA,opB,flags,resulthi,resultlo  (DX,AX)
// 5 = opAl,opAH,opB,flags,resulthi,resultlo  (DX,AX)
// 6 = opA,opB,flags,result  (but result in AX)
static const char* testArray[]={ 
    // Byte Tests
#if 1
    "\x00""\xC1",       (const char*)2,         (const char*)CheckAddResultB,       "add al,cl",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)0,
    "\x10""\xC1",       (const char*)2,         (const char*)CheckAdcResultB,       "adc al,cl",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)1,
    "\xF6""\xD9",       (const char*)2,         (const char*)CheckNegResultB,       "neg cl",       (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)2,
    "\xF6""\xE1",       (const char*)2,         (const char*)CheckMulB,             "mul cl",       (const char*)(FLAG_O|FLAG_C),                                   (const char*)3,
    "\xF6""\xE9",       (const char*)2,         (const char*)CheckIMulB,            "imul cl",      (const char*)(FLAG_O|FLAG_C),                                   (const char*)3,
    "\xF6""\xF1",       (const char*)2,         (const char*)GuardCheckDivB,        "div cl",       (const char*)(0),                                               (const char*)6,
//    "\xF6""\xF9",       (const char*)2,         (const char*)GuardCheckIDivB,       "idiv cl",      (const char*)(0),                                               (const char*)6,
#endif    


    (const char*)1,     0,                      0,                                  0,              0,                                                              0,// switch to word based


    // Word Tests
#if 1
    "\x01""\xC1",       (const char*)2,         (const char*)CheckAddResultW,       "add cx,ax",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)0,
    "\x11""\xC1",       (const char*)2,         (const char*)CheckAdcResultW,       "adc cx,ax",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)1,
    "\xF7""\xD9",       (const char*)2,         (const char*)CheckNegResultW,       "neg cx",       (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)2,
    "\xF7""\xE1",       (const char*)2,         (const char*)CheckMulW,             "mul cx",       (const char*)(FLAG_O|FLAG_C),                                   (const char*)4,
    "\xF7""\xE9",       (const char*)2,         (const char*)CheckIMulW,            "imul cx",      (const char*)(FLAG_O|FLAG_C),                                   (const char*)4,
    "\xF7""\xF1",       (const char*)2,         (const char*)GuardCheckDivW,            "div cx",      (const char*)(0),                                   (const char*)5,
    //"\xF7""\xF9",       (const char*)2,         (const char*)GuardCheckIDivW,           "idiv cx",      (const char*)(0),                                   (const char*)5,
#endif
    0};

static int testPos=0;
static int testState=-1;
static int testCntr=0;
static int byteMode=1;

static long currentTestCnt;
static long currentTestCounter;

typedef int (*Validate0)(int,int,int,int);
typedef int (*Validate1)(int,int,int,int,int);
typedef int (*Validate2)(int,int,int);
typedef int (*Validate3)(int,int,int,int,int,int);

static int operandA,operandB,cIn,operandC;

static int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    if (testCntr>0)
    {
        testCntr--;
        return 0;
    }

    if (TICK_LIMIT)
    {
        if (ticks>TICK_LIMIT)
            return 1;
    }

    switch (testState)
    {
        case -1:
            // Fill RAM with values
            for (int a=0;a<0xFFFFF;a++)
            {
                RAM[a]=((a>>12)&0xF0)|(a&0xF);
            }
            testState++;
            break;
        case 0:
            if (testArray[testPos*TEST_MULT]==0)
            {
                testState=99;
            }
            else
            {
                if (testArray[testPos*TEST_MULT]==(const char*)1)
                {
                    byteMode=false;
                    testPos++;
                    break;
                }

                testState++;
                if (byteMode)
                {
                    if (testArray[testPos*TEST_MULT+5]==(const char*)2)
                        currentTestCnt=256;
                    else
                    {
                        if (testArray[testPos*TEST_MULT+5]==(const char*)6)
                            currentTestCnt=65536l*65536;
                        else
                            currentTestCnt=65536;
                    }
                    currentTestCounter=0;//30480285;
                }
                else
                {
                    if (testArray[testPos*TEST_MULT+5]==(const char*)2)
                        currentTestCnt=65536;
                    else
                        currentTestCnt = 65536l * 65536;
                    currentTestCounter=0;
                }

                if (testArray[testPos*TEST_MULT+5]==(const char*)1)
                    currentTestCnt*=2;  // need an extra bit for carry
                if (testArray[testPos*TEST_MULT+5]==(const char*)5)
                    currentTestCnt*=65536;
            }
            break;
        case 1:
            {
                tb->RESET=1;
                printf("Running test (%s) : %ld / %ld\r",testArray[testPos*TEST_MULT+3],currentTestCounter+1, currentTestCnt);
                int total = (int)(intptr_t)testArray[testPos*TEST_MULT+1];
                for (int a=0;a<total;a++)
                {
                    RAM[0xFFFF0+a]=testArray[testPos*TEST_MULT+0][a];
                }
                testState++;
                testCntr=16;
            }
            break;
        case 2:
            tb->RESET=0;
            if (tb->top->eu->executionState == 0x1FD)   // Instruction Fetch
            {
                if (byteMode)
                {
                    if (testArray[testPos*TEST_MULT+5]==(const char*)6)
                    {
                        operandA=currentTestCounter&0xFFFF;
                        operandB=(currentTestCounter>>16)&0xFF;
                        cIn=(currentTestCounter>>24)&1;
                    }
                    else
                    {
                        operandA=currentTestCounter&0xFF;
                        operandB=(currentTestCounter>>8)&0xFF;
                        cIn=(currentTestCounter>>16)&1;
                    }
                }
                else
                {
                    operandA=currentTestCounter&0xFFFF;
                    operandB=(currentTestCounter>>16)&0xFFFF;
                    if (testArray[testPos*TEST_MULT+5]==(const char*)5)
                    {
                        operandC=(currentTestCounter>>32)&0xFFFF;
                    }
                    else
                    {
                        cIn=(currentTestCounter>>32)&1;
                    }
                }
                if (testArray[testPos*TEST_MULT+5]==(const char*)2)
                    tb->top->eu->CX=operandA;
                else
                    tb->top->eu->CX=operandB;
                tb->top->eu->AX=operandA;
                tb->top->eu->DX=operandC;

                if (testArray[testPos*TEST_MULT+5]==(const char*)1)
                {
                    if (cIn)
                        tb->top->eu->FLAGS|=FLAG_C;
                }

                testState++;
            }
            break;
        case 3:
            if (tb->top->eu->executionState != 0x1FD)
            {
                testState++;
                tb->top->eu->TRACE_MODE=1;  // prevent further instruction execution
            }
            break;
        case 4:
            if (tb->top->eu->executionState == 0x1FD && tb->CLK==1 && (tb->top->eu->flush==0) && (tb->top->biu->suspending==0) && (tb->top->biu->indirectBusOpInProgress==0))
            {
                int ok = 0;
                switch ((int)(intptr_t)testArray[testPos*TEST_MULT+5])
                {
                    case 0:
                        ok = ((Validate0)testArray[testPos*TEST_MULT+2])(operandA,operandB,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->CX);
                        break;
                    case 1:
                        ok = ((Validate1)testArray[testPos*TEST_MULT+2])(operandA,operandB,cIn,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->CX);
                        break;
                    case 2:
                        ok = ((Validate2)testArray[testPos*TEST_MULT+2])(operandA,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->CX);
                        break;
                    case 3:
                    case 6:
                        ok = ((Validate0)testArray[testPos*TEST_MULT+2])(operandA,operandB,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->AX);
                        break;
                    case 4:
                        ok = ((Validate1)testArray[testPos*TEST_MULT+2])(operandA,operandB,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->AX,tb->top->eu->DX);
                        break;
// 5 = opAl,opAH,opB,flags,resulthi,resultlo  (DX,AX)
                    case 5:
                        ok = ((Validate3)testArray[testPos*TEST_MULT+2])(operandA,operandC,operandB,(int)(intptr_t)testArray[testPos*TEST_MULT+4],tb->top->eu->AX,tb->top->eu->DX);
                        break;
                }
                if (!ok)
                {
                    if (byteMode)
                    {
                        if (testArray[testPos*TEST_MULT+5]==(const char*)6)
                            printf("\nERROR %s %04X,%02X\n",testArray[testPos*TEST_MULT+3],operandA,operandB);
                        else
                            printf("\nERROR %s %02X,%02X\n",testArray[testPos*TEST_MULT+3],operandA,operandB);
                    }
                    else
                    {
                        if (testArray[testPos*TEST_MULT+5]==(const char*)5)
                            printf("\nERROR %s %04X%04X,%04X\n",testArray[testPos*TEST_MULT+3],operandC,operandA,operandB);
                        else
                            printf("\nERROR %s %04X,%04X\n",testArray[testPos*TEST_MULT+3],operandA,operandB);
                    }
                    exitStatus = EXIT_FAILURE;
                    testState=99;
                }
                else
                {
                    if (currentTestCnt<=65536)
                    {
                        currentTestCounter++;
                    }
                    else
                    {
                        if (testArray[testPos*TEST_MULT+5]==(const char*)5)
                            currentTestCounter+=0x00010007000D;
                        else
                            currentTestCounter+=0x01000D;
                    }
                    if (currentTestCounter>=currentTestCnt)
                    {
                        printf("\nAll Tests PASSED (%s)\n",testArray[testPos*TEST_MULT+3]);
                        testPos++;

                        testState=0;
                    }
                    else
                    {
                        testState=1;
                    }
                }
            }
            break;
        case 99:
            return 1;
    }

    return 0;
}


// Address FFFF0 - FFFFF encoded instruction

static void SimulateInterface(Vtop *tb)
{
    if (tb->RESET==1)
        return;
    if (tb->ALE)
    {
        uint32_t address = tb->A<<8;
        address|=tb->outAD;
        latchedAddress=address;
    }
    if (tb->RD_n==0)
    {
        tb->inAD = RAM[latchedAddress];
    }
}

static void tick(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    tb->CLK=ck;
    tb->CLKx4=1;
#if TRACE
    trace->dump(ticks*10-2);
#endif
    tb->eval();
#if TRACE
    trace->dump(ticks*10);
#endif
    tb->CLKx4=0;
    tb->eval();
#if TRACE
    trace->dump(ticks*10+5);
    trace->flush();
#endif

    internalClock--;
    if (internalClock==0)
    {
        internalClock=CLK_DIVISOR;
        ck=(~ck)&1;
    }

    SimulateInterface(tb);
    
}

static int doNTicks(Vtop *tb, VerilatedVcdC* trace, int ticks, int n)
{
    for (int a=0;a<n;a++)
    {
        tick(tb,trace,ticks+a);
    }
    return ticks+n;
}

int ArithTestsMain(int argc, char** argv)
{
#if TRACE
	Verilated::traceEverOn(true);
#endif

	tb = new Vtop;

	VerilatedVcdC *trace = new VerilatedVcdC;

#if TRACE
	tb->trace(trace, 99);
	trace->open("trace_arith.vcd");
#endif

    tb->RESET=1;
    tb->READY = 1;
    tb->NMI = 0;
    tb->INTR=0;
    tb->HOLD=0;
    tb->TEST_n=0;

    // Lets check the various signals according to spec, start of reset for a few ticks
	int ticks=1;
    ticks = doNTicks(tb,trace,ticks,100);

    tb->RESET=0;

    while (!Done(tb,trace,ticks))
    {
        ticks = doNTicks(tb,trace,ticks,1);
    }

#if TRACE
	trace->close();
#endif
    delete tb;

	return exitStatus;
}
