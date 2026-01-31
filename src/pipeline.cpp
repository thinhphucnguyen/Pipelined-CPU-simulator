#include "pipeline.h"
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std;

bool Instr::writesReg() const {
    return op == Op::ADD || op == Op::SUB || op == Op::LW;
}
int Instr::destReg() const {
    if (op == Op::ADD || op == Op::SUB) return rd;
    if (op == Op::LW) return rt;
    return 0;
}
bool Instr::usesRS() const {
    return op == Op::ADD || op == Op::SUB || op == Op::LW || op == Op::SW || op == Op::BEQ;
}
bool Instr::usesRT() const {
    return op == Op::ADD || op == Op::SUB || op == Op::SW || op == Op::BEQ;
}

PipelineSim::PipelineSim(vector<Instr> program, SimConfig cfg_)
: prog(std::move(program)), cfg(cfg_) {
    R.fill(0);

    if (cfg.init_demo_memory) {
        memory[0]  = 7;
        memory[4]  = 0;
        memory[8]  = 0;
        memory[12] = 0;
    }
}

int PipelineSim::sign_extend16(int x) {
    return (int16_t)(x & 0xFFFF);
}

string PipelineSim::instr_pretty(const Instr& i) {
    if (i.op == Op::NOP) return "nop";
    return i.raw;
}

string PipelineSim::stage_str(const string& label, bool valid, const Instr& instr) {
    ostringstream oss;
    oss << setw(3) << label << " : " << (valid ? instr_pretty(instr) : "—");
    return oss.str();
}

bool PipelineSim::pipeline_empty() const {
    bool no_fetch = (PC >= (int)prog.size());
    return no_fetch && !ifid.valid && !idex.valid && !exmem.valid && !memwb.valid;
}

SimResult PipelineSim::run() {
    SimResult res{};

    auto get_wb_value = [&](const MEMWB& wb)->int {
        if (!wb.valid) return 0;
        if (wb.instr.op == Op::LW) return wb.memData;
        return wb.aluOut;
    };

    while (!pipeline_empty()) {
        res.cycles++;

        // ---------------- WB ----------------
        if (memwb.valid && memwb.instr.writesReg()) {
            int dst = memwb.instr.destReg();
            if (dst != 0) {
                int val = (memwb.instr.op == Op::LW) ? memwb.memData : memwb.aluOut;
                R[dst] = val;
            }
            res.retired++;
        } else if (memwb.valid && memwb.instr.op != Op::NOP) {
            res.retired++;
        }
        R[0] = 0;

        // ---------------- MEM ----------------
        MEMWB next_memwb{};
        if (exmem.valid) {
            next_memwb.valid = true;
            next_memwb.instr = exmem.instr;
            next_memwb.aluOut = exmem.aluOut;

            if (exmem.instr.op == Op::LW) {
                int addr = exmem.aluOut;
                next_memwb.memData = memory.count(addr) ? memory[addr] : 0;
            } else if (exmem.instr.op == Op::SW) {
                int addr = exmem.aluOut;
                memory[addr] = exmem.rtValFwd;
            }
        }

        // ---------------- EX (+ forwarding) ----------------
        EXMEM next_exmem{};
        bool branch_taken = false;
        int branch_target = PC;

        if (idex.valid) {
            next_exmem.valid = true;
            next_exmem.instr = idex.instr;

            int A = idex.rsVal;
            int B = idex.rtVal;

            // Forward from EX/MEM (ALU ops only; LW data not ready here)
            if (exmem.valid && exmem.instr.writesReg() && exmem.instr.op != Op::LW) {
                int d = exmem.instr.destReg();
                if (d != 0) {
                    if (idex.instr.usesRS() && d == idex.instr.rs) A = exmem.aluOut;
                    if (idex.instr.usesRT() && d == idex.instr.rt) B = exmem.aluOut;
                }
            }
            // Forward from MEM/WB
            if (memwb.valid && memwb.instr.writesReg()) {
                int d = memwb.instr.destReg();
                if (d != 0) {
                    int wbVal = get_wb_value(memwb);
                    if (idex.instr.usesRS() && d == idex.instr.rs) A = wbVal;
                    if (idex.instr.usesRT() && d == idex.instr.rt) B = wbVal;
                }
            }

            int imm = sign_extend16(idex.instr.imm);

            switch (idex.instr.op) {
                case Op::ADD: next_exmem.aluOut = A + B; break;
                case Op::SUB: next_exmem.aluOut = A - B; break;
                case Op::LW:
                case Op::SW:
                    next_exmem.aluOut = A + imm;
                    next_exmem.rtValFwd = B;
                    break;
                case Op::BEQ:
                    next_exmem.zero = (A == B);
                    next_exmem.branchTarget = idex.instr.pc + 1 + imm;
                    if (next_exmem.zero) {
                        branch_taken = true;
                        branch_target = next_exmem.branchTarget;
                    }
                    break;
                case Op::NOP:
                    break;
            }
        }

        // ---------------- ID (+ load-use stall) ----------------
        IDEX next_idex{};
        bool stall = false;

        if (ifid.valid) {
            Instr ins = ifid.instr;

            // load-use hazard: if EX is lw and ID reads its dst -> stall
            if (idex.valid && idex.instr.op == Op::LW) {
                int loadDst = idex.instr.destReg();
                if (loadDst != 0) {
                    bool uses = (ins.usesRS() && ins.rs == loadDst) ||
                                (ins.usesRT() && ins.rt == loadDst);
                    if (uses) stall = true;
                }
            }

            if (!stall) {
                next_idex.valid = true;
                next_idex.instr = ins;
                next_idex.rsVal = R[ins.rs];
                next_idex.rtVal = R[ins.rt];
            } else {
                // bubble into EX
                next_idex.valid = true;
                next_idex.instr = Instr{};
                next_idex.instr.pc = -1;
                next_idex.instr.raw = "nop";
                next_idex.rsVal = 0;
                next_idex.rtVal = 0;
            }
        }

        // ---------------- IF ----------------
        IFID next_ifid = ifid; // default hold
        if (branch_taken) {
            PC = branch_target;
            next_ifid.valid = false; // flush wrong-path IF/ID
        } else if (stall) {
            // hold PC & IF/ID
        } else {
            if (PC < (int)prog.size()) {
                next_ifid.valid = true;
                next_ifid.instr = prog[PC];
            } else {
                next_ifid.valid = false;
            }
            PC++;
        }

        // ---------------- Trace ----------------
        if (cfg.print_trace) {
            cout << "Cycle " << res.cycles << ":\n";
            cout << "  " << stage_str("ID",  ifid.valid, ifid.instr)  << "\n";
            cout << "  " << stage_str("EX",  idex.valid, idex.instr)  << "\n";
            cout << "  " << stage_str("MEM", exmem.valid, exmem.instr)<< "\n";
            cout << "  " << stage_str("WB",  memwb.valid, memwb.instr)<< "\n";
            if (stall) cout << "  [stall] load-use hazard -> bubble inserted\n";
            if (branch_taken) cout << "  [branch taken] PC <- " << branch_target << " (flush IF/ID)\n";
            cout << "\n";
        }

        // ---------------- Latch ----------------
        memwb = next_memwb;
        exmem = next_exmem;
        idex  = next_idex;
        ifid  = next_ifid;

        R[0] = 0;
    }

    if (res.retired > 0) res.cpi = (double)res.cycles / (double)res.retired;
    return res;
}
