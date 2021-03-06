
module top
  (
    input               CLKx4,

    input               CLK,
    input               RESET,

    input               READY,
    input               INTR,
    input               NMI,
    input               HOLD,

    input               TEST_n,

    input   [7:0]       inAD,

    output  [7:0]       outAD,
    output  [7:0]       enAD,
    
    output [19:8]       A,

    output              ALE,
    output              INTA_n,
    output              RD_n,
    output              WR_n,
    output              IOM,
    output              DTR,
    output              DEN_n,
    output              HOLDA
  );

wire [7:0] prefetchTop;
wire [19:0] prefetchTopLinearAddress;
wire prefetchEmpty, prefetchFull, indirectBusOpInProgress,suspending,pendingIRQ;

wire readTop,flush,suspend,correct,indirect,irq,wait_n;
wire ind_ioMreq,ind_readWrite,ind_byteWord;
wire latchPC,latchCS,latchDS,latchSS,latchES;
wire [15:0] IND,OPRw,OPRr,IP,CS,DS,ES,SS,latchValue;
wire [2:0] indSeg;

bus_interface biu(.CLKx4(CLKx4),.CLK(CLK),.RESET(RESET),.READY(READY),.INTR(INTR),
    .NMI(NMI),.HOLD(HOLD),.inAD(inAD),.outAD(outAD),.enAD(enAD),.A(A),
    .ALE(ALE),.INTA_n(INTA_n),.RD_n(RD_n),.WR_n(WR_n),.IOM(IOM),.DTR(DTR),.DEN_n(DEN_n),.HOLDA(HOLDA),.TEST_n(TEST_n),
    .prefetchTop(prefetchTop),.prefetchTopLinearAddress(prefetchTopLinearAddress),
    .prefetchEmpty(prefetchEmpty),.prefetchFull(prefetchFull),.indirectBusOpInProgress(indirectBusOpInProgress),.suspending(suspending),.irqPending(pendingIRQ),
    .advanceTop(readTop),.flush(flush),.suspend(suspend),.correct(correct),.indirect(indirect),.irq(irq),.wait_n(wait_n),
    .ind_ioMreq(ind_ioMreq),.ind_readWrite(ind_readWrite),.ind_byteWord(ind_byteWord),
    .latchPC(latchPC),.latchCS(latchCS),.latchDS(latchDS),.latchSS(latchSS),.latchES(latchES),
    .IND(IND),.indirectSeg(indSeg),.OPRw(OPRw),.OPRr(OPRr),
    .REGISTER_IP(IP),.REGISTER_CS(CS),.REGISTER_DS(DS),.REGISTER_ES(ES),.REGISTER_SS(SS),.UpdateReg(latchValue));


execution eu(.CLKx4(CLKx4),.CLK(CLK),.RESET(RESET),.prefetchTop(prefetchTop),.prefetchTopLinearAddress(prefetchTopLinearAddress),
    .prefetchEmpty(prefetchEmpty),.prefetchFull(prefetchFull),.indirectBusOpInProgress(indirectBusOpInProgress),.suspending(suspending),.irqPending(pendingIRQ),
    .readTop(readTop),.flush(flush),.suspend(suspend),.correct(correct),.indirect(indirect),.irq(irq),.wait_n(wait_n),
    .ind_ioMreq(ind_ioMreq),.ind_readWrite(ind_readWrite),.ind_byteWord(ind_byteWord),
    .latchPC(latchPC),.latchCS(latchCS),.latchDS(latchDS),.latchSS(latchSS),.latchES(latchES),
    .IND(IND),.indirectSeg(indSeg),.OPRw(OPRw),.OPRr(OPRr),
    .REGISTER_IP(IP),.REGISTER_CS(CS),.REGISTER_DS(DS),.REGISTER_ES(ES),.REGISTER_SS(SS),.UpdateReg(latchValue));


endmodule
