
// myOps
// 0000 PassA   S=(A)
// 0001 NotA    S=(~A)
// 0010 IncA    S=(A++)
// 0011 DecA    S=(A--)

// (ALU)
// 1000 Add     S=(A + B)
// 1001 Or      S=(A | B)
// 1010 Adc     S=(A + B + carryIn)
// 1011 Sbb     S=(A - B - carryIn)
// 1100 And     S=(A & B)
// 1101 Sub     S=(A - B)
// 1110 Xor     S=(A ^ B)
// 1111 Cmp     S=(A - B)
//

module alu
  (
      input [15:0] A,
      input [15:0] B,
      input [3:0] Operation,
      input byteWord,
      input carryIn,
      output [15:0] S,

      output F_Overflow,
      output F_Neg,
      output F_Zero,
      output F_Aux,
      output F_Parity,
      output F_Carry
  );

//Generate adder (allows us to extract multiple carries)

wire [16:0] carry;
wire [15:0] Ai,Bi,resultAdder,result;

wire [3:0] OperationL;
wire [15:0] OperationPassA;
wire [15:0] OperationNotA;
wire [15:0] OperationIncA;
wire [15:0] OperationDecA;
wire [15:0] OperationAdd;
wire [15:0] OperationOr;
wire [15:0] OperationAdc;
wire [15:0] OperationSbb;
wire [15:0] OperationAnd;
wire [15:0] OperationSub;
wire [15:0] OperationXor;
wire [15:0] OperationCmp;

wire op2Inv,opHasCarry;
wire clearOC;

assign OperationL = ~Operation;
assign OperationPassA   = {16{OperationL[3] & OperationL[2] & OperationL[1] & OperationL[0] }};
assign OperationNotA    = {16{OperationL[3] & OperationL[2] & OperationL[1] & Operation [0] }};
assign OperationIncA    = {16{OperationL[3] & OperationL[2] & Operation [1] & OperationL[0] }};
assign OperationDecA    = {16{OperationL[3] & OperationL[2] & Operation [1] & Operation [0] }};

assign OperationAdd     = {16{Operation [3] & OperationL[2] & OperationL[1] & OperationL[0] }};
assign OperationOr      = {16{Operation [3] & OperationL[2] & OperationL[1] & Operation [0] }};
assign OperationAdc     = {16{Operation [3] & OperationL[2] & Operation [1] & OperationL[0] }};
assign OperationSbb     = {16{Operation [3] & OperationL[2] & Operation [1] & Operation [0] }};
assign OperationAnd     = {16{Operation [3] & Operation [2] & OperationL[1] & OperationL[0] }};
assign OperationSub     = {16{Operation [3] & Operation [2] & OperationL[1] & Operation [0] }};
assign OperationXor     = {16{Operation [3] & Operation [2] & Operation [1] & OperationL[0] }};
assign OperationCmp     = {16{Operation [3] & Operation [2] & Operation [1] & Operation [0] }};

assign Ai = 
            ( ( A)  & OperationPassA )  |
            ( (~A)  & OperationNotA )  |
            ( ( A)  & OperationIncA )  |
            ( ( A)  & OperationDecA )  |
            ( ( A)  & (OperationAdd | OperationOr | OperationAdc | OperationSbb | OperationAnd | OperationSub | OperationXor | OperationCmp) );

assign Bi = 
            ( ( 0)  & OperationPassA )  |
            ( ( 0)  & OperationNotA )  |
            ( ( 1)  & OperationIncA )  |
            ( (~1)  & OperationDecA )  |
            ( ( B)  & (OperationAdd | OperationOr | OperationAdc | OperationAnd | OperationXor)) |
            ( (~B)  & (OperationSbb | OperationSub | OperationCmp) );

assign op2Inv = (OperationDecA[0] | OperationSbb[0] | OperationSub[0] | OperationCmp[0]);
assign opHasCarry = OperationAdc[0] | OperationSbb[0];
assign clearOC = OperationAnd[0] | OperationOr[0] | OperationXor[0];


assign carry[0] = opHasCarry ? (op2Inv ? ~carryIn : carryIn ) : (op2Inv ? 1 : 0);
genvar i;
generate
  for (i=0; i < 16; i=i+1) 
    begin : GEN_ADDER
      assign resultAdder[i] =  Ai[i] ^ Bi[i] ^ carry[i];     
      assign carry[i+1]   = (Ai[i] & Bi[i]) | (Ai[i] & carry[i]) | (Bi[i] & carry[i]);
    end
endgenerate

assign S = (resultAdder & {16{~clearOC}})                |
           ((Ai | Bi)   & {16{ clearOC}} & OperationOr)  |
           ((Ai & Bi)   & {16{ clearOC}} & OperationAnd) |
           ((Ai ^ Bi)   & {16{ clearOC}} & OperationXor);


// todo 8bit math flags
assign F_Overflow = clearOC ? 0 : byteWord ? carry[16] ^ carry[15] : carry[8] ^ carry[7];
assign F_Neg = byteWord ? S[15] : S[7];
assign F_Zero = byteWord ? (S[15:0] == 0) : (S[7:0] == 0);
assign F_Aux = carry[4] ^ op2Inv;
assign F_Parity = ~(S[0] ^ S[1] ^ S[2] ^ S[3] ^ S[4] ^ S[5] ^ S[6] ^ S[7]);
assign F_Carry = clearOC ? 0 : byteWord ? carry[16] ^ op2Inv : carry[8] ^ op2Inv;

endmodule
