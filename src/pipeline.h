#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

enum class Op { NOP, ADD, SUB, LW, SW, BEQ };

struct Instr {
    Op op = Op::NOP;
    int rd = 0, rs = 0, rt = 0;
    int imm = 0;     // lw/sw/beq
    int pc = 0;      // instruction index (NOT bytes)
    std::string raw = "nop";

    bool isNop() const { return op == Op::NOP; }
    bool writesReg() const;
    int  destReg() const;
    bool usesRS() const;
    bool usesRT() const;
};

struct SimConfig {
    bool print_trace = true;
    bool init_demo_memory = true;
};

struct SimResult {
    long long cycles = 0;
    long long retired = 0;
    double cpi = 0.0;
};

class PipelineSim {
public:
    PipelineSim(std::vector<Instr> program, SimConfig cfg = {});
    SimResult run();

    const std::array<int,32>& regs() const { return R; }
    const std::unordered_map<int,int>& mem() const { return memory; }

private:
    // pipeline registers
    struct IFID { bool valid=false; Instr instr; };
    struct IDEX { bool valid=false; Instr instr; int rsVal=0; int rtVal=0; };
    struct EXMEM {
        bool valid=false; Instr instr;
        int aluOut=0;
        int rtValFwd=0;
        bool zero=false;
        int branchTarget=0;
    };
    struct MEMWB { bool valid=false; Instr instr; int memData=0; int aluOut=0; };

    std::vector<Instr> prog;
    SimConfig cfg;

    std::unordered_map<int,int> memory;
    std::array<int,32> R{};

    int PC = 0; // instruction index

    IFID ifid{};
    IDEX idex{};
    EXMEM exmem{};
    MEMWB memwb{};

private:
    // helpers
    static int  sign_extend16(int x);
    static std::string instr_pretty(const Instr& i);
    static std::string stage_str(const std::string& label, bool valid, const Instr& instr);

    bool pipeline_empty() const;
};
