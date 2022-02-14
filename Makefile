
MODULE=top

.PHONY:sim
sim: trace.vcd

.PHONY:verilate
verilate: .stamp.verilate

.PHONY:build
build: obj_dir/Valu

.PHONY:waves
waves: trace.vcd
	@echo
	@echo "### WAVES ###"
	gtkwave trace.vcd

trace.vcd: ./obj_dir/V$(MODULE)
	@echo
	@echo "### SIMULATING ###"
	@./obj_dir/V$(MODULE)

./obj_dir/V$(MODULE): .stamp.verilate
	@echo
	@echo "### BUILDING SIM ###"
	make -C obj_dir -f V$(MODULE).mk V$(MODULE)
#	make CXXFLAGS='-g' -C obj_dir -f V$(MODULE).mk V$(MODULE)

.stamp.verilate: $(MODULE).v tb_$(MODULE).cpp
	@echo
	@echo "### VERILATING ###"
	verilator --Wno-UNOPTFLAT --Wno-WIDTH --Wno-IMPLICIT --trace -cc $(MODULE).v --exe tb_$(MODULE).cpp
#	verilator -Wall --trace -cc $(MODULE).v --exe tb_$(MODULE).cpp
	@touch .stamp.verilate

.PHONY:lint
lint: $(MODULE).v
	verilator --lint-only $(MODULE).v

.PHONY: clean
clean:
	rm -rf .stamp.*;
	rm -rf ./obj_dir
	rm -rf trace.vcd
