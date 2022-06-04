#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include "Vtop.h"
#include "Vtop_top.h"
#include "Vtop_execution.h"
#include "Vtop_bus_interface.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#define TRACE  0

#define SHOW_READS 0
#define SHOW_WRITES 0

#define CLK_DIVISOR 8

static int internalClock=CLK_DIVISOR;
static int ck=0;

static uint32_t latchedAddress;
static int lastWrite=1,lastRead=1;
extern Vtop *tb;

#define ROMSIZE 1024*1024
static unsigned char ROM[ROMSIZE];

#define SEGTOPHYS(seg,off)	( ((seg)<<4) + (off) )				// Convert Segment,offset pair to physical address

#define RAND_IO_SIZE 16
int IORand[RAND_IO_SIZE]={0x00,0x00,0xFF,0xFF,0x00,0x80,0xFF,0x7F,0x11,0x11,0x22,0x22,0x33,0x33,0x44,0x44};
int randomIOIdx=0;

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
    if (tb->RD_n==0 && lastRead==1)
    {
        if (tb->INTA_n == 0)
        {
            tb->inAD = 0x21;
#if SHOW_READS
            printf("Read Int Vector : %02X\n", tb->inAD);
#endif 

        }
        else if (tb->IOM==1)
        {
            tb->inAD = ROM[latchedAddress & (ROMSIZE-1)];
#if SHOW_READS
            printf("Read RAM : %08X -> %02X\n", latchedAddress, tb->inAD);
#endif 
        }
        else
        {
            tb->inAD = IORand[(randomIOIdx++)&(RAND_IO_SIZE-1)];
#if SHOW_READS
            printf("Read IO : %08X -> %02X\n", latchedAddress, tb->inAD);
#endif
        }
    }
    if (tb->WR_n==1 && lastWrite==0)    // Only log once per write
    {
        if (tb->IOM==1)
        {
            ROM[latchedAddress & (ROMSIZE-1)]=tb->outAD;
#if SHOW_WRITES
            printf("Write To Ram : %08X <- %02X\n",latchedAddress,tb->outAD);
#endif
        }
        else
        {
#if SHOW_WRITES
            printf("Write To IO : %08X <- %02X\n",latchedAddress,tb->outAD);
#endif
        }
    }
    lastWrite=tb->WR_n;
    lastRead=tb->RD_n;
}

static int tickLimit=-1;
static int testState=0;

static int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    if (tickLimit!=-1 && ticks>tickLimit)
        return 1;

    switch (testState)
    {
        case 0:
            if (tb->top->eu->executionState == 0x1FD)   // Wait For Instruction Fetch
            {
                testState++;
            }
            break;
        case 1:
            if (tb->top->eu->executionState != 0x1FD)   // Wait For Execute
            {
                testState++;
                tb->top->eu->TRACE_MODE=1;  // prevent further instruction execution
            }
            break;
        case 2:
            if (tb->top->eu->executionState == 0x1FD && tb->CLK==1 && (tb->top->eu->flush==0) && (tb->top->biu->suspending==0) && (tb->top->biu->indirectBusOpInProgress==0) && (tb->top->biu->prefetchEmpty==0))
            {
                // At this point an instruction has completed..

                testState++;
                return 2;
            }
            break;
    }

    return 0;
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

int Reset(Vtop *tb, VerilatedVcdC* trace,int ticks)
{
    tb->RESET=1;

    ticks = doNTicks(tb,trace,ticks,100);
    tb->RESET=0;
    tb->top->eu->TRACE_MODE=1;
    while (tb->top->eu->executionState!=0x1fd)
        ticks = doNTicks(tb,trace,ticks,100);
    testState=0;
    tb->top->eu->TRACE_MODE=0;


    return ticks;
}

int tickRes;

int Tick1000(Vtop *tb, VerilatedVcdC* trace,int ticks)
{
    tickLimit=1000+ticks;
    while (!(tickRes=Done(tb,trace,ticks)))
    {
        ticks = doNTicks(tb,trace,ticks,1);
    }
    // exits if an instruction was completed, or tick count runs out

    return ticks;
}

int TestWaitForTEST(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing WAIT instruction Vs TEST_n pin\n");
    // Test Wait
    ROM[0xFFFF0]=0x9B;
    ticks = Reset(tb,trace,ticks);
    tb->TEST_n=1;

    ticks = Tick1000(tb,trace,ticks);

    // Since TEST_n is not 0, we should not have finished an instruction
    if (testState==3)
    {
        printf("WAIT instruction completed while TEST pin was HIGH\n");
        return 0;
    }
    tb->TEST_n=0;

    ticks = Tick1000(tb,trace,ticks);
    // Since TEST_n is 0, we should have finished an instruction
    if (testState!=3)
    {
        printf("WAIT instruction did not complete after TEST pin was made LOW\n");
        return 0;
    }

    printf("OK\n");
    return 1;
}

void SetupInterruptVector()
{
    ROM[0x00084]=0x00;
    ROM[0x00085]=0x00;
    ROM[0x00086]=0x00;
    ROM[0x00087]=0x00;
    ROM[0x00000]=0xCF;  // IRET
}

int CheckIRQ(Vtop *tb, VerilatedVcdC* trace,int& ticks,const char* name,int resumes)
{
    ticks = Tick1000(tb,trace,ticks);

    // Since interrupt is not pending, we should not have finished an instruction
    if (testState==3)
    {
        printf("%s instruction completed while no IRQ pending\n", name);
        return 0;
    }

    tb->INTR=1;

    ticks = Tick1000(tb,trace,ticks);

    // Since IRQ masked, we should not have finished an instruction
    if (testState==3)
    {
        printf("%s instruction completed while masked IRQ pending\n",name);
        return 0;
    }

    tb->top->eu->FLAGS|=0x200;

    ticks = Tick1000(tb,trace,ticks);

    // The interrupt should have caused the test instruction to exit
    if (testState!=3)
    {
        printf("%s instruction did not complete after INT pin was made HIGH\n",name);
        return 0;
    }

    if (resumes)
    {
        // At this point, we are going to interrupt, PC should point to FFFF0 still
        if (tb->top->biu->prefetchTopLinearAddress != 0xFFFF0)
        {
            printf("%s instruction not ready to resume post interrupt!\n",name);
            return 0;
        }
    }

    testState=0;
    tb->top->eu->TRACE_MODE=0;
    // Give time for interrupt handling
    ticks = Tick1000(tb,trace,ticks);
    ticks = Tick1000(tb,trace,ticks);
    ticks = Tick1000(tb,trace,ticks);

    tb->INTR=0;

    if (testState!=3)
    {
        printf("%s instruction interrupt not complete in time\n",name);
    }

    if (tb->top->biu->prefetchTopLinearAddress != 0x0)
    {
        printf("%s instruction not ready to execute IRET!\n",name);
        return 0;
    }

    testState=0;
    tb->top->eu->TRACE_MODE=0;
    ticks = Tick1000(tb,trace,ticks);

    if (testState!=3)
    {
        printf("%s instruction IRET did not complete correctly\n",name);
    }

    if (resumes)
    {
        if (tb->top->biu->prefetchTopLinearAddress != 0xFFFF0)
        {
            printf("%s instruction not ready to execute after interrupt!\n",name);
            return 0;
        }
    }

    return 1;
}

int TestWaitInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing WAIT instruction Vs Interrupt\n");
    // Test Wait
    ROM[0xFFFF0]=0x9B;
    ticks = Reset(tb,trace,ticks);
    tb->TEST_n=1;
    SetupInterruptVector();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"WAIT",1))
        return 0;

    printf("OK\n");
    return 1;
}

int TestHalt(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing HALT instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF4;
    ticks = Reset(tb,trace,ticks);
    SetupInterruptVector();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"HALT", 0))
        return 0;
        
    if (tb->top->biu->prefetchTopLinearAddress != 0xFFFF1)
    {
        printf("next address to execute after HALT is not correct\n");
        return 0;
    }

    printf("OK\n");
    return 1;
}

void SetupForRepMemory()
{
    tb->top->eu->CX=0xFFFF;
    tb->top->biu->REGISTER_DS=0x4000;
    tb->top->biu->REGISTER_ES=0x5000;
    tb->top->eu->DI=0;
    tb->top->eu->SI=0;
}

int TestSTOSInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing STOS instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF3;  // REP
    ROM[0xFFFF1]=0xAA;  // STOSB
    ticks = Reset(tb,trace,ticks);
    SetupInterruptVector();
    SetupForRepMemory();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"STOS", 1))
        return 0;

    printf("OK\n");
    return 1;

}

int TestLODSInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing LODS instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF3;  // REP
    ROM[0xFFFF1]=0xAC;  // LODSB
    ticks = Reset(tb,trace,ticks);
    SetupInterruptVector();
    SetupForRepMemory();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"LODS", 1))
        return 0;

    printf("OK\n");
    return 1;

}

int TestMOVSInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing MOVS instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF3;  // REP
    ROM[0xFFFF1]=0xA4;  // MOVSB
    ticks = Reset(tb,trace,ticks);
    SetupInterruptVector();
    SetupForRepMemory();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"MOVS", 1))
        return 0;

    printf("OK\n");
    return 1;

}

int TestCMPSInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing CMPS instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF3;  // REPE (memory contains 00s, so this will always repeat until CX==0)
    ROM[0xFFFF1]=0xA6;  // CMPSB
    ticks = Reset(tb,trace,ticks);
    SetupInterruptVector();
    SetupForRepMemory();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"CMPS", 1))
        return 0;

    printf("OK\n");
    return 1;

}

int TestSCASInterrupted(Vtop *tb, VerilatedVcdC* trace,int& ticks)
{
    printf("Testing SCAS instruction Vs Interrupt\n");
    // Test Halt
    ROM[0xFFFF0]=0xF3;  // REPE (memory contains 00s, so this will always repeat until CX==0)
    ROM[0xFFFF1]=0xAE;  // SCASB
    ticks = Reset(tb,trace,ticks);
    tb->top->eu->AX=0x00;
    SetupInterruptVector();
    SetupForRepMemory();
    tb->top->eu->FLAGS=0;   // ensure interrupts masked

    if (!CheckIRQ(tb,trace,ticks,"SCAS", 1))
        return 0;

    printf("OK\n");
    return 1;

}


int IrqWaitTestsMain(int argc, char** argv)
{
    int returnCode=EXIT_SUCCESS;

#if TRACE
	Verilated::traceEverOn(true);
#endif

	tb = new Vtop;

	VerilatedVcdC *trace = new VerilatedVcdC;

#if TRACE
	tb->trace(trace, 99);
	trace->open("trace_wait_irq.vcd");
#endif

    tb->READY = 1;
    tb->NMI = 0;
    tb->INTR=0;
    tb->HOLD=0;
    tb->TEST_n=0;

    int ticks=1;
    
    if (!TestWaitForTEST(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }
    
    if (!TestWaitInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }
    
    if (!TestHalt(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

    if (!TestSTOSInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

    if (!TestLODSInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

    if (!TestMOVSInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

    if (!TestCMPSInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

    if (!TestSCASInterrupted(tb,trace,ticks))
    {
        returnCode=EXIT_FAILURE;
        goto end;
    }

end:
#if TRACE
	trace->close();
#endif
    delete tb;

	return returnCode;
}
