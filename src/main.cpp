#include "pipeline.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cctype>

using namespace std;

static inline string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static inline string lower_str(string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static inline vector<string> split_tokens(const string& line) {
    vector<string> out;
    string cur;
    auto push_cur = [&]() { if (!cur.empty()) { out.push_back(cur); cur.clear(); } };
    for (char ch : line) {
        if (isspace((unsigned char)ch) || ch==',' || ch=='(' || ch==')') push_cur();
        else cur.push_back(ch);
    }
    push_cur();
    return out;
}
static inline int parse_reg(const string& tok) {
    string t = lower_str(tok);
    if (t.size() >= 2 && (t[0]=='r' || t[0]=='$')) {
        int v = stoi(t.substr(1));
        if (v < 0 || v > 31) throw runtime_error("Register out of range: " + tok);
        return v;
    }
    throw runtime_error("Bad register token: " + tok);
}

static Instr parse_line(const string& line, int pc) {
    string s = line;

    // strip comments
    size_t p = s.find('#');
    if (p != string::npos) s = s.substr(0, p);
    p = s.find("//");
    if (p != string::npos) s = s.substr(0, p);

    s = trim(lower_str(s));

    Instr ins;
    ins.pc = pc;
    ins.raw = s.empty() ? "nop" : s;

    if (s.empty()) { ins.op = Op::NOP; ins.raw="nop"; return ins; }

    auto toks = split_tokens(s);
    if (toks.empty()) { ins.op = Op::NOP; ins.raw="nop"; return ins; }

    string op = toks[0];

    if (op == "nop") { ins.op = Op::NOP; ins.raw="nop"; return ins; }

    if (op == "add" || op == "sub") {
        if (toks.size() != 4) throw runtime_error("Expected: add/sub rd, rs, rt | got: " + s);
        ins.op = (op=="add") ? Op::ADD : Op::SUB;
        ins.rd = parse_reg(toks[1]);
        ins.rs = parse_reg(toks[2]);
        ins.rt = parse_reg(toks[3]);
        return ins;
    }

    if (op == "lw" || op == "sw") {
        // lw rt, imm(rs) -> toks: op rt imm rs
        if (toks.size() != 4) throw runtime_error("Expected: lw/sw rt, imm(rs) | got: " + s);
        ins.op = (op=="lw") ? Op::LW : Op::SW;
        ins.rt = parse_reg(toks[1]);
        ins.imm = stoi(toks[2]);
        ins.rs = parse_reg(toks[3]);
        return ins;
    }

    if (op == "beq") {
        if (toks.size() != 4) throw runtime_error("Expected: beq rs, rt, imm | got: " + s);
        ins.op = Op::BEQ;
        ins.rs = parse_reg(toks[1]);
        ins.rt = parse_reg(toks[2]);
        ins.imm = stoi(toks[3]); // imm in instruction count
        return ins;
    }

    throw runtime_error("Unknown op: " + op);
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " program.asm\n";
        return 1;
    }

    vector<Instr> prog;
    {
        ifstream in(argv[1]);
        if (!in) {
            cerr << "Failed to open: " << argv[1] << "\n";
            return 1;
        }
        string line;
        int pc = 0;
        while (getline(in, line)) {
            // skip blank/comment-only lines without consuming PC
            string cleaned = line;
            size_t p = cleaned.find('#');
            if (p != string::npos) cleaned = cleaned.substr(0, p);
            p = cleaned.find("//");
            if (p != string::npos) cleaned = cleaned.substr(0, p);
            cleaned = trim(cleaned);
            if (cleaned.empty()) continue;

            prog.push_back(parse_line(line, pc));
            pc++;
        }
    }

    PipelineSim sim(prog, SimConfig{ .print_trace = true, .init_demo_memory = true });
    SimResult r = sim.run();

    cout << "Done.\n";
    cout << "Cycles: " << r.cycles << "\n";
    cout << "Retired (non-bubble): " << r.retired << "\n";
    if (r.retired > 0) cout << "CPI: " << fixed << setprecision(3) << r.cpi << "\n";

    cout << "\nFinal regs (r0..r7):\n";
    auto& R = sim.regs();
    for (int i = 0; i < 8; i++) cout << "r" << i << " = " << R[i] << "\n";

    cout << "\nFinal mem (addresses 0,4,8,12):\n";
    auto& M = sim.mem();
    for (int a : {0,4,8,12}) cout << "[" << a << "] = " << (M.count(a) ? M.at(a) : 0) << "\n";

    return 0;
}
