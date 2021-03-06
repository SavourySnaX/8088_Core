#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include "Vtop.h"
#include "Vtop_top.h"
#include "Vtop_execution.h"
#include "Vtop_bus_interface.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include "tb_top_wait_irq.h"
#include "tb_top_arith_tests.h"

#include "testing_x86.h"

#define TRACE  0

#define IRQ_WAIT_TESTS 1
#define ARITH_TESTS 1
#define UNIT_TESTS 1
#define ALL_TESTS 1

#define CLK_DIVISOR 8

#define SHOW_WRITES 0
#define SHOW_READS 0

#define TICK_LIMIT  0 && 300000

int internalClock=CLK_DIVISOR;
int ck=0;

int readWriteFailure;

uint32_t latchedAddress;
int lastWrite=1,lastRead=1;
Vtop *tb;

#define RAMSIZE 1024*1024
unsigned char RAM[RAMSIZE];

#define MAX_READWRITE_CAPTURE   100

int segOverride=3;

int captureIdx=0;
uint32_t readWriteLatchedAddress[MAX_READWRITE_CAPTURE];
int readWriteLatchedType[MAX_READWRITE_CAPTURE];
uint8_t  lastWriteCapture[MAX_READWRITE_CAPTURE];
uint8_t  lastReadCapture[MAX_READWRITE_CAPTURE];

int NumMatch(const char* testData, char code)
{
    int num=0;
    while (*testData!=0)
    {
        if (*testData == code)
            num++;
        testData++;
    }

    return num;
}


const uint8_t LHTable[16]={0x00,0x01,0xFF,0x7F,0x80,0xE7,0xEF,0xF7,0x02,0x04,0x08,0x10,0x20,0x40,0xAA,0x99};
const uint8_t lhTable[4]={0x00,0xFF,0x7F,0x80};

int FetchTestNumBitsForSpecial(char code)
{
    switch (code)
    {
        default:
            return 0;
        case 'l':
        case 'h':
            return 2;
        case 'L':
        case 'H':
            return 4;
    }
}


int FetchTestBitCountForCode(char code,int numBits)
{
    switch (code)
    {
        default:
            return numBits;
        case 'l':
        case 'h':
        case 'L':
        case 'H':
            return FetchTestNumBitsForSpecial(code);
    }
}

int RealValueFromIndex(char code, int idx)
{
    switch (code)
    {
        default:
            return idx;
        case 'l':
        case 'h':
            return lhTable[idx];
        case 'L':
        case 'H':
            return LHTable[idx];
    }

}

int Extract(const char* testData, char code, int counter, int testCnt)
{
    int value=0;
    int numCodeBits = NumMatch(testData, code);
    int numSpecialBits = FetchTestNumBitsForSpecial(code);
    int matchCounter = numSpecialBits;
    int romOffs=0;
    int mask=testCnt>>1;

    if (matchCounter==0)
        matchCounter = numCodeBits;

    while (*testData!=0)
    {
        if (*testData == '0' || *testData == ' ' || *testData == '1')
        {
            //printf("Skipping fixed position %c\n", *testData);
        }
        else 
        {
            if (*testData == code)
            {
                // extract current code index from bits. codes must be consecutive
                //printf("Match : %c, %d, %08X\n", code,counter,mask);
                for (int a=0;a<matchCounter;a++)
                {
                    value<<=1;
                    value|=counter&mask?1:0;
                    mask>>=1;
                }
                //printf("Value : %08X\n", value);
                break;
            }
            else
            {
                char code = *testData;
                int numCodeBits = NumMatch(testData, code);
                //printf("Code Bits : %d\n", numCodeBits);
                int numSpecialBits = FetchTestNumBitsForSpecial(code);
                //printf("Special Bits : %d\n", numSpecialBits);
                int numBits = numSpecialBits;
                if (numBits==0)
                    numBits=numCodeBits;
            
                mask>>=numBits;
                testData+=numCodeBits-1;
                //printf("No Match (%c) %d,%08X\n", code, counter, mask);
            }
        }
        testData++;
    }

    // value is now known, if numSpecialBits !=0 then look up value in table using current value as index, else return value

    if (numSpecialBits!=0)
    {
        //printf("Idx : %08X\n", value);
        value=RealValueFromIndex(code, value);
    }

    //printf("Value : %08X\n", value);
    return value;
}

int FetchByteRegister(int registerNumber)
{
    switch (registerNumber)
    {
        case 0: // AL
            return tb->top->eu->AX&0xFF;
            break;
        case 1: // CL
            return tb->top->eu->CX&0xFF;
            break;
        case 2: // DL
            return tb->top->eu->DX&0xFF;
            break;
        case 3: // BL
            return tb->top->eu->BX&0xFF;
            break;
        case 4: // AH
            return tb->top->eu->AX>>8;
            break;
        case 5: // CH
            return tb->top->eu->CX>>8;
            break;
        case 6: // DH
            return tb->top->eu->DX>>8;
            break;
        case 7: // BH
            return tb->top->eu->BX>>8;
            break;
        default:
            printf("Bad Register Index : %d\n", registerNumber);
            exit(1);
    }
}

int FetchWordRegister(int registerNumber)
{
    switch (registerNumber)
    {
        case 0: // AX
            return tb->top->eu->AX;
            break;
        case 1: // CX
            return tb->top->eu->CX;
            break;
        case 2: // DX
            return tb->top->eu->DX;
            break;
        case 3: // BX
            return tb->top->eu->BX;
            break;
        case 4: // SP
            return tb->top->eu->SP;
            break;
        case 5: // BP
            return tb->top->eu->BP;
            break;
        case 6: // SI
            return tb->top->eu->SI;
            break;
        case 7: // DI
            return tb->top->eu->DI;
            break;
        default:
            printf("Bad Register Index : %d\n", registerNumber);
            exit(1);
    }
}

int FetchSR(int registerNumber)
{
    switch (registerNumber)
    {
        case 0: // ES
            return tb->top->biu->REGISTER_ES;
            break;
        case 1: // CS
            return tb->top->biu->REGISTER_CS;
            break;
        case 2: // SS
            return tb->top->biu->REGISTER_SS;
            break;
        case 3: // DS
            return tb->top->biu->REGISTER_DS;
            break;
        default:
            printf("Bad SRegister Index : %d\n", registerNumber);
            exit(1);
    }
}


int ByteRegisterCheck(int registerNumber, int immediateValue)
{
    int regValue=FetchByteRegister(registerNumber);
    return (regValue&0xFF) == (immediateValue&0xFF);
}

int WordRegisterCheck(int registerNumber, int immediateValue)
{
    int regValue=FetchWordRegister(registerNumber);
    return (regValue&0xFFFF) == (immediateValue&0xFFFF);
}


// Validation called once execuction execState == 1FD 

void DefaultTestInit(int regInitVal)
{
    tb->top->eu->FLAGS=0;
    tb->top->eu->AX=regInitVal;
    tb->top->eu->CX=regInitVal;
    tb->top->eu->DX=regInitVal;
    tb->top->eu->BX=regInitVal;
    tb->top->eu->SP=regInitVal;
    tb->top->eu->BP=regInitVal;
    tb->top->eu->SI=regInitVal;
    tb->top->eu->DI=regInitVal;
    tb->top->biu->REGISTER_DS=0x1000;
    tb->top->biu->REGISTER_ES=0x3000;
    tb->top->biu->REGISTER_SS=0x5000;
}

int FetchInitialSR(int reg)
{
    switch (reg)
    {
        case 0: // ES
            return 0x3000;
            break;
        case 1: // CS
            return 0xFFFF;
            break;
        case 2: // SS
            return 0x5000;
            break;
        case 3: // DS
            return 0x1000;
            break;
        default:
            printf("Bad SRegister Index : %d\n", reg);
            exit(1);
    }
}

int initialRegisters[8];

void ResetRegisterNumInitial()
{
    initialRegisters[0]=0x0981;
    initialRegisters[1]=0x1658;
    initialRegisters[2]=0x2323;
    initialRegisters[3]=0x3124;
    initialRegisters[4]=0x4450;
    initialRegisters[5]=0x5782;
    initialRegisters[6]=0x6665;
    initialRegisters[7]=0x7776;
}

void ResetRegisterNumInitial2()
{
    initialRegisters[0]=0x8981;
    initialRegisters[1]=0x9658;
    initialRegisters[2]=0xA323;
    initialRegisters[3]=0xB124;
    initialRegisters[4]=0xC450;
    initialRegisters[5]=0xD782;
    initialRegisters[6]=0xE665;
    initialRegisters[7]=0xF776;
}

void ResetRegisterNumInitial3()
{
    initialRegisters[0]=20;
    initialRegisters[1]=0;
    initialRegisters[2]=1;
    initialRegisters[3]=2;
    initialRegisters[4]=3;
    initialRegisters[5]=4;
    initialRegisters[6]=5;
    initialRegisters[7]=6;
}

int RegisterNumInitial(int regNum)
{
    if (regNum>=0 && regNum<8)
        return initialRegisters[regNum];
    return 0xFFFFFFFF;
}

int RegisterNumInitialWord(int regNum)
{
    return RegisterNumInitial(regNum)&0xFFFF;
}

int RegisterNumInitialByte(int regNum)
{
    int v = RegisterNumInitial(regNum&3);
    if (regNum&0x4)
        v>>=8;
    return v&0xFF;
}

void LoadRegisters()
{
    tb->top->eu->FLAGS=0;
    tb->top->eu->AX=RegisterNumInitial(0);
    tb->top->eu->CX=RegisterNumInitial(1);
    tb->top->eu->DX=RegisterNumInitial(2);
    tb->top->eu->BX=RegisterNumInitial(3);
    tb->top->eu->SP=RegisterNumInitial(4);
    tb->top->eu->BP=RegisterNumInitial(5);
    tb->top->eu->SI=RegisterNumInitial(6);
    tb->top->eu->DI=RegisterNumInitial(7);
    tb->top->biu->REGISTER_DS=0x1000;
    tb->top->biu->REGISTER_ES=0x3000;
    tb->top->biu->REGISTER_SS=0x5000;
}

void RegisterNum(int regInitVal)
{
    ResetRegisterNumInitial();
    LoadRegisters();
}

void RegisterNum2(int regInitVal)
{
    ResetRegisterNumInitial2();
    LoadRegisters();
}

void RegisterNum3(int regInitVal)
{
    ResetRegisterNumInitial3();
    LoadRegisters();
}

void CXFlags(int regInitVal)
{
    tb->top->eu->CX=regInitVal>>16;
    tb->top->eu->FLAGS=regInitVal&0xFFFF;
}

void RegisterNumAX(int regInitVal)
{
    RegisterNum(regInitVal);
    tb->top->eu->AX=regInitVal;
    initialRegisters[ERegisterNum::AX]=regInitVal;
}

void RegisterNumCX(int regInitVal)
{
    RegisterNum(regInitVal);
    tb->top->eu->CX=regInitVal;
    initialRegisters[ERegisterNum::CX]=regInitVal;
}

void RegisterNumSP(int regInitVal)
{
    RegisterNum(regInitVal);
    tb->top->eu->SP=regInitVal;
    initialRegisters[ERegisterNum::SP]=regInitVal;
}

void RegisterNumCXCarry(int regInitVal)
{
    RegisterNumCX(regInitVal);
    tb->top->eu->FLAGS=FLAG_C;
}

void RegisterNumFlags(int regInitVal)
{
    RegisterNum(regInitVal);
    tb->top->eu->FLAGS=regInitVal;
}

void SetFlags(int initFlags)
{
    tb->top->eu->FLAGS|=initFlags;
}

void ClearFlags(int initFlags)
{
    tb->top->eu->FLAGS&=~initFlags;
}

int ValidateMovRImmediateByte(const char* testData, int counter, int testCnt, int regInitVal)
{
    int registerNumber = Extract(testData,'R', counter, testCnt);
    //printf("Register : %02X\n", registerNumber);
    int immediateValue = Extract(testData,'L', counter, testCnt);
    //printf("Immediate : %02X\n", immediateValue);
    return ByteRegisterCheck(registerNumber,immediateValue);
}

int ValidateMovRImmediateWord(const char* testData, int counter, int testCnt, int regInitVal)
{
    int registerNumber = Extract(testData,'R', counter, testCnt);
    int immediateValueL = Extract(testData,'L', counter, testCnt);
    int immediateValueH = Extract(testData,'H', counter, testCnt);
    //printf("register : %02X\n", registerNumber);
    //printf("ImmediateL : %02X\n", immediateValueL);
    //printf("ImmediateH : %02X\n", immediateValueH);
    return WordRegisterCheck(registerNumber,(immediateValueH<<8)|immediateValueL);
}

int ValidateIncWordRegister(const char* testData, int counter, int testCnt, int regInitVal)
{
    int registerNumber = Extract(testData,'R', counter, testCnt);
    return CheckAddFlagsW(regInitVal,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P) && WordRegisterCheck(registerNumber,regInitVal+1);
}

int ValidateDecWordRegister(const char* testData, int counter, int testCnt, int regInitVal)
{
    int registerNumber = Extract(testData,'R', counter, testCnt);
    return CheckSubFlagsW(regInitVal,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P) && WordRegisterCheck(registerNumber,regInitVal-1);
}

int SignExt8Bit(int v)
{
    v&=0xFF;
    v|=(v&0x80)?0xFFFFFF00:0;
    return v;
}

int ValidateJmpRel8(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = SignExt8Bit(Extract(testData,'l', counter, testCnt));
    int ip = tb->top->biu->REGISTER_IP;
    return (ip) == ((0x0002 + immediateValueL)&0xFFFF);
}

int ValidateJmpRel16(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = Extract(testData,'l', counter, testCnt);
    int immediateValueH = Extract(testData,'h', counter, testCnt);
    int ip = (tb->top->biu->REGISTER_IP)&0xFFFF;
    return (ip) == ((0x0003 + ((immediateValueH<<8)|immediateValueL))&0xFFFF);
}

int ValidateJmpInterSeg(const char* testData, int counter, int testCnt, int regInitVal)
{
    int offsL = Extract(testData,'l', counter, testCnt);
    int offsH = Extract(testData,'h', counter, testCnt);
    int segL = Extract(testData,'L', counter, testCnt);
    int segH = Extract(testData,'H', counter, testCnt);
    int ip = (tb->top->biu->REGISTER_IP)&0xFFFF;
    int cs = (tb->top->biu->REGISTER_CS)&0xFFFF;
    return (ip == (offsH<<8)|offsL) && (cs == (segH<<8)|segL);
}

int ValidateLoop(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = SignExt8Bit(Extract(testData,'l', counter, testCnt));
    int ip = (tb->top->biu->REGISTER_IP)&0xFFFF;
    int expected=(tb->top->eu->CX==0)?2:(2+immediateValueL)&0xFFFF;
    if (tb->top->eu->CX != ((regInitVal-1)&0xFFFF))
    {
        printf("Failed - CX != initial CX -1   (%04X)!=(%04X)\n", tb->top->eu->CX,(regInitVal-1)&0xFFFF);
        return 0;
    }
    if (ip!=expected)
    {
        printf("Failed - IP != Expected IP     (%04X)!=(%04X)\n", ip,expected);
        return 0;
    }
    return 1;
}

int ValidateOutA(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int immediateValueL = Extract(testData,'L', counter, testCnt);

    if (readWriteLatchedAddress[captureIdx]!=immediateValueL)
    {
        printf("Failed - first Address Written != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],immediateValueL);
        return 0;
    }
    if (readWriteLatchedType[captureIdx]!=0)
    {
        printf("Failed - first Address Written != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
        return 0;
    }
    if (lastWriteCapture[captureIdx]!=(regInitVal&0xFF))
    {
        printf("Failed - first Byte Written != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],regInitVal&0xFF);
        return 0;
    }
    captureIdx++;
    if (word)
    {
        if (readWriteLatchedAddress[captureIdx]!=immediateValueL+1)
        {
            printf("Failed - second Address Written != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],immediateValueL+1);
            return 0;
        }
        if (readWriteLatchedType[captureIdx]!=0)
        {
            printf("Failed - second Address Written != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
            return 0;
        }
        if (lastWriteCapture[captureIdx]!=(regInitVal>>8))
        {
            printf("Failed - second Byte Written != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],regInitVal>>8);
            return 0;
        }
        captureIdx++;
    }

    return 1;
}

int ValidateInA(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int immediateValueL = Extract(testData,'L', counter, testCnt);

    if (readWriteLatchedAddress[captureIdx]!=immediateValueL)
    {
        printf("Failed - first Address Read != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],immediateValueL);
        return 0;
    }
    if (readWriteLatchedType[captureIdx]!=0)
    {
        printf("Failed - first Address Read != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
        return 0;
    }
    if (lastReadCapture[captureIdx]!=(tb->top->eu->AX&0xFF))
    {
        printf("Failed - first Byte Read != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],(tb->top->eu->AX&0xFF));
        return 0;
    }
    captureIdx++;
    if (word)
    {
        if (readWriteLatchedAddress[captureIdx]!=immediateValueL+1)
        {
            printf("Failed - second Address Read != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],immediateValueL+1);
            return 0;
        }
        if (readWriteLatchedType[captureIdx]!=0)
        {
            printf("Failed - second Address Read != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
            return 0;
        }
        if (lastReadCapture[captureIdx]!=(tb->top->eu->AX>>8))
        {
            printf("Failed - second Byte Read != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],(tb->top->eu->AX>>8));
            return 0;
        }
        captureIdx++;
    }

    return 1;
}


int ValidateFlagClear(const char* testData, int counter, int testCnt, int regInitVal)
{
    return (tb->top->eu->FLAGS & regInitVal)==0;
}

int ValidateFlagSet(const char* testData, int counter, int testCnt, int regInitVal)
{
    return (tb->top->eu->FLAGS & regInitVal)==regInitVal;
}

void ComputeEffectiveAddressMOD00(int rm,int dispL, int dispH, int* seg, int* off)
{
    *seg = FetchInitialSR(segOverride);
    switch (rm)
    {
        case 0:
            *off = (RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::SI));
            return;
        case 1:
            *off = (RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::DI));
            return;
        case 2:
            *off = (RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::SI));
            return;
        case 3:
            *off = (RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::DI));
            return;
        case 4:
            *off = (RegisterNumInitialWord(ERegisterNum::SI));
            return;
        case 5:
            *off = (RegisterNumInitialWord(ERegisterNum::DI));
            return;
        case 6:
            *off = ((dispH<<8)|dispL);
            return;
        case 7:
            *off = (RegisterNumInitialWord(ERegisterNum::BX));
            return;
    }
    *off = 0xFFFFFFFF;
    *seg = 0xFFFFFFFF;
}

int ComputeEffectiveAddressMODoffs(int rm,int imm)
{
    switch (rm)
    {
        case 0:
            return ((RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::SI)+imm)&0xFFFF);
        case 1:
            return ((RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::DI)+imm)&0xFFFF);
        case 2:
            return ((RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::SI)+imm)&0xFFFF);
        case 3:
            return ((RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::DI)+imm)&0xFFFF);
        case 4:
            return ((RegisterNumInitialWord(ERegisterNum::SI)+imm)&0xFFFF);
        case 5:
            return ((RegisterNumInitialWord(ERegisterNum::DI)+imm)&0xFFFF);
        case 6:
            return ((RegisterNumInitialWord(ERegisterNum::BP)+imm)&0xFFFF);
        case 7:
            return ((RegisterNumInitialWord(ERegisterNum::BX)+imm)&0xFFFF);
    }
    return 0xFFFFFFFF;
}

void ComputeEffectiveAddressMOD01(int rm,int dispL, int dispH, int* seg, int* off)
{
    *seg = FetchInitialSR(segOverride);
    int imm=SignExt8Bit(dispL);
    *off = ComputeEffectiveAddressMODoffs(rm,imm);
}

void ComputeEffectiveAddressMOD10(int rm,int dispL, int dispH, int* seg, int* off)
{
    *seg = FetchInitialSR(segOverride);
    int imm =((dispH<<8)|dispL)&0xFFFF;
    *off = ComputeEffectiveAddressMODoffs(rm,imm);
}

void ComputeEffectiveAddress(int mod,int rm,int dispL, int dispH, int* seg,int* off)
{
    if (mod==0)
    {
        ComputeEffectiveAddressMOD00(rm, dispL, dispH, seg, off);
        return;
    }
    if (mod==1)
    {
        ComputeEffectiveAddressMOD01(rm, dispL, dispH, seg, off);
        return;
    }
    if (mod==2)
    {
        ComputeEffectiveAddressMOD10(rm, dispL, dispH, seg, off);
        return;
    }
    *seg=0xFFFFFFFF;
    *off=0xFFFFFFFF;
}

int FetchWrittenMemory(int word, int seg, int offset)
{
    int firstAddress=(seg*16 + (offset&0xFFFF))&0xFFFFF;
    int secondAddress=(seg*16 + ((offset+1)&0xFFFF))&0xFFFFF;

    if (firstAddress != readWriteLatchedAddress[captureIdx])
    {
        printf("Failed - First Address Mismatch : %05X!=%05X", firstAddress, readWriteLatchedAddress[captureIdx]);
        readWriteFailure=1;
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        readWriteFailure=1;
        return -1;
    }
    if (word)
    {
        if (secondAddress != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", secondAddress, readWriteLatchedAddress[captureIdx+1]);
            readWriteFailure=1;
            return -1;
        }
        if (readWriteLatchedType[captureIdx+1]!=1)
        {
            printf("Failed - Second Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx+1]);
            readWriteFailure=1;
            return -1;
        }
    }
    if (word)
    {
        int r = (lastWriteCapture[captureIdx+1]<<8)|(lastWriteCapture[captureIdx]);
        captureIdx+=2;
        return r;
    }

    return lastWriteCapture[captureIdx++];
}

int FetchDestValueMemory(int word, int mod, int rm, int dispL, int dispH)
{
    int seg,off;
    ComputeEffectiveAddress(mod,rm,dispL,dispH, &seg, &off);
    return FetchWrittenMemory(word, seg, off);
}

int FetchReadMemory(int word, int seg, int offset)
{
    int firstAddress=(seg*16 + (offset&0xFFFF))&0xFFFFF;
    int secondAddress=(seg*16 + ((offset+1)&0xFFFF))&0xFFFFF;

    if (firstAddress != readWriteLatchedAddress[captureIdx])
    {
        printf("Failed - First Address Mismatch : %05X!=%05X", firstAddress, readWriteLatchedAddress[captureIdx]);
        readWriteFailure=1;
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        readWriteFailure=1;
        return -1;
    }
    if (word)
    {
        if (secondAddress != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", secondAddress, readWriteLatchedAddress[captureIdx+1]);
            readWriteFailure=1;
            return -1;
        }
        if (readWriteLatchedType[captureIdx+1]!=1)
        {
            printf("Failed - Second Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx+1]);
            readWriteFailure=1;
            return -1;
        }
    }
    if (word)
    {
        int r = (lastReadCapture[captureIdx+1]<<8)|(lastReadCapture[captureIdx]);
        captureIdx+=2;
        return r;
    }

    return lastReadCapture[captureIdx++];
}

int FetchSourceValueMemory(int word, int mod, int rm, int dispL, int dispH)
{
    int seg,off;
    ComputeEffectiveAddress(mod,rm,dispL,dispH, &seg, &off);
    return FetchReadMemory(word,seg,off);
}


int FetchDestValue(int direction,int word, int mod, int reg, int rm, int dispL, int dispH)
{
    int bits=reg;
    if (direction == 0)
        bits=rm;
    else
        mod=3;  // always register

    if (mod==3)
    {
        if (word)
            return FetchWordRegister(bits);
        return FetchByteRegister(bits);
    }

    return FetchDestValueMemory(word,mod,bits,dispL,dispH);
}

int FetchModRMLength(int direction, int word, int mod, int reg, int rm)
{
    if (direction == 0)
        mod=3;

    if (mod==1)
        return 2;
    if (mod==2)
        return 3;
    if (mod==0 && rm==6)
        return 3;
    return 1;
}

int FetchSourceValue(int direction,int word, int mod, int reg, int rm, int dispL, int dispH)
{
    int bits=rm;
    if (direction == 0)
    {
        bits=reg;
        mod=3;  // always register
    }

    if (mod==3)
    {
        if (word)
            return RegisterNumInitialWord(bits);
        return RegisterNumInitialByte(bits);
    }
    
    return FetchSourceValueMemory(word,mod,bits,dispL,dispH);
}

int FetchDestRM(int word, int mod, int rm, int dispL, int dispH)
{
    if (mod==3)
    {
        if (word)
            return FetchWordRegister(rm);
        return FetchByteRegister(rm);
    }

    return FetchDestValueMemory(word,mod,rm,dispL,dispH);
}

int FetchSourceRM(int word, int mod, int rm, int dispL, int dispH)
{
    if (mod==3)
    {
        if (word)
            return RegisterNumInitialWord(rm);
        return RegisterNumInitialByte(rm);
    }
    
    return FetchSourceValueMemory(word,mod,rm,dispL,dispH);
}


int ValidateMovMRMReg(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = Extract(testData,'D',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int hValue = FetchDestValue(direction,word,mod,reg,RM,dispL,dispH);
    int tValue = FetchSourceValue(direction,word,mod,reg,RM,dispL,dispH);

    return hValue==tValue;
}

int ValidateMovRMSR(const char* testData, int counter, int testCnt, int regInitVal)
{
    int direction = Extract(testData,'D',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int hValue,tValue;

    if (direction==0)
    {
        //source reg is SR
        //dest reg is R/M
        hValue=FetchDestRM(1,mod,RM,dispL,dispH);
        tValue=FetchInitialSR(reg);
    }
    if (direction==1)
    {
        //dest reg is SR
        //source is R/M
        hValue=FetchSR(reg);
        tValue=FetchSourceRM(1,mod,RM,dispL,dispH);
    }

    return hValue==tValue;
}

int ValidateMovMRMRegPrefixed(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = 0;
    int direction = 0;
    int mod = 0;
    int reg = 0;
    int RM = 6;
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    segOverride=Extract(testData,'R',counter,testCnt);

    int hValue = FetchDestValue(direction,word,mod,reg,RM,dispL,dispH);
    int tValue = FetchSourceValue(direction,word,mod,reg,RM,dispL,dispH);

    return hValue==tValue;
}

int ValidateMovRMImmediate(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = 0;
    int mod = regInitVal;//Extract(testData,'M',counter,testCnt);   // Tested like this, as length of opcode varies by MM
    int reg = 0;//Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);
    int immL = Extract(testData,'L', counter, testCnt);
    int immH = Extract(testData,'H', counter, testCnt);

    int hValue = FetchDestValue(direction,word,mod,reg,RM,dispL,dispH);

    if (word)
    {
        int tValue = ((immH<<8)|immL);
        return hValue == tValue;
    }

    return hValue==immL;
}

int CheckALUOp(int word, int aluOp, int carryIn, int A, int B, int result)
{
    int res;
    if (word)
    {
        switch (aluOp)
        {
            case 0:
                return CheckAddResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 1:
                return CheckOrResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 2:
                return CheckAdcResultW(A,B,carryIn, FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 3:
                return CheckSbbResultW(A,B,carryIn, FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 4:
                return CheckAndResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 5:
                return CheckSubResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 6:
                return CheckXorResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 7:
                return CheckCmpResultW(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            default:
                break;
        }
    }
    else
    {
        switch (aluOp)
        {
            case 0:
                return CheckAddResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 1:
                return CheckOrResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 2:
                return CheckAdcResultB(A,B,carryIn, FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 3:
                return CheckSbbResultB(A,B,carryIn, FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 4:
                return CheckAndResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 5:
                return CheckSubResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            case 6:
                return CheckXorResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);        // don't validate U flags
            case 7:
                return CheckCmpResultB(A,B,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
            default:
                break;
        }
    }
    return 0;
}

int CheckShifterOp(int word, int aluOp, int carryIn, int A, int result)
{
    int res;
    if (word)
    {
        switch (aluOp)
        {
            case 0:
                return CheckRolResultW(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 1:
                return CheckRorResultW(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 2:
                return CheckRclResultW(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 3:
                return CheckRcrResultW(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 4:
            case 6:
                return CheckShlResultW(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            case 5:
                return CheckShrResultW(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            case 7:
                return CheckSarResultW(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            default:
                break;
        }
    }
    else
    {
        switch (aluOp)
        {
            case 0:
                return CheckRolResultB(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 1:
                return CheckRorResultB(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 2:
                return CheckRclResultB(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 3:
                return CheckRcrResultB(A,carryIn,FLAG_O|FLAG_C,result);     // Only O & C for rotates
            case 4:
            case 6:
                return CheckShlResultB(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            case 5:
                return CheckShrResultB(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            case 7:
                return CheckSarResultB(A,carryIn,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,result);     // don't validate U flags
            default:
                break;
        }
    }
    return 0;
}

int ValidateAluAImmediate(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int alu = Extract(testData,'A',counter,testCnt);
    int immL = Extract(testData,'L', counter, testCnt);
    int immH = Extract(testData,'H', counter, testCnt);

    return CheckALUOp(word, alu, regInitVal, RegisterNumInitial(0), ((immH<<8)|immL), tb->top->eu->AX);
}

int ValidateAluRMR(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = Extract(testData,'D',counter,testCnt);
    int alu = Extract(testData,'A',counter,testCnt);
    int mod = regInitVal;
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(0,word,mod,reg,RM,dispL,dispH);
    int opBValue = FetchSourceValue(1,word,mod,reg,RM,dispL,dispH);

    if (direction!=1)
    {
        int t = opAValue;
        opAValue=opBValue;
        opBValue=t;
    }
    int hValue;

    if (alu!=7)
        hValue = FetchDestValue(direction,word,mod,reg,RM,dispL,dispH);
    else
        hValue = opAValue;

    return CheckALUOp(word, alu, 0, opAValue, opBValue, hValue);
}

int ValidateSTOS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int seg = FetchInitialSR(ESRReg::ES);
    int off = RegisterNumInitialWord(ERegisterNum::DI);
    int store=RegisterNumInitialByte(ERegisterNum::AX);
    if (word)
        store=RegisterNumInitialWord(ERegisterNum::AX);

    int hValue = FetchWrittenMemory(word,seg,off);

    int hSeg = tb->top->biu->REGISTER_ES;
    if (seg != hSeg)
    {
        printf("FAILED - Segment Register Mismatch : %04X != %04X\n", seg, hSeg);
        return 0;
    }
    int hOff = tb->top->eu->DI;
    if (regInitVal==FLAG_D)
    {
        if (word)
            off-=2;
        else
            off-=1;
    }
    else
    {
        if (word)
            off+=2;
        else
            off+=1;
    }
    if (off != hOff)
    {
        printf("FAILED - Offset Mismatch : %04X != %04X\n", off, hOff);
        return 0;
    }

    return store==hValue;
}

int ValidateSTOSREP(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int seg = FetchInitialSR(ESRReg::ES);
    int off = RegisterNumInitialWord(ERegisterNum::DI);
    int store=RegisterNumInitialByte(ERegisterNum::AX);
    if (word)
        store=RegisterNumInitialWord(ERegisterNum::AX);

    int hSeg = tb->top->biu->REGISTER_ES;
    if (seg != hSeg)
    {
        printf("FAILED - Segment Register Mismatch : %04X != %04X\n", seg, hSeg);
        return 0;
    }

    int hValue=0;
    for (int a=0;a<regInitVal;a++)
    {
        hValue = FetchWrittenMemory(word,seg,off);
        if (word)
            off+=2;
        else
            off+=1;
        if (store!=hValue)
        {
            printf("FAILED - Mismatch Written Value : %04X != %04X\n", store, hValue);
        }
    }

    int hOff = tb->top->eu->DI;
    if (off != hOff)
    {
        printf("FAILED - Offset Mismatch : %04X != %04X\n", off, hOff);
        return 0;
    }

    return tb->top->eu->CX == 0;
}

int ValidateLODS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int seg = FetchInitialSR(segOverride);
    int off = RegisterNumInitialWord(ERegisterNum::SI);
    
    int hValue=FetchByteRegister(ERegisterNum::AX);
    if (word)
        hValue=FetchWordRegister(ERegisterNum::AX);

    int load = FetchReadMemory(word,seg,off);

    int hSeg = FetchSR(segOverride);
    if (seg != hSeg)
    {
        printf("FAILED - Segment Register Mismatch : %04X != %04X\n", seg, hSeg);
        return 0;
    }
    int hOff = tb->top->eu->SI;
    if (regInitVal==FLAG_D)
    {
        if (word)
            off-=2;
        else
            off-=1;
    }
    else
    {
        if (word)
            off+=2;
        else
            off+=1;
    }
    if (off != hOff)
    {
        printf("FAILED - Offset Mismatch : %04X != %04X\n", off, hOff);
        return 0;
    }

    return load==hValue;
}

int ValidateLODSREP(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int seg = FetchInitialSR(segOverride);
    int off = RegisterNumInitialWord(ERegisterNum::SI);
    int hValue=FetchByteRegister(ERegisterNum::AX);
    if (word)
        hValue=FetchWordRegister(ERegisterNum::AX);

    int hSeg = FetchSR(segOverride);
    if (seg != hSeg)
    {
        printf("FAILED - Segment Register Mismatch : %04X != %04X\n", seg, hSeg);
        return 0;
    }

    int load=0;
    for (int a=0;a<regInitVal;a++)
    {
        load = FetchReadMemory(word,seg,off);
        if (word)
            off+=2;
        else
            off+=1;
    }
    if (regInitVal)
    {
        if (load!=hValue)
        {
            printf("FAILED - Mismatch Read Value : %04X != %04X\n", load, hValue);
            return 0;
        }
    }

    int hOff = tb->top->eu->SI;
    if (off != hOff)
    {
        printf("FAILED - Offset Mismatch : %04X != %04X\n", off, hOff);
        return 0;
    }

    return tb->top->eu->CX == 0;
}

int ValidateMOVS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int segSrc = FetchInitialSR(segOverride);
    int offSrc = RegisterNumInitialWord(ERegisterNum::SI);
    int segDst = FetchInitialSR(ESRReg::ES);
    int offDst = RegisterNumInitialWord(ERegisterNum::DI);
    
    int load = FetchReadMemory(word,segSrc,offSrc);
    int store= FetchWrittenMemory(word,segDst,offDst);

    int hSegSrc = FetchSR(segOverride);
    if (segSrc != hSegSrc)
    {
        printf("FAILED - Segment Source Register Mismatch : %04X != %04X\n", segSrc, hSegSrc);
        return 0;
    }
    int hSegDst = FetchSR(ESRReg::ES);
    if (segDst != hSegDst)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segDst, hSegDst);
        return 0;
    }
    int hOffSrc = tb->top->eu->SI;
    int hOffDst = tb->top->eu->DI;
    if (regInitVal==FLAG_D)
    {
        if (word)
        {
            offSrc-=2;
            offDst-=2;
        }
        else
        {
            offSrc-=1;
            offDst-=1;
        }
    }
    else
    {
        if (word)
        {
            offSrc+=2;
            offDst+=2;
        }
        else
        {
            offSrc+=1;
            offDst+=1;
        }
    }
    if (offSrc != hOffSrc)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrc, hOffSrc);
        return 0;
    }
    if (offDst != hOffDst)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrc, hOffSrc);
        return 0;
    }

    return load == store;
}

int ValidateMOVSREP(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int segSrc = FetchInitialSR(segOverride);
    int offSrc = RegisterNumInitialWord(ERegisterNum::SI);
    int segDst = FetchInitialSR(ESRReg::ES);
    int offDst = RegisterNumInitialWord(ERegisterNum::DI);
    
    int hSegSrc = FetchSR(segOverride);
    if (segSrc != hSegSrc)
    {
        printf("FAILED - Segment Source Register Mismatch : %04X != %04X\n", segSrc, hSegSrc);
        return 0;
    }
    int hSegDst = FetchSR(ESRReg::ES);
    if (segDst != hSegDst)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segDst, hSegDst);
        return 0;
    }
    for (int a=0;a<regInitVal;a++)
    {
        int load = FetchReadMemory(word,segSrc,offSrc);
        int store= FetchWrittenMemory(word,segDst,offDst);

        if (load != store)
        {
            printf("FAILED - load vs store Mismatch : %04X != %04X\n", load, store);
            return 0;
        }

        if (word)
        {
            offSrc+=2;
            offDst+=2;
        }
        else
        {
            offSrc+=1;
            offDst+=1;
        }
    }

    int hOffSrc = tb->top->eu->SI;
    int hOffDst = tb->top->eu->DI;
    if (offSrc != hOffSrc)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrc, hOffSrc);
        return 0;
    }
    if (offDst != hOffDst)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrc, hOffSrc);
        return 0;
    }

    return 1;
}

int ValidateIncRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,-1,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,-1,RM,dispL,dispH);

    if (word)
        return CheckAddResultW(opAValue,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P,hValue);
    return CheckAddResultB(opAValue,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P,hValue);
}

int ValidateDecRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,-1,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,-1,RM,dispL,dispH);

    if (word)
        return CheckSubResultW(opAValue,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P,hValue);
    return CheckSubResultB(opAValue,1,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P,hValue);
}

int ValidateAluRMImmediate(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int signExt = Extract(testData,'S',counter,testCnt);
    int alu = Extract(testData,'A',counter,testCnt);
    int mod = regInitVal;
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);
    int immL = Extract(testData,'L',counter,testCnt);
    int immH = Extract(testData,'H',counter,testCnt);

    if (word==0)
    {
        immH = 0;
    }
    else
    {
        if (signExt==1)
            immH = (immL & 0x80) ? 0xFF : 0x00;
    }

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);
    int opBValue = (immH<<8)|immL;

    int hValue;

    if (alu!=7)
        hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);
    else
        hValue = opAValue;
    
    return CheckALUOp(word, alu, 0, opAValue, opBValue, hValue);
}

int ValidateShiftRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int shiftOp = Extract(testData,'S',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);
    
    return CheckShifterOp(word, shiftOp, regInitVal==FLAG_C, opAValue, hValue);
}

int ValidateNegRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);
    
    if (word)
        return CheckNegResultW(opAValue,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,hValue);
    return CheckNegResultB(opAValue,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,hValue);
}

int JccTaken(int cond, int regInitVal)
{
    switch (cond)
    {
        case 0x0:   // JO
            return (regInitVal&FLAG_O);
        case 0x1:   // JNO
            return !(regInitVal&FLAG_O);
        case 0x2:   // JC
            return (regInitVal&FLAG_C);
        case 0x3:   // JAE
            return !(regInitVal&FLAG_C);
        case 0x4:   // JE
            return (regInitVal&FLAG_Z);
        case 0x5:   // JNE
            return !(regInitVal&FLAG_Z);
        case 0x6:   // JBE
            return (regInitVal&FLAG_Z) || (regInitVal&FLAG_C);
        case 0x7:   // JA
            return !((regInitVal&FLAG_Z) || (regInitVal&FLAG_C));
        case 0x8:   // JS
            return (regInitVal&FLAG_S);
        case 0x9:   // JNS
            return !(regInitVal&FLAG_S);
        case 0xA:   // JP
            return (regInitVal&FLAG_P);
        case 0xB:   // JPO
            return !(regInitVal&FLAG_P);
        case 0xC:   // JL
            return ((regInitVal&FLAG_S)==FLAG_S) != ((regInitVal&FLAG_O)==FLAG_O);
        case 0xD:   // JGE
            return ((regInitVal&FLAG_S)==FLAG_S) == ((regInitVal&FLAG_O)==FLAG_O);
        case 0xE:   // JLE
            return (((regInitVal&FLAG_S)==FLAG_S) != ((regInitVal&FLAG_O)==FLAG_O)) || (regInitVal&FLAG_Z);
        case 0xF:   // JG
            return !((((regInitVal&FLAG_S)==FLAG_S) != ((regInitVal&FLAG_O)==FLAG_O)) || (regInitVal&FLAG_Z));
    }
    return -1;
}

int ValidateJcc(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = SignExt8Bit(Extract(testData,'l', counter, testCnt));
    int cond = Extract(testData, 'C', counter, testCnt);
    int ip = tb->top->biu->REGISTER_IP;

    if (JccTaken(cond, regInitVal))
        return (ip) == ((0x0002 + immediateValueL)&0xFFFF);
    return (ip == 0x0002);
}

int ValidateCallCW(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = Extract(testData,'L', counter, testCnt);
    int immediateValueH = Extract(testData,'H', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    int expectedip = ((0x0003 + ((immediateValueH<<8)|immediateValueL)) & 0xFFFF);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    // 3 should be on stack
    if (3 != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", 3, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    if (ip != expectedip)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", expectedip, ip);
        return 0;
    }

    return 1;
}

int ValidateCallRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int instructionLength = 1 + FetchModRMLength(1,1,mod,99,RM);

    int opAValue = FetchSourceValue(1,1,mod,99,RM,dispL,dispH);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    if (instructionLength != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", instructionLength, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    if (ip != opAValue)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", opAValue, ip);
        return 0;
    }

    return 1;
}

int ValidatePushSR(const char* testData, int counter, int testCnt, int regInitVal)
{
    int r = Extract(testData,'r', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sr = FetchSR(r);
    int sp = tb->top->eu->SP;

    int isr = FetchInitialSR(r);

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    // sr should be on stack
    if (isr != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", isr, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    if (sr != isr)
    {
        printf("Segment Register Mismatch %04X != %04X\n", isr, sr);
        return 0;
    }

    return 1;
}

int ValidatePushRW(const char* testData, int counter, int testCnt, int regInitVal)
{
    int r = Extract(testData,'r', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sr = FetchWordRegister(r);
    int sp = tb->top->eu->SP;

    int isr = RegisterNumInitialWord(r);

    if (r == 4)
        isr=isr-2;  // Push SP pushes SP decremented already.

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    // sr should be on stack
    if (isr != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", isr, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    if (sr != isr)
    {
        printf("Register Mismatch %04X != %04X\n", isr, sr);
        return 0;
    }

    return 1;
}

int ValidateTestRMImmediate(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = regInitVal;
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);
    int immL = Extract(testData,'L',counter,testCnt);
    int immH = Extract(testData,'H',counter,testCnt);

    if (word==0)
    {
        immH = 0;
    }

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);
    int opBValue = (immH<<8)|immL;

    int hValue = opAValue;
    
    if (word==1)
        return CheckTestResultW(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);          // don't validate U flags
    return CheckTestResultB(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);              // don't validate U flags
}

int ValidateTestAImmediate(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int immL = Extract(testData,'L', counter, testCnt);
    int immH = Extract(testData,'H', counter, testCnt);

    int opAValue = RegisterNumInitial(ERegisterNum::AX);
    int opBValue = (immH<<8)|immL;

    int hValue = opAValue;
    if (word==1)
        return CheckTestResultW(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);          // don't validate U flags
    return CheckTestResultB(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);              // don't validate U flags
}

int CheckShifterOpCl(int word, int aluOp, int cl, int carryIn, int A, int result)
{
    int res;
    int checkO = cl==1 ? FLAG_O : 0;
    int checkSF = cl==0 ? 0 : FLAG_S|FLAG_Z|FLAG_P;// don't validate U flags
    int checkF = cl==0? 0 : FLAG_C;
    if (word)
    {
        switch (aluOp)
        {
            case 0:
                return CheckRolClResultW(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 1:
                return CheckRorClResultW(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 2:
                return CheckRclClResultW(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 3:
                return CheckRcrClResultW(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 4:
            case 6:
                return CheckShlClResultW(A,cl,carryIn,checkO|checkSF|checkF,result);
            case 5:
                return CheckShrClResultW(A,cl,carryIn,checkO|checkSF|checkF,result);
            case 7:
                return CheckSarClResultW(A,cl,carryIn,checkO|checkSF|checkF,result);
            default:
                break;
        }
    }
    else
    {
        switch (aluOp)
        {
            case 0:
                return CheckRolClResultB(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 1:
                return CheckRorClResultB(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 2:
                return CheckRclClResultB(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 3:
                return CheckRcrClResultB(A,cl,carryIn,checkO|checkF,result);     // Only O & C for rotates
            case 4:
            case 6:
                return CheckShlClResultB(A,cl,carryIn,checkO|checkSF|checkF,result);     
            case 5:
                return CheckShrClResultB(A,cl,carryIn,checkO|checkSF|checkF,result);
            case 7:
                return CheckSarClResultB(A,cl,carryIn,checkO|checkSF|checkF,result);
            default:
                break;
        }
    }
    return 0;
}

int ValidateShiftRMclNoCarry(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int shiftOp = Extract(testData,'S',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);

    return CheckShifterOpCl(word, shiftOp, regInitVal, 0, opAValue, hValue);
}

int ValidateShiftRMclCarry(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int shiftOp = Extract(testData,'S',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);
    
    return CheckShifterOpCl(word, shiftOp, regInitVal, 1, opAValue, hValue);
}

int ValidateMulRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);
    int opBValue = RegisterNumInitial(ERegisterNum::AX);

    int hValue=FetchWordRegister(ERegisterNum::AX);

    if (word)
        return CheckMulW(opAValue, opBValue, FLAG_C | FLAG_O, hValue, FetchWordRegister(ERegisterNum::DX));
    
    return CheckMulB(opAValue, opBValue, FLAG_C | FLAG_O, hValue); // Don't check Unknown flags
}

int ValidateIMulRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opBValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);
    int opAValue = RegisterNumInitial(ERegisterNum::AX);

    int hValue=FetchWordRegister(ERegisterNum::AX);

    if (word)
        return CheckIMulW(opAValue&0xFFFF, opBValue&0xFFFF, FLAG_C | FLAG_O, hValue, FetchWordRegister(ERegisterNum::DX));
    
    return CheckIMulB(opAValue&0xFF, opBValue&0xFF, FLAG_C | FLAG_O, hValue); // Don't check Unknown flags
}

int ValidatePopSR(const char* testData, int counter, int testCnt, int regInitVal)
{
    int r = Extract(testData,'r', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sr = FetchSR(r);
    int sp = tb->top->eu->SP;

    int isr = FetchInitialSR(r);

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchReadMemory(1,iss, isp);

    // Stack segment should not change (unless we are popping SS of course)
    if (ss!=iss && r!=ESRReg::SS)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 more
    if (sp!=isp+2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+2, sp);
        return 0;
    }

    // sr should match the value from the stack
    if (sr != stackValue)
    {
        printf("Segment Register Mismatch %04X != %04X\n", stackValue, sr);
        return 0;
    }

    return 1;
}

int ValidatePopRW(const char* testData, int counter, int testCnt, int regInitVal)
{
    int r = Extract(testData,'r', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sr = FetchWordRegister(r);
    int sp = tb->top->eu->SP;

    int isr = RegisterNumInitialWord(r);

    //if (r == ERegisterNum::SP)
//        isr=isr-2;  // Push SP pushes SP decremented already.

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchReadMemory(1,iss, isp);

    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 more (unless popping SP)
    if (sp!=isp+2 && r!=ERegisterNum::SP)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+2, sp);
        return 0;
    }

    if (sr != stackValue)
    {
        printf("Register Mismatch %04X != %04X\n", stackValue, sr);
        return 0;
    }

    return 1;
}

int ValidateJmpRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int instructionLength = 1 + FetchModRMLength(1,1,mod,99,RM);

    int opAValue = FetchSourceValue(1,1,mod,99,RM,dispL,dispH);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int cs = tb->top->biu->REGISTER_CS;

    int ics = FetchInitialSR(ESRReg::CS);
    
    // Code segment should not change
    if (cs!=ics)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", ics, cs);
        return 0;
    }

    if (ip != opAValue)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", opAValue, ip);
        return 0;
    }

    return 1;
}

int ValidatePushRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,1,mod,99,RM,dispL,dispH);
    
    // Special case for SP
    if (mod==3 && RM==ERegisterNum::SP)
    {
        opAValue-=2;    // Because SP will be decremented before being pushed
    }

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    // opAValue should be on stack
    if (opAValue != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", opAValue, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    return 1;
}

int ValidatePopRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchReadMemory(1,iss, isp);

    int opAValue = FetchDestValue(0,1,mod,99,RM,dispL,dispH);
    
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 more (unless popping SP)
    if (sp!=isp+2 && !(mod==3 && RM==ERegisterNum::SP))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+2, sp);
        return 0;
    }

    if (opAValue != stackValue)
    {
        printf("Register Mismatch %04X != %04X\n", opAValue, stackValue);
        return 0;
    }

    return 1;
}

int ValidateDivRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = RegisterNumInitial(ERegisterNum::AX);
    int opAHValue = RegisterNumInitial(ERegisterNum::DX);
    int opBValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    if (opBValue==0)
    {
        int vector = FetchReadMemory(1,0,0);

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        return ip==vector;
    }
    else
    {
        uint32_t divA,divB;
        if (word)
        {
            divA= (opAHValue<<16)|(opAValue);
            divB= opBValue;

            if (divA/divB > 65535)
            {
                int vector = FetchReadMemory(1,0,0);

                int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
                return ip==vector;
            }
        }
        else
        {
            divA= opAValue;
            divB= opBValue;

            if (divA/divB > 255)
            {
                int vector = FetchReadMemory(1,0,0);

                int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
                return ip==vector;
            }
        }

        int hValue=FetchWordRegister(ERegisterNum::AX);

        if (word)
        {
            int rValue = FetchWordRegister(ERegisterNum::DX);
            return CheckDivW(opAValue,opAHValue, opBValue, 0, hValue, FetchWordRegister(ERegisterNum::DX));
        }
        return CheckDivB(opAValue, opBValue, 0, hValue); // Don't check Unknown flags
    }
}

int ValidateIDivRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = RegisterNumInitial(ERegisterNum::AX);
    int opAHValue = RegisterNumInitial(ERegisterNum::DX);
    int opBValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    if (opBValue==0)
    {
        int vector = FetchReadMemory(1,0,0);

        int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
        return ip==vector;
    }
    else
    {
        int32_t divA,divB;
        if (word)
        {
            divA= ((opAHValue<<16)|(opAValue));
            divB= opBValue;
            if ((divB&0x8000) == 0x8000)
                divB|=0xFFFF0000;

            if ((divA/divB > 32767) || (divA/divB<-32768))
            {
                int vector = FetchReadMemory(1,0,0);

                int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
                return ip==vector;
            }
        }
        else
        {
            divA= opAValue;
            if ((divA&0x8000) == 0x8000)
                divA|=0xFFFF0000;
            divB= opBValue;
            if ((divB&0x80) == 0x80)
                divB|=0xFFFFFF00;

            if ((divA/divB > 127) || (divA/divB<-128))
            {
                int vector = FetchReadMemory(1,0,0);

                int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
                return ip==vector;
            }
        }

        int hValue=FetchWordRegister(ERegisterNum::AX);

        if (word)
        {
            int rValue = FetchWordRegister(ERegisterNum::DX);
            return CheckIDivW(opAValue,opAHValue, opBValue, 0, hValue, FetchWordRegister(ERegisterNum::DX));
        }
        return CheckIDivB(opAValue, opBValue, 0, hValue); // Don't check Unknown flags
    }
}

int ValidateMovAXmem(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = Extract(testData,'D',counter,testCnt);
    int offL = Extract(testData,'L', counter, testCnt);
    int offH = Extract(testData,'H', counter, testCnt);
    int seg = FetchInitialSR(segOverride);
    int off = (offH<<8)|offL;

    if (direction == 0)
    {
        int dValue = word ? FetchWordRegister(ERegisterNum::AX) : FetchByteRegister(ERegisterNum::AX);
        int sValue = FetchReadMemory(word,seg,off);
        return dValue==sValue;
    }
    else
    {
        int sValue = word ? RegisterNumInitialWord(ERegisterNum::AX) : RegisterNumInitialByte(ERegisterNum::AX);
        int dValue = FetchWrittenMemory(word,seg,off);
        return dValue==sValue;
    }
}

int ValidateMovAXmemSegOverride(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = Extract(testData,'D',counter,testCnt);
    segOverride=Extract(testData,'R',counter,testCnt);
    int offL = Extract(testData,'L', counter, testCnt);
    int offH = Extract(testData,'H', counter, testCnt);
    int seg = FetchInitialSR(segOverride);
    int off = (offH<<8)|offL;

    if (direction == 0)
    {
        int dValue = word ? FetchWordRegister(ERegisterNum::AX) : FetchByteRegister(ERegisterNum::AX);
        int sValue = FetchReadMemory(word,seg,off);
        return dValue==sValue;
    }
    else
    {
        int sValue = word ? RegisterNumInitialWord(ERegisterNum::AX) : RegisterNumInitialByte(ERegisterNum::AX);
        int dValue = FetchWrittenMemory(word,seg,off);
        return dValue==sValue;
    }
}

int ValidateRet(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = regInitVal;
    
    int stackValue = FetchReadMemory(1,iss, isp);

    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 more
    if (sp!=((isp+2)&0xFFFF))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+2, sp);
        return 0;
    }

    if (ip != stackValue)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", stackValue, ip);
        return 0;
    }

    return 1;
}

int ValidateRetF(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;
    int cs = tb->top->biu->REGISTER_CS;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = regInitVal;
    
    int stackValueIP = FetchReadMemory(1,iss, isp);
    int stackValueCS = FetchReadMemory(1,iss, isp+2);

    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 4 more
    if (sp!=((isp+4)&0xFFFF))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+4, sp);
        return 0;
    }

    if (ip != stackValueIP)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", stackValueIP, ip);
        return 0;
    }
    if (cs != stackValueCS)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", stackValueCS, cs);
        return 0;
    }

    return 1;
}

int ValidateRetRetFImm(const char* testData, int counter, int testCnt, int regInitVal)
{
    int far = Extract(testData,'F',counter,testCnt);
    int offL = Extract(testData,'L', counter, testCnt);
    int offH = Extract(testData,'H', counter, testCnt);
    int imm = (offH<<8)|offL;

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;
    int cs = tb->top->biu->REGISTER_CS;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = regInitVal;
    
    int stackValueIP = FetchReadMemory(1,iss, isp);
    int stackValueCS;
    if (far)
    {
        stackValueCS = FetchReadMemory(1,iss, isp+2);
    }

    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be imm more
    if (sp!=((isp+imm+(far?4:2))&0xFFFF))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+imm+(far?4:2), sp);
        return 0;
    }

    if (ip != stackValueIP)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", stackValueIP, ip);
        return 0;
    }
    if (far)
    {
        if (cs != stackValueCS)
        {
            printf("Code Segment Register Mismatch %04X != %04X\n", stackValueCS, cs);
            return 0;
        }
    }

    return 1;
}

int ValidateLea(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = regInitVal;
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int hValue = FetchWordRegister(reg);

    int seg,off;
    ComputeEffectiveAddress(mod,RM,dispL,dispH, &seg, &off);

    return hValue==off;
}

int ValidateXchgRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int direction = 1;
    int mod = Extract(testData,'M',counter,testCnt);
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int originalLHS = word ? RegisterNumInitialWord(reg) : RegisterNumInitialByte(reg);
    int finalLHS = FetchDestValue(direction,word,mod,reg,RM,dispL,dispH);
    int originalRHS;
    int finalRHS;
    if (mod != 3)
    {
        int seg,off;
        ComputeEffectiveAddress(mod,RM,dispL,dispH, &seg, &off);

        originalRHS = FetchReadMemory(word,seg,off);
        finalRHS = FetchWrittenMemory(word,seg,off);
    }
    else
    {
        originalRHS = word ? RegisterNumInitialWord(RM) : RegisterNumInitialByte(RM);
        finalRHS = word ? FetchWordRegister(RM) : FetchByteRegister(RM);
    }

    return originalLHS==finalRHS && originalRHS==finalLHS;
}

int ValidateXchgAXrw(const char* testData, int counter, int testCnt, int regInitVal)
{
    int reg = Extract(testData,'R',counter,testCnt);

    int originalLHS = RegisterNumInitialWord(ERegisterNum::AX);
    int finalLHS = FetchWordRegister(ERegisterNum::AX);
    int originalRHS = RegisterNumInitialWord(reg);
    int finalRHS = FetchWordRegister(reg);

    return originalLHS==finalRHS && originalRHS==finalLHS;
}

// Need to validate against 8088 for empty bits
int ValidatePushF(const char* testData, int counter, int testCnt, int regInitVal)
{
    int opAValue = tb->top->eu->FLAGS;
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchWrittenMemory(1,ss, sp);

    // opAValue should be on stack
    if (opAValue != stackValue)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", opAValue, stackValue);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 less
    if (sp!=isp-2)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-2, sp);
        return 0;
    }

    return 1;
}

// Need to validate against 8088 for empty bits
int ValidatePopF(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValue = FetchReadMemory(1,iss, isp);

    int opAValue = tb->top->eu->FLAGS;
    
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 2 more (unless popping SP)
    if (sp!=((isp+2)&0xFFFF))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+2, sp);
        return 0;
    }

    if (opAValue != stackValue)
    {
        printf("Register Mismatch %04X != %04X\n", opAValue, stackValue);
        return 0;
    }

    return 1;
}

int ValidateCBW(const char* testData, int counter, int testCnt, int regInitVal)
{
    int8_t al = RegisterNumInitialByte(ERegisterNum::AX);
    int expected = al;
    expected&=0xFFFF;
    int ax = FetchWordRegister(ERegisterNum::AX);

    return expected == ax;
}

int ValidateCWD(const char* testData, int counter, int testCnt, int regInitVal)
{
    int16_t i = RegisterNumInitialWord(ERegisterNum::AX);
    int expected = i;
    int expectedAX=expected&0xFFFF;
    int expectedDX=(expected>>16)&0xFFFF;
    int ax = FetchWordRegister(ERegisterNum::AX);
    int dx = FetchWordRegister(ERegisterNum::DX);

    return expectedAX == ax && expectedDX == dx;
}

int ValidateNotRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);

    int hValue = FetchDestValue(0,word,mod,99,RM,dispL,dispH);
    
    if (word)
        return ((~opAValue)&0xFFFF) == hValue;

    return ((~opAValue)&0xFF) == hValue;
}

int ValidateIRet(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = tb->top->eu->SP;
    int cs = tb->top->biu->REGISTER_CS;
    int flags = tb->top->eu->FLAGS;

    int iss = FetchInitialSR(ESRReg::SS);
    int isp = regInitVal;
    
    int stackValueIP = FetchReadMemory(1,iss, isp);
    int stackValueCS = FetchReadMemory(1,iss, isp+2);
    int stackValueFlags = FetchReadMemory(1,iss, isp+4);

    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 6 more
    if (sp!=((isp+6)&0xFFFF))
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp+6, sp);
        return 0;
    }

    if (ip != stackValueIP)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", stackValueIP, ip);
        return 0;
    }
    if (cs != stackValueCS)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", stackValueCS, cs);
        return 0;
    }
    if (flags != stackValueFlags)
    {
        printf("Flags Register Mismatch %04X != %04X\n", stackValueFlags, flags);
        return 0;
    }


    return 1;
}

int ValidateCallCD(const char* testData, int counter, int testCnt, int regInitVal)
{
    int destipL = Extract(testData,'L', counter, testCnt);
    int destipH = Extract(testData,'H', counter, testCnt);
    int destcsL = Extract(testData,'l', counter, testCnt);
    int destcsH = Extract(testData,'h', counter, testCnt);
    
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int cs = tb->top->biu->REGISTER_CS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int ics = FetchInitialSR(ESRReg::CS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    int expectedip = (destipH<<8)|destipL;
    int expectedcs = (destcsH<<8)|destcsL;
    
    int stackValueCS = FetchWrittenMemory(1,ss, isp-2);
    int stackValueIP = FetchWrittenMemory(1,ss, isp-4);

    if (ics != stackValueCS)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", ics, stackValueCS);
        return 0;
    }
    // 5 should be on stack
    if (5 != stackValueIP)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", 5, stackValueIP);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 4 less
    if (sp!=isp-4)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-4, sp);
        return 0;
    }

    if ((ip&0xFFFF) != expectedip)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", expectedip, (ip&0xFFFF));
        return 0;
    }

    if (cs != expectedcs)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", expectedcs, cs);
        return 0;
    }

    return 1;
}

int ValidateCallFarRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = regInitVal;
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'L', counter, testCnt);
    int dispH = Extract(testData,'H', counter, testCnt);

    int instructionLength = 1 + FetchModRMLength(1,1,mod,99,RM);

    int seg,off;
    ComputeEffectiveAddress(mod,RM,dispL,dispH,&seg,&off);

    int expectedip = FetchReadMemory(1,seg,off);
    int expectedcs = FetchReadMemory(1,seg,off+2);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int ss = tb->top->biu->REGISTER_SS;
    int cs = tb->top->biu->REGISTER_CS;
    int sp = tb->top->eu->SP;

    int iss = FetchInitialSR(ESRReg::SS);
    int ics = FetchInitialSR(ESRReg::CS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    int stackValueCS = FetchWrittenMemory(1,ss, isp-2);
    int stackValueIP = FetchWrittenMemory(1,ss, isp-4);

    if (ics != stackValueCS)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", ics, stackValueCS);
        return 0;
    }
    if (instructionLength != stackValueIP)
    {
        printf("Stack Contents Mismatch %04X != %04X\n", instructionLength, stackValueIP);
        return 0;
    }
    // Stack segment should not change
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    // SP should be 4 less
    if (sp!=isp-4)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp-4, sp);
        return 0;
    }

    if ((ip&0xFFFF) != expectedip)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", expectedip, (ip&0xFFFF));
        return 0;
    }

    if (cs != expectedcs)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", expectedcs, cs);
        return 0;
    }

    return 1;
}

int ValidateJmpFarRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = regInitVal;
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'L', counter, testCnt);
    int dispH = Extract(testData,'H', counter, testCnt);

    int instructionLength = 1 + FetchModRMLength(1,1,mod,99,RM);

    int seg,off;
    ComputeEffectiveAddress(mod,RM,dispL,dispH,&seg,&off);

    int expectedip = FetchReadMemory(1,seg,off);
    int expectedcs = FetchReadMemory(1,seg,off+2);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int cs = tb->top->biu->REGISTER_CS;
    int ss = tb->top->biu->REGISTER_SS;
    int sp = FetchWordRegister(ERegisterNum::SP);
    int iss = FetchInitialSR(ESRReg::SS);
    int isp = RegisterNumInitialWord(ERegisterNum::SP);
    
    // Stack should not change
    if (sp!=isp)
    {
        printf("Stack Pointer Register Mismatch %04X != %04X\n", isp, sp);
        return 0;
    }
    if (ss!=iss)
    {
        printf("Stack Segment Register Mismatch %04X != %04X\n", iss, ss);
        return 0;
    }
    if (cs!=expectedcs)
    {
        printf("Code Segment Register Mismatch %04X != %04X\n", expectedcs, cs);
        return 0;
    }

    if (ip != expectedip)
    {
        printf("Instruction Pointer Register Mismatch %04X != %04X\n", expectedip, ip);
        return 0;
    }

    return 1;
}


int ValidateTestRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int mod = Extract(testData,'M',counter,testCnt);
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,reg,RM,dispL,dispH);
    int opBValue = FetchDestValue(1,word,mod,reg,RM,dispL,dispH);

    int hValue = opAValue;
    
    if (word==1)
        return CheckTestResultW(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);          // don't validate U flags
    return CheckTestResultB(opAValue,opBValue,FLAG_O|FLAG_S|FLAG_Z|/*FLAG_A|*/FLAG_P|FLAG_C,hValue);              // don't validate U flags
}

int ValidateSALC(const char* testData, int counter, int testCnt, int regInitVal)
{
    if (regInitVal == FLAG_C)
    {
        return FetchByteRegister(ERegisterNum::AX) == 0xFF;
    }
    return FetchByteRegister(ERegisterNum::AX)==0x00;
}

int ValidateInADX(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int address = FetchWordRegister(ERegisterNum::DX);
    if (readWriteLatchedAddress[captureIdx]!=address)
    {
        printf("Failed - first Address Read != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],address);
        return 0;
    }
    if (readWriteLatchedType[captureIdx]!=0)
    {
        printf("Failed - first Address Read != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
        return 0;
    }
    if (lastReadCapture[captureIdx]!=(tb->top->eu->AX&0xFF))
    {
        printf("Failed - first Byte Read != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],(tb->top->eu->AX&0xFF));
        return 0;
    }
    captureIdx++;
    if (word)
    {
        if (readWriteLatchedAddress[captureIdx]!=address+1)
        {
            printf("Failed - second Address Read != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],address+1);
            return 0;
        }
        if (readWriteLatchedType[captureIdx]!=0)
        {
            printf("Failed - second Address Read != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
            return 0;
        }
        if (lastReadCapture[captureIdx]!=(tb->top->eu->AX>>8))
        {
            printf("Failed - second Byte Read != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],(tb->top->eu->AX>>8));
            return 0;
        }
        captureIdx++;
    }

    return 1;
}

int ValidateOutADX(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);
    int address = FetchWordRegister(ERegisterNum::DX);

    if (readWriteLatchedAddress[captureIdx]!=address)
    {
        printf("Failed - first Address Written != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],address);
        return 0;
    }
    if (readWriteLatchedType[captureIdx]!=0)
    {
        printf("Failed - first Address Written != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
        return 0;
    }
    if (lastWriteCapture[captureIdx]!=(regInitVal&0xFF))
    {
        printf("Failed - first Byte Written != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],regInitVal&0xFF);
        return 0;
    }
    captureIdx++;
    if (word)
    {
        if (readWriteLatchedAddress[captureIdx]!=address+1)
        {
            printf("Failed - second Address Written != expected (%08X)!=(%08X)\n", readWriteLatchedAddress[captureIdx],address+1);
            return 0;
        }
        if (readWriteLatchedType[captureIdx]!=0)
        {
            printf("Failed - second Address Written != IO (%d)!=(%d)\n", readWriteLatchedType[captureIdx],0);
            return 0;
        }
        if (lastWriteCapture[captureIdx]!=(regInitVal>>8))
        {
            printf("Failed - second Byte Written != (expected) (%02X)!=(%02X)\n", lastWriteCapture[captureIdx],regInitVal>>8);
            return 0;
        }
        captureIdx++;
    }

    return 1;
}

int ValidateLES(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = regInitVal;
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'L', counter, testCnt);
    int dispH = Extract(testData,'H', counter, testCnt);

    int seg,off;
    ComputeEffectiveAddress(mod,RM,dispL,dispH,&seg,&off);

    int expectedr = FetchReadMemory(1,seg,off);
    int expectedes = FetchReadMemory(1,seg,off+2);

    int es = tb->top->biu->REGISTER_ES;
    int r = FetchWordRegister(reg);
    
    if (es!=expectedes)
    {
        printf("Extra Segment Register Mismatch %04X != %04X\n", expectedes, es);
        return 0;
    }

    if (r != expectedr)
    {
        printf("Register Mismatch %04X != %04X\n", expectedr, r);
        return 0;
    }

    return 1;
}

int ValidateLDS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int mod = regInitVal;
    int reg = Extract(testData,'R',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'L', counter, testCnt);
    int dispH = Extract(testData,'H', counter, testCnt);

    int seg,off;
    ComputeEffectiveAddress(mod,RM,dispL,dispH,&seg,&off);

    int expectedr = FetchReadMemory(1,seg,off);
    int expectedds = FetchReadMemory(1,seg,off+2);

    int ds = tb->top->biu->REGISTER_DS;
    int r = FetchWordRegister(reg);
    
    if (ds!=expectedds)
    {
        printf("Data Segment Register Mismatch %04X != %04X\n", expectedds, ds);
        return 0;
    }

    if (r != expectedr)
    {
        printf("Register Mismatch %04X != %04X\n", expectedr, r);
        return 0;
    }

    return 1;
}

int ValidateWait(const char* testData, int counter, int testCnt, int regInitVal)
{
    return 1;   // For now, we have the TEST pin held such that the instruction acts as a NOP
}

int ValidateSAHF(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ah=RegisterNumInitialWord(ERegisterNum::AX)>>8;
    int flags=tb->top->eu->FLAGS & 0xFF;

    return ah==flags;
}

int ValidateLAHF(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ah=FetchWordRegister(ERegisterNum::AX)>>8;
    int flags=regInitVal&0xFF;

    return ah==flags;
}

int ValidateEscRM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = 1;   // Always a word fetch?
    int mod = Extract(testData,'M',counter,testCnt);
    int RM = Extract(testData,'m',counter,testCnt);
    int dispL = Extract(testData,'l', counter, testCnt);
    int dispH = Extract(testData,'h', counter, testCnt);

    int opAValue = FetchSourceValue(1,word,mod,99,RM,dispL,dispH);
    return opAValue!=-1;
}

int ValidateXLAT(const char* testData, int counter, int testCnt, int regInitVal)
{
    int base = RegisterNumInitialWord(ERegisterNum::BX);
    int off = RegisterNumInitialByte(ERegisterNum::AX);

    int seg = FetchInitialSR(segOverride);
    
    int load = FetchReadMemory(0,seg,(base+off)&0xFFFF);

    int check = FetchByteRegister(ERegisterNum::AX);
    return check==load;
}

int ValidateXLATPrefixed(const char* testData, int counter, int testCnt, int regInitVal)
{
    segOverride = Extract(testData,'R',counter,testCnt);

    int base = RegisterNumInitialWord(ERegisterNum::BX);
    int off = RegisterNumInitialByte(ERegisterNum::AX);

    int seg = FetchInitialSR(segOverride);
    
    int load = FetchReadMemory(0,seg,(base+off)&0xFFFF);

    int check = FetchByteRegister(ERegisterNum::AX);
    return check==load;
}

int ValidateCMPS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int segSrcA = FetchInitialSR(segOverride);
    int offSrcA = RegisterNumInitialWord(ERegisterNum::SI);
    int segSrcB = FetchInitialSR(ESRReg::ES);
    int offSrcB = RegisterNumInitialWord(ERegisterNum::DI);
    
    int srcA = FetchReadMemory(word,segSrcA,offSrcA);
    int srcB = FetchReadMemory(word,segSrcB,offSrcB);

    int hSegSrcA = FetchSR(segOverride);
    if (segSrcA != hSegSrcA)
    {
        printf("FAILED - Segment Source Register Mismatch : %04X != %04X\n", segSrcA, hSegSrcA);
        return 0;
    }
    int hSegSrcB = FetchSR(ESRReg::ES);
    if (segSrcB != hSegSrcB)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segSrcB, hSegSrcB);
        return 0;
    }
    int hOffSrcA = tb->top->eu->SI;
    int hOffSrcB = tb->top->eu->DI;
    if (regInitVal==FLAG_D)
    {
        if (word)
        {
            offSrcA-=2;
            offSrcB-=2;
        }
        else
        {
            offSrcA-=1;
            offSrcB-=1;
        }
    }
    else
    {
        if (word)
        {
            offSrcA+=2;
            offSrcB+=2;
        }
        else
        {
            offSrcA+=1;
            offSrcB+=1;
        }
    }
    if (offSrcA != hOffSrcA)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrcA, hOffSrcA);
        return 0;
    }
    if (offSrcB != hOffSrcB)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrcB, hOffSrcB);
        return 0;
    }

    int result = srcA-srcB;
    if (word)
        return CheckSubResultW(srcA,srcB,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
    return CheckSubResultB(srcA,srcB,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
}

int ValidateCMPSREP(const char* testData, int counter, int testCnt, int regInitVal)
{
    int z = Extract(testData,'Z',counter,testCnt);
    int word = Extract(testData,'W',counter,testCnt);

    int segSrcA = FetchInitialSR(segOverride);
    int offSrcA = RegisterNumInitialWord(ERegisterNum::SI);
    int segSrcB = FetchInitialSR(ESRReg::ES);
    int offSrcB = RegisterNumInitialWord(ERegisterNum::DI);
    
    int count = RegisterNumInitialWord(ERegisterNum::CX);

    int hSegSrcA = FetchSR(segOverride);
    if (segSrcA != hSegSrcA)
    {
        printf("FAILED - Segment Source Register Mismatch : %04X != %04X\n", segSrcA, hSegSrcA);
        return 0;
    }
    int hSegSrcB = FetchSR(ESRReg::ES);
    if (segSrcB != hSegSrcB)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segSrcB, hSegSrcB);
        return 0;
    }

    for (int a=0;a<regInitVal;a++)
    {
        int srcA = FetchReadMemory(word,segSrcA,offSrcA);
        int srcB = FetchReadMemory(word,segSrcB,offSrcB);

        if (word)
        {
            offSrcA+=2;
            offSrcB+=2;
        }
        else
        {
            offSrcA+=1;
            offSrcB+=1;
        }
        count--;

        int result = srcA-srcB;
        if (result==0 && z==0)
            break;
        if (result!=0 && z==1)
            break;
    }

    int hOffSrcA = tb->top->eu->SI;
    int hOffSrcB = tb->top->eu->DI;
    if (offSrcA != hOffSrcA)
    {
        printf("FAILED - SourceA Offset Mismatch : %04X != %04X\n", offSrcA, hOffSrcA);
        return 0;
    }
    if (offSrcB != hOffSrcB)
    {
        printf("FAILED - SourceB Offset Mismatch : %04X != %04X\n", offSrcB, hOffSrcB);
        return 0;
    }

    int finalCX = FetchWordRegister(ERegisterNum::CX);
    return (finalCX==(count&0xFFFF));
}

int ValidateSCAS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int word = Extract(testData,'W',counter,testCnt);

    int segSrcB = FetchInitialSR(ESRReg::ES);
    int offSrcB = RegisterNumInitialWord(ERegisterNum::DI);
    
    int srcA = word ? RegisterNumInitialWord(ERegisterNum::AX) : RegisterNumInitialByte(ERegisterNum::AX);
    int srcB = FetchReadMemory(word,segSrcB,offSrcB);

    int hSegSrcB = FetchSR(ESRReg::ES);
    if (segSrcB != hSegSrcB)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segSrcB, hSegSrcB);
        return 0;
    }
    int hOffSrcB = tb->top->eu->DI;
    if (regInitVal==FLAG_D)
    {
        if (word)
        {
            offSrcB-=2;
        }
        else
        {
            offSrcB-=1;
        }
    }
    else
    {
        if (word)
        {
            offSrcB+=2;
        }
        else
        {
            offSrcB+=1;
        }
    }
    if (offSrcB != hOffSrcB)
    {
        printf("FAILED - Source Offset Mismatch : %04X != %04X\n", offSrcB, hOffSrcB);
        return 0;
    }

    int result = srcA-srcB;
    if (word)
        return CheckSubResultW(srcA,srcB,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
    return CheckSubResultB(srcA,srcB,FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C,result);
}

int ValidateSCASREP(const char* testData, int counter, int testCnt, int regInitVal)
{
    int z = Extract(testData,'Z',counter,testCnt);
    int word = Extract(testData,'W',counter,testCnt);

    int segSrcB = FetchInitialSR(ESRReg::ES);
    int offSrcB = RegisterNumInitialWord(ERegisterNum::DI);
    
    int count = RegisterNumInitialWord(ERegisterNum::CX);

    int hSegSrcB = FetchSR(ESRReg::ES);
    if (segSrcB != hSegSrcB)
    {
        printf("FAILED - Segment Destination Register Mismatch : %04X != %04X\n", segSrcB, hSegSrcB);
        return 0;
    }

    int srcA = word ? RegisterNumInitialWord(ERegisterNum::AX) : RegisterNumInitialByte(ERegisterNum::AX);
    for (int a=0;a<regInitVal;a++)
    {
        int srcB = FetchReadMemory(word,segSrcB,offSrcB);

        if (word)
        {
            offSrcB+=2;
        }
        else
        {
            offSrcB+=1;
        }
        count--;

        int result = srcA-srcB;
        if (result==0 && z==0)
            break;
        if (result!=0 && z==1)
            break;
    }

    int hOffSrcB = tb->top->eu->DI;
    if (offSrcB != hOffSrcB)
    {
        printf("FAILED - SourceB Offset Mismatch : %04X != %04X\n", offSrcB, hOffSrcB);
        return 0;
    }

    int finalCX = FetchWordRegister(ERegisterNum::CX);
    return (finalCX==(count&0xFFFF));
}

int ValidateJCXZ(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = SignExt8Bit(Extract(testData,'l', counter, testCnt));

    int ip = tb->top->biu->REGISTER_IP;

    if (FetchWordRegister(ERegisterNum::CX)==0)
        return (ip) == ((0x0002 + immediateValueL)&0xFFFF);
    return (ip) == 0x0002;
}

int ValidateLoopENE(const char* testData, int counter, int testCnt, int regInitVal)
{
    int z = Extract(testData,'Z',counter,testCnt);
    int immediateValueL = SignExt8Bit(Extract(testData,'l', counter, testCnt));
    int ip = (tb->top->biu->REGISTER_IP)&0xFFFF;

    int zeroFlag = tb->top->eu->FLAGS & FLAG_Z?1:0;
    int shouldJump = (tb->top->eu->CX!=0) && (z==zeroFlag);
    int expected=shouldJump?(2+immediateValueL)&0xFFFF:2;

    int expectedCX = (((regInitVal-0x10000)>>16)&0xFFFF);

    if (tb->top->eu->CX != expectedCX)
    {
        printf("Failed - CX != initial CX -1   (%04X)!=(%04X)\n", tb->top->eu->CX,expectedCX);
        return 0;
    }
    if (ip!=expected)
    {
        printf("Failed - IP != Expected IP     (%04X)!=(%04X)\n", ip,expected);
        return 0;
    }
    return 1;
}

int ValidateDAADAS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int expectedAL = RegisterNumInitialWord(ERegisterNum::AX)>>8;
    int expectedFlags = regInitVal>>16;

    int resultAL = FetchByteRegister(ERegisterNum::AX);

    return resultAL == expectedAL && TestFlags(tb->top->eu->FLAGS,expectedFlags,FLAG_S|FLAG_Z|FLAG_P|FLAG_C|FLAG_A);
}

int ValidateAAAAAS(const char* testData, int counter, int testCnt, int regInitVal)
{
    int expectedAL = (RegisterNumInitialWord(ERegisterNum::AX)>>4)&0xF;
    int expectedAH = RegisterNumInitialWord(ERegisterNum::AX)>>8;

    int expectedFlags = regInitVal>>16;

    if (expectedFlags&FLAG_C)
    {
        expectedAH = (expectedFlags&FLAG_S)?expectedAH-1:expectedAH+1;
    }

    int resultAL = FetchByteRegister(ERegisterNum::AX);
    int resultAH = FetchWordRegister(ERegisterNum::AX)>>8;

    return resultAH == expectedAH && resultAL == expectedAL && TestFlags(tb->top->eu->FLAGS,expectedFlags,FLAG_C|FLAG_A);
}

int ValidateAAD(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = Extract(testData,'L', counter, testCnt);

    int initAL = regInitVal&0xFF;
    int initAH = (regInitVal>>8)&0xFF;

    int resultAL = FetchByteRegister(ERegisterNum::AX);
    int resultAH = FetchWordRegister(ERegisterNum::AX)>>8;

    int expectedAL = (initAL + (initAH * immediateValueL))&0xFF;

    if (resultAH!=0)
        return 0;

    return resultAL==expectedAL;
}

int ValidateAAM(const char* testData, int counter, int testCnt, int regInitVal)
{
    int initAL = regInitVal&0xFF;
    int imm8 = (regInitVal>>8)&0xFF;

    int resultAL = FetchByteRegister(ERegisterNum::AX);
    int resultAH = FetchWordRegister(ERegisterNum::AX)>>8;

    int expectedAH = initAL / imm8;
    int expectedAL = initAL % imm8;

    return resultAL==expectedAL && resultAH==expectedAH;
}

int ValidateINTO(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    if (regInitVal==FLAG_O)
    {
        int vector = FetchReadMemory(1,0,4*4);

        return ip==vector;
    }
    else
    {
        return ip==1;
    }
}               

int ValidateINT3(const char* testData, int counter, int testCnt, int regInitVal)
{
    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int vector = FetchReadMemory(1,0,3*4);

    return ip==vector;
}               

int ValidateINTn(const char* testData, int counter, int testCnt, int regInitVal)
{
    int immediateValueL = Extract(testData,'L', counter, testCnt);

    int ip = tb->top->biu->REGISTER_IP - tb->top->biu->qSize;
    int vector = FetchReadMemory(1,0,immediateValueL*4);

    return ip==vector;
}               

#define TEST_MULT 4

const char* testArray[]={ 
#if ALL_TESTS
    "10110RRR LLLLLLLL ",                                       (const char*)ValidateMovRImmediateByte,         (const char*)DefaultTestInit,   (const char*)0x0000,      // mov r,i (byte)
    "10111RRR LLLLLLLL HHHHHHHH ",                              (const char*)ValidateMovRImmediateWord,         (const char*)DefaultTestInit,   (const char*)0x0000,      // mov r,i (word)
    "01000RRR ",                                                (const char*)ValidateIncWordRegister,           (const char*)DefaultTestInit,   (const char*)0x0000,      // inc r (word)
    "01000RRR ",                                                (const char*)ValidateIncWordRegister,           (const char*)DefaultTestInit,   (const char*)0xFFFF,      // inc r (word)
    "01000RRR ",                                                (const char*)ValidateIncWordRegister,           (const char*)DefaultTestInit,   (const char*)0x7000,      // inc r (word)
    "01000RRR ",                                                (const char*)ValidateIncWordRegister,           (const char*)DefaultTestInit,   (const char*)0x7FFF,      // inc r (word)
    "01000RRR ",                                                (const char*)ValidateIncWordRegister,           (const char*)DefaultTestInit,   (const char*)0x000F,      // inc r (word)
    "01001RRR ",                                                (const char*)ValidateDecWordRegister,           (const char*)DefaultTestInit,   (const char*)0x0000,      // dec r (word)
    "01001RRR ",                                                (const char*)ValidateDecWordRegister,           (const char*)DefaultTestInit,   (const char*)0xFF00,      // dec r (word)
    "01001RRR ",                                                (const char*)ValidateDecWordRegister,           (const char*)DefaultTestInit,   (const char*)0x0001,      // dec r (word)
    "01001RRR ",                                                (const char*)ValidateDecWordRegister,           (const char*)DefaultTestInit,   (const char*)0x8000,      // dec r (word)
    "01001RRR ",                                                (const char*)ValidateDecWordRegister,           (const char*)DefaultTestInit,   (const char*)0x0010,      // dec r (word)
    "11101011 llllllll ",                                       (const char*)ValidateJmpRel8,                   (const char*)DefaultTestInit,   (const char*)0x0000,      // jmp rel8
    "11101001 llllllll hhhhhhhh ",                              (const char*)ValidateJmpRel16,                  (const char*)DefaultTestInit,   (const char*)0x0000,      // jmp rel16
    "11101010 llllllll hhhhhhhh LLLLLLLL HHHHHHHH ",            (const char*)ValidateJmpInterSeg,               (const char*)DefaultTestInit,   (const char*)0x0000,      // jmp ofs seg
    "11100010 llllllll ",                                       (const char*)ValidateLoop,                      (const char*)DefaultTestInit,   (const char*)0x0001,      // loop (not taken)
    "11100010 llllllll ",                                       (const char*)ValidateLoop,                      (const char*)DefaultTestInit,   (const char*)0x0000,      // loop (taken)
    "1110011W LLLLLLLL ",                                       (const char*)ValidateOutA,                      (const char*)DefaultTestInit,   (const char*)0x0000,      // out ib, al/ax
    "1110011W LLLLLLLL ",                                       (const char*)ValidateOutA,                      (const char*)DefaultTestInit,   (const char*)0x1234,      // out ib, al/ax
    "11111010 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_I),    // cli
    "100010DW MMRRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateMovMRMReg,                 (const char*)RegisterNum,       (const char*)0x1000,      // mov rm<->r
    "100011D0 MM0RRmmm llllllll hhhhhhhh ",                     (const char*)ValidateMovRMSR,                   (const char*)RegisterNum,       (const char*)0x1000,      // mov rm<->sr
    "001RR110 10001000 00000110 llllllll hhhhhhhh ",            (const char*)ValidateMovMRMRegPrefixed,         (const char*)RegisterNum,       (const char*)0x1000,      // segment prefix (via mov prefix:[disp],al)
    "1100011W 11000mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateMovRMImmediate,            (const char*)RegisterNum,       (const char*)0x0003,      // mov rm,i (byte/word) (reg)
    "1100011W 10000mmm llllllll hhhhhhhh LLLLLLLL HHHHHHHH ",   (const char*)ValidateMovRMImmediate,            (const char*)RegisterNum,       (const char*)0x0002,      // mov rm,i (byte/word) (mem)
    "00AAA10W LLLLLLLL HHHHHHHH ",                              (const char*)ValidateAluAImmediate,             (const char*)RegisterNumFlags,  (const char*)(0),         // alu A,i  (carry clear)
    "00AAA10W LLLLLLLL HHHHHHHH ",                              (const char*)ValidateAluAImmediate,             (const char*)RegisterNumFlags,  (const char*)(FLAG_C),    // alu A,i  (carry set)
    "00AAA0DW 11RRRmmm ",                                       (const char*)ValidateAluRMR,                    (const char*)RegisterNum,       (const char*)0x0003,      // alu rm<->r (byte/word) (reg) (no need to check Carry in)
    "00AAA0DW 01RRRmmm llllllll ",                              (const char*)ValidateAluRMR,                    (const char*)RegisterNum,       (const char*)0x0001,      // alu rm<->r (byte/word) (mem) (no need to check Carry in)
    "11111100 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_D),    // cld
    "1110010W LLLLLLLL ",                                       (const char*)ValidateInA,                       (const char*)DefaultTestInit,   (const char*)0x0000,      // in ib, al/ax
    "1111111W MM000mmm llllllll hhhhhhhh ",                     (const char*)ValidateIncRM,                     (const char*)RegisterNum,       (const char*)0x0000,      // inc rm
    "1111111W MM001mmm llllllll hhhhhhhh ",                     (const char*)ValidateDecRM,                     (const char*)RegisterNum,       (const char*)0x0000,      // dec rm
    "100000SW 11AAAmmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateAluRMImmediate,            (const char*)RegisterNum,       (const char*)0x0003,      // alu rm,i (reg)
    "100000SW 01AAAmmm llllllll LLLLLLLL HHHHHHHH ",            (const char*)ValidateAluRMImmediate,            (const char*)RegisterNum,       (const char*)0x0001,      // alu rm,i (mem)
    "11111011 ",                                                (const char*)ValidateFlagSet,                   (const char*)ClearFlags,        (const char*)(FLAG_I),    // sti
    "1101000W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRM,                   (const char*)RegisterNumFlags,  (const char*)0,           // rot rm,1 (C clear)
    "1101000W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRM,                   (const char*)RegisterNumFlags,  (const char*)(FLAG_C),    // rot rm,1 (C set)
    "1111011W MM011mmm llllllll hhhhhhhh ",                     (const char*)ValidateNegRM,                     (const char*)RegisterNum,       (const char*)0,           // neg rm
    "11111101 ",                                                (const char*)ValidateFlagSet,                   (const char*)ClearFlags,        (const char*)(FLAG_D),    // std
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)0,                                         // jcond (flags clear)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_O|FLAG_C|FLAG_S|FLAG_P|FLAG_Z),      // jcond (flags set)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_C|FLAG_Z),                           // jcond (flags set)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_O|FLAG_Z),                           // jcond (flags set)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_C|FLAG_O),                           // jcond (flags set)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_S|FLAG_O),                           // jcond (flags set)
    "0111CCCC llllllll ",                                       (const char*)ValidateJcc,                       (const char*)RegisterNumFlags,  (const char*)(FLAG_S|FLAG_Z),                           // jcond (flags set)
    "11101000 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateCallCW,                    (const char*)RegisterNum,       (const char*)0,           // call cw
    "11111111 MM010mmm llllllll hhhhhhhh ",                     (const char*)ValidateCallRM,                    (const char*)RegisterNum,       (const char*)0,           // call rm
    "1111011W 11000mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateTestRMImmediate,           (const char*)RegisterNum,       (const char*)0x0003,      // TEST rm,i (reg)
    "1111011W 10000mmm llllllll hhhhhhhh LLLLLLLL HHHHHHHH ",   (const char*)ValidateTestRMImmediate,           (const char*)RegisterNum,       (const char*)0x0002,      // TEST rm,i (mem)
    "1010100W LLLLLLLL HHHHHHHH ",                              (const char*)ValidateTestAImmediate,            (const char*)RegisterNumFlags,  (const char*)(0),         // TEST A,i
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)0,           // rot rm,cl (cl=0) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)0,           // rot rm,cl (cl=0) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)1,           // rot rm,cl (cl=1) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)1,           // rot rm,cl (cl=1) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)2,           // rot rm,cl (cl=2) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)2,           // rot rm,cl (cl=2) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)8,           // rot rm,cl (cl=8) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)8,           // rot rm,cl (cl=8) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)16,          // rot rm,cl (cl=16) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)16,          // rot rm,cl (cl=16) (initial carry)
    "000rr110 ",                                                (const char*)ValidatePushSR,                    (const char*)RegisterNum,       (const char*)0,           // push sr
    "01010rrr ",                                                (const char*)ValidatePushRW,                    (const char*)RegisterNum,       (const char*)0,           // push rw
    "1111011W MM100mmm llllllll hhhhhhhh ",                     (const char*)ValidateMulRM,                     (const char*)RegisterNum,       (const char*)0,           // mul rm 
    "1111011W MM100mmm llllllll hhhhhhhh ",                     (const char*)ValidateMulRM,                     (const char*)RegisterNum2,      (const char*)0,           // mul rm
    "1111011W MM100mmm llllllll hhhhhhhh ",                     (const char*)ValidateMulRM,                     (const char*)RegisterNum3,      (const char*)0,           // mul rm
    "1111011W MM101mmm llllllll hhhhhhhh ",                     (const char*)ValidateIMulRM,                    (const char*)RegisterNum,       (const char*)0,           // imul rm 
    "1111011W MM101mmm llllllll hhhhhhhh ",                     (const char*)ValidateIMulRM,                    (const char*)RegisterNum2,      (const char*)0,           // imul rm 
    "1111011W MM101mmm llllllll hhhhhhhh ",                     (const char*)ValidateIMulRM,                    (const char*)RegisterNum3,      (const char*)0,           // imul rm 
    "000rr111 ",                                                (const char*)ValidatePopSR,                     (const char*)RegisterNum,       (const char*)0,           // pop sr
    "01011rrr ",                                                (const char*)ValidatePopRW,                     (const char*)RegisterNum,       (const char*)0,           // pop rw
    "11111111 MM100mmm llllllll hhhhhhhh ",                     (const char*)ValidateJmpRM,                     (const char*)RegisterNum,       (const char*)0,           // jmp rm
    "1111011W MM110mmm llllllll hhhhhhhh ",                     (const char*)ValidateDivRM,                     (const char*)RegisterNum,       (const char*)0,           // div rm 
    "1111011W MM110mmm llllllll hhhhhhhh ",                     (const char*)ValidateDivRM,                     (const char*)RegisterNum2,      (const char*)0,           // div rm 
    "1111011W MM110mmm llllllll hhhhhhhh ",                     (const char*)ValidateDivRM,                     (const char*)RegisterNum3,      (const char*)0,           // div rm 
    "1111011W MM111mmm llllllll hhhhhhhh ",                     (const char*)ValidateIDivRM,                    (const char*)RegisterNum,       (const char*)0,           // idiv rm 
    "1111011W MM111mmm llllllll hhhhhhhh ",                     (const char*)ValidateIDivRM,                    (const char*)RegisterNum2,      (const char*)0,           // idiv rm 
    "1111011W MM111mmm llllllll hhhhhhhh ",                     (const char*)ValidateIDivRM,                    (const char*)RegisterNum3,      (const char*)0,           // idiv rm 
    "11111000 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_C),    // clc
    "11111001 ",                                                (const char*)ValidateFlagSet,                   (const char*)ClearFlags,        (const char*)(FLAG_C),    // stc
    "11110101 ",                                                (const char*)ValidateFlagSet,                   (const char*)ClearFlags,        (const char*)(FLAG_C),    // cmc
    "11110101 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_C),    // cmc
    "11111111 MM110mmm llllllll hhhhhhhh ",                     (const char*)ValidatePushRM,                    (const char*)RegisterNum,       (const char*)0,           // push rm
    "10001111 MM000mmm llllllll hhhhhhhh ",                     (const char*)ValidatePopRM,                     (const char*)RegisterNum,       (const char*)0,           // pop rm
    "101000DW LLLLLLLL HHHHHHHH ",                              (const char*)ValidateMovAXmem,                  (const char*)RegisterNum,       (const char*)0,           // MOV A,[i] & MOV [i],A
    "001RR110 101000DW LLLLLLLL HHHHHHHH ",                     (const char*)ValidateMovAXmemSegOverride,       (const char*)RegisterNum,       (const char*)0,           // segment prefix MOV A,[i] & MOV [i],A
    "11000011 ",                                                (const char*)ValidateRet,                       (const char*)RegisterNumSP,     (const char*)0,           // ret
    "11000011 ",                                                (const char*)ValidateRet,                       (const char*)RegisterNumSP,     (const char*)0x8000,      // ret
    "11000011 ",                                                (const char*)ValidateRet,                       (const char*)RegisterNumSP,     (const char*)0xFFFF,      // ret
    "11000011 ",                                                (const char*)ValidateRet,                       (const char*)RegisterNumSP,     (const char*)0x1010,      // ret
    "11000011 ",                                                (const char*)ValidateRet,                       (const char*)RegisterNumSP,     (const char*)0x9090,      // ret
    "11001011 ",                                                (const char*)ValidateRetF,                      (const char*)RegisterNumSP,     (const char*)0,           // retf
    "11001011 ",                                                (const char*)ValidateRetF,                      (const char*)RegisterNumSP,     (const char*)0x8000,      // retf
    "11001011 ",                                                (const char*)ValidateRetF,                      (const char*)RegisterNumSP,     (const char*)0xFFFF,      // retf
    "11001011 ",                                                (const char*)ValidateRetF,                      (const char*)RegisterNumSP,     (const char*)0x1010,      // retf
    "11001011 ",                                                (const char*)ValidateRetF,                      (const char*)RegisterNumSP,     (const char*)0x9090,      // retf
    "1100F010 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateRetRetFImm,                (const char*)RegisterNumSP,     (const char*)0,           // ret/retf iw
    "1100F010 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateRetRetFImm,                (const char*)RegisterNumSP,     (const char*)0x8000,      // ret/retf iw
    "1100F010 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateRetRetFImm,                (const char*)RegisterNumSP,     (const char*)0xFFFF,      // ret/retf iw
    "1100F010 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateRetRetFImm,                (const char*)RegisterNumSP,     (const char*)0x1010,      // ret/retf iw
    "1100F010 LLLLLLLL HHHHHHHH ",                              (const char*)ValidateRetRetFImm,                (const char*)RegisterNumSP,     (const char*)0x9090,      // ret/retf iw
    "10001101 00RRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateLea,                       (const char*)RegisterNum,       (const char*)0,           // lea r,rm
    "10001101 01RRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateLea,                       (const char*)RegisterNum,       (const char*)1,           // lea r,rm
    "10001101 10RRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateLea,                       (const char*)RegisterNum,       (const char*)2,           // lea r,rm
    "1000011W MMRRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateXchgRM,                    (const char*)RegisterNum,       (const char*)0,           // xchg r,rm
    "10010RRR ",                                                (const char*)ValidateXchgAXrw,                  (const char*)RegisterNum,       (const char*)0,           // xchg AX,rw
    "10011100 ",                                                (const char*)ValidatePushF,                     (const char*)RegisterNumFlags,  (const char*)(0),               // pushf
    "10011100 ",                                                (const char*)ValidatePushF,                     (const char*)RegisterNumFlags,  (const char*)(FLAG_C|FLAG_Z),   // pushf
    "10011100 ",                                                (const char*)ValidatePushF,                     (const char*)RegisterNumFlags,  (const char*)(0xFFFF),          // pushf
    "10011101 ",                                                (const char*)ValidatePopF,                      (const char*)RegisterNumSP,     (const char*)(0),          // popf
    "10011101 ",                                                (const char*)ValidatePopF,                      (const char*)RegisterNumSP,     (const char*)(0x8000),     // popf
    "10011101 ",                                                (const char*)ValidatePopF,                      (const char*)RegisterNumSP,     (const char*)(0xFFFF),     // popf
    "10011101 ",                                                (const char*)ValidatePopF,                      (const char*)RegisterNumSP,     (const char*)(0x1010),     // popf
    "10011101 ",                                                (const char*)ValidatePopF,                      (const char*)RegisterNumSP,     (const char*)(0x9090),     // popf
    "10011000 ",                                                (const char*)ValidateCBW,                       (const char*)RegisterNumAX,     (const char*)(0x0000),     // cbw
    "10011000 ",                                                (const char*)ValidateCBW,                       (const char*)RegisterNumAX,     (const char*)(0x0001),     // cbw
    "10011000 ",                                                (const char*)ValidateCBW,                       (const char*)RegisterNumAX,     (const char*)(0x9900),     // cbw
    "10011000 ",                                                (const char*)ValidateCBW,                       (const char*)RegisterNumAX,     (const char*)(0x0080),     // cbw
    "10011000 ",                                                (const char*)ValidateCBW,                       (const char*)RegisterNumAX,     (const char*)(0x00FF),     // cbw
    "10011001 ",                                                (const char*)ValidateCWD,                       (const char*)RegisterNumAX,     (const char*)(0x0000),     // cwd
    "10011001 ",                                                (const char*)ValidateCWD,                       (const char*)RegisterNumAX,     (const char*)(0x0001),     // cwd
    "10011001 ",                                                (const char*)ValidateCWD,                       (const char*)RegisterNumAX,     (const char*)(0x9999),     // cwd
    "10011001 ",                                                (const char*)ValidateCWD,                       (const char*)RegisterNumAX,     (const char*)(0x8000),     // cwd
    "10011001 ",                                                (const char*)ValidateCWD,                       (const char*)RegisterNumAX,     (const char*)(0xFFFF),     // cwd
    "1111011W MM010mmm llllllll hhhhhhhh ",                     (const char*)ValidateNotRM,                     (const char*)RegisterNum,       (const char*)0,            // not rm
    "11001111 ",                                                (const char*)ValidateIRet,                      (const char*)RegisterNumSP,     (const char*)0,           // iret
    "11001111 ",                                                (const char*)ValidateIRet,                      (const char*)RegisterNumSP,     (const char*)0x8000,      // iret
    "11001111 ",                                                (const char*)ValidateIRet,                      (const char*)RegisterNumSP,     (const char*)0xFFFF,      // iret
    "11001111 ",                                                (const char*)ValidateIRet,                      (const char*)RegisterNumSP,     (const char*)0x1010,      // iret
    "11001111 ",                                                (const char*)ValidateIRet,                      (const char*)RegisterNumSP,     (const char*)0x9090,      // iret
    "10011010 LLLLLLLL HHHHHHHH llllllll hhhhhhhh ",            (const char*)ValidateCallCD,                    (const char*)RegisterNum,       (const char*)0,           // call cd
    "11111111 00011mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateCallFarRM,                 (const char*)RegisterNum,       (const char*)0,           // call FAR rm (mod 0)
    "11111111 01011mmm LLLLLLLL ",                              (const char*)ValidateCallFarRM,                 (const char*)RegisterNum,       (const char*)1,           // call FAR rm (mod 1)
    "11111111 10011mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateCallFarRM,                 (const char*)RegisterNum,       (const char*)2,           // call FAR rm (mod 2)
    "1000010W MMRRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateTestRM,                    (const char*)RegisterNum,       (const char*)0,           // test rm,r
    "11010110 ",                                                (const char*)ValidateSALC,                      (const char*)SetFlags,          (const char*)FLAG_C,      // SALC (carry set)
    "11010110 ",                                                (const char*)ValidateSALC,                      (const char*)ClearFlags,        (const char*)0xFFFF,      // SALC (carry clear)
    "1110110W ",                                                (const char*)ValidateInADX,                     (const char*)DefaultTestInit,   (const char*)0x0000,      // in al/ax,DX
    "1110110W ",                                                (const char*)ValidateInADX,                     (const char*)DefaultTestInit,   (const char*)0x1234,      // in al/ax,DX
    "1110111W ",                                                (const char*)ValidateOutADX,                    (const char*)DefaultTestInit,   (const char*)0x0000,      // out DX, al/ax
    "1110111W ",                                                (const char*)ValidateOutADX,                    (const char*)DefaultTestInit,   (const char*)0x1234,      // out DX, al/ax
    "11111111 00101mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateJmpFarRM,                  (const char*)RegisterNum,       (const char*)0,           // jmp FAR rm (mod 0)
    "11111111 01101mmm LLLLLLLL ",                              (const char*)ValidateJmpFarRM,                  (const char*)RegisterNum,       (const char*)1,           // jmp FAR rm (mod 1)
    "11111111 10101mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateJmpFarRM,                  (const char*)RegisterNum,       (const char*)2,           // jmp FAR rm (mod 2)
    "11000100 00RRRmmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateLES,                       (const char*)RegisterNum,       (const char*)0,           // LES r,m (mod 0)
    "11000100 01RRRmmm LLLLLLLL ",                              (const char*)ValidateLES,                       (const char*)RegisterNum,       (const char*)1,           // LES r,m (mod 1)
    "11000100 10RRRmmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateLES,                       (const char*)RegisterNum,       (const char*)2,           // LES r,m (mod 2)
    "11000101 00RRRmmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateLDS,                       (const char*)RegisterNum,       (const char*)0,           // LDS r,m (mod 0)
    "11000101 01RRRmmm LLLLLLLL ",                              (const char*)ValidateLDS,                       (const char*)RegisterNum,       (const char*)1,           // LDS r,m (mod 1)
    "11000101 10RRRmmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateLDS,                       (const char*)RegisterNum,       (const char*)2,           // LDS r,m (mod 2)
    "10011011 ",                                                (const char*)ValidateWait,                      (const char*)RegisterNum,       (const char*)0,           // Wait
    "1010101W ",                                                (const char*)ValidateSTOS,                      (const char*)RegisterNumFlags,  (const char*)0,           // STOS (D clear)
    "1010101W ",                                                (const char*)ValidateSTOS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // STOS (D set)
    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP STOS (CX==0) 
    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP STOS (CX==5) 
    "1010110W ",                                                (const char*)ValidateLODS,                      (const char*)RegisterNumFlags,  (const char*)0,           // LODS (D clear)
    "1010110W ",                                                (const char*)ValidateLODS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // LODS (D set)
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP LODS (CX==0)
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP LODS (CX==5)
    "1010010W ",                                                (const char*)ValidateMOVS,                      (const char*)RegisterNumFlags,  (const char*)0,           // MOVS (D clear)
    "1010010W ",                                                (const char*)ValidateMOVS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // MOVS (D set)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP MOVS (CX==0)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP MOVS (CX==5)
    "10011110 ",                                                (const char*)ValidateSAHF,                      (const char*)RegisterNumFlags,  (const char*)0,           // SAHF
    "10011111 ",                                                (const char*)ValidateLAHF,                      (const char*)RegisterNumFlags,  (const char*)(0xAA55),    // LAHF
    "11011OOO MMooommm llllllll hhhhhhhh ",                     (const char*)ValidateEscRM,                     (const char*)RegisterNum,       (const char*)0,           // ESC rm (OOOooo 6bit instruction code)
    "11010111 ",                                                (const char*)ValidateXLAT,                      (const char*)RegisterNumAX,     (const char*)0,           // XLAT
    "11010111 ",                                                (const char*)ValidateXLAT,                      (const char*)RegisterNumAX,     (const char*)0xFF,        // XLAT
    "11010111 ",                                                (const char*)ValidateXLAT,                      (const char*)RegisterNumAX,     (const char*)0x01,        // XLAT
    "11010111 ",                                                (const char*)ValidateXLAT,                      (const char*)RegisterNumAX,     (const char*)0x80,        // XLAT
    "001RR110 11010111 ",                                       (const char*)ValidateXLATPrefixed,              (const char*)RegisterNumAX,     (const char*)0,           // XLAT segment prefixed
    "001RR110 11010111 ",                                       (const char*)ValidateXLATPrefixed,              (const char*)RegisterNumAX,     (const char*)0xFF,        // XLAT segment prefixed 
    "001RR110 11010111 ",                                       (const char*)ValidateXLATPrefixed,              (const char*)RegisterNumAX,     (const char*)0x01,        // XLAT segment prefixed
    "001RR110 11010111 ",                                       (const char*)ValidateXLATPrefixed,              (const char*)RegisterNumAX,     (const char*)0x80,        // XLAT segment prefixed
    "1010011W ",                                                (const char*)ValidateCMPS,                      (const char*)RegisterNumFlags,  (const char*)0,           // CMPS (D clear)
    "1010011W ",                                                (const char*)ValidateCMPS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // CMPS (D set)
    "1111001Z 1010011W ",                                       (const char*)ValidateCMPSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REPE/NE CMPS (CX==0)
    "1111001Z 1010011W ",                                       (const char*)ValidateCMPSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REPE/NE CMPS (CX==5)
    "1010111W ",                                                (const char*)ValidateSCAS,                      (const char*)RegisterNumFlags,  (const char*)0,           // SCAS (D clear)
    "1010111W ",                                                (const char*)ValidateSCAS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // SCAS (D set)
    "1111001Z 1010111W ",                                       (const char*)ValidateSCASREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REPE/NE SCAS (CX==0)
    "1111001Z 1010111W ",                                       (const char*)ValidateSCASREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REPE/NE SCAS (CX==5)
    "11100011 llllllll ",                                       (const char*)ValidateJCXZ,                      (const char*)RegisterNumCX,     (const char*)0x0000,      // JCXZ
    "11100011 llllllll ",                                       (const char*)ValidateJCXZ,                      (const char*)RegisterNumCX,     (const char*)0x1000,      // JCXZ
    "1110000Z llllllll ",                                       (const char*)ValidateLoopENE,                   (const char*)CXFlags,           (const char*)0x00010000,              // loop(e/ne)
    "1110000Z llllllll ",                                       (const char*)ValidateLoopENE,                   (const char*)CXFlags,           (const char*)0x00000000,              // loop(e/ne)
    "1110000Z llllllll ",                                       (const char*)ValidateLoopENE,                   (const char*)CXFlags,           (const char*)(0x00010000 | FLAG_Z),   // loop(e/ne)
    "1110000Z llllllll ",                                       (const char*)ValidateLoopENE,                   (const char*)CXFlags,           (const char*)(0x00000000 | FLAG_Z),   // loop(e/ne)
    "00100111 ",                                                (const char*)ValidateDAADAS,                    (const char*)RegisterNumAX,     (const char*)(0x14AE|((FLAG_C|FLAG_P|FLAG_A)<<16)), // DAA
    "00100111 ",                                                (const char*)ValidateDAADAS,                    (const char*)RegisterNumAX,     (const char*)(0x342E|((FLAG_C|FLAG_A)<<16)),        // DAA
    "00101111 ",                                                (const char*)ValidateDAADAS,                    (const char*)RegisterNumAX,     (const char*)(0x88EE|((FLAG_C|FLAG_P|FLAG_A|FLAG_S)<<16)), // DAS
    "00110111 ",                                                (const char*)ValidateAAAAAS,                    (const char*)RegisterNumAX,     (const char*)(0x8022),                          // AAA
    "00110111 ",                                                (const char*)ValidateAAAAAS,                    (const char*)RegisterNumAX,     (const char*)(0x802C|((FLAG_C|FLAG_A)<<16)),    // AAA
    "00111111 ",                                                (const char*)ValidateAAAAAS,                    (const char*)RegisterNumAX,     (const char*)(0x8022|((FLAG_S)<<16)),           // AAS
    "00111111 ",                                                (const char*)ValidateAAAAAS,                    (const char*)RegisterNumAX,     (const char*)(0x809F|((FLAG_C|FLAG_A|FLAG_S)<<16)), // AAS
    "11010101 LLLLLLLL ",                                       (const char*)ValidateAAD,                       (const char*)RegisterNumAX,     (const char*)0x0000,        // AAD
    "11010101 LLLLLLLL ",                                       (const char*)ValidateAAD,                       (const char*)RegisterNumAX,     (const char*)0x0102,        // AAD
    "11010101 LLLLLLLL ",                                       (const char*)ValidateAAD,                       (const char*)RegisterNumAX,     (const char*)0xFFFF,        // AAD
    "11010100 00001010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x0A00,        // AAM
    "11010100 00001010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x0A02,        // AAM
    "11010100 00001010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x0AFF,        // AAM
    "11010100 00000010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x0200,        // AAM
    "11010100 00000010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x0202,        // AAM
    "11010100 00000010 ",                                       (const char*)ValidateAAM,                       (const char*)RegisterNumAX,     (const char*)0x02FF,        // AAM
    "11001110 ",                                                (const char*)ValidateINTO,                      (const char*)RegisterNumFlags,  (const char*)(0),           // INTO (overflow clr)
    "11001110 ",                                                (const char*)ValidateINTO,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_O),      // INTO (overflow set)
    "11001100 ",                                                (const char*)ValidateINT3,                      (const char*)RegisterNum,       (const char*)(0),           // INT3
    "11001101 LLLLLLLL ",                                       (const char*)ValidateINTn,                      (const char*)RegisterNum,       (const char*)(0),           // INT n
#endif
    // END MARKER
    0
};

int NumFromTestCode(const char* testData)
{
    char done[256]={0};
    int counter=0;
    while (*testData!=0)
    {
        switch (*testData)
        {
            case '0':
            case '1':
            case ' ':
                break;
            default:
                if (done[*testData]==0)
                {
                    done[*testData]=1;
                    int numBits = NumMatch(testData, *testData);
                    counter+=FetchTestBitCountForCode(*testData,numBits);
                }
                break;
        }
        testData++;
    }

    return 1<<counter;
}

void NextTestState(const char* testData, int counter, int testCnt)
{
    uint8_t byte=0;
    int romOffs=0xFFFF0;
    int mask=testCnt>>1;
    
    //printf("%08X : %08X\n",counter,mask);
    while (*testData!=0)
    {
        switch (*testData)
        {
        case '0':
            byte<<=1;
            break;
        case '1':
            byte<<=1;
            byte|=1;
            break;
        case ' ':
            //printf("Writing byte to RAM [%02X]<-%02X\n",romOffs,byte);
            RAM[romOffs++]=byte;
            byte=0;
            break;
        default:
            char code = *testData;
            int numCodeBits = NumMatch(testData, code);
            //printf("Code Bits : %d\n", numCodeBits);
            int numSpecialBits = FetchTestNumBitsForSpecial(code);
            //printf("Special Bits : %d\n", numSpecialBits);
            int numBits = numSpecialBits;
            if (numBits==0)
                numBits=numCodeBits;
            
            int v=0;
            for (int a=0;a<numBits;a++)
            {
                v<<=1;
                v|=counter&mask?1:0;
                mask>>=1;
            }
            //printf("V (%c) : %d\n", code, v);

            if (numSpecialBits!=0)
            {
                v=RealValueFromIndex(code,v);
                //printf("V adjusted (%c) : %d\n", code, v);
            }

            int vMask=1<<(numCodeBits-1);
            for (int a=0;a<numCodeBits;a++)
            {
                byte<<=1;
                byte|=v&vMask?1:0;
                vMask>>=1;
            }
            testData+=numCodeBits-1;
            break;
        }
        testData++;
    }
}

int testPos=0;
int testState=-1;
int testCntr=0;

int currentTestCnt;
int currentTestCounter;

typedef void (*Initialiser)(int);
typedef int (*Validate)(const char*,int,int,int);

int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
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
                testState++;
                currentTestCnt = NumFromTestCode(testArray[testPos*TEST_MULT+0]);
                currentTestCounter=0;
                printf("Num Tests for (%s) %d : %d\n",testArray[testPos*TEST_MULT+0],testPos,currentTestCnt);
            }
            break;
        case 1:
            tb->RESET=1;
            segOverride=3;
            printf("Running test (%s) %d : %d / %d\r",testArray[testPos*TEST_MULT+0],testPos,currentTestCounter+1, currentTestCnt);
            NextTestState(testArray[testPos*TEST_MULT+0],currentTestCounter,currentTestCnt);
            testState++;
            testCntr=16;
            break;
        case 2:
            tb->RESET=0;
            if (tb->top->eu->executionState == 0x1FD)   // Instruction Fetch
            {
                ((Initialiser)testArray[testPos*TEST_MULT+2])((int)(intptr_t)testArray[testPos*TEST_MULT+3]);
                testState++;
            }
            break;
        case 3:
            if (tb->top->eu->executionState != 0x1FD)
            {
                captureIdx=0;
                testState++;
                tb->top->eu->TRACE_MODE=1;  // prevent further instruction execution
            }
            break;
        case 4:
            if (tb->top->eu->executionState == 0x1FD && tb->CLK==1 && (tb->top->eu->flush==0) && (tb->top->biu->suspending==0) && (tb->top->biu->indirectBusOpInProgress==0))
            {
                captureIdx=0;
                readWriteFailure=0;
                int vRes = ((Validate)testArray[testPos*TEST_MULT+1])(testArray[testPos*TEST_MULT+0],currentTestCounter,currentTestCnt,(int)(intptr_t)testArray[testPos*TEST_MULT+3]);
                if (readWriteFailure!=0 || !vRes)
                {
                    printf("\nERROR\n");
                    testState=99;
                }
                else
                {
                    currentTestCounter++;
                    if (currentTestCounter==currentTestCnt)
                    {
                        printf("\nAll Tests PASSED (%s) %d\n",testArray[testPos*TEST_MULT+0],testPos);
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

void SimulateInterface(Vtop *tb)
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
#if 1
    if (tb->RD_n==1 && lastRead==0)
    {
        if (tb->top->biu->indirectBusCycle==1 && captureIdx<MAX_READWRITE_CAPTURE)
        {
            readWriteLatchedAddress[captureIdx]=latchedAddress;
            readWriteLatchedType[captureIdx]=tb->IOM;
            lastReadCapture[captureIdx++]=tb->inAD;

#if SHOW_READS
            if (tb->IOM==1)
                printf("Reading from RAM : %08X -> %02X\n", latchedAddress, tb->inAD);
            else
                printf("Reading from IO : %08X -> %02X\n", latchedAddress, tb->inAD);
#endif
        }
    }
#endif
    if (tb->WR_n==1 && lastWrite==0)    // Only log once per write
    {
        if (tb->top->biu->indirectBusOpInProgress==1 && captureIdx<MAX_READWRITE_CAPTURE)
        {
            readWriteLatchedAddress[captureIdx]=latchedAddress&0xFFFFF;
            readWriteLatchedType[captureIdx]=tb->IOM;
            lastWriteCapture[captureIdx++]=tb->outAD;
 #if SHOW_WRITES
            if (tb->IOM==1)
                printf("\nWrite To Ram : %08X <- %02X\n",latchedAddress,tb->outAD);
            else
                printf("\nWrite To IO : %08X <- %02X\n",latchedAddress,tb->outAD);
#endif
        }
    }
    lastWrite=tb->WR_n;
    lastRead=tb->RD_n;
}

void tick(Vtop *tb, VerilatedVcdC* trace, int ticks)
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

int doNTicks(Vtop *tb, VerilatedVcdC* trace, int ticks, int n)
{
    for (int a=0;a<n;a++)
    {
        tick(tb,trace,ticks+a);
    }
    return ticks+n;
}


/*

    input           clk_int,
    input           reset_n,
    input           rdy,
    input           nmi,
    input           intr,

    output          o_inta,
    output          o_ale,
    output          o_rd_n,
    output          o_wr_n,
    output          o_iom,
    output          o_dtr,
    output          o_den,
    output          o_oe,
    output [19:0]   o_ad_out,
    input  [7:0]    ad_in,

    output  o_eu_flag_i,

*/

int ThisFileTestsMain(int argc, char** argv)
{

#if TRACE
	Verilated::traceEverOn(true);
#endif

	tb = new Vtop;

	VerilatedVcdC *trace = new VerilatedVcdC;

#if TRACE
	tb->trace(trace, 99);
	trace->open("trace.vcd");
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

	return EXIT_SUCCESS;
}

int main(int argc,char** argv)
{
	Verilated::commandArgs(argc,argv);

    int retVal = EXIT_SUCCESS;
#if IRQ_WAIT_TESTS
    retVal = IrqWaitTestsMain(argc,argv);
    if (retVal != EXIT_SUCCESS) return retVal;
#endif
#if ARITH_TESTS
    retVal = ArithTestsMain(argc,argv);
    if (retVal != EXIT_SUCCESS) return retVal;
#endif
#if UNIT_TESTS
    retVal = ThisFileTestsMain(argc,argv);
    if (retVal != EXIT_SUCCESS) return retVal;
#endif
    return retVal;
}