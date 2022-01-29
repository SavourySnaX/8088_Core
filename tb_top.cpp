#include <stdlib.h>
#include "Vtop.h"
#include "Vtop_top.h"
#include "Vtop_execution.h"
#include "Vtop_bus_interface.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#define UNIT_TEST 1
#define TINY_ROM 0
#define VIDEO_BOOTSTRAP 0
#define TEST_PAGING 0

#define CLK_DIVISOR 8

#define NO_TRACE    (0 && UNIT_TEST) | (!UNIT_TEST)

#define TICK_LIMIT  0//300000

int internalClock=CLK_DIVISOR;
int ck=0;

uint32_t latchedAddress;
int lastWrite=1,lastRead=1;
Vtop *tb;

#if UNIT_TEST       // RUN ON 64BIT X86 since i cheated and use asm for flags

#define MAX_READWRITE_CAPTURE   5

int segOverride=3;

int captureIdx=0;
uint32_t readWriteLatchedAddress[MAX_READWRITE_CAPTURE];
int readWriteLatchedType[MAX_READWRITE_CAPTURE];
uint8_t  lastWriteCapture[MAX_READWRITE_CAPTURE];
uint8_t  lastReadCapture[MAX_READWRITE_CAPTURE];

enum ERegisterNum
{
    AX = 0,
    CX = 1,
    DX = 2,
    BX = 3,
    SP = 4,
    BP = 5,
    SI = 6,
    DI = 7,
};

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

int RegisterNumInitial(int regNum)
{
    switch (regNum)
    {
        case 0:
            return 0x0981;
        case 1:
            return 0x1658;
        case 2:
            return 0x2323;
        case 3:
            return 0x3124;
        case 4:
            return 0x4450;
        case 5:
            return 0x5782;
        case 6:
            return 0x6665;
        case 7:
            return 0x7776;
    }

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

void RegisterNum(int regInitVal)
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

enum Flags
{
    FLAG_C=1<<0,
    FLAG_P=1<<2,    
    FLAG_A=1<<4,
    FLAG_Z=1<<6,
    FLAG_S=1<<7,
    FLAG_T=1<<8,
    FLAG_I=1<<9,
    FLAG_D=1<<10,
    FLAG_O=1<<11
};

int EvenParity(int x)
{
    x ^=x>>4;
    x ^=x>>2;
    x ^=x>>1;
    return (~x)&1;
}

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

/*
FUNCTION INTERNAL	res[16]		OrWord			dst[16],src[16]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET { dst | src }->res;
	AFFECT P AS PARITYEVEN { res[0..7] };
}

FUNCTION INTERNAL	res[8]		OrByte			dst[8],src[8]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET, P AS PARITYEVEN { dst | src }->res;
}

FUNCTION INTERNAL	res[16]		SubWord			dst[16],src[16],carry[1]
{
	AFFECT S AS SIGN, Z AS ZERO, A AS CARRY(3), O AS OVERFLOW(dst,src,15), C AS CARRY(15) { (dst - src)-carry }->res;
	AFFECT P AS PARITYEVEN { res[0..7] };
}

FUNCTION INTERNAL	res[8]		SubByte			dst[8],src[8],carry[1]
{
	AFFECT S AS SIGN, Z AS ZERO, A AS CARRY(3), O AS OVERFLOW(dst,src,7), C AS CARRY(7), P AS PARITYEVEN { (dst - src)-carry }->res;
}

FUNCTION INTERNAL	res[16]		AndWord			dst[16],src[16]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET { dst & src }->res;
	AFFECT P AS PARITYEVEN { res[0..7] };
}

FUNCTION INTERNAL	res[8]		AndByte			dst[8],src[8]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET, P AS PARITYEVEN { dst & src }->res;
}

FUNCTION INTERNAL	res[16]		XorWord			dst[16],src[16]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET { dst ^ src }->res;
	AFFECT P AS PARITYEVEN { res[0..7] };
}

FUNCTION INTERNAL	res[8]		XorByte			dst[8],src[8]
{
	AFFECT S AS SIGN, Z AS ZERO, O AS FORCERESET, C AS FORCERESET, P AS PARITYEVEN { dst ^ src }->res;
}
*/

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
    int immediateValueL = Extract(testData,'l', counter, testCnt);

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

int ValidateFlagClear(const char* testData, int counter, int testCnt, int regInitVal)
{
    return (tb->top->eu->FLAGS & regInitVal)==0;
}

int ValidateFlagSet(const char* testData, int counter, int testCnt, int regInitVal)
{
    return (tb->top->eu->FLAGS & regInitVal)==regInitVal;
}

int ComputeEffectiveAddressMOD00(int rm,int dispL, int dispH)
{
    int addressBase = FetchInitialSR(segOverride)*16; // TODO override
    switch (rm)
    {
        case 0:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::SI));
        case 1:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::DI));
        case 2:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::SI));
        case 3:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::DI));
        case 4:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::SI));
        case 5:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::DI));
        case 6:
            return addressBase + ((dispH<<8)|dispL);
        case 7:
            return addressBase + (RegisterNumInitialWord(ERegisterNum::BX));
    }
    return 0xFFFFFFFF;
}

int ComputeEffectiveAddressMODoffs(int rm,int offs, int addressBase)
{
    switch (rm)
    {
        case 0:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::SI)+offs)&0xFFFF);
        case 1:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BX)+RegisterNumInitialWord(ERegisterNum::DI)+offs)&0xFFFF);
        case 2:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::SI)+offs)&0xFFFF);
        case 3:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BP)+RegisterNumInitialWord(ERegisterNum::DI)+offs)&0xFFFF);
        case 4:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::SI)+offs)&0xFFFF);
        case 5:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::DI)+offs)&0xFFFF);
        case 6:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BP)+offs)&0xFFFF);
        case 7:
            return addressBase + ((RegisterNumInitialWord(ERegisterNum::BX)+offs)&0xFFFF);
    }
    return 0xFFFFFFFF;
}

int ComputeEffectiveAddressMOD01(int rm,int dispL, int dispH)
{
    int addressBase = FetchInitialSR(segOverride)*16; // TODO override
    int offs=SignExt8Bit(dispL);
    return ComputeEffectiveAddressMODoffs(rm,offs,addressBase);
}

int ComputeEffectiveAddressMOD10(int rm,int dispL, int dispH)
{
    int addressBase = FetchInitialSR(segOverride)*16; // TODO override
    int offs =((dispH<<8)|dispL)&0xFFFF;
    return ComputeEffectiveAddressMODoffs(rm,offs,addressBase);
}

int ComputeEffectiveAddress(int mod,int rm,int dispL, int dispH)
{
    if (mod==0)
    {
        return ComputeEffectiveAddressMOD00(rm, dispL, dispH)&0xFFFFF;
    }
    if (mod==1)
    {
        return ComputeEffectiveAddressMOD01(rm, dispL, dispH)&0xFFFFF;
    }
    if (mod==2)
    {
        return ComputeEffectiveAddressMOD10(rm, dispL, dispH)&0xFFFFF;
    }
    return 0xFFFFFFFF;
}

int FetchDestValueMemory(int word, int mod, int rm, int dispL, int dispH)
{
    int address=ComputeEffectiveAddress(mod,rm,dispL,dispH);
    if (address != readWriteLatchedAddress[captureIdx])
    {
        printf("Failed - First Address Mismatch : %05X!=%05X", address, readWriteLatchedAddress[captureIdx]);
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        return -1;
    }
    if (word)
    {
        if (address+1 != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", address+1, readWriteLatchedAddress[captureIdx+1]);
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

int FetchSourceValueMemory(int word, int mod, int rm, int dispL, int dispH)
{
    int address=ComputeEffectiveAddress(mod,rm,dispL,dispH);
    if (address != readWriteLatchedAddress[captureIdx])
    {
        printf("Failed - First Address Mismatch : %05X!=%05X", address, readWriteLatchedAddress[captureIdx]);
        return -1;
    }
    if (readWriteLatchedType[captureIdx]!=1)
    {
        printf("Failed - First Address Type Mismatch : %d!=%d", 1, readWriteLatchedType[captureIdx]);
        return -1;
    }
    if (word)
    {
        if (address+1 != readWriteLatchedAddress[captureIdx+1])
        {
            printf("Failed - Second Address Mismatch : %05X!=%05X", address+1, readWriteLatchedAddress[captureIdx+1]);
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
        int r = (lastReadCapture[1]<<8)|(lastReadCapture[0]);
        captureIdx+=2;
        return r;
    }

    return lastReadCapture[captureIdx++];

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


#define TEST_MULT 4

const char* testArray[]={ 
#if 0
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
    "1110011W llllllll ",                                       (const char*)ValidateOutA,                      (const char*)DefaultTestInit,   (const char*)0x0000,      // out ib, al/ax
    "1110011W llllllll ",                                       (const char*)ValidateOutA,                      (const char*)DefaultTestInit,   (const char*)0x1234,      // out ib, al/ax
    "11111010 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_I),    // cli
    "100010DW MMRRRmmm llllllll hhhhhhhh ",                     (const char*)ValidateMovMRMReg,                 (const char*)RegisterNum,       (const char*)0x1000,      // mov rm<->r
    "100011D0 MM0RRmmm llllllll hhhhhhhh ",                     (const char*)ValidateMovRMSR,                   (const char*)RegisterNum,       (const char*)0x1000,      // mov rm<->sr
    "001RR110 10001000 00000110 llllllll hhhhhhhh ",            (const char*)ValidateMovMRMRegPrefixed,         (const char*)RegisterNum,       (const char*)0x1000,      // segment prefix (via mov prefix:[disp],al)
    "1100011W 11000mmm LLLLLLLL HHHHHHHH ",                     (const char*)ValidateMovRMImmediate,            (const char*)RegisterNum,       (const char*)0x0003,      // mov rm,i (byte/word) (reg)
    "1100011W 10000mmm llllllll hhhhhhhh LLLLLLLL HHHHHHHH ",   (const char*)ValidateMovRMImmediate,            (const char*)RegisterNum,       (const char*)0x0002,      // mov rm,i (byte/word) (mem)
    "00AAA10W LLLLLLLL HHHHHHHH ",                              (const char*)ValidateAluAImmediate,             (const char*)RegisterNumFlags,  (const char*)(0),         // alu A,i  (carry clear)
    "00AAA10W LLLLLLLL HHHHHHHH ",                              (const char*)ValidateAluAImmediate,             (const char*)RegisterNumFlags,  (const char*)(FLAG_C),    // alu A,i  (carry set)
    "00AAA0DW 11RRRmmm ",                                       (const char*)ValidateAluRMR,                    (const char*)RegisterNum,       (const char*)0x0003,      // mov rm,i (byte/word) (reg) (no need to check Carry in)
    "00AAA0DW 01RRRmmm llllllll ",                              (const char*)ValidateAluRMR,                    (const char*)RegisterNum,       (const char*)0x0001,      // mov rm,i (byte/word) (mem) (no need to check Carry in)
    "11111100 ",                                                (const char*)ValidateFlagClear,                 (const char*)SetFlags,          (const char*)(FLAG_D),    // cld
#endif
    0
};

#define RAMSIZE 1024*1024
unsigned char RAM[RAMSIZE];

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
        if (tb->top->biu->indirectBusCycle==1)
        {
            readWriteLatchedAddress[captureIdx]=latchedAddress;
            readWriteLatchedType[captureIdx]=tb->IOM;
            lastReadCapture[captureIdx++]=tb->inAD;

#if 0
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
        if (tb->top->biu->indirectBusOpInProgress==1)
        {
            readWriteLatchedAddress[captureIdx]=latchedAddress&0xFFFFF;
            readWriteLatchedType[captureIdx]=tb->IOM;
            lastWriteCapture[captureIdx++]=tb->outAD;
 #if 0
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
#elif TEST_PAGING

#define ROMSIZE 16
const unsigned char ROM[16] = {0xb9, 0x02, 0x00, 0xE2, 0xFE, 0xFA, 0xB8, 0x00, 0x90, 0x8E, 0xD0, 0xBC, 0x99, 0x88, 0x66, 0x77};

#endif

#if !UNIT_TEST

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
        tb->inAD = ROM[latchedAddress & (ROMSIZE-1)];
    }
    if (tb->WR_n==1 && lastWrite==0)    // Only log once per write
    {
        if (tb->IOM==1)
            printf("Write To Ram : %08X <- %02X\n",latchedAddress,tb->outAD);
        else
            printf("Write To IO : %08X <- %02X\n",latchedAddress,tb->outAD);
    }
    lastWrite=tb->WR_n;
}

int Done(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    return ticks >= 200000;
}

#endif

void tick(Vtop *tb, VerilatedVcdC* trace, int ticks)
{
    tb->CLK=ck;
    tb->CLKx4=1;
#if !NO_TRACE
    trace->dump(ticks*10-2);
#endif
    tb->eval();
#if !NO_TRACE
    trace->dump(ticks*10);
#endif
    tb->CLKx4=0;
    tb->eval();
#if !NO_TRACE
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

#if !NO_TRACE
	Verilated::traceEverOn(true);
#endif

	tb = new Vtop;

	VerilatedVcdC *trace = new VerilatedVcdC;

#if !NO_TRACE
	tb->trace(trace, 99);
	trace->open("trace.vcd");
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

#if !NO_TRACE
	trace->close();
#endif
	exit(EXIT_SUCCESS);
}






