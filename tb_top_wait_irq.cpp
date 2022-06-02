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
        if (tb->IOM==1)
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
            if (tb->top->eu->executionState == 0x1FD && tb->CLK==1 && (tb->top->eu->flush==0) && (tb->top->biu->suspending==0) && (tb->top->biu->indirectBusOpInProgress==0) && (tb->top->biu->prefetchFull==1))
            {
                // At this point an instruction has completed.. (and the prefetch q is full)

                tb->top->eu->TRACE_MODE=0;
                testState++;
                return 1;
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
    testState=0;

    tb->RESET=0;

    return ticks;
}

int Tick1000(Vtop *tb, VerilatedVcdC* trace,int ticks)
{
    tickLimit=1000+ticks;
    while (!Done(tb,trace,ticks))
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
    ticks = Reset(tb,trace,ticks);
    tb->TEST_n=1;
    ROM[0xFFFF0]=0x9B;

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

/*
// interrupts during Checks 

    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP STOS (CX==0) 
    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP STOS (CX==5) 
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP LODS (CX==0)
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP LODS (CX==5)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP MOVS (CX==0)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP MOVS (CX==5)
    "10011011 ",                                                (const char*)ValidateWait,                      (const char*)RegisterNum,       (const char*)0,           // Wait

     HLT test
*/



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

end:
#if TRACE
	trace->close();
#endif
    delete tb;

	return returnCode;
}
