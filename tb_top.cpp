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

#define TRACE  0

#define ARITH_TESTS 0
#define UNIT_TEST 1
#define ALL_TESTS 1
#define TINY_ROM 0
#define VIDEO_BOOTSTRAP 0
#define TEST_P88 0
#define P88_FILEPATH "/home/snax/ROMS/KONIX_ATTACK_OF_THE_MUTANT_CAMELS_V0_4_FIXED.P88"

#define CLK_DIVISOR 8

#define SHOW_WRITES 0
#define SHOW_READS 0

#define TICK_LIMIT  0 && 300000

int internalClock=CLK_DIVISOR;
int ck=0;

uint32_t latchedAddress;
int lastWrite=1,lastRead=1;
Vtop *tb;

#if UNIT_TEST || ARITH_TESTS

#define RAMSIZE 1024*1024
unsigned char RAM[RAMSIZE];

// OF|DF|IF|TF|SF|ZF|U |AF|U |PF|U |CF

int TestFlags(int hFlags,int tFlags, int flagMask)
{
    if (flagMask & FLAG_Z)
    {
        int isZeroH = (hFlags & FLAG_Z)?1:0;
        int isZeroT = (tFlags & FLAG_Z)?1:0;
        if (isZeroT!=isZeroH)
        {
            printf("Z flag mismatch %d != %d\n", isZeroH, isZeroT);
            return 0;
        }
    }
    if (flagMask & FLAG_S)
    {
        int isSignH = (hFlags & FLAG_S)?1:0;
        int isSignT = (tFlags & FLAG_S)?1:0;
        if (isSignH!=isSignT)
        {
            printf("S flag mismatch %d != %d\n", isSignH, isSignT);
            return 0;
        }
    }
    if (flagMask & FLAG_C)
    {
        int isCarryH = (hFlags & FLAG_C)?1:0;
        int isCarryT = (tFlags & FLAG_C)?1:0;
        if (isCarryH!=isCarryT)
        {
            printf("C flag mismatch %d != %d\n", isCarryH, isCarryT);
            return 0;
        }
    }
    if (flagMask & FLAG_A)
    {
        int isAuxCarryH = (hFlags & FLAG_A)?1:0;
        int isAuxCarryT = (tFlags & FLAG_A)?1:0;
        if (isAuxCarryH!=isAuxCarryT)
        {
            printf("A flag mismatch %d != %d\n", isAuxCarryH, isAuxCarryT);
            return 0;
        }
    }
    if (flagMask & FLAG_O)
    {
        int isOverflowH = (hFlags & FLAG_O)?1:0;
        int isOverflowT = (tFlags & FLAG_O)?1:0;
        if (isOverflowH!=isOverflowT)
        {
            printf("O flag mismatch %d != %d\n", isOverflowH, isOverflowT);
            return 0;
        }
    }
    if (flagMask & FLAG_P)
    {
        int isParityH = (hFlags & FLAG_P)?1:0;
        int isParityT = (tFlags & FLAG_P)?1:0;
        if (isParityH!=isParityT)
        {
            printf("P flag mismatch %d != %d\n", isParityH, isParityT);
            return 0;
        }
    }

    return 1;

}

#endif

#if UNIT_TEST       // RUN ON 64BIT X86 since i cheated and use asm for flags

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
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        return -1;
    }
    if (word)
    {
        if (secondAddress != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", secondAddress, readWriteLatchedAddress[captureIdx+1]);
            return -1;
        }
        if (readWriteLatchedType[captureIdx+1]!=1)
        {
            printf("Failed - Second Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx+1]);
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
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        return -1;
    }
    if (word)
    {
        if (secondAddress != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", secondAddress, readWriteLatchedAddress[captureIdx+1]);
            return -1;
        }
        if (readWriteLatchedType[captureIdx+1]!=1)
        {
            printf("Failed - Second Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx+1]);
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
    "1010101W ",                                                (const char*)ValidateSTOS,                      (const char*)RegisterNumFlags,  (const char*)0,           // STOS (D clear)
    "1010101W ",                                                (const char*)ValidateSTOS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // STOS (D set)
    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP STOS (CX==0) 
    "1111001Z 1010101W ",                                       (const char*)ValidateSTOSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP STOS (CX==5) 
    "1110010W LLLLLLLL ",                                       (const char*)ValidateInA,                       (const char*)DefaultTestInit,   (const char*)0x0000,      // in ib, al/ax
    "1111111W MM000mmm llllllll hhhhhhhh ",                     (const char*)ValidateIncRM,                     (const char*)RegisterNum,       (const char*)0x0000,      // inc rm
    "1111111W MM001mmm llllllll hhhhhhhh ",                     (const char*)ValidateDecRM,                     (const char*)RegisterNum,       (const char*)0x0000,      // dec rm
    "1010110W ",                                                (const char*)ValidateLODS,                      (const char*)RegisterNumFlags,  (const char*)0,           // LODS (D clear)
    "1010110W ",                                                (const char*)ValidateLODS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // LODS (D set)
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP LODS (CX==0)
    "1111001Z 1010110W ",                                       (const char*)ValidateLODSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP LODS (CX==5)
    "1010010W ",                                                (const char*)ValidateMOVS,                      (const char*)RegisterNumFlags,  (const char*)0,           // MOVS (D clear)
    "1010010W ",                                                (const char*)ValidateMOVS,                      (const char*)RegisterNumFlags,  (const char*)(FLAG_D),    // MOVS (D set)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)0,           // REP MOVS (CX==0)
    "1111001Z 1010010W ",                                       (const char*)ValidateMOVSREP,                   (const char*)RegisterNumCX,     (const char*)5,           // REP MOVS (CX==5)
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
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)1,           // rot rm,cl (cl=2) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)1,           // rot rm,cl (cl=2) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)1,           // rot rm,cl (cl=8) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)1,           // rot rm,cl (cl=8) (initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclNoCarry,          (const char*)RegisterNumCX,     (const char*)1,           // rot rm,cl (cl=16) (no initial carry)
    "1101001W MMSSSmmm llllllll hhhhhhhh ",                     (const char*)ValidateShiftRMclCarry,            (const char*)RegisterNumCXCarry,(const char*)1,           // rot rm,cl (cl=16) (initial carry)
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
#endif
    // TODO ADD TESTS FOR  : HLT, irq
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
                if (!((Validate)testArray[testPos*TEST_MULT+1])(testArray[testPos*TEST_MULT+0],currentTestCounter,currentTestCnt,(int)(intptr_t)testArray[testPos*TEST_MULT+3]))
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

#elif TINY_ROM

// B8 34 12     mov $1234,ax                     4                32
//loop:
// E7 0D        out ax,$d                        10
// 40           inc ax                           2
// EB FB        jmp loop                         15

#define ROMSIZE 16
const unsigned char ROM[16] = {0xb8, 0x34, 0x12, 0xE7, 0x0D, 0x40, 0xEB, 0xFB, /*NOT EXECUTED BYTES FOLLOWING*/ 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};

#elif VIDEO_BOOTSTRAP

#define ROMSIZE 64
const unsigned char ROM[64] = {
0xB8,
0x3C,
0x00,
0xE7,
0x02,
0xB8,
0x03,
0x01,
0xE7,
0x11,
0xB0,
0x0F,
0xE6,
0x0B,
0xE6,
0x16,
0xB0,
0x01,
0xE6,
0x13,
0xB0,
0x01,
0xE6,
0x0C,
0xB0,
0x00,
0xE6,
0x0F,
0xE6,
0x10,
0xB8,
0x00,
0x01,
0xE7,
0x08,
0xE6,
0x0A,
0xE7,
0x0D,
0x40,
0xEB,
0xFB,
0x00,
0x00,
0x00,
0x00,
0x00,
0x00,
0xEA,
0x00,
0x00,
0x00,
0x80,
0x00,
0x00,
0x00};
#elif TEST_P88

#define ROMSIZE 1024*1024
unsigned char ROM[ROMSIZE];

#define SEGTOPHYS(seg,off)	( ((seg)<<4) + (off) )				// Convert Segment,offset pair to physical address
int HandleLoadSection(FILE* inFile)
{
	uint16_t	segment,offset;
	uint16_t	size;
	int		a=0;
	uint8_t		byte;

	if (2!=fread(&segment,1,2,inFile))
	{
		printf("Failed to read segment for LoadSection\n");
		exit(1);
	}
	if (2!=fread(&offset,1,2,inFile))
	{
		printf("Failed to read offset for LoadSection\n");
		exit(1);
	}
	fseek(inFile,2,SEEK_CUR);		// skip unknown
	if (2!=fread(&size,1,2,inFile))
	{
		printf("Failed to read size for LoadSection\n");
		exit(1);
	}

	printf("Found Section Load Memory : %04X:%04X   (%08X bytes)\n",segment,offset,size);

	for (a=0;a<size;a++)
	{
		if (1!=fread(&byte,1,1,inFile))
		{
			printf("Failed to read data from LoadSection\n");
			exit(1);
		}
        ROM[(a+SEGTOPHYS(segment,offset))&(ROMSIZE-1)] = byte;
	}

	return 8+size;
}

int HandleExecuteSection(FILE* inFile)
{
	uint16_t	segment,offset;
	
	if (2!=fread(&segment,1,2,inFile))
	{
		printf("Failed to read segment for ExecuteSection\n");
		exit(1);
	}
	if (2!=fread(&offset,1,2,inFile))
	{
		printf("Failed to read offset for ExecuteSection\n");
		exit(1);
	}

    //offset+=0x2d0e;  // HACK IMUL loc
    offset+=0x11d;

    // Create lJMP in RESET address
    ROM[0xFFFF0&(ROMSIZE-1)]=0xEA;
    ROM[0xFFFF1&(ROMSIZE-1)]=offset;
    ROM[0xFFFF2&(ROMSIZE-1)]=offset>>8;
    ROM[0xFFFF3&(ROMSIZE-1)]=segment;
    ROM[0xFFFF4&(ROMSIZE-1)]=segment>>8;


	printf("Found Section Execute : %04X:%04X\n",segment,offset);

	return 4;
}

void LoadP88(const char* path)
{
    FILE *p88 = fopen(path, "rb");
    fseek(p88,0,SEEK_END);
    long size = ftell(p88);
    fseek(p88,0,SEEK_SET);

    while (size)
    {
        unsigned char t;
        if (1!=fread(&t, 1,1, p88))
        {
            printf("FAILED TO READ SECTION\n");
            exit(1);
        }
        size--;

        switch (t)
        {
            case 0xFF:
                break;
            case 0xC8:
                size-=HandleLoadSection(p88);
                break;
            case 0xCA:
                size-=HandleExecuteSection(p88);
                break;
        }
    }
}

int testState=1;

unsigned char PeekByte(unsigned int address)
{
    return ROM[address & (ROMSIZE-1)];
}

#include "disasm.c"

int startDebuggingAddress = 0x7007D;
int showDebugger=1;

#define RAND_IO_SIZE 16
int IORand[RAND_IO_SIZE]={0x00,0x00,0xFF,0xFF,0x00,0x80,0xFF,0x7F,0x11,0x11,0x22,0x22,0x33,0x33,0x44,0x44};
int randomIOIdx=0;

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
    if (tb->RD_n==0 && lastRead==1)
    {
        if (tb->IOM==1)
        {
            tb->inAD = ROM[latchedAddress & (ROMSIZE-1)];
            printf("Read RAM : %08X -> %02X\n", latchedAddress, tb->inAD);
        }
        else
        {
            tb->inAD = IORand[(randomIOIdx++)&(RAND_IO_SIZE-1)];
            printf("Read IO : %08X -> %02X\n", latchedAddress, tb->inAD);
        }
    }
    if (tb->WR_n==1 && lastWrite==0)    // Only log once per write
    {
        if (tb->IOM==1)
        {
            ROM[latchedAddress & (ROMSIZE-1)]=tb->outAD;
            printf("Write To Ram : %08X <- %02X\n",latchedAddress,tb->outAD);
        }
        else
            printf("Write To IO : %08X <- %02X\n",latchedAddress,tb->outAD);
    }
    lastWrite=tb->WR_n;
    lastRead=tb->RD_n;
}

int tickLimit=-1;

int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    if (tickLimit!=-1 && ticks>tickLimit)
        return 1;
    if (TICK_LIMIT)
    {
        if (ticks>TICK_LIMIT)
            return 1;
    }

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

                int address = (tb->top->biu->REGISTER_CS*16) + (tb->top->biu->REGISTER_IP - tb->top->biu->qSize);
                if (address == startDebuggingAddress)
                    showDebugger=1;
                if (showDebugger)
                {
                    // Dump State 

                    InStream a;
                    a.bytesRead=0;
                    a.curAddress=address;
                    a.findSymbol=NULL;
                    a.useAddress=1;
                    Disassemble(&a,0);

                    printf("\nAX : %04X | BX : %04X | CX : %04X | DX : %04X\n", tb->top->eu->AX, tb->top->eu->BX, tb->top->eu->CX, tb->top->eu->DX);
                    printf("BP : %04X | SP : %04X | DI : %04X | SI : %04X\n", tb->top->eu->BP, tb->top->eu->SP, tb->top->eu->DI, tb->top->eu->SI);
                    printf("CS : %04X | DS : %04X | ES : %04X | SS : %04X\n", tb->top->biu->REGISTER_CS, tb->top->biu->REGISTER_DS, tb->top->biu->REGISTER_ES, tb->top->biu->REGISTER_SS);
                    printf("FLAGS : %04X    O  D  I  T  S  Z  -  A  -  P  -  C\n", tb->top->eu->FLAGS);
                    printf("                %s  %s  %s  %s  %s  %s  %s  %s  %s  %s  %s  %s\n",
                        tb->top->eu->FLAGS & 0x800 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x400 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x200 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x100 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x080 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x040 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x020 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x010 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x008 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x004 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x002 ? "1" : "0",
                        tb->top->eu->FLAGS & 0x001 ? "1" : "0");
                    printf("\nCS:IP : %05X    ", address);
                    for (int b=0;b<a.bytesRead;b++)
                    {
                        printf("%02X ", PeekByte(address+b));
                    }
                    for (int b=a.bytesRead;b<9;b++)
                    {
                        printf("   ");
                    }
                    printf("%s\n",GetOutputBuffer());
                    int v=getchar();
                    printf("%d\n",v);
                    if (v=='f')
                    {
                        tb->top->eu->AX=0x80;
                        tb->top->eu->CX=5;
                        tickLimit=ticks+2000;
                        getchar();// consume CR
                        return 0;
                    }
                    else
                    {
                        if (v!=10)
                            return 1;
                    }
                }
                
                tb->top->eu->TRACE_MODE=0;
                testState=1;
            }
            break;
        case 99:
            return 1;
    }

    return 0;
}

#endif

#if ARITH_TESTS

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
const char* testArray[]={ 
    // Byte Tests
#if 1
//    "\x00""\xC1",       (const char*)2,         (const char*)CheckAddResultB,       "add al,cl",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)0,
//    "\x10""\xC1",       (const char*)2,         (const char*)CheckAdcResultB,       "adc al,cl",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)1,
//    "\xF6""\xD9",       (const char*)2,         (const char*)CheckNegResultB,       "neg cl",       (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)2,
//    "\xF6""\xE1",       (const char*)2,         (const char*)CheckMulB,             "mul cl",       (const char*)(FLAG_O|FLAG_C),                                   (const char*)3,
//    "\xF6""\xE9",       (const char*)2,         (const char*)CheckIMulB,            "imul cl",      (const char*)(FLAG_O|FLAG_C),                                   (const char*)3,
//    "\xF6""\xF1",       (const char*)2,         (const char*)GuardCheckDivB,        "div cl",       (const char*)(0),                                               (const char*)6,
    "\xF6""\xF9",       (const char*)2,         (const char*)GuardCheckIDivB,       "idiv cl",      (const char*)(0),                                               (const char*)6,
#endif    


    (const char*)1,     0,                      0,                                  0,              0,                                                              0,// switch to word based


    // Word Tests
#if 1
    //"\x01""\xC1",       (const char*)2,         (const char*)CheckAddResultW,       "add cx,ax",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)0,
    //"\x11""\xC1",       (const char*)2,         (const char*)CheckAdcResultW,       "adc cx,ax",    (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)1,
    //"\xF7""\xD9",       (const char*)2,         (const char*)CheckNegResultW,       "neg cx",       (const char*)(FLAG_O|FLAG_S|FLAG_Z|FLAG_A|FLAG_P|FLAG_C),       (const char*)2,
//    "\xF7""\xE1",       (const char*)2,         (const char*)CheckMulW,             "mul cx",       (const char*)(FLAG_O|FLAG_C),                                   (const char*)4,
//    "\xF7""\xE9",       (const char*)2,         (const char*)CheckIMulW,            "imul cx",      (const char*)(FLAG_O|FLAG_C),                                   (const char*)4,
    //"\xF7""\xF1",       (const char*)2,         (const char*)GuardCheckDivW,            "div cx",      (const char*)(0),                                   (const char*)5,
    "\xF7""\xF9",       (const char*)2,         (const char*)GuardCheckIDivW,           "idiv cx",      (const char*)(0),                                   (const char*)5,
#endif
    0};

int testPos=0;
int testState=-1;
int testCntr=0;
int byteMode=1;

long currentTestCnt;
long currentTestCounter;

typedef int (*Validate0)(int,int,int,int);
typedef int (*Validate1)(int,int,int,int,int);
typedef int (*Validate2)(int,int,int);
typedef int (*Validate3)(int,int,int,int,int,int);

int operandA,operandB,cIn,operandC;

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
#if TRACE
                        operandA=0xFF00;
                        operandB=0x02;
                        currentTestCnt=1;
#else
                        operandA=currentTestCounter&0xFFFF;
                        operandB=(currentTestCounter>>16)&0xFF;
                        cIn=(currentTestCounter>>24)&1;
#endif
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
                    //testState=99;
                }
                //else
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
}
#endif



#if !UNIT_TEST && !TEST_P88 && !ARITH_TESTS

#define RAND_IO_SIZE 16
int IORand[RAND_IO_SIZE]={0x00,0x00,0xFF,0xFF,0x00,0x80,0xFF,0x7F,0x11,0x11,0x22,0x22,0x33,0x33,0x44,0x44};
int randomIOIdx=0;

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
    if (tb->RD_n==0 && lastRead==1)
    {
        if (tb->IOM==1)
        {
            tb->inAD = ROM[latchedAddress & (ROMSIZE-1)];
            printf("Read RAM : %08X -> %02X\n", latchedAddress, tb->inAD);
        }
        else
        {
            tb->inAD = IORand[(randomIOIdx++)&(RAND_IO_SIZE-1)];
            printf("Read IO : %08X -> %02X\n", latchedAddress, tb->inAD);
        }
    }
    if (tb->WR_n==1 && lastWrite==0)    // Only log once per write
    {
        if (tb->IOM==1)
            printf("Write To Ram : %08X <- %02X\n",latchedAddress,tb->outAD);
        else
            printf("Write To IO : %08X <- %02X\n",latchedAddress,tb->outAD);
    }
    lastWrite=tb->WR_n;
    lastRead=tb->RD_n;
}

#endif

#if !UNIT_TEST && !TEST_P88 && !ARITH_TESTS
int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    return ticks >= 200000;
}

#endif

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

int main(int argc, char** argv)
{
	Verilated::commandArgs(argc,argv);

#if TRACE
	Verilated::traceEverOn(true);
#endif

	tb = new Vtop;

	VerilatedVcdC *trace = new VerilatedVcdC;

#if TRACE
	tb->trace(trace, 99);
	trace->open("trace.vcd");
#endif

#if TEST_P88
    LoadP88(P88_FILEPATH);
#endif

    tb->RESET=1;
    tb->READY = 1;
    tb->NMI = 0;
    tb->INTR=0;
    tb->HOLD=0;

    // Lets check the various signals according to spec, start of reset for a few ticks
	int ticks=1;
    ticks = doNTicks(tb,trace,ticks,100);

    tb->RESET=0;
/*
    // Test routines

    ticks = doNTicks(tb,trace,ticks,CLK_DIVISOR*20);
    tb->HOLD=1;
    ticks = doNTicks(tb,trace,ticks,CLK_DIVISOR*20);
    tb->HOLD=0;
    
*/


    while (!Done(tb,trace,ticks))
    {
        ticks = doNTicks(tb,trace,ticks,1);
    }

#if TRACE
	trace->close();
#endif
	exit(EXIT_SUCCESS);
}
