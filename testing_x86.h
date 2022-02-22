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

enum ESRReg
{
    ES=0,
    CS=1,
    SS=2,
    DS=3
};

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

int TestFlags(int hFlags,int tFlags, int flagMask);
int CheckAddFlagsW(int a,int b, int flagMask);
int CheckSubFlagsW(int a,int b, int flagMask);
int CheckAddResultW(int a,int b, int flagMask, int expected);
int CheckAddResultB(int a,int b, int flagMask, int expected);
int CheckOrResultW(int a,int b, int flagMask, int expected);
int CheckOrResultB(int a,int b, int flagMask, int expected);
int CheckAdcResultW(int a,int b, int carryIn, int flagMask, int expected);
int CheckAdcResultB(int a,int b, int carryIn, int flagMask, int expected);
int CheckSbbResultW(int a,int b, int carryIn, int flagMask, int expected);
int CheckSbbResultB(int a,int b, int carryIn, int flagMask, int expected);
int CheckAndResultW(int a,int b, int flagMask, int expected);
int CheckAndResultB(int a,int b, int flagMask, int expected);
int CheckSubResultW(int a,int b, int flagMask, int expected);
int CheckSubResultB(int a,int b, int flagMask, int expected);
int CheckXorResultW(int a,int b, int flagMask, int expected);
int CheckXorResultB(int a,int b, int flagMask, int expected);
int CheckCmpResultW(int a,int b, int flagMask, int expected);
int CheckCmpResultB(int a,int b, int flagMask, int expected);
int CheckShlResultW(int a,int carryIn, int flagMask, int expected);
int CheckShlResultB(int a,int carryIn, int flagMask, int expected);
int CheckShrResultW(int a,int carryIn, int flagMask, int expected);
int CheckShrResultB(int a,int carryIn, int flagMask, int expected);
int CheckSarResultW(int a,int carryIn, int flagMask, int expected);
int CheckSarResultB(int a,int carryIn, int flagMask, int expected);
int CheckRorResultW(int a,int carryIn, int flagMask, int expected);
int CheckRorResultB(int a,int carryIn, int flagMask, int expected);
int CheckRolResultW(int a,int carryIn, int flagMask, int expected);
int CheckRolResultB(int a,int carryIn, int flagMask, int expected);
int CheckRclResultW(int a,int carryIn, int flagMask, int expected);
int CheckRclResultB(int a,int carryIn, int flagMask, int expected);
int CheckRcrResultW(int a,int carryIn, int flagMask, int expected);
int CheckRcrResultB(int a,int carryIn, int flagMask, int expected);
int CheckNegResultW(int a,int flagMask,int expected);
int CheckNegResultB(int a,int flagMask,int expected);
int CheckTestResultW(int a,int b, int flagMask, int expected);
int CheckTestResultB(int a,int b, int flagMask, int expected);
int CheckShlClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckShlClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckShrClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckShrClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckSarClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckSarClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRorClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRorClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRolClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRolClResultB(int a,int cl, int carryIn, int flagMask, int expected);
int CheckRclClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRclClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRcrClResultW(int a,int cl,int carryIn, int flagMask, int expected);
int CheckRcrClResultB(int a,int cl,int carryIn, int flagMask, int expected);
int CheckMulB(int a,int b, int flagMask, int expected);
int CheckMulW(int a,int b, int flagMask, int expected, int expected2);
int CheckIMulB(int a,int b, int flagMask, int expected);
int CheckIMulW(int a,int b, int flagMask, int expected, int expected2);
