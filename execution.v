
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

    input [15:0]        REGISTER_IP,
    input [15:0]        REGISTER_ES,
    input [15:0]        REGISTER_CS,
    input [15:0]        REGISTER_DS,
    input [15:0]        REGISTER_SS,

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
    output reg latchPC,
    output reg latchCS,
    output reg latchDS,
    output reg latchSS,
    output reg latchES,

    output reg ind_ioMreq,       // indirect io/data request  (io assumes 0000 as SEG)
    output reg ind_readWrite,    // indirect bus read/write request
    output reg ind_byteWord      // indirect bus byte/word request
);

reg [8:0] executionState /* verilator public */;
reg [7:0] instruction /* verilator public */;
reg [7:0] modrm /* verilator public */;

reg [2:0] clkEdgeSample;
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
wire [15:0] SIGMA;

// microcodeing
reg [3:0]   code_M;            // Word|RegIndex
reg [1:0]   cope_SR;
reg         code_TmpB2M;
reg         code_TmpB2R;
reg         code_Sigma2M;
reg         code_M2TmpB;
reg         code_R2TmpB;
reg         code_M2SR;
reg         code_SR2M;

reg [1:0] aluAselect;  // 00 tmpa 01 tmpb 10 tmpc 11 ...
reg [1:0] aluBselect;
reg aluWord;

wire [15:0] aluA,aluB;

wire fo,fs,fz,fa,fp,fc;

parameter FLAG_C_IDX = 0;
parameter FLAG_P_IDX = 2;
parameter FLAG_A_IDX = 4;
parameter FLAG_Z_IDX = 6;
parameter FLAG_S_IDX = 7;
parameter FLAG_I_IDX = 9;
parameter FLAG_O_IDX = 11;

parameter SEG_ZERO = 3'b100;
parameter SEG_CS = 3'b001;
parameter SEG_DS = 3'b011;
parameter SEG_ES = 3'b000;
parameter SEG_SS = 3'b010;


parameter ALU_OP_PASS = 4'b0000;
parameter ALU_OP_INC = 4'b0010;
parameter ALU_OP_DEC = 4'b0011;

parameter ALU_OP_ADD = 4'b1000;
parameter ALU_OP_OR  = 4'b1001;
parameter ALU_OP_ADC = 4'b1010;
parameter ALU_OP_SBB = 4'b1011;
parameter ALU_OP_AND = 4'b1100;
parameter ALU_OP_SUB = 4'b1101;
parameter ALU_OP_XOR = 4'b1110;
parameter ALU_OP_CMP = 4'b1111;

reg [8:0] PostEffectiveAddressReturn;   // EA calculation finsh jumps to here

// alu
alu myalu(.A(aluA),.B(aluB),.Operation(operation),.byteWord(aluWord),.carryIn(FLAGS[F_Carry]),.S(SIGMA),.F_Overflow(fo), .F_Neg(fs), .F_Zero(fz), .F_Aux(fa), .F_Parity(fp), .F_Carry(fc));

assign aluA = (tmpa & {16{aluAselect==2'b00}}) |
              (tmpb & {16{aluAselect==2'b01}}) |
              (tmpc & {16{aluAselect==2'b10}});

assign aluB = (tmpa & {16{aluBselect==2'b00}}) |
              (tmpb & {16{aluBselect==2'b01}}) |
              (tmpc & {16{aluBselect==2'b10}});

function automatic [8:0] FetchExecStateFromInstruction(input [7:0] inst);
begin
    if (inst[7:2] == 6'b100010)                              // MOV rmw<->r
    begin
        PostEffectiveAddressReturn = 9'h000;
        FetchExecStateFromInstruction = 9'h1f5;
    end
    else if (inst[7:4] == 4'b1011)                            // MOV rrr,i
        FetchExecStateFromInstruction = 9'h01C;
    else if (inst[7:1] == 7'b1100011)                         // MOV rm,i
    begin
        instruction[1]<=0;                               // acts as if direction is 0
        PostEffectiveAddressReturn = 9'h014;
        FetchExecStateFromInstruction = 9'h1f5;
    end
    else if ({inst[7:2],inst[0]} == 7'b1000110)          // MOV rmw<->sr
    begin
        instruction[0]<=1;                               // its a word operation
        PostEffectiveAddressReturn = 9'h0EC;
        FetchExecStateFromInstruction = 9'h1f5;
    end
    else if (inst[7:1] == 7'b1110011)                    // OUT ib, AL/AX
        FetchExecStateFromInstruction = 9'h0B0;
    else if (inst[7:4] == 4'b0100)                       // INC/DEC rp
        FetchExecStateFromInstruction = 9'h17C;
    else if ({inst[7:2],inst[0]} == 7'b1110101)          // JMP rel8/rel16
        FetchExecStateFromInstruction = 9'h0D0;
    else if (inst == 8'b11101010)                        // JMP offs segment
        FetchExecStateFromInstruction = 9'h0E0;
    else if (inst == 8'b11100010)                        // LOOP
        FetchExecStateFromInstruction = 9'h140;
    else if (inst == 8'b11111010)                        // CLI (not microcoded)
    begin
        FLAGS[FLAG_I_IDX]<=0;
        FetchExecStateFromInstruction = 9'h143;          // RNI
    end
    else if ({inst[7:5],inst[2:0]} == 6'b001110)         // SEGMENT PREFIX
        FetchExecStateFromInstruction = 9'h005;
    else
        FetchExecStateFromInstruction = 9'h1FD;
end
endfunction

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
            3'b000:     ReadFromRegister=AX[7:0];
            3'b001:     ReadFromRegister=CX[7:0];
            3'b010:     ReadFromRegister=DX[7:0];
            3'b011:     ReadFromRegister=BX[7:0];
            3'b100:     ReadFromRegister=AX[15:8];
            3'b101:     ReadFromRegister=CX[15:8];
            3'b110:     ReadFromRegister=DX[15:8];
            3'b111:     ReadFromRegister=BX[15:8];
        endcase
    end
end
endfunction

task automatic WriteToSRRegister(input[1:0] sr, input [15:0] in);
begin
    OPRw<=in;
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

always @(posedge(CLKx4))
begin
    
    clkEdgeSample = clkEdgeSample << 1;
    clkEdgeSample[0]=CLK;

    if (RESET == 1)
    begin
        
        executionState <= 9'h1E4;   // RESET
        instruction <= 8'h90;

    end
    else
    begin
        if (clkEdgeSample[2]==1'b1 && clkEdgeSample[1]==1'b0)   // negative slope
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

            code_TmpB2M=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_TmpB2R=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_Sigma2M=0;     // Can be merged into a single value ultimately (ie enable bits)
            code_M2TmpB=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_R2TmpB=0;      // Can be merged into a single value ultimately (ie enable bits)
            code_SR2M=0;        // 
            code_M2SR=0;        //


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
                            segPrefix<=instruction[4:3];
                            instruction<=prefetchTop;
                            readTop<=1;
                            executionState<=FetchExecStateFromInstruction(prefetchTop);
                        end
                    end

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

//023   CD FGHI  L N     TU       MP    -> tmpa      5   UNC   EAOFFSET                  [BP]
                9'h023:
                    begin
                        // MP -> tmpa    
                        tmpa<=BP;
                        executionState<=9'h1f7; //EAOFFSET
                    end

//037   CD FGH J L N     TU       HL    -> tmpa      5   UNC   EAOFFSET                  [BX]
                9'h037:
                    begin
                        // HL -> tmpa    
                        tmpa<=BX;
                        executionState<=9'h1f7; //EAOFFSET
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
                        aluAselect<=2'b01;     // ALUA = tmpB
                        aluWord=1'b1;
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
                        
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord=1'b1;
                        operation<=ALU_OP_ADD;     // A+B

                        executionState<=9'h0D5;
                    end
//0d5   C  F  I  L     R          SIGMA -> PC        4   FLUSH RNI 
                9'h0D5:
                    begin
                        // SIGMA -> PC   FLUSH RNI
                        OPRw<=SIGMA;
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
                        OPRw<=tmpb;
                        latchPC<=1;
                        executionState<=9'h0E6;
                    end
//0e6 A     G I  L     R          tmpa  -> RC        4   FLUSH RNI
                9'h0E6:
                    begin
                        // tmpa -> RC   FLUSH RNI
                        OPRw<=tmpa;
                        latchCS<=1;
                        flush<=1;   // FLUSH (and resumes prefetch queue)
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


//140  BCD FGH    MNO  RS         BC    -> tmpc      1   DEC   tmpc        011100010.00  LOOP
                9'h140:
                    begin
                        // BC->tmpc
                        tmpc<=CX;
                        aluAselect<=2'b10;     // ALUA = tmpc
                        aluWord=1'b1;
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

                        aluAselect<=2'b01;     // ALUA = tmpb
                        aluWord=1'b1;
                        operation<=ALU_OP_INC+instruction[3];  // Inc/Dec

                        executionState<=9'h17D;
                    end
//17d  B  EF  I KL  OPQR          SIGMA -> M         4   none  RNI      F                
                9'h17D:
                    begin
                        // SIGMA -> M     RNI
                        code_M={1'b1,instruction[2:0]};
                        code_Sigma2M=1;

                        // Flags update
                        FLAGS[FLAG_O_IDX]<=fo;
                        FLAGS[FLAG_S_IDX]<=fs;
                        FLAGS[FLAG_Z_IDX]<=fz;
                        FLAGS[FLAG_A_IDX]<=fa;
                        FLAGS[FLAG_P_IDX]<=fp;

                        executionState<=9'h1FD; // RNI
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
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord=1'b1;
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
                        aluAselect<=2'b00;     // ALUA = tmpa
                        aluBselect<=2'b01;     // ALUB = tmpb
                        aluWord=1'b1;
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
                            aluAselect<=2'b00;     // ALUA = tmpa
                            aluBselect<=2'b01;     // ALUB = tmpb
                            aluWord=1'b1;
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
                        OPRw<=0;
                        latchDS<=1;
                        suspend<=1;
                        executionState<=9'h1E5;
                    end
//1e5 A    F HI  L  OPQRSTU       ONES  -> RC                                            
                9'h1E5:
                    begin
                        // ONES -> RC 
                        OPRw<={16{1'b1}};
                        latchCS<=1;
                        executionState<=9'h1E6;
                    end
//1e6   C  F HIJ L     RSTU       ZERO  -> PC        4   FLUSH none                      
                9'h1E6:
                    begin
                        // ZERO -> PC    FLUSH
                        OPRw<=0;
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
                        OPRw<=0;
                        latchES<=1;
                        latchSS<=1; // RESET 1 early for testing purposes
                        executionState<=9'h1E9;
                    end
//1e9  B   F HIJ L  OPQR          ZERO  -> RS        4   none  RNI             
                9'h1E9:
                    begin
                        // ZERO -> RS   RNI
                        OPRw<=0;
                        //latchSS<=1;
                        executionState<=9'h1FD;     // RNI
                    end

//1f2 (NOT REAL mOP) EAFINISH
                9'h1F2:
                    begin
                        if (instruction[1]==0)
                        begin
                            executionState<=9'h1E3; // EADONE
                        end
                        else
                        begin
                            executionState<=9'h1e1; // EALOAD
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
                        if (instruction[1]==0)
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
//1fd (NOT REAL mOP)
                // RNI
                default:        // 3'h1FD - Waiting for instruction state
                    begin

                        if ((prefetchEmpty|indirectBusOpInProgress)==0)
                        begin
                            modrm<=8'hFF;
                            segPrefix<=SEG_DS;
                            instruction<=prefetchTop;
                            executionState<=FetchExecStateFromInstruction(prefetchTop);
                            readTop<=1;
                        end

                    end
            endcase


            // Handlers

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
            if (code_R2TmpB)
            begin
                tmpb<=ReadFromRegister(code_M[3],code_M[2:0]);
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




        end


    end

end

endmodule