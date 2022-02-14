
module execution
(
    input               CLKx4,

    input               CLK,
    input               RESET,

    input [7:0] prefetchTop,
    input prefetchEmpty, 
    input prefetchFull,
    input indirectBusOpInProgress,
    input suspending,
    input irqPending,

    input [15:0]        REGISTER_IP,
    input [15:0]        REGISTER_ES,
    input [15:0]        REGISTER_CS,
    input [15:0]        REGISTER_DS,
    input [15:0]        REGISTER_SS,
    output reg [15:0]   UpdateReg,

    output reg [15:0] IND, //offset for read/write
    output reg [2:0]  indirectSeg,
    input      [15:0] OPRr, //word read from bus
    output reg [15:0] OPRw, //word written to bus


    // BIU control
    output reg readTop,
    output reg suspend,
    output reg correct,
    output reg flush /*verilator public*/,
    output reg indirect,
    output reg irq,
    output reg latchPC,
    output reg latchCS,
    output reg latchDS,
    output reg latchSS,
    output reg latchES,

    output reg ind_ioMreq,       // indirect io/data request  (io assumes 0000 as SEG)
    output reg ind_readWrite,    // indirect bus read/write request
    output reg ind_byteWord      // indirect bus byte/word request
);

reg TRACE_MODE /* verilator public */;

reg [8:0] executionState /* verilator public */;
reg [7:0] instruction /* verilator public */;
reg [7:0] modrm /* verilator public */;

reg clkEdgeSample;
reg [2:0] segPrefix;

reg tick;

// registers held locally
reg [15:0] AX /* verilator public */;
reg [15:0] BX /* verilator public */;
reg [15:0] CX /* verilator public */;
reg [15:0] DX /* verilator public */;
reg [15:0] SP /* verilator public */;
reg [15:0] BP /* verilator public */;
reg [15:0] SI /* verilator public */;
reg [15:0] DI /* verilator public */;

reg [15:0] FLAGS /* verilator public */;                       // R | R | R | R |OF|DF|IF|TF|SF|ZF|U |AF|U |PF|U |CF

reg [3:0] operation;
reg [15:0] tmpa;
reg [15:0] tmpb;
reg [15:0] tmpc;
reg selectShifter;
wire [15:0] SIGMA,sigmaALU,sigmaShifter;

// microcodeing
reg [3:0]   code_M;            // Word|RegIndex
reg         code_TmpA2M;        // TODO collapse these so we can switch between tmpA/B/C/Sigma ??
reg         code_TmpB2M;
reg         code_TmpB2R;
reg         code_Sigma2M;
reg         code_Sigma2R;
reg         code_M2TmpA;
reg         code_M2TmpB;
reg         code_R2TmpA;
reg         code_R2TmpB;
reg         code_M2SR;
reg         code_SR2M;
reg [15:0]  code_FLAGS;

reg readModifyWrite;

reg [1:0] aluAselect;  // 00 tmpa 01 tmpb 10 tmpc 11 ...
reg [1:0] aluBselect;
reg aluWord;

reg repeatF,repeatFZ;

wire [15:0] aluA,aluB;

wire fo,fs,fz,fa,fp,fc;         // alu flags
wire sho,shs,shz,sha,shp,shc;   // shifter flags

parameter FLAG_C_IDX = 0;
parameter FLAG_P_IDX = 2;
parameter FLAG_A_IDX = 4;
parameter FLAG_Z_IDX = 6;
parameter FLAG_S_IDX = 7;
parameter FLAG_T_IDX = 8;
parameter FLAG_I_IDX = 9;
parameter FLAG_D_IDX = 10;
parameter FLAG_O_IDX = 11;

parameter FLAG_C_MSK = 16'h0001<<FLAG_C_IDX;
parameter FLAG_P_MSK = 16'h0001<<FLAG_P_IDX;
parameter FLAG_A_MSK = 16'h0001<<FLAG_A_IDX;
parameter FLAG_Z_MSK = 16'h0001<<FLAG_Z_IDX;
parameter FLAG_S_MSK = 16'h0001<<FLAG_S_IDX;
parameter FLAG_T_MSK = 16'h0001<<FLAG_T_IDX;
parameter FLAG_I_MSK = 16'h0001<<FLAG_I_IDX;
parameter FLAG_D_MSK = 16'h0001<<FLAG_D_IDX;
parameter FLAG_O_MSK = 16'h0001<<FLAG_O_IDX;

parameter SEG_ZERO = 3'b100;
parameter SEG_CS = 3'b001;
parameter SEG_DS = 3'b011;
parameter SEG_ES = 3'b000;
parameter SEG_SS = 3'b010;


parameter ALU_OP_PASS = 4'b0000;
parameter ALU_OP_INC =  4'b0010;
parameter ALU_OP_DEC =  4'b0011;
parameter ALU_OP_INC2 = 4'b0100;
parameter ALU_OP_DEC2 = 4'b0101;
parameter ALU_OP_NEG  = 4'b0110;

parameter ALU_OP_ADD = 4'b1000;
parameter ALU_OP_OR  = 4'b1001;
parameter ALU_OP_ADC = 4'b1010;
parameter ALU_OP_SBB = 4'b1011;
parameter ALU_OP_AND = 4'b1100;
parameter ALU_OP_SUB = 4'b1101;
parameter ALU_OP_XOR = 4'b1110;
parameter ALU_OP_CMP = 4'b1111;

parameter SHIFTER_OP_ROL = 3'b000;
parameter SHIFTER_OP_ROR = 3'b001;
parameter SHIFTER_OP_RCL = 3'b010;
parameter SHIFTER_OP_RRL = 3'b011;
parameter SHIFTER_OP_SHL = 3'b100;
parameter SHIFTER_OP_SHR = 3'b101;
parameter SHIFTER_OP_SAR = 3'b111;


wire Cond_O, Cond_NO, Cond_C, Cond_AE, Cond_E, Cond_NE, Cond_BE, Cond_A, Cond_S, Cond_NS, Cond_P, Cond_PO, Cond_L, Cond_GE, Cond_LE, Cond_G;

assign Cond_O  = FLAGS[FLAG_O_IDX];
assign Cond_NO =~FLAGS[FLAG_O_IDX];
assign Cond_C  = FLAGS[FLAG_C_IDX];
assign Cond_AE =~FLAGS[FLAG_C_IDX];
assign Cond_E  = FLAGS[FLAG_Z_IDX];
assign Cond_NE =~FLAGS[FLAG_Z_IDX];
assign Cond_BE =   FLAGS[FLAG_C_IDX]  |   FLAGS[FLAG_Z_IDX];
assign Cond_A  = (~FLAGS[FLAG_C_IDX]) & (~FLAGS[FLAG_Z_IDX]);
assign Cond_S  = FLAGS[FLAG_S_IDX];
assign Cond_NS =~FLAGS[FLAG_S_IDX];
assign Cond_P  = FLAGS[FLAG_P_IDX];
assign Cond_PO =~FLAGS[FLAG_P_IDX];
assign Cond_L  = FLAGS[FLAG_S_IDX] != FLAGS[FLAG_O_IDX];
assign Cond_GE = FLAGS[FLAG_S_IDX] == FLAGS[FLAG_O_IDX];
assign Cond_LE =(FLAGS[FLAG_S_IDX] != FLAGS[FLAG_O_IDX]) |   FLAGS[FLAG_Z_IDX];
assign Cond_G  =(FLAGS[FLAG_S_IDX] == FLAGS[FLAG_O_IDX]) & (~FLAGS[FLAG_Z_IDX]);

reg [8:0] PostEffectiveAddressReturn;   // EA calculation finsh jumps to here

// alu
alu myalu(.CLKx4(CLKx4), .A(aluA),.B(aluB),.Operation(operation),.byteWord(aluWord),.carryIn(FLAGS[FLAG_C_IDX]),.S(sigmaALU),.F_Overflow(fo), .F_Neg(fs), .F_Zero(fz), .F_Aux(fa), .F_Parity(fp), .F_Carry(fc));
shifter myshifter(.CLKx4(CLKx4), .A(aluA),.Operation(operation[2:0]),.byteWord(aluWord),.carryIn(FLAGS[FLAG_C_IDX]),.S(sigmaShifter),.F_Overflow(sho), .F_Neg(shs), .F_Zero(shz), .F_Aux(sha), .F_Parity(shp), .F_Carry(shc));

assign SIGMA=selectShifter? sigmaShifter : sigmaALU;

assign aluA = (tmpa & {16{aluAselect==2'b00}}) |
              (tmpb & {16{aluAselect==2'b01}}) |
              (tmpc & {16{aluAselect==2'b10}});

assign aluB = (tmpa & {16{aluBselect==2'b00}}) |
              (tmpb & {16{aluBselect==2'b01}}) |
              (tmpc & {16{aluBselect==2'b10}});

task automatic FetchExecStateFromInstruction(input [7:0] inst);
begin
    readModifyWrite = 0;
    if (inst[7:2] == 6'b100010)                              // MOV rmw<->r
    begin
        PostEffectiveAddressReturn <= 9'h000;
        executionState <= 9'h1f5;
    end
    else if (inst[7:4] == 4'b1011)                            // MOV rrr,i
        executionState <= 9'h01C;
    else if (inst[7:1] == 7'b1100011)                         // MOV rm,i
    begin
        instruction[1]<=0;                               // acts as if direction is 0
        PostEffectiveAddressReturn <= 9'h014;
        executionState <= 9'h1f5;
    end
    else if ({inst[7:2],inst[0]} == 7'b1000110)          // MOV rmw<->sr
    begin
        instruction[0]<=1;                               // its a word operation
        PostEffectiveAddressReturn <= 9'h0EC;
        executionState <= 9'h1f5;
    end
    else if (inst[7:1] == 7'b1110011)                    // OUT ib, AL/AX
        executionState <= 9'h0B0;
    else if (inst[7:1] == 7'b1110010)                    // IN AL/AX, ib
        executionState <= 9'h0AC;
    else if (inst[7:4] == 4'b0100)                       // INC/DEC rp
        executionState <= 9'h17C;
    else if ({inst[7:2],inst[0]} == 7'b1110101)          // JMP rel8/rel16
        executionState <= 9'h0D0;
    else if (inst == 8'b11101010)                        // JMP offs segment
        executionState <= 9'h0E0;
    else if (inst == 8'b11100010)                        // LOOP
        executionState <= 9'h140;
    else if (inst == 8'b11111011)                        // STI (not microcoded)
    begin
        FLAGS[FLAG_I_IDX]<=1;
        executionState <= 9'h143;          // RNI
    end
    else if (inst == 8'b11111010)                        // CLI (not microcoded)
    begin
        FLAGS[FLAG_I_IDX]<=0;
        executionState <= 9'h143;          // RNI
    end
    else if (inst == 8'b11111100)                        // CLD (not microcoded)
    begin
        FLAGS[FLAG_D_IDX]<=0;
        executionState <= 9'h143;          // RNI
    end
    else if ({inst[7:5],inst[2:0]} == 6'b001110)         // SEGMENT PREFIX
        executionState <= 9'h005;
    else if ({inst[7:6],inst[2:1]} == 4'b0010)           // alu A,i
        executionState <= 9'h018;
    else if ({inst[7:6],inst[2]} == 3'b000)              // alu rm<->r
    begin
        readModifyWrite = 1;
        PostEffectiveAddressReturn <= 9'h008;
        executionState <= 9'h1f5;
    end
    else if (inst[7:1] == 7'b1010101)                    // STOS
        executionState <= 9'h11C;
    else if (inst[7:1] == 7'b1111001)                    // REP
        executionState <= 9'h006;
    else if (inst[7:4] == 4'b0111)                       // Jcond
        executionState <= 9'h0e8;
    else if (inst[7:1] == 7'b1111111)                    // FE/FF prefixed INC/DEC rm
    begin
        executionState <= 9'h1f3;
    end
    else if (inst[7:1] == 7'b1111011)                    // F2/F3 prefixed NEG.. rm
    begin
        executionState <= 9'h1f4;
    end
    else if ({inst[7:4],inst[2:1]} == 6'b101010)           // LODS/MOVS
        executionState <= 9'h12c;
    else if (inst[7:2] == 6'b100000)                       // alu rm,i
    begin
        readModifyWrite = 1;
        PostEffectiveAddressReturn <= 9'h00c;
        executionState <= 9'h1f5;
    end
    else if (inst[7:0] == 8'b11101000)                    // CALL cw
        executionState <= 9'h07c;
    else if ({inst[7:5],inst[2:0]} == 6'b000110)          // PUSH sr
        executionState <= 9'h02c;
    else if ({inst[7:5],inst[2:0]} == 6'b000111)          // POP sr
        executionState <= 9'h038;
    else if (inst[7:0] == 8'b11000011)                    // RET
        executionState <= 9'h0bc;
    else if (inst[7:0] == 8'b11001111)                    // IRET
        executionState <= 9'h0c8;
    else if (inst[7:0] == 8'b11110100)                    // HLT
        executionState <= 9'h1fb;
    else if (inst[7:1] == 7'b1010000)                    // MOV A,[i]
        executionState <= 9'h060;
    else if (inst[7:1] == 7'b1010001)                    // MOV [i],A
        executionState <= 9'h064;
    else if (inst[7:3] == 5'b01010)                      // PUSH rw
        executionState <= 9'h028;
    else if (inst[7:3] == 5'b01011)                      // POP rw
        executionState <= 9'h034;
    else if (inst[7:1] == 7'b1000011)                    // XCHG rm,r
    begin
        readModifyWrite = 1;
        PostEffectiveAddressReturn <= 9'h0a4;
        executionState <= 9'h1f5;
    end
    else if (inst[7:1] == 7'b1101000)                    // rot rm,1
    begin
        readModifyWrite = 1;
        PostEffectiveAddressReturn <= 9'h088;
        executionState <= 9'h1f5;
    end
    else
    begin
        executionState <= 9'h1FD;
        $displayb("%h UNKNOWN INSTRUCTION %b\n",REGISTER_IP, inst);
        TRACE_MODE<=1;
    end
end
endtask

task automatic WriteToRegister(input W, input [2:0] regNum, input [15:0] in);
begin
    if (W==1)
    begin
        // AX|CX|DX|BX|SP|BP|SI|DI
        case (regNum)
            3'b000:     AX<=in;
            3'b001:     CX<=in;
            3'b010:     DX<=in;
            3'b011:     BX<=in;
            3'b100:     SP<=in;
            3'b101:     BP<=in;
            3'b110:     SI<=in;
            3'b111:     DI<=in;
        endcase
    end
    else
    begin
        // AL|CL|DL|BL|AH|CH|DH|BH
        case (regNum)
            3'b000:     AX[7:0]<=in[7:0];
            3'b001:     CX[7:0]<=in[7:0];
            3'b010:     DX[7:0]<=in[7:0];
            3'b011:     BX[7:0]<=in[7:0];
            3'b100:     AX[15:8]<=in[7:0];
            3'b101:     CX[15:8]<=in[7:0];
            3'b110:     DX[15:8]<=in[7:0];
            3'b111:     BX[15:8]<=in[7:0];
        endcase
    end
end
endtask

function automatic [15:0] ReadFromRegister(input W, input [2:0] regNum);
begin
    if (W==1)
    begin
        // AX|CX|DX|BX|SP|BP|SI|DI
        case (regNum)
            3'b000:     ReadFromRegister=AX;
            3'b001:     ReadFromRegister=CX;
            3'b010:     ReadFromRegister=DX;
            3'b011:     ReadFromRegister=BX;
            3'b100:     ReadFromRegister=SP;
            3'b101:     ReadFromRegister=BP;
            3'b110:     ReadFromRegister=SI;
            3'b111:     ReadFromRegister=DI;
        endcase
    end
    else
    begin
        // AL|CL|DL|BL|AH|CH|DH|BH
        case (regNum)
            3'b000:     ReadFromRegister={8'h00,AX[7:0]};
            3'b001:     ReadFromRegister={8'h00,CX[7:0]};
            3'b010:     ReadFromRegister={8'h00,DX[7:0]};
            3'b011:     ReadFromRegister={8'h00,BX[7:0]};
            3'b100:     ReadFromRegister={8'h00,AX[15:8]};
            3'b101:     ReadFromRegister={8'h00,CX[15:8]};
            3'b110:     ReadFromRegister={8'h00,DX[15:8]};
            3'b111:     ReadFromRegister={8'h00,BX[15:8]};
        endcase
    end
end
endfunction

task automatic WriteToSRRegister(input[1:0] sr, input [15:0] in);
begin
    UpdateReg<=in;
    case (sr)
        2'b00: latchES<=1;
        2'b01: latchCS<=1;
        2'b10: latchSS<=1;
        2'b11: latchDS<=1;
    endcase
end
endtask

function automatic [15:0] ReadFromSRRegister(input [1:0] sr);
begin
    case (sr)
        2'b00: ReadFromSRRegister=REGISTER_ES;
        2'b01: ReadFromSRRegister=REGISTER_CS;
        2'b10: ReadFromSRRegister=REGISTER_SS;
        2'b11: ReadFromSRRegister=REGISTER_DS;
    endcase
end
endfunction

/* verilator lint_off BLKSEQ */
always @(posedge(CLKx4))
begin
    
    clkEdgeSample<=CLK;

    if (RESET == 1)
    begin
        
        executionState <= 9'h1E4;   // RESET
        instruction <= 8'h90;
        TRACE_MODE<=0;
    end
    else
    begin
        if (clkEdgeSample==1'b1 && CLK==1'b0)   // negative slope
        begin
            tick<=~suspending;
        end
/*        else if (clkEdgeSample[2]==1'b0 && clkEdgeSample[1]==1'b1)
        begin
            tick<=1;        // Double rate
        end*/
        else
        begin
            tick<=0;
            readTop<=0;
            suspend<=0;
            correct<=0;
            flush<=0;
            latchPC<=0;
            latchCS<=0;
            latchDS<=0;
            latchSS<=0;
            latchES<=0;
            indirect<=0;
        end

            code_TmpA2M=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_TmpB2M=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_TmpB2R=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_Sigma2M=0;     // Can be merged into a single value ultimately (ie enable bits)
            code_Sigma2R=0;     // Can be merged into a single value ultimately (ie enable bits)
            code_M2TmpA=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_M2TmpB=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_R2TmpB=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_R2TmpA=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_SR2M=0;        // 
            code_M2SR=0;        //
            code_FLAGS=0;


        if (tick==1)
        begin
            // ROM

            case (executionState)
//000 A CD F H J L  OPQR  U       R     -> tmpb      4   none  WB,NX       0100010??.00  MOV rm<->r
                9'h000:
                    begin
                        // R->tmpb
                        if (instruction[1] == 0)
                        begin
                            //R->tmpb
                            code_M={instruction[0],modrm[5:3]};
                            code_R2TmpB=1;
                        end
                        else
                        begin
                            //M->tmpb
                            code_M={instruction[0],modrm[2:0]};
                            code_M2TmpB=1;
                        end
                        executionState<=9'h001;
                    end
//001  B  E GHI  L  OPQR          tmpb  -> M         4   none  RNI                      
                9'h001:
                    begin
                        // tmpb->M
                        if (instruction[1] == 0)
                        begin
                            //tmpb->M
                            code_M={instruction[0],modrm[2:0]};
                            code_TmpB2M=1;
                            if (modrm[7:6]==2'b11)
                                executionState<=9'h1FD;
                            else
                                executionState<=9'h002;
                        end
                        else
                        begin
                            //tmpb->R
                            code_M={instruction[0],modrm[5:3]};
                            code_TmpB2R=1;
                            executionState<=9'h1FD;
                        end
                    end
//002 ABC  F HI  LM O QRSTU                          6   W     DD,P0                     
                9'h002:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//003   CD FG IJ L N     TU       IJ    -> tmpa      5   UNC   EAOFFSET                  [SI]
                9'h003:
                    begin
                        // IJ -> tmpa    
                        tmpa<=SI;
                        executionState<=9'h1f7; //EAOFFSET
                    end

//005 (not real mOP) - segment prefix
                9'h005:
                    begin
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            segPrefix<={1'b0,instruction[4:3]};
                            instruction<=prefetchTop;
                            readTop<=1;
                            FetchExecStateFromInstruction(prefetchTop);
                        end
                    end

//006 (not real mOP) - REP 
                9'h006:
                    begin
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpc<=CX;     // MOVED This from 112, we shouldn't reload this every iteration
                            repeatF<=1;
                            repeatFZ<=instruction[0];
                            instruction<=prefetchTop;
                            readTop<=1;
                            FetchExecStateFromInstruction(prefetchTop);
                        end
                    end

//008   CD F   J  MN   R          M     -> tmpa      1   XI    tmpa        000???0??.00  alu rm<->r
                9'h008:
                    begin
                        // M->tmpa  XI tmpa,NX
                        code_M={instruction[0],modrm[2:0]};
                        if (instruction[1] == 0)
                            code_M2TmpA=1;      // M -> tmpa
                        else
                            code_M2TmpB=1;      // M -> tmpb
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=instruction[0];
                        operation<={1'b1,instruction[5:3]};
                        executionState<=9'h009;
                    end
//009 A CD F H J L  OPQR  U       R     -> tmpb      4   none  WB,NX                     
                9'h009:
                    begin
                        // R -> tmpb
                        code_M={instruction[0],modrm[5:3]};
                        if (instruction[1] == 0)
                            code_R2TmpB=1;
                        else
                            code_R2TmpA=1;
                        executionState<=9'h00A;
                    end
//00a  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h00A:
                    begin
                        // SIGMA -> M/R
                        if (instruction[1] == 0)
                        begin
                            //SIGMA->M
                            code_Sigma2M=instruction[5:3]!=ALU_OP_CMP[2:0];
                            code_M={instruction[0],modrm[2:0]};
                            if ((modrm[7:6]==2'b11) || (code_Sigma2M==0))
                                executionState<=9'h1FD;
                            else
                                executionState<=9'h00b;
                        end
                        else
                        begin
                            //SIGMA->R
                            code_M={instruction[0],modrm[5:3]};
                            code_Sigma2R=instruction[5:3]!=ALU_OP_CMP[2:0];
                            executionState<=9'h1FD;
                        end
                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK|FLAG_C_MSK;
                    end
//00b ABC  F HI  LM O QRSTU                          6   W     DD,P0         
                9'h00B:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end
//00c A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        0100000??.00  alu rm,i
                9'h00C:
                    begin
                        // Q->tmpbL   L8
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            if (instruction[1]==0)
                                tmpb[15:8]<=0;
                            else
                                tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[1:0]==2'b01)
                                executionState<=9'h00d;
                            else
                                executionState<=9'h00e;     // L8
                        end
                    end
//00d ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h00D:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h00e;
                        end
                    end
//00e   CD F   J  MN   R  U       M     -> tmpa      1   XI    tmpa, NX                  
                9'h00E:
                    begin
                        // M->tmpa  XI tmpa,NX
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpA=1;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=instruction[0];
                        operation<={1'b1,modrm[5:3]};
                        executionState<=9'h00f;
                    end
//00f  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h00F:
                    begin
                        // SIGMA -> M
                        code_M={instruction[0],modrm[2:0]};
                        code_Sigma2M=modrm[5:3]!=ALU_OP_CMP[2:0];
                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK|FLAG_C_MSK;
                        if ((modrm[7:6]==2'b11) || (code_Sigma2M==0))
                            executionState<=9'h1FD;
                        else
                            executionState<=9'h010;
                    end
//010 ABC  F HI  LM O QRSTU                          6   W     DD,P0       0?00000??.01  
                9'h010:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end
//011                                                                                    
//012                                                                                    
//013                                                                            
//014 A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        01100011?.00  MOV rm,i
                9'h014:
                    begin
                        // Q->tmpbL   L8            // ??? Should we validate reg == 0 here?
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[0]==1)
                                executionState<=9'h015;
                            else
                                executionState<=9'h016;     // L8
                        end
                    end
//015 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h015:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h016;
                        end
                    end
//016  B  E GHI  L  OPQR          tmpb  -> M         4   none  RNI                       
                9'h016:
                    begin
                        // tmpb->M
                        code_M={instruction[0],modrm[2:0]};
                        code_TmpB2M=1;
                        if (modrm[7:6]==2'b11)
                            executionState<=9'h1FD;
                        else
                            executionState<=9'h017;
                    end
//017 ABC  F HI  LM O QRSTU                          6   W     DD,P0                     
                9'h017:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//018 A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        000???10?.00  alu A,i
                9'h018:
                    begin
                        // Q->tmpbL   L8            // ??? Should we validate reg == 0 here?
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[0]==1)
                                executionState<=9'h019;
                            else
                                executionState<=9'h01a;     // L8
                        end
                    end
//019 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h019:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h01a;
                        end
                    end
//01a   CD F   J  MN   R  U       M     -> tmpa      1   XI    tmpa, NX                  
                9'h01A:
                    begin
                        // M->tmpa  XI tmpa,NX
                        code_M={instruction[0],3'b000};
                        code_M2TmpA=1;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=instruction[0];
                        operation<={1'b1,instruction[5:3]};
                        executionState<=9'h01b;
                    end
//01b  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h01B:
                    begin
                        // SIGMA -> M
                        code_M={instruction[0],3'b000};
                        code_Sigma2M=instruction[5:3]!=ALU_OP_CMP[2:0];
                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK|FLAG_C_MSK;
                        executionState<=9'h1FD;
                    end

//01c A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        01011????.00  MOV r,i
                9'h01C:
                    begin
                        // Q->tmpbL   L8
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[3]==1)
                                executionState<=9'h01D;
                            else
                                executionState<=9'h01E;     // L8
                        end
                    end
//01d ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h01D:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h01E;
                        end
                    end
//01e  B  E GHI  L  OPQR          tmpb  -> M         4   none  RNI    
                9'h01E:
                    begin
                        // tmpb -> M     RNI
                        code_M=instruction[3:0];
                        code_TmpB2M=1;

                        executionState<=9'h1FD; // RNI
                    end

//01f   CD FGHIJ L N     TU       IK    -> tmpa      5   UNC   EAOFFSET                  [DI]
                9'h01F:
                    begin
                        // IK -> tmpa    
                        tmpa<=DI;
                        executionState<=9'h1f7; //EAOFFSET
                    end

//020 A CD F   J  MN   R TU       M     -> tmpb      1   XI    tmpb, NX    ?1111100?.00   INC/DEC rm
                9'h020:
                    begin
                        // M->tmpb
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpB=1;
                        
                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluWord<=instruction[0];
                        operation<=ALU_OP_INC+{3'h0,modrm[3]};  // Inc/Dec
                        
                        executionState<=9'h021;
                    end
//021  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h021:
                    begin
                        // SIGMA -> M     RNI
                        code_M={instruction[0],modrm[2:0]};
                        code_Sigma2M=1;

                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK;

                        if (modrm[7:6]==2'b11)
                            executionState<=9'h1FD; // RNI
                        else
                            executionState<=9'h022;
                    end
//022 ABC  F HI  LM O QRSTU                          6   W     DD,P0  
                9'h022:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//023   CD FGHI  L N     TU       MP    -> tmpa      5   UNC   EAOFFSET                  [BP]
                9'h023:
                    begin
                        // MP -> tmpa    
                        tmpa<=BP;
                        executionState<=9'h1f7; //EAOFFSET
                    end

//028   CD FG I   MNOP R          SP    -> tmpa      1   DEC2  tmpa        001010???.00  PUSH rw
                9'h028:
                    begin
                        // SP->tmpa  DEC2 tmpa
                        tmpa<=SP;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluWord<=1'b1;
                        operation<=ALU_OP_DEC2;   // DEC2
                        executionState<=9'h029;
                    end
//029 A C  F  I  L  OPQRSTU       SIGMA -> IND                                           
                9'h029:
                    begin
                        // SIGMA -> IND
                        IND<=SIGMA;
                        executionState<=9'h02a;
                    end
//02a   CDEF  I  L  OPQRSTU       SIGMA -> SP                                            
                9'h02A:
                    begin
                        // SIGMA -> SP
                        SP<=SIGMA;
                        executionState<=9'h02b;
                    end
//02b  BC  F   J LM O QR TU       M     -> OPR       6   W     DS,P0   
                9'h02B:
                    begin
                        // R -> OPR  DS,P0
                        OPRw<=ReadFromRegister(1'b1,instruction[2:0]);
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//02c   CD FG I   MNOP R          SP    -> tmpa      1   DEC2  tmpa        0000??110.00  PUSH sr
                9'h02C:
                    begin
                        // SP -> tmpa DEC2
                        tmpa<=SP;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluWord<=1'b1;
                        operation<=ALU_OP_DEC2;   // DEC2
                        executionState<=9'h02d;
                    end
//02d A C  F  I  L  OPQRSTU       SIGMA -> IND                                           
                9'h02D:
                    begin
                        // SIGMA -> IND
                        IND<=SIGMA;
                        executionState<=9'h02e;
                    end
//02e   CDEF  I  L  OPQRSTU       SIGMA -> SP                                            
                9'h02E:
                    begin
                        // SIGMA -> SP
                        SP<=SIGMA;
                        executionState<=9'h02f;
                    end
//02f  BC  F H J LM O QR TU       R     -> OPR       6   W     DS,P0
                9'h02F:
                    begin
                        // R -> OPR  DS,P0
                        OPRw<=ReadFromSRRegister(instruction[4:3]);
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//034 A C  FG I  LM    R          SP    -> IND       6   R     DS,P2       001011???.00  POP rw
                9'h034:
                    begin
                        // SP -> IND   R DS,P2
                        IND<=SP;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h035;
                    end
//035   CDE  HI  L  OPQRS U       IND   -> SP        4   none  NWB,NX                    
                9'h035:
                    begin
                        // IND->SP    NWB,NX
                        if (indirectBusOpInProgress==0)
                        begin
                            SP<=IND+2;
                            executionState<=9'h036; 
                        end
                    end
//036  B  E   IJ L  OPQR          OPR   -> M         4   none  RNI                   
                9'h036:
                    begin
                        WriteToRegister(1'b1,instruction[2:0],OPRr);
                        executionState<=9'h1fd;  // RNI
                    end

//037   CD FGH J L N     TU       HL    -> tmpa      5   UNC   EAOFFSET                  [BX]
                9'h037:
                    begin
                        // HL -> tmpa    
                        tmpa<=BX;
                        executionState<=9'h1f7; //EAOFFSET
                    end
//038 A C  FG I  LM    R          SP    -> IND       6   R     DS,P2       0000??111.00  POP sr
                9'h038:
                    begin
                        // SP -> IND
                        IND<=SP;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h039;
                    end
//039   CDE  HI  L  OPQRS U       IND   -> SP        4   none  NWB,NX                    
                9'h039:
                    begin
                        if (indirectBusOpInProgress==0)
                        begin
                            SP<=IND+2;
                            executionState<=9'h03a; 
                        end
                    end
//03a AB  E   IJ L  OPQR          OPR   -> R         4   none  RNI    
                9'h03A:
                    begin
                        WriteToSRRegister(instruction[4:3],OPRr);
                        executionState<=9'h1fd;  // RNI
                    end

//050 A CD F   J  MN   R TU       M     -> tmpb      1   XI    tmpb, NX    ?11110011.00   NEG rm
                9'h050:
                    begin
                        // M->tmpb
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpB=1;
                        
                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluWord<=instruction[0];
                        operation<=ALU_OP_NEG;
                        
                        executionState<=9'h051;
                    end
//051  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h051:
                    begin
                        code_M={instruction[0],modrm[2:0]};
                        code_Sigma2M=1;
                        
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK|FLAG_C_MSK;

                        if (modrm[7:6]==2'b11)
                            executionState<=9'h1FD; // RNI
                        else
                            executionState<=9'h052;
                    end
//052 ABC  F HI  LM O QRSTU                          6   W     DD,P0                     
                9'h052:
                    begin
                        // DD,P0  (DS with override)
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end
 
//060 A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           01010000?.00  MOV A,[i]
                9'h060:
                    begin
                        // Q->tmpbL 
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h061;
                        end
                    end
//061 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h061:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h062;
                        end
                    end
//062 A C   GHI  LM    RSTU       tmpb  -> IND       6   R     DD,P0                     
                9'h062:
                    begin
                        // tmpb->IND   R DD,P0
                        IND<=tmpb;
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h063;
                    end
//063  B  E   IJ L  OPQR          OPR   -> M         4   none  RNI    
                9'h063:
                    begin
                        // OPR->M   RNI
                        if (indirectBusOpInProgress==0)
                        begin
                            if (instruction[0])
                                AX<=OPRr;
                            else
                                AX[7:0]<=OPRr[7:0];
                            executionState<=9'h1fd; //RNI
                        end
                    end

//064 A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           01010001?.00  MOV [i],A
                9'h064:
                    begin
                        // Q->tmpbL 
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h065;
                        end
                    end
//065 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h065:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h066;
                        end
                    end
//066 A C   GHI  L  OPQRSTU       tmpb  -> IND                                           
                9'h066:
                    begin
                        IND <= tmpb;
                        executionState<=9'h067;
                    end
//067  BC  F   J LM O QRSTU       M     -> OPR       6   W     DD,P0                     
                9'h067:
                    begin
                        // M -> OPR  DD,P0
                        OPRw<=AX;
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//06c A C  F  I  L  OPQR T        SIGMA -> IND       4   none  CORR        ?11111011.01  FARCALL2
                9'h06C:
                    begin
                        // SIGMA->IND
                        IND<=SIGMA;
                        correct<=1;
                        executionState<=9'h06d;
                    end
//06d  BC    H   LM O  R T        RC    -> OPR       6   w     DS,M2                     
                9'h06D:
                    begin
                        // RC->OPR  w DS,M2
                        OPRw<=REGISTER_CS;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h06e;
                    end
//06e A     G I   MN    S         tmpa  -> RC        1   PASS  tmpc                      
                9'h06E:
                    begin
                        // tmpa->RC  PASS tmpc
                        if (indirectBusOpInProgress==0)
                        begin
                            IND<=IND-2;
                            UpdateReg<=tmpa;
                            latchCS<=1;
                            selectShifter<=0;
                            aluAselect<=2'b10;     // ALUA = tmpC
                            aluWord<=1'b1;
                            operation<=ALU_OP_PASS;   // PASS A
                            executionState<=9'h06f;
                        end
                    end
//06f  BC     I  L N      U       PC    -> OPR       5   UNC   NEARCALL               
                9'h06F:
                    begin
                        // PC->OPR  UNC NEARCALL
                        OPRw<=REGISTER_IP;
                        executionState<=9'h077;
                    end

//077   C   GHI  L     RSTU       tmpb  -> PC        4   FLUSH none                      NEARCALL
                9'h077:
                    begin
                        // tmpb->PC   FLUSH
                        UpdateReg<=tmpb;
                        latchPC<=1;
                        flush<=1;
                        executionState<=9'h078;
                    end
//078  BCD   HI  L  OPQRSTU       IND   -> tmpc                            ?11111010.01  
                9'h078:
                    begin
                        // IND->tmpc
                        tmpc<=IND;
                        executionState<=9'h079;
                    end
//079 A C  F  I  L  OPQRSTU       SIGMA -> IND                                           
                9'h079:
                    begin
                        // SIGMA->IND
                        IND<=SIGMA;
                        executionState<=9'h07a;
                    end
//07a   CDEF  I  LM O QR TU       SIGMA -> SP        6   W     DS,P0                     
                9'h07A:
                    begin
                        // SIGMA->SP  W DS,P0
                        SP<=SIGMA;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1fd; //RNI
                    end

//07c A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           011101000.00  CALL cw
                9'h07C:
                    begin
                        // Q->tmpbL 
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h07d;
                        end
                    end
//07d ABC E  HIJ  MNOP RS         Q     -> tmpbH     1   DEC2  tmpc                      
                9'h07D:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            selectShifter<=0;
                            aluAselect<=2'b10;     // ALUA = tmpC
                            aluWord<=1'b1;
                            operation<=ALU_OP_DEC2;   // DEC2
                            executionState<=9'h07e;
                        end
                    end
//07e  BCD FG I  L  OPQR TU       SP    -> tmpc      4   none  SUSP                      
                9'h07E:
                    begin
                        tmpc <= SP;
                        suspend<=1;
                        executionState<=9'h07f;
                    end
//07f A C  F  I  L  OPQR T        SIGMA -> IND       4   none  CORR                      
                9'h07F:
                    begin
                        IND <= SIGMA;
                        correct<=1;
                        executionState<=9'h080;
                    end
//080   CD    I   M               PC    -> tmpa      1   ADD   tmpa        011101000.01  
                9'h080:
                    begin
                        // PC->tmpa  ADD
                        tmpa<=REGISTER_IP;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_ADD;     // A+B
                        executionState<=9'h081;
                    end
//081   C  F  I  L     RSTU       SIGMA -> PC        4   FLUSH none                      
                9'h081:
                    begin
                        // SIGMA -> PC   FLUSH 
                        UpdateReg<=SIGMA;
                        latchPC<=1;
                        flush<=1;   // FLUSH (and resumes prefetch queue)
                        executionState<=9'h082;
                    end
//082   CDE  HI    N    STU       IND   -> SP        0   UNC      7                      
                9'h082:
                    begin
                        // IND-> SP   UNC  7
                        SP<=IND;
                        executionState<=9'h083;
                    end
//083  BC   G I  LM O QR TU       tmpa  -> OPR       6   W     DS,P0  
                9'h083:
                    begin
                        // tmpa -> OPRw  DS,P0
                        OPRw<=tmpa;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//088 A CD F   J  MN   R TU       M     -> tmpb      1   XI    tmpb, NX    01101000?.00  rot rm,1
                9'h088:
                    begin
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpB=1;
                        selectShifter<=1;
                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluWord<=instruction[0];
                        operation<={1'b0,modrm[5:3]};
                        executionState<=9'h089;
                    end
//089  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h089:
                    begin
                        //SIGMA->M
                        code_Sigma2M=1;
                        code_M={instruction[0],modrm[2:0]};
                        code_FLAGS=FLAG_O_MSK|FLAG_C_MSK;
                        if (modrm[5]==1'b1)
                            code_FLAGS=FLAG_O_MSK|FLAG_C_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK;
                        else
                            code_FLAGS=FLAG_O_MSK|FLAG_C_MSK;
                        if ((modrm[7:6]==2'b11))
                            executionState<=9'h1FD;
                        else
                            executionState<=9'h08a;
                        end
//08a ABC  F HI  LM O QRSTU                          6   W     DD,P0                     
                9'h08A:
                    begin
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//098 A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        ?1111000?.00   TEST rm,i
                9'h098:
                    begin
                        // Q->tmpbL  L8
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[0]==0)
                                executionState<=9'h09a;     // L8
                            else
                                executionState<=9'h099;
                        end
                    end
//099 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h099:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h09a;
                        end
                    end
//09a   CD F   J  M  P    U       M     -> tmpa      1   AND   tmpa, NX                  
                9'h09A:
                    begin
                        // M->tmpa  AND tmpa,tmpb
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpA=1;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=instruction[0];
                        operation<=ALU_OP_AND;     // A&B
                        executionState<=9'h09b;
                    end
//09b ABC  F  I KL  OPQR          SIGMA -> no dest   4   none  RNI      F  
                9'h09B:
                    begin
                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK|FLAG_C_MSK;
                        executionState<=9'h1fd; //RNI
                    end

//0a4   CD F H J L  OPQRSTU       R     -> tmpa                            01000011?.00  XCHG rm,r
                9'h0A4:
                    begin
                        code_M={instruction[0],modrm[5:3]};
                        code_R2TmpA=1;
                        executionState<=9'h0a5;
                    end

//0a5 A CD F   J L  OPQRSTU       M     -> tmpb                                          
                9'h0A5:
                    begin
                        code_M={instruction[0],modrm[2:0]};
                        code_M2TmpB=1;
                        executionState<=9'h0a6;
                    end
//0a6 AB  E GHI  L  OPQR  U       tmpb  -> R         4   none  WB,NX
                9'h0A6:
                    begin
                        code_M={instruction[0],modrm[5:3]};
                        code_TmpB2R=1;
                        executionState<=9'h0a7;
                    end
//0a7  B  E G I  L  OPQR          tmpa  -> M         4   none  RNI                       
                9'h0A7:
                    begin
                        //tmpa->M
                        code_TmpA2M=1;
                        code_M={instruction[0],modrm[2:0]};
                        if ((modrm[7:6]==2'b11))
                            executionState<=9'h1FD;
                        else
                            executionState<=9'h0a8;
                        end
//0a8 ABC  F HI  LM O QRSTU                          6   W     DD,P0       01000011?.01  
                9'h0A8:
                    begin
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//0ac A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           01110010?.00  IN A,ib
                9'h0AC:
                    begin
                        // Q->tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop; // no need to sign extend
                            readTop<=1;
                            executionState<=9'h0ad;
                        end
                    end
//0ad ABC EF HIJ  MN     T        ZERO  -> tmpbH     1   PASS  tmpb                      
                9'h0AD:
                    begin
                        // 0->tmpbH  PASS tmpb
                        tmpb[15:8]<=0;
                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpB
                        aluWord<=1'b1;
                        operation<=ALU_OP_PASS;   // PASS A
                        executionState<=9'h0ae;
                    end
//0ae A C  F  I  LM     STU       SIGMA -> IND       6   R     D0,P0                     
                9'h0AE:
                    begin
                        // SIGMA -> IND
                        IND<=SIGMA;
                        indirect<=1;
                        indirectSeg<=SEG_ZERO;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=0;
                        ind_readWrite<=0;
                        executionState<=9'h0af;
                    end
//0af  B  E   IJ L  OPQR          OPR   -> M         4   none  RNI          
                9'h0AF:
                    begin
                        if (indirectBusOpInProgress==0)
                        begin
                            if (instruction[0])
                                AX<=OPRr;
                            else
                                AX[7:0]<=OPRr[7:0];
                            executionState<=9'h1fd; // RNI
                        end
                    end

//0b0 A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           01110011?.00  OUT ib,A
                9'h0B0:
                    begin
                        // Q->tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop; // no need to sign extend
                            readTop<=1;
                            executionState<=9'h0b1;
                        end
                    end
//0b1 ABC EF HIJ  MN     T        ZERO  -> tmpbH     1   PASS  tmpb                      
                9'h0B1:
                    begin
                        // 0->tmpbH  PASS tmpb
                        tmpb[15:8]<=0;
                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpB
                        aluWord<=1'b1;
                        operation<=ALU_OP_PASS;   // PASS A
                        executionState<=9'h0b2;
                    end
//0b2 A C  F  I  L  OPQRSTU       SIGMA -> IND                                           
                9'h0B2:
                    begin
                        // SIGMA -> IND
                        IND<=SIGMA;
                        executionState<=9'h0b3;
                    end
//0b3  BC  FG    LM O Q STU       XA    -> OPR       6   W     D0,P0 
                9'h0B3:
                    begin
                        // XA -> OPR   D0,P0
                        OPRw<=AX;
                        indirect<=1;
                        indirectSeg<=SEG_ZERO;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=0;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end

//0bc A C  FG I  LM    R          SP    -> IND       6   R     DS,P2       0110000?1.00  RET
                9'h0BC:
                    begin
                        // SIGMA -> IND
                        IND<=SP;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h0bd;
                    end
//0bd ABC  F HI  L  OPQR TU                          4   none  SUSP
                9'h0BD:
                    begin
                        suspend<=1;
                        executionState<=9'h0be;
                    end                      
//0be   C     IJ L     RSTU       OPR   -> PC        4   FLUSH none
                9'h0BE:
                    begin
                        if (indirectBusOpInProgress==0)
                        begin
                            UpdateReg<=OPRr;
                            latchPC<=1;
                            flush<=1;
                            executionState<=9'h0bf;
                        end
                    end
//0bf   CDE  HI  L  OPQR          IND   -> SP        4   none  RNI         
                9'h0BF:
                    begin
                        SP<=IND+2;
                        executionState<=9'h1fd;     //RNI
                    end

//0c2 A C  FG I  LM    R          SP    -> IND       6   R     DS,P2                     FARRET
                9'h0C2:
                    begin
                        // SP->IND  R DS,P2
                        IND<=SP;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h0c3;
                    end
//0c3 ABC  F HI  L  OPQR TU                          4   none  SUSP                      
                9'h0C3:
                    begin
                        if (indirectBusOpInProgress==0)
                        begin
                            IND<=IND+2;
                            suspend<=1;
                            executionState<=9'h0c4; 
                        end
                    end
//0c4   C     IJ   N PQ ST        OPR   -> PC        0   X0       6        0110010?1.01  
                9'h0C4:
                    begin
                        // OPR->PC   X0  6
                        UpdateReg<=OPRr;
                        latchPC<=1;
                        if (instruction[3])
                            executionState<=9'h0c6;
                        else
                            executionState<=9'h0c5;
                    end
//0c5 ABC  F HI  L     RS                            4   FLUSH RTN                       
                9'h0C5:
                    begin
                        // FLUSH RTN
                        flush<=1;
                        executionState<=PostEffectiveAddressReturn; //RTN
                    end
//0c6 ABC  F HI  LM    R                             6   R     DS,P2                     
                9'h0C6:
                    begin
                        // R DS,P2
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h0c7;
                    end
//0c7 A       IJ L     RS         OPR   -> RC        4   FLUSH RTN                       
                9'h0C7:
                    begin
                        // OPR->RC   FLUSH RTN
                        if (indirectBusOpInProgress==0)
                        begin
                            IND<=IND+2;
                            UpdateReg<=OPRr;
                            latchCS<=1;
                            flush<=1;
                            executionState<=PostEffectiveAddressReturn; //RTN
                        end
                    end

//0c8 ABC  F HI  LMN                                 7   UNC   FARRET      011001111.00  IRET
                9'h0C8:
                    begin
                        PostEffectiveAddressReturn<=9'h0c9;
                        executionState<=9'h0c2;
                    end
//0c9 ABC  F HI  LM    R                             6   R     DS,P2                     
                9'h0C9:
                    begin
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h0ca;
                    end
//0ca ABCD    IJ L  OPQRSTU       OPR   -> F                                             
                9'h0CA:
                    begin
                        // OPR->F
                        if (indirectBusOpInProgress==0)
                        begin
                            FLAGS<=OPRr;
                            executionState<=9'h0cb; 
                        end
                    end
//0cb   CDE  HI  L  OPQR          IND   -> SP        4   none  RNI                       
                9'h0CB:
                    begin
                        // IND -> SP    RNI
                        SP<=IND+2;
                        executionState<=9'h1fd;
                    end

//0d0 A C E  HIJ     P   T        Q     -> tmpbL     0   L8       2        0111010?1.00  JMP cw/JMP cb
                9'h0D0:
                    begin
                        // Q->tmpbL   L8
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (instruction[1]==0)
                                executionState<=9'h0D1;
                            else
                                executionState<=9'h0D2;     // L8
                        end
                    end
//0d1 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h0D1:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h0D2;
                        end
                    end
//0d2 ABC  F HI  L  OPQR TU                          4   none  SUSP                      RELJMP
                9'h0D2:
                    begin
                        suspend<=1;
                        executionState<=9'h0D3;
                    end
//0d3 ABC  F HI  L  OPQR T                           4   none  CORR                      
                9'h0D3:
                    begin
                        correct<=1;
                        executionState<=9'h0D4;
                    end
//0d4   CD    I   M               PC    -> tmpa      1   ADD   tmpa        0111010?1.01  
                9'h0D4:
                    begin
                        // PC -> tmpa
                        tmpa<=REGISTER_IP;
                        
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_ADD;     // A+B

                        executionState<=9'h0D5;
                    end
//0d5   C  F  I  L     R          SIGMA -> PC        4   FLUSH RNI 
                9'h0D5:
                    begin
                        // SIGMA -> PC   FLUSH RNI
                        UpdateReg<=SIGMA;
                        latchPC<=1;
                        flush<=1;   // FLUSH (and resumes prefetch queue)
                        executionState<=9'h1FD; // RNI
                    end

//0e0 A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           011101010.00  JMP cd
                9'h0E0:
                    begin
                        // Q-> tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h0E1;
                        end
                    end
//0e1 ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h0E1:
                    begin
                        // Q-> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h0E2;
                        end
                    end
//0e2   C E  HIJ L  OPQRSTU       Q     -> tmpaL                                         
                9'h0E2:
                    begin
                        // Q-> tmpaL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpa[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h0E3;
                        end
                    end
//0e3  BC E  HIJ L  OPQRSTU       Q     -> tmpaH                                         
                9'h0E3:
                    begin
                        // Q-> tmpaH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpa[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h0E4;
                        end
                    end
//0e4 ABC  F HI  L  OPQR TU                          4   none  SUSP        011101010.01  
                9'h0E4:
                    begin
                        suspend<=1;
                        executionState<=9'h0E5;
                    end
//0e5   C   GHI  L  OPQRSTU       tmpb  -> PC                                            
                9'h0E5:
                    begin
                        // tmpb -> PC
                        UpdateReg<=tmpb;
                        latchPC<=1;
                        executionState<=9'h0E6;
                    end
//0e6 A     G I  L     R          tmpa  -> RC        4   FLUSH RNI
                9'h0E6:
                    begin
                        // tmpa -> RC   FLUSH RNI
                        UpdateReg<=tmpa;
                        latchCS<=1;
                        flush<=1;   // FLUSH (and resumes prefetch queue)
                        executionState<=9'h1FD; // RNI
                    end

//0e8 A C E  HIJ L  OPQRSTU       Q     -> tmpbL                           0011?????.00  Jcond cb
                9'h0E8:
                    begin
                        // Q -> tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            executionState<=9'h0e9;
                        end
                    end
//0e9 ABC  F HI  L NOPQ  T                           5   XC    RELJMP                    
                9'h0E9:
                    begin
                        case (instruction[3:0])
                            4'b0000:    if (Cond_O ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0001:    if (Cond_NO) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0010:    if (Cond_C ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0011:    if (Cond_AE) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0100:    if (Cond_E ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0101:    if (Cond_NE) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0110:    if (Cond_BE) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b0111:    if (Cond_A ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1000:    if (Cond_S ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1001:    if (Cond_NS) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1010:    if (Cond_P ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1011:    if (Cond_PO) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1100:    if (Cond_L ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1101:    if (Cond_GE) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1110:    if (Cond_LE) executionState<=9'h0d2; else executionState<=9'h0ea;
                            4'b1111:    if (Cond_G ) executionState<=9'h0d2; else executionState<=9'h0ea;
                            default: begin end
                        endcase
                    end
//0ea ABC  F HI  L  OPQR                             4   none  RNI                   
                9'h0EA:
                    begin
                        executionState<=9'h1FD; // RNI 
                    end

//0ec  B  EF H J L  OPQR          R     -> M         4   none  RNI         0100011?0.00  MOV rmw<->sr
                9'h0EC:
                    begin
                        // R/M->tmpb
                        if (modrm[5]==1)
                            executionState<=9'h1FD; // RNI (invalid modrm combination)
                        else
                        begin
                            if (instruction[1] == 0)
                            begin
                                code_SR2M=1;
                                code_M={instruction[0],modrm[2:0]};
                                if (modrm[7:6]==2'b11)
                                    executionState<=9'h1FD;
                                else
                                    executionState<=9'h0ed;
                            end
                            else
                            begin
                                code_M2SR=1;
                                code_M={instruction[0],modrm[2:0]};
                                executionState<=9'h1FD;
                            end
                        end
                    end
//0ed ABC  F HI  LM O QRSTU                          6   W     DD,P0                     
                9'h0ED:
                    begin
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1FD; // RNI
                    end
//112  BCD FGH    MN    S         BC    -> tmpc      1   PASS  tmpc                      RPTS
                9'h112:
                    begin
                        //tmpc<=CX;     // MOVED This to the REP instruction, we shouldn't reload this every iteration
                        selectShifter<=0;
                        aluAselect<=2'b10;     // ALUA = tmpC
                        aluWord<=1'b1;
                        operation<=ALU_OP_PASS;   // PASS A
                        executionState<=9'h113;
                    end
//113 ABC  F  I   MNO  RS         SIGMA -> no dest   1   DEC   tmpc                      
                9'h113:
                    begin
                        selectShifter<=0;
                        aluAselect<=2'b10;     // ALUA = tmpC
                        aluWord<=1'b1;
                        operation<=ALU_OP_DEC;   // DEC (for next iteration)
                        if (fz)
                            executionState<=9'h115;
                        else
                            executionState<=9'h114;
                    end
//114 ABC  F HI    N P R T                           0   NZ      10        011010111.10  
                9'h114:
                    begin
                        executionState<=9'h116;
                    end
//115 ABC  F HI  L  OPQR                             4   none  RNI     
                9'h115:
                    begin
                        if (repeatF)
                            CX<=tmpc;
                        executionState<=9'h1FD; // RNI
                    end
//116 ABC  F HI  L  OPQRS                            4   none  RTN                       
                9'h116:
                    begin
                        executionState<=PostEffectiveAddressReturn;
                    end

//11c A C  FGHIJ LMNO Q   U       IK    -> IND       7   F1    RPTS        01010101?.00  STOS
                9'h11C:
                    begin
                        IND <= DI;
                        if (repeatF)
                        begin
                            PostEffectiveAddressReturn<=9'h11d;
                            executionState<=9'h112;
                        end
                        else
                            executionState<=9'h11d;
                    end

//11d  BC  F   J LM O     U       M     -> OPR       6   w     DA,BL                     
                9'h11D:
                    begin
                        OPRw <= AX;
                        indirect<=1;
                        indirectSeg<=SEG_ES;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h11e;
                    end
//11e ABCDE  HI    N  Q S U       IND   -> IK        0   NF1      5                      
                9'h11E:
                    begin
                        if ((indirectBusOpInProgress)==0) // should we wait here?? or at 11d which would allow better throughput
                        begin
                            // Adjusted IND value here - probably via BIU originally, for now, just do adjustment directly
                            DI <= FLAGS[FLAG_D_IDX]==0 ? IND + (instruction[0]?2:1) : IND - (instruction[0]?2:1);
                            
                            if (!repeatF)
                                executionState<= 9'h115;    // ?? unclear exact dest, this seems ok though
                            else
                                executionState<=9'h11f;
                        end
                    end
//11f  BCD F  I  L NOP R          SIGMA -> tmpc      5   INT   RPTI
                9'h11F:
                    begin
                        tmpc<=SIGMA;
                        // TODO REP interrupt checks
                        executionState<=9'h11c;
                    end

//12c ABC  F HI  LMNO Q   U                          7   F1    RPTS        01010?10?.00  MOVS/LODS
                9'h12C:
                    begin
                        if (repeatF)
                        begin
                            PostEffectiveAddressReturn<=9'h12d;
                            executionState<=9'h112;
                        end
                        else
                            executionState<=9'h12d;
                    end
//12d A C  FG IJ LM    RS U       IJ    -> IND       6   R     DD,BL                     
                9'h12D:
                    begin
                        IND<=SI;
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h12e;
                    end
//12e  BCDE  HI    N PQR          IND   -> IJ        0   X0       8                      
                9'h12E:
                    begin
                        if ((indirectBusOpInProgress)==0) 
                        begin
                            // Adjusted IND value here - probably via BIU originally, for now, just do adjustment directly
                            SI <= FLAGS[FLAG_D_IDX]==0 ? IND + (instruction[0]?2:1) : IND - (instruction[0]?2:1);
                            
                            if (instruction[3]==1)
                                executionState<= 9'h1f8;    // LODS
                            else
                                executionState<=9'h12f;
                        end
                    end
//12f A C  FGHIJ LM O     U       IK    -> IND       6   w     DA,BL                     
                9'h12F:
                    begin
                        IND <= DI;
                        OPRw <= OPRr;
                        indirect<=1;
                        indirectSeg<=SEG_ES;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h130;
                    end
//130 ABCDE  HI    N  Q STU       IND   -> IK        0   NF1      7        01010?10?.01  
                9'h130:
                    begin
                        if ((indirectBusOpInProgress)==0) // should we wait here?? or at 12f which would allow better throughput
                        begin
                            // Adjusted IND value here - probably via BIU originally, for now, just do adjustment directly
                            DI <= FLAGS[FLAG_D_IDX]==0 ? IND + (instruction[0]?2:1) : IND - (instruction[0]?2:1);
                            
                            if (!repeatF)
                                executionState<= 9'h133;
                            else
                                executionState<=9'h131;
                        end
                    end
//131  BCD F  I  L NOP R          SIGMA -> tmpc      5   INT   RPTI                      
                9'h131:
                    begin
                        tmpc<=SIGMA;
                        // TODO REP interrupt checks
                        executionState<=9'h12c; // running on would also work, and would make sense with the fetching of BC per REP - TODO come back and recheck
                    end
//132 A  DE G IJ   N P    U       tmpc  -> BC        0   NZ       1                      
//133 ABC  F HI  L  OPQR                             4   none  RNI             
                9'h133:
                    begin
                        executionState<=9'h1FD; // RNI
                    end

//140  BCD FGH    MNO  RS         BC    -> tmpc      1   DEC   tmpc        011100010.00  LOOP
                9'h140:
                    begin
                        // BC->tmpc
                        tmpc<=CX;
                        selectShifter<=0;
                        aluAselect<=2'b10;     // ALUA = tmpc
                        aluWord<=1'b1;
                        operation<=ALU_OP_DEC; // DEC A
                        executionState<=9'h141;
                    end
//141 A  DEF  I  L  OPQRSTU       SIGMA -> BC                                            
                9'h141:
                    begin
                        // SIGMA -> BC
                        CX<=SIGMA;
                        executionState<=9'h142; // RNI
                    end
//142 A C E  HIJ L N P   T        Q     -> tmpbL     5   NZ    RELJMP                    
                9'h142:
                    begin
                        // Q -> tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            if (fz==0)
                                executionState<=9'h0D2; // RELJMP
                            else
                                executionState<=9'h143;
                        end
                    end
//143 ABC  F HI  L  OPQR                             4   none  RNI     
                9'h143:
                    begin
                        // RNI
                        executionState<=9'h1FD; //RNI
                    end

//17c A CD F   J  MN   R TU       M     -> tmpb      1   XI    tmpb, NX    00100????.00  INC/DEC
                9'h17C:
                    begin
                        // tmpb -> M     RNI
                        code_M={1'b1,instruction[2:0]};
                        code_M2TmpB=1;

                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_INC+{3'h0,instruction[3]};  // Inc/Dec

                        executionState<=9'h17D;
                    end
//17d  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h17D:
                    begin
                        // SIGMA -> M     RNI
                        code_M={1'b1,instruction[2:0]};
                        code_Sigma2M=1;

                        // Flags update
                        code_FLAGS=FLAG_O_MSK|FLAG_S_MSK|FLAG_Z_MSK|FLAG_A_MSK|FLAG_P_MSK;

                        executionState<=9'h1FD; // RNI
                    end

//19a  BCD FG    LM  P  STU       XA    -> tmpc      6   IRQ   D0,P0                     IRQ 
                9'h19A:
                    begin
                        // XA -> tmpc     IRQ D0,P0
                        tmpc<=AX;
                        IND<=0; // Should float ..
                        indirect<=1;
                        irq<=1; // do an irq indirect cycle (not a strobe)
                        indirectSeg<=SEG_ZERO;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h19b;
                    end
//19b    DE   IJ L  OPQR TU       OPR   -> XA        4   none  SUSP                      
                9'h19B:
                    begin
                        if ((indirectBusOpInProgress)==0)
                        begin
                            // OPR -> XA    SUSP
                            irq<=0;
                            AX<=OPRr;
                            suspend<=1;
                            executionState<=9'h19c;
                        end
                    end
//19c A C EF     L  OPQRSTU       X     -> tmpbL                           100000000.01  
                9'h19C:
                    begin
                        // X -> tmbl
                        tmpb[7:0]<=AX[15:8];
                        executionState<=9'h19d;
                    end
//19d    DE G IJ L  OPQRSTU       tmpc  -> XA                                            INTR/// THIS MIGHT BE 19E not 19D
                9'h19D:
                    begin
                        // tmpc -> XA
                        AX<=tmpc;
                        executionState<=9'h19e;
                    end
//19e ABC EF HIJ  M      T        ZERO  -> tmpbH     1   ADD   tmpb                      
                9'h19E:
                    begin
                        // ZERO -> tmpbH  ADD
                        tmpb[15:8]<=0;
                        selectShifter<=0;
                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_ADD;     // A+B
                        executionState<=9'h19f;
                    end
//19f A CD F  I  L  OPQRSTU       SIGMA -> tmpb                                          
                9'h19F:
                    begin
                        // SIGMA -> tmpb
                        tmpb<=SIGMA;
                        executionState<=9'h1a0;
                    end
//1a0 A C  F  I  LM     S         SIGMA -> IND       6   R     D0,P2       100000000.10  
                9'h1A0:
                    begin
                        // SIGMA -> IND    R D0,P2
                        IND<=SIGMA;
                        indirect<=1;
                        indirectSeg<=SEG_ZERO;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h1a1;
                    end
//1a1 A CD    IJ  MNOP RS         OPR   -> tmpb      1   DEC2  tmpc                      
                9'h1A1:
                    begin
                        // OPR -> tmpb  DEC2 tmpc
                        if (indirectBusOpInProgress==0)
                        begin
                            tmpb<=OPRr; // IP component
                            selectShifter<=0;
                            aluAselect<=2'b10;     // ALUA = tmpc
                            aluWord<=1'b1;
                            operation<=ALU_OP_DEC2;   // DEC2
                            executionState<=9'h1a2;
                        end
                    end
//1a2  BCD FG I  LM     STU       SP    -> tmpc      6   R     D0,P0                     
                9'h1A2:
                    begin
                        // SP -> tmpc    R D0,P0
                        tmpc<=SP;
                        IND<=IND+2;
                        indirect<=1;
                        indirectSeg<=SEG_ZERO;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h1a3;
                    end
//1a3   CD    IJ L  OPQR TU       OPR   -> tmpa      4   none  SUSP                      
                9'h1A3:
                    begin
                        // OPR -> tmpa   SUSP
                        if (indirectBusOpInProgress==0)
                        begin
                            tmpa<=OPRr; // CS component
                            suspend<=1;
                            executionState<=9'h1a4;
                        end
                    end
//1a4  BC   GHIJ L    QRSTU       F     -> OPR       4   CITF  none        100000000.11  
                9'h1A4:
                    begin
                        // F->OPR   CITF
                        OPRw<=FLAGS;
                        FLAGS[FLAG_I_IDX]<=0;
                        FLAGS[FLAG_T_IDX]<=0;
                        executionState<=9'h1a5;
                    end
//1a5 A C  F  I  LM O  R TU       SIGMA -> IND       6   w     DS,P0                     
                9'h1A5:
                    begin
                        IND<=SIGMA;
                        indirect<=1;
                        indirectSeg<=SEG_SS;
                        ind_byteWord<=1;
                        ind_ioMreq<=1;
                        ind_readWrite<=1;
                        executionState<=9'h1a6;
                    end
//1a6  BCD   HI  L N    S U       IND   -> tmpc      5   UNC   FARCALL2                  
                9'h1A6:
                    begin
                        // IND->tmpc   FARCALL2
                        if (indirectBusOpInProgress==0)
                        begin
                            tmpc<=IND;
                            executionState<=9'h06c;
                        end
                    end

//1d4   CD FGH J L  OPQRSTU       HL    -> tmpa                            101000000.00  [BX+SI]
                9'h1D4:
                    begin
                        // HL -> tmpa    
                        tmpa<=BX;
                        executionState<=9'h1d5;
                    end
//1d5 A CD FG IJ L  OPQRSTU       IJ    -> tmpb                                          
                9'h1D5:
                    begin
                        // IJ -> tmpb
                        tmpb<=SI;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_ADD;     // A+B
                        executionState<=9'h1d6;
                    end
//1d6   CD F  I  L N     TU       SIGMA -> tmpa      5   UNC   EAOFFSET                  
                9'h1D6:
                    begin
                        // SIGMA -> tmpa    
                        tmpa<=SIGMA;
                        executionState<=9'h1f7; //EA OFFSET
                    end

//1d7   CD FGHI  L  OPQRSTU       MP    -> tmpa                                          [BP+DI]
                9'h1D7:
                    begin
                        // MP -> tmpa    
                        tmpa<=BP;
                        executionState<=9'h1d8;
                    end
//1d8 A CD FGHIJ L  OPQRSTU       IK    -> tmpb                            101000000.01  
                9'h1D8:
                    begin
                        // IK -> tmpb
                        tmpb<=DI;
                        selectShifter<=0;
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord<=1'b1;
                        operation<=ALU_OP_ADD;     // A+B
                        executionState<=9'h1d9;
                    end
//1d9   CD F  I  L N     TU       SIGMA -> tmpa      5   UNC   EAOFFSET                  
                9'h1D9:
                    begin
                        // SIGMA -> tmpa    
                        tmpa<=SIGMA;
                        executionState<=9'h1f7; //EA OFFSET
                    end

//1da   CD FGH J   N    S         HL    -> tmpa      0   UNC      4                      [BX+DI]
                9'h1DA:
                    begin
                        // HL -> tmpa    
                        tmpa<=BX;
                        executionState<=9'h1d8;
                    end

//1db   CD FGHI    N      U       MP    -> tmpa      0   UNC      1                      [BP+SI]
                9'h1DB:
                    begin
                        // MP -> tmpa    
                        tmpa<=BP;
                        executionState<=9'h1d5;
                    end

//1dc   C E  HIJ L  OPQRSTU       Q     -> tmpaL                           101000000.10  [iw]
                9'h1DC:
                    begin
                        // Q -> tmpaL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpa[7:0]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h1dd;
                        end
                    end
//1dd  BC E  HIJ L N    S         Q     -> tmpaH     5   UNC   EAFINISH                  
                9'h1DD:
                    begin
                        // Q -> tmpaH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpa[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h1f2; // EAFINISH
                        end
                    end

//1de A C E  HIJ      QRS         Q     -> tmpbL     0   MOD1    12                      [i]
                9'h1DE:
                    begin
                        // Q -> tmpbL
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[7:0]<=prefetchTop;
                            tmpb[15:8]<={8{prefetchTop[7]}};
                            readTop<=1;
                            selectShifter<=0;
                            aluAselect<=2'b00;     // ALUA = tmpa
                            aluBselect<=2'b01;     // ALUB = tmpb
                            aluWord<=1'b1;
                            operation<=ALU_OP_ADD;     // A+B
                            if (modrm[7:6]==2'b01)
                                executionState<=9'h1e0;  // MOD1
                            else
                                executionState<=9'h1df;
                        end
                    end
//1df ABC E  HIJ L  OPQRSTU       Q     -> tmpbH                                         
                9'h1DF:
                    begin
                        // Q -> tmpbH
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            tmpb[15:8]<=prefetchTop;
                            readTop<=1;
                            executionState<=9'h1e0;
                        end
                    end
//1e0   CD F  I  L N    S         SIGMA -> tmpa      5   UNC   EAFINISH    101000000.11  
                9'h1E0:
                    begin
                        // SIGMA -> tmpa    
                        tmpa<=SIGMA;
                        executionState<=9'h1f2; //EAFINISH
                    end

//1e1 A C   G I  LM    RSTU       tmpa  -> IND       6   R     DD,P0                     EALOAD
                9'h1E1:
                    begin
                        // DD,P0  (DS with override)
                        IND<=tmpa;
                        indirect<=1;
                        indirectSeg<=segPrefix;
                        ind_byteWord<=instruction[0];
                        ind_ioMreq<=1;
                        ind_readWrite<=0;
                        executionState<=9'h1e2; 
                    end
//1e2 A CD    IJ L  OPQRS         OPR   -> tmpb      4   none  RTN                       
                9'h1E2:
                    begin
                        if ((indirectBusOpInProgress)==0)
                        begin
                            // OPR -> tmpb
                            tmpb<=OPRr;  // ?? (think this is redundant) perhaps, return address +1?
                            executionState<=PostEffectiveAddressReturn; // RTN
                        end
                    end

//1e3 A C   G I  L  OPQRS         tmpa  -> IND       4   none  RTN                       EADONE
                9'h1E3:
                    begin
                        // tmpa -> IND
                        IND<=tmpa;
                        executionState<=PostEffectiveAddressReturn; // RTN
                    end

//1e4 AB   F HIJ L  OPQR TU       ZERO  -> RD        4   none  SUSP        110000000.00  RESET
                9'h1E4:
                    begin
                        // ZERO -> RD    SUSP
                        UpdateReg<=0;
                        latchDS<=1;
                        suspend<=1;
                        executionState<=9'h1E5;
                    end
//1e5 A    F HI  L  OPQRSTU       ONES  -> RC                                            
                9'h1E5:
                    begin
                        // ONES -> RC 
                        UpdateReg<={16{1'b1}};
                        latchCS<=1;
                        executionState<=9'h1E6;
                    end
//1e6   C  F HIJ L     RSTU       ZERO  -> PC        4   FLUSH none                      
                9'h1E6:
                    begin
                        // ZERO -> PC    FLUSH
                        UpdateReg<=0;
                        latchPC<=1;
                        flush<=1;
                        executionState<=9'h1E7;
                    end
//1e7 ABCD F HIJ L  OPQRSTU       ZERO  -> F                                             
                9'h1E7:
                    begin
                        // ZERO -> F
                        FLAGS<=0;
                        executionState<=9'h1E8;
                    end
//1e8      F HIJ L  OPQRSTU       ZERO  -> RA                              110000000.01  
                9'h1E8:
                    begin
                        // ZERO -> RA
                        UpdateReg<=0;
                        latchES<=1;
                        latchSS<=1; // RESET 1 early for testing purposes
                        executionState<=9'h1E9;
                    end
//1e9  B   F HIJ L  OPQR          ZERO  -> RS        4   none  RNI             
                9'h1E9:
                    begin
                        // ZERO -> RS   RNI
                        UpdateReg<=0;
                        //latchSS<=1;
                        executionState<=9'h1FD;     // RNI
                    end

//1f2 (NOT REAL mOP) EAFINISH
                9'h1F2:
                    begin
                        if ((instruction[1]|readModifyWrite)==0)
                        begin
                            executionState<=9'h1e3; // EADONE
                        end
                        else
                        begin
                            executionState<=9'h1e1; // EALOAD
                        end
                    end

//1f3 (NOT REAL mOP) Q -> MODRM (reg == instruction kind e.g. INC/DEC...)
                9'h1F3:
                    begin
                        // Q -> MODRM
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            modrm[7:0]<=prefetchTop;
                            readTop<=1;
                            if (prefetchTop[5:4]==2'b00)
                            begin
                                readModifyWrite=1;
                                if (prefetchTop[7:6]==2'b11)
                                    executionState<=9'h020;
                                else
                                begin
                                    PostEffectiveAddressReturn<=9'h020;
                                    executionState<=9'h1f6;
                                end
                            end
                            // TODO handle other FE/FF instructions (fornow, will go very wrong here)
                        end
                    end

//1f4 (NOT REAL mOP) Q -> MODRM (reg == instruction kind e.g. TEST,NEG)
                9'h1F4:
                    begin
                        // Q -> MODRM
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            modrm[7:0]<=prefetchTop;
                            readTop<=1;
                            case ({prefetchTop[7]&prefetchTop[6],prefetchTop[5:3]})
                                4'b0000: begin readModifyWrite=1; executionState<=9'h1f6; PostEffectiveAddressReturn<=9'h098; end
                                4'b1000: executionState<=9'h098;
                                4'b0011: begin readModifyWrite=1; executionState<=9'h1f6; PostEffectiveAddressReturn<=9'h050; end
                                4'b1011: executionState<=9'h050;
                                default: begin end
                            endcase
                        end
                    end

//1f5 (NOT REAL mOP) Q -> MODRM
                9'h1F5:
                    begin
                        // Q -> MODRM
                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            modrm[7:0]<=prefetchTop;
                            readTop<=1;
                            if (prefetchTop[7:6]==2'b11)
                                executionState<=PostEffectiveAddressReturn;
                            else
                                executionState<=9'h1f6;
                        end
                    end
//1f6 (NOT REAL mOP) jump to correct entry for 00/01/11 mod
                9'h1F6:
                    begin
                        case ({modrm[7:6],modrm[2:0]})
                            5'b00000:                   executionState<=9'h1d4;
                            5'b00001:                   executionState<=9'h1da;
                            5'b00010:                   executionState<=9'h1db;
                            5'b00011:                   executionState<=9'h1d7;
                            5'b00100:                   executionState<=9'h003;
                            5'b00101:                   executionState<=9'h01f;
                            5'b00110:                   executionState<=9'h1dc;
                            5'b00111:                   executionState<=9'h037;
                            5'b01000:                   executionState<=9'h1d4;
                            5'b01001:                   executionState<=9'h1da;
                            5'b01010:                   executionState<=9'h1db;
                            5'b01011:                   executionState<=9'h1d7;
                            5'b01100:                   executionState<=9'h003;
                            5'b01101:                   executionState<=9'h01f;
                            5'b01110:                   executionState<=9'h023;
                            5'b01111:                   executionState<=9'h037;
                            5'b10000:                   executionState<=9'h1d4;
                            5'b10001:                   executionState<=9'h1da;
                            5'b10010:                   executionState<=9'h1db;
                            5'b10011:                   executionState<=9'h1d7;
                            5'b10100:                   executionState<=9'h003;
                            5'b10101:                   executionState<=9'h01f;
                            5'b10110:                   executionState<=9'h023;
                            5'b10111:                   executionState<=9'h037;
                            default:                    begin end
                        endcase
                    end

//1f7 (NOT REAL mOP) EAOFFSET
                9'h1F7:
                    begin
                        if ((instruction[1]|readModifyWrite)==0)
                        begin
                            if (modrm[7:6]==2'b00)
                                executionState<=9'h1e3; // EADONE
                            else
                                executionState<=9'h1de; // [i]
                        end
                        else
                        begin
                            if (modrm[7:6]==2'b00)
                                executionState<=9'h1e1; // EALOAD
                            else
                                executionState<=9'h1de; // [i]
                        end
                    end

//1f8  B  E   IJ   NO Q S U       OPR   -> M         0   F1       5        01010?10?.10  MOVS/LODS continued
                9'h1F8:
                    begin
                        if (instruction[0])
                            AX<=OPRr;
                        else
                            AX[7:0]<=OPRr[7:0];
                        
                        if (repeatF)
                            executionState<= 9'h131;
                        else
                            executionState<=9'h1f9;
                    end
//1f9 ABC  F HI  L  OPQR                             4   none  RNI   
                9'h1F9:
                    begin
                        executionState<=9'h1FD; // RNI
                    end

//1fb (NOT REAL mOP) - Used for HLT
                9'h1FB:
                    begin
                        if ((irqPending & FLAGS[FLAG_I_IDX])|TRACE_MODE) // TODO other interrupt sources
                            executionState<=9'h1fd; // RNI
                    end

//1fd (NOT REAL mOP)
                // RNI
                default:        // 9'h1FD - Waiting for instruction state
                    begin

                        if ((prefetchEmpty|indirectBusOpInProgress|TRACE_MODE)==0)
                        begin

                            repeatF<=0;
                            modrm<=8'hFF;
                            segPrefix<=SEG_DS;
                            if (irqPending & FLAGS[FLAG_I_IDX])
                            begin
                                // don't consume prefetch, instead interrupt
                                executionState<=9'h19A;
                            end
                            else
                            begin
                                instruction<=prefetchTop;
                                FetchExecStateFromInstruction(prefetchTop);
                                readTop<=1;
                            end
                        end

                    end
            endcase


            // Handlers

            if (code_TmpA2M)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    OPRw<=tmpa;
                end
                else
                begin
                    WriteToRegister(code_M[3],code_M[2:0],tmpa);
                end
            end
            if (code_TmpB2M)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    OPRw<=tmpb;
                end
                else
                begin
                    WriteToRegister(code_M[3],code_M[2:0],tmpb);
                end
            end
            if (code_TmpB2R)
            begin
                WriteToRegister(code_M[3],code_M[2:0],tmpb);
            end
            if (code_Sigma2M)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    OPRw<=SIGMA;
                end
                else
                begin
                    WriteToRegister(code_M[3],code_M[2:0],SIGMA);
                end
            end
            if (code_Sigma2R)
            begin
                WriteToRegister(code_M[3],code_M[2:0],SIGMA);
            end
            if (code_M2TmpB)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    tmpb<=OPRr;
                end
                else
                begin
                    tmpb<=ReadFromRegister(code_M[3],code_M[2:0]);
                end
            end
            if (code_M2TmpA)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    tmpa<=OPRr;
                end
                else
                begin
                    tmpa<=ReadFromRegister(code_M[3],code_M[2:0]);
                end
            end
            if (code_R2TmpB)
            begin
                tmpb<=ReadFromRegister(code_M[3],code_M[2:0]);
            end
            if (code_R2TmpA)
            begin
                tmpa<=ReadFromRegister(code_M[3],code_M[2:0]);
            end
            if (code_SR2M)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    OPRw<=ReadFromSRRegister(modrm[4:3]);
                end
                else
                begin
                    WriteToRegister(code_M[3],code_M[2:0],ReadFromSRRegister(modrm[4:3]));
                end
            end
            if (code_M2SR)
            begin
                if (modrm[7:6]!=2'b11)
                begin
                    WriteToSRRegister(modrm[4:3],OPRr);
                end
                else
                begin
                    WriteToSRRegister(modrm[4:3],ReadFromRegister(code_M[3],code_M[2:0]));
                end
            end
            if (code_FLAGS[FLAG_O_IDX]==1) FLAGS[FLAG_O_IDX]<=(selectShifter?sho:fo);
            if (code_FLAGS[FLAG_S_IDX]==1) FLAGS[FLAG_S_IDX]<=(selectShifter?shs:fs);
            if (code_FLAGS[FLAG_Z_IDX]==1) FLAGS[FLAG_Z_IDX]<=(selectShifter?shz:fz);
            if (code_FLAGS[FLAG_A_IDX]==1) FLAGS[FLAG_A_IDX]<=(selectShifter?sha:fa);
            if (code_FLAGS[FLAG_P_IDX]==1) FLAGS[FLAG_P_IDX]<=(selectShifter?shp:fp);
            if (code_FLAGS[FLAG_C_IDX]==1) FLAGS[FLAG_C_IDX]<=(selectShifter?shc:fc);
        end


    end

end

endmodule