#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <utility>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace {
// stur/ldur on x29 allow a 9-bit signed immediate (-256..255).
constexpr int kFrameOffsetImmMin = -256;
constexpr int kFrameOffsetImmMax = 255;
// Larger aligned byte offsets use separate address computation (ldr / add with temp).
constexpr long kPtrLargeOffsetMax = 32760;
}  // namespace

using namespace std;

static const set<string> NONTERMINALS = {"start", "procedures", "procedure", "main", "params", "paramlist", "type", "dcl", "dcls", "statements",
                                         "statement", "test", "expr", "term", "factor", "arglist", "lvalue"};

static vector<string> split(const string& s) {
  vector<string> out;
  string cur;
  for (char c : s) {
    if (c == '#' || c == ':') {
      if (!cur.empty()) { out.push_back(cur); cur.clear(); }
      string s2(1, c); out.push_back(s2);
      if (c == '#') break;
    } else if (isspace((unsigned char)c)) {
      if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static void stripTypeSuffix(vector<string>& toks, string& outType) {
  outType.clear();
  for (size_t i = 0; i < toks.size(); ++i) {
    if (toks[i] == ":") {
      for (size_t j = i + 1; j < toks.size(); ++j) { if (!outType.empty()) outType += " "; outType += toks[j]; }
      toks.resize(i);
      return;
    }
  }
}

static string mangleUserProc(const string& userName) { return "P" + userName; }
// AArch64 ABI requires the stack pointer to stay 16-byte aligned at calls.
static int align16Up(int n) { return (n % 16 == 0) ? n : (n / 16 + 1) * 16; }

static const map<string, const char*> kTestBranch = {{"EQ", "eq"}, {"NE", "ne"}, {"LT", "lt"},
                                                     {"LE", "le"}, {"GE", "ge"}, {"GT", "gt"}};

struct Node {
  bool isTerminal;
  string kind, lexeme, type;
  vector<string> rhs;
  vector<unique_ptr<Node>> children;
  mutable optional<pair<bool, long>> memoConst; mutable optional<bool> memoPure; mutable int memoSu = -1; mutable optional<size_t> memoHash;
  static unique_ptr<Node> makeTerminal(string k, string l) { auto n = make_unique<Node>(); n->isTerminal = true; n->kind = std::move(k); n->lexeme = std::move(l); return n; }
  static unique_ptr<Node> makeRule(string lhs, vector<string> r, vector<unique_ptr<Node>> c) { auto n = make_unique<Node>(); n->isTerminal = false; n->kind = std::move(lhs); n->rhs = std::move(r); n->children = std::move(c); return n; }

  const Node* child(size_t i) const { return children.at(i).get(); }
  Node* child(size_t i) { return children.at(i).get(); }
  size_t numChildren() const { return children.size(); }
};

struct Parser {
  unique_ptr<Node> parseOne() {
    string line;
    while (getline(cin, line)) {
      auto toks = split(line); if (toks.empty()) continue;
      string nodeType; stripTypeSuffix(toks, nodeType);
      if (NONTERMINALS.count(toks[0])) {
        string lhs = toks[0]; vector<string> rhs(toks.begin() + 1, toks.end());
        if (rhs.size() == 1 && rhs[0] == ".EMPTY") rhs.clear();
        vector<unique_ptr<Node>> children;
        for (size_t k = 0; k < rhs.size(); ++k) children.push_back(parseOne());
        auto n = Node::makeRule(lhs, std::move(rhs), std::move(children)); n->type = nodeType; return n;
      } else {
        string lex; if (toks[0] == "NULL" && toks.size() >= 2 && toks[1] == "NULL") lex = "NULL"; else if (toks.size() >= 2) lex = toks[1];
        auto n = Node::makeTerminal(toks[0], lex); n->type = nodeType; return n;
      }
    }
    return nullptr;
  }
};

static int asmLineSizeBytes(const string& line) {
  if (line.size() < 3) return 0;
  if (line[0] == ' ' && line[1] == ' ' && line[2] != '.') return 4;
  if (line.rfind(".8byte", 0) == 0) return 8;
  return 0;
}

static string typeFromDcl(const Node* d) {
  if (!d || d->numChildren() < 1) return "long";
  const Node* t = d->child(0);
  return (t->rhs.size() == 2) ? "long*" : "long";
}

static bool isEmptySt(const Node* n) { return !n || n->rhs.empty() || (n->rhs.size() == 1 && n->rhs[0] == ".EMPTY"); }

// Structural codegen (high impact):
// A1. Literal pool: per-procedure buffer; emitLoadLitPayload records fixups; finalizeLiteralPool() appends
//     dedup .8byte slots, 8-byte aligns pool start, patches ldr xN, imm with byte delta (multiple of 4).
// A2. Single frame: one emitSubSpImm(totalFrameBytes); x29 = sp + belowFpBytes; params at [x29,+0..];
//     epilogue restores sp by adding back the params area (including alignment padding).
// A3. Constants loaded with emitLoadConst(targetReg, val) — no extra mov; 0 => emitZeroReg (sub xT,xT,xT).
// A4. Call sites: emitSaveTempsForCall(nf, targetReg) saves only r<nf excluding target; 8-byte slots;
//     emitRestoreTempsAfterCall restores non-x0 first, then result to target, then x0, then pop.

struct CodeGen {
  map<string, int> symTab;
  map<string, string> varType;
  map<string, int> regTab;
  int numLocalsInFrame = 0, numParamsInFrame = 0, labelCounter = 0, structureCounter = 0, totalFrameBytes = 0;
  int belowFpBytes = 0;
  bool literalPoolActive = false;
  vector<string> instBuffer;
  struct PayloadInfo { string payload; int id; };
  unordered_map<string, int> payloadToId;
  vector<string> idToPayload;

  void emit(const string& instr) { instBuffer.push_back(instr); }
  string freshLabel(const string& prefix) { return prefix + to_string(labelCounter++); }
  int nextStructureId() { return structureCounter++; }
  string labelWithId(const string& prefix, int id) { return prefix + to_string(id); }
  static const int SAVED_REG_BYTES = 16, WAIN_PARAMS = 2;
  struct Fixup { string tag; string payload; };
  vector<Fixup> fixups;
  int fixupCounter = 0;

  void beginLiteralPool() { literalPoolActive = true; payloadToId.clear(); idToPayload.clear(); instBuffer.clear(); fixups.clear(); fixupCounter = 0; }
  void endLiteralPool() { literalPoolActive = false; }
  string flushBuffer() {
    finalizeLiteralPool();
    ostringstream oss;
    for (const auto& s : instBuffer) oss << s << "\n";
    return oss.str();
  }
  void finalizeLiteralPool();

  void emitLoadLitPayload(int reg, const string& payload) {
    if (!literalPoolActive) { emit("  ldr x" + to_string(reg) + ", 8"); emit("  b 12"); emit("  .8byte " + payload); return; }
    if (!payloadToId.count(payload)) {
      payloadToId[payload] = (int)idToPayload.size();
      idToPayload.push_back(payload);
    }
    string tag = "PFIX" + to_string(fixupCounter++) + "!";
    emit("  ldr x" + to_string(reg) + ", " + tag);
    fixups.push_back({tag, payload});
  }
  void emitZeroReg(int reg) { emit("  sub x" + to_string(reg) + ", x" + to_string(reg) + ", x" + to_string(reg)); }
  // Avoid 'xzr' and 'x31' in add/br as some linkasm versions reject them.
  void emitMovReg(int dstReg, int srcReg) {
    if (dstReg == srcReg) return;
    emitZeroReg(dstReg);
    emit("  add x" + to_string(dstReg) + ", x" + to_string(dstReg) + ", x" + to_string(srcReg));
  }

  void emitLoadConst(int reg, long value) {
    if (value == 0) {
      emitZeroReg(reg);
      return;
    }
    emitLoadLitPayload(reg, to_string(value));
  }
  void emitLoadSymbolAddr(int reg, const string& symbol) { emitLoadLitPayload(reg, symbol); }
  void emitSubSpImm(long bytes) {
    emitLoadConst(9, bytes);
    emit("  sub sp, sp, x9");
  }
  void emitAddSpImm(long bytes) {
    emitLoadConst(9, bytes);
    emit("  add sp, sp, x9");
  }
  void emitCall(const string& symbol) { emitLoadSymbolAddr(8, symbol); emit("  blr x8"); }
  int assignRhsSaveOff() const {
    int nToSave = min((int)regTab.size(), 9);
    // Reserved space is align16Up(localsSize + 16 + 8 * nToSave + 32).
    // We can safely use the range [-(8*numLocals + 16 + 8*nToSave + 8), -(8*numLocals + 16 + 8*nToSave + 32)].
    return -(8 * numLocalsInFrame + 16 + 8 * nToSave + 8);
  }

  void emitGetcharStub() {
    string eofL = freshLabel("getcharEOF"); emitSubSpImm(16); emit("  stur x30, [sp, 0]");
    emitLoadConst(11, 0xc000000000010000ULL); emit("  ldur x0, [x11, 0]");
    emitLoadConst(1, -1); emit("  cmp x0, x1"); emit("  b.eq " + eofL);
    emit("  ldur x30, [sp, 0]"); emitAddSpImm(16); emit("  br x30");
    emit(eofL + ":"); emitLoadConst(0, -1); emit("  ldur x30, [sp, 0]"); emitAddSpImm(16); emit("  br x30");
  }
  void emitPutcharStub() {
    emitSubSpImm(16); emit("  stur x30, [sp, 0]"); emitLoadConst(11, 0xc000000000010008ULL);
    emit("  stur x0, [x11, 0]"); emit("  ldur x30, [sp, 0]"); emitAddSpImm(16); emit("  br x30");
  }
  void emitPrologue(int nParams, int numLocals, bool /*isWain*/) {
    resetProcedureCodegenState(); numLocalsInFrame = numLocals; numParamsInFrame = nParams;
    const int localsSize = 8 * numLocals, saveOff = -(localsSize + 8), linkOff = -(localsSize + 16), scratchBytes = 32;
    int nToSave = min((int)regTab.size(), 9);
    belowFpBytes = align16Up(localsSize + SAVED_REG_BYTES + 8 * nToSave + scratchBytes);
    totalFrameBytes = align16Up(8 * numParamsInFrame + belowFpBytes);
    emitZeroReg(10); emit("  add x10, x10, x29");
    if (totalFrameBytes > 0) emitSubSpImm(totalFrameBytes);
    // Standard layout: x29 points to start of params area (sp + belowFpBytes).
    if (belowFpBytes == 0) { emitZeroReg(29); emit("  add x29, x29, sp"); }
    else { emitLoadConst(9, belowFpBytes); emit("  add x29, sp, x9"); }
    if (numParamsInFrame > 0) {
      static const char* argRegs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
      for (int i = 0; i < min(numParamsInFrame, 8); ++i) emit("  stur " + string(argRegs[i]) + ", [x29, " + to_string(8 * i) + "]");
      if (numParamsInFrame > 8) {
        int paramsAllocStackArea = align16Up(8 * numParamsInFrame);
        for (int i = 8; i < numParamsInFrame; ++i) {
          // Extra parameters from the caller are at [sp_entry + 8*(i-8)].
          // x29 = sp_entry - paramsAllocStackArea. So sp_entry = x29 + paramsAllocStackArea.
          emitLoadFromFrame(11, paramsAllocStackArea + 8 * (i - 8));
          emitStoreToFrame(11, 8 * i);
        }
      }
    }
    emitStoreToFrame(30, saveOff); emitStoreToFrame(10, linkOff);
    for (int i = 0; i < nToSave; ++i) emitStoreToFrame(19 + i, -(localsSize + 16 + 8 * (i + 1)));
  }
  void emitEpilogue() { emitEpilogueWithoutReturn(); emit("  br x30"); }
  void emitEpilogueWithoutReturn() {
    int nToSave = min((int)regTab.size(), 9);
    for (int i = 0; i < nToSave; ++i) emitLoadFromFrame(19 + i, -(8 * numLocalsInFrame + 16 + 8 * (i + 1)));
    emitLoadFromFrame(30, -(8 * numLocalsInFrame + 8)); emitLoadFromFrame(10, -(8 * numLocalsInFrame + 16));
    // Restore sp to caller: x29 = sp_after_sub + belowFpBytes, so sp_before = x29 + (totalFrameBytes - belowFpBytes).
    int paramsAllocated = totalFrameBytes - belowFpBytes;
    if (paramsAllocated > 0) { emitLoadConst(9, paramsAllocated); emit("  add sp, x29, x9"); }
    else { emitZeroReg(9); emit("  add sp, x29, x9"); }
    emitZeroReg(29); emit("  add x29, x29, x10");
  }
  void emitStoreToFrame(int reg, int offset) {
    if (offset >= kFrameOffsetImmMin && offset <= kFrameOffsetImmMax)
      emit("  stur x" + to_string(reg) + ", [x29, " + to_string(offset) + "]");
    else {
      emitLoadConst(9, offset);
      emit("  add x11, x29, x9");
      emit("  stur x" + to_string(reg) + ", [x11, 0]");
    }
  }
  void emitLoadFromFrame(int reg, int offset) {
    if (offset >= kFrameOffsetImmMin && offset <= kFrameOffsetImmMax)
      emit("  ldur x" + to_string(reg) + ", [x29, " + to_string(offset) + "]");
    else {
      emitLoadConst(9, offset);
      emit("  add x11, x29, x9");
      emit("  ldur x" + to_string(reg) + ", [x11, 0]");
    }
  }

  string typeOf(const Node* n) const { if (!n) return "long"; return n->type.empty() ? "long" : n->type; }
  void genExpr(const Node* n, int targetReg = 0, int nextFree = 1);
  void genTerm(const Node* n, int targetReg = 0, int nextFree = 1);
  void genFactor(const Node* n, int targetReg = 0, int nextFree = 1);
  void genLvalue(const Node* n, int targetReg = 0);
  void genLvalueAddress(const Node* n, int targetReg = 0);
  void genDcls(const Node* n);
  void genStatements(const Node* n);
  void genStatement(const Node* n);
  void genTest(const Node* n, const string& trueLabel);
  void genInvertedTest(const Node* n, const string& falseLabel);
  bool isTailCall(const Node* n, string& outId, vector<const Node*>& outArgs);
  bool isPtrOffset(const Node* n, const Node*& outPtr, long& outOff);
  void resetProcedureCodegenState() {}
  void emitSaveTempsForCall(int nextFree, int excludeReg, vector<int>& outSavedRegs, int& outSaveBytes);
  void emitRestoreTempsAfterCall(const vector<int>& savedRegs, int saveBytes, int targetReg);
};

static bool isPure(const Node* n) {
  if (!n || n->isTerminal) return true;
  if (n->memoPure.has_value()) return *n->memoPure;
  bool res = true;
  if (n->kind == "factor") {
    const string& k = n->rhs[0]; if (k == "GETCHAR" || k == "NEW" || (n->rhs.size() >= 2 && n->rhs[1] == "LPAREN")) res = false;
  }
  if (res) {
    for (const auto& c : n->children) if (!isPure(c.get())) { res = false; break; }
  }
  n->memoPure = res;
  return res;
}

static bool isConst(const Node* n, long& v) {
  if (!n) return false;
  if (n->memoConst.has_value()) { v = n->memoConst->second; return n->memoConst->first; }
  bool res = false; v = 0;
  if (n->kind == "factor") {
    if (n->rhs[0] == "NUM") { v = stol(n->child(0)->lexeme); res = true; }
    else if (n->rhs[0] == "NULL") { v = 1; res = true; }
    else if (n->rhs[0] == "LPAREN") res = isConst(n->child(1), v);
  } else if (n->kind == "term" || n->kind == "expr") {
    if (n->rhs.size() == 1) res = isConst(n->child(0), v);
    else if (n->rhs.size() == 3) {
      const string& op = n->rhs[1];
      long v1, v2;
      if (op == "PLUS") {
        if (isConst(n->child(0), v1) && v1 == 0 && isConst(n->child(2), v2)) { v = v2; res = true; }
        else if (isConst(n->child(2), v2) && v2 == 0 && isConst(n->child(0), v1)) { v = v1; res = true; }
        else if (isConst(n->child(0), v1) && isConst(n->child(2), v2)) { v = v1 + v2; res = true; }
      } else if (op == "MINUS") {
        if (isConst(n->child(0), v1) && isConst(n->child(2), v2)) { v = v1 - v2; res = true; }
      } else if (op == "STAR") {
        if (isConst(n->child(2), v2) && v2 == 1 && isConst(n->child(0), v1)) { v = v1; res = true; }
        else if (isConst(n->child(0), v1) && v1 == 1 && isConst(n->child(2), v2)) { v = v2; res = true; }
        else if (isConst(n->child(0), v1) && v1 == 0 && isPure(n->child(2))) { v = 0; res = true; }
        else if (isConst(n->child(2), v2) && v2 == 0 && isPure(n->child(0))) { v = 0; res = true; }
        else if (isConst(n->child(0), v1) && isConst(n->child(2), v2)) { v = v1 * v2; res = true; }
      } else if (op == "SLASH") {
        if (isConst(n->child(0), v1) && isConst(n->child(2), v2) && v2 != 0) { v = v1 / v2; res = true; }
      } else if (op == "PCT") {
        if (isConst(n->child(0), v1) && isConst(n->child(2), v2) && v2 != 0) { v = v1 % v2; res = true; }
      }
    }
  }
  n->memoConst = {res, v};
  return res;
}

void CodeGen::finalizeLiteralPool() {
  if (idToPayload.empty()) return;
  vector<string>& lines = instBuffer;
  vector<int> addr(lines.size(), 0); int cur = 0; for (int i = 0; i < (int)lines.size(); ++i) { addr[i] = cur; cur += asmLineSizeBytes(lines[i]); }
  // NOP to pad the instruction stream to 8-byte alignment before .8byte pool entries.
  if ((cur % 8) != 0) {
    // Single 4-byte instruction to preserve size accounting.
    lines.push_back("  sub x9, x9, x9");
    cur += 4;
  }
  vector<int> pla(idToPayload.size()); for (size_t i = 0; i < idToPayload.size(); ++i) { pla[i] = cur; lines.push_back(".8byte " + idToPayload[i]); cur += 8; }
  unordered_map<string, string> tagToPayload; for (const auto& f : fixups) tagToPayload[f.tag] = f.payload;
  for (int i = 0; i < (int)lines.size(); ++i) {
    string& l = lines[i]; size_t pos = l.find("PFIX");
    if (pos != string::npos) {
      size_t end = l.find("!", pos);
      if (end != string::npos) {
        string tag = l.substr(pos, end - pos + 1);
        if (tagToPayload.count(tag)) {
          int delta = pla[payloadToId[tagToPayload[tag]]] - addr[i];
          l.replace(pos, tag.length(), to_string(delta));
        }
      }
    }
  }
}

void CodeGen::emitSaveTempsForCall(int nf, int ex, vector<int>& sv, int& sb) {
  sv.clear(); for (int r = 0; r < min(nf, 8); ++r) if (r != ex) sv.push_back(r);
  if (sv.empty()) { sb = 0; return; } sb = align16Up(8 * sv.size()); emitSubSpImm(sb);
  for (size_t i = 0; i < sv.size(); ++i) emit("  stur x" + to_string(sv[i]) + ", [sp, " + to_string(8 * i) + "]");
}

void CodeGen::emitRestoreTempsAfterCall(const vector<int>& sv, int sb, int t) {
  if (sv.empty()) {
    if (t != 0) emitMovReg(t, 0);
    return;
  }
  bool savedX0 = false;
  for (size_t i = 0; i < sv.size(); ++i) {
    if (sv[i] == 0) savedX0 = true;
    else emit("  ldur x" + to_string(sv[i]) + ", [sp, " + to_string(8 * i) + "]");
  }
  if (t != 0) emitMovReg(t, 0);
  if (savedX0) {
    for (size_t i = 0; i < sv.size(); ++i) {
      if (sv[i] != 0) continue;
      emit("  ldur x0, [sp, " + to_string(8 * i) + "]");
      break;
    }
  }
  if (sb > 0) emitAddSpImm(sb);
}

void CodeGen::genFactor(const Node* n, int t, int nf) {
  const vector<string>& r = n->rhs;
  if (r[0] == "NUM") {
    long v = stol(n->child(0)->lexeme);
    if (v == 0) emitZeroReg(t);
    else emitLoadConst(t, v);
  } else if (r[0] == "NULL") {
    emitLoadConst(t, 1);
  } else if (r[0] == "ID" && r.size() == 1) {
    string id = n->child(0)->lexeme;
    if (regTab.count(id)) emitMovReg(t, regTab[id]);
    else emitLoadFromFrame(t, symTab.at(id));
  } else if (r[0] == "LPAREN") genExpr(n->child(1), t, nf);
  else if (r[0] == "GETCHAR") { vector<int> sv; int sb; emitSaveTempsForCall(nf, t, sv, sb); emitCall("getchar"); emitRestoreTempsAfterCall(sv, sb, t); }
  else if (r[0] == "AMP") genLvalueAddress(n->child(1), t);
  else if (r[0] == "STAR") {
    const Node* b;
    long o;
    if (isPtrOffset(n->child(1), b, o)) {
      string ldOp = (o >= kFrameOffsetImmMin && o <= kFrameOffsetImmMax) ? "ldur" : "ldr";
      int baseReg = (nf < 8 && nf != t) ? nf : t;
      genExpr(b, baseReg, baseReg + 1);
      emit("  " + ldOp + " x" + to_string(t) + ", [x" + to_string(baseReg) + ", " + to_string(o) + "]");
    } else {
      genFactor(n->child(1), t, nf);
      emit("  ldur x" + to_string(t) + ", [x" + to_string(t) + ", 0]");
    }
  }
  else if (r[0] == "ID" && r[1] == "LPAREN") {
    string id = n->child(0)->lexeme; vector<const Node*> args; if (r.size() == 4) { const Node* curr = n->child(2); while (1) { args.push_back(curr->child(0)); if (curr->numChildren() == 3) curr = curr->child(2); else break; } }
    vector<int> sv; int sb; emitSaveTempsForCall(nf, t, sv, sb);
    int extraArgs = max(0, (int)args.size() - 8);
    int stackArgsBytes = align16Up(8 * extraArgs);
    if (stackArgsBytes > 0) {
      emitSubSpImm(stackArgsBytes);
      for (int i = 8; i < (int)args.size(); ++i) {
        genExpr(args[i], 0, 1);
        emit("  stur x0, [sp, " + to_string(8 * (i - 8)) + "]");
      }
    }
    for (int i = 0; i < (int)args.size() && i < 8; ++i) genExpr(args[i], i, i + 1);
    emitCall(mangleUserProc(id));
    if (stackArgsBytes > 0) emitAddSpImm(stackArgsBytes);
    emitRestoreTempsAfterCall(sv, sb, t);
  } else if (r[0] == "NEW") {
    vector<int> sv;
    int sb;
    emitSaveTempsForCall(nf, t, sv, sb);
    genExpr(n->child(3), 0, 1);
    emitCall("new");
    string k = freshLabel("nok");
    emitZeroReg(9);
    emit("  cmp x0, x9");
    emit("  b.ne " + k);
    emitLoadConst(0, 1);
    emit(k + ":");
    emitRestoreTempsAfterCall(sv, sb, t);
  }
}

void CodeGen::genTerm(const Node* n, int t, int nf) {
  long v;
  if (isConst(n, v)) {
    if (v == 0) emitZeroReg(t);
    else emitLoadConst(t, v);
    return;
  }
  const vector<string>& r = n->rhs;
  if (r.size() == 1) {
    genFactor(n->child(0), t, nf);
    return;
  }
  const string& op = r[1];
  if (op == "STAR") {
    if (isConst(n->child(2), v) && v == 1) {
      genTerm(n->child(0), t, nf);
      return;
    }
    if (isConst(n->child(0), v) && v == 1) {
      genFactor(n->child(2), t, nf);
      return;
    }
    if (isConst(n->child(2), v) && v == 0 && isPure(n->child(0))) {
      emitZeroReg(t);
      return;
    }
    if (isConst(n->child(0), v) && v == 0 && isPure(n->child(2))) {
      emitZeroReg(t);
      return;
    }
    if (isConst(n->child(2), v) && v == 2) {
      genTerm(n->child(0), t, nf);
      emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t));
      return;
    }
  }
  int l = t, r2 = nf;
  bool sp = (r2 > 7);
  int aR = sp ? 12 : r2, pNF = sp ? 8 : nf + 1;
  if (sp) {
    genTerm(n->child(0), l, pNF);
    emitSubSpImm(16);
    emit("  stur x" + to_string(l) + ", [sp, 0]");
    genFactor(n->child(2), l, nf);
    emitMovReg(aR, l);
    emit("  ldur x" + to_string(l) + ", [sp, 0]");
    emitAddSpImm(16);
  }
  else { genTerm(n->child(0), l, pNF); genFactor(n->child(2), aR, pNF); }
  if (op == "STAR") emit("  mul x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
  else if (op == "SLASH" || op == "PCT") {
    string ok = freshLabel("divok");
    emitZeroReg(9);
    emit("  cmp x" + to_string(aR) + ", x9");
    emit("  b.ne " + ok);
    emit("  br x9"); // Division by zero trap
    emit(ok + ":");
    if (op == "SLASH") emit("  sdiv x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
    else { emit("  sdiv x9, x" + to_string(l) + ", x" + to_string(aR)); emit("  mul x10, x9, x" + to_string(aR)); emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x10"); }
  }
}

void CodeGen::genExpr(const Node* n, int t, int nf) {
  long v;
  if (isConst(n, v)) {
    if (v == 0) emitZeroReg(t);
    else emitLoadConst(t, v);
    return;
  }
  const vector<string>& r = n->rhs;
  if (r.size() == 1) {
    genTerm(n->child(0), t, nf);
    return;
  }
  const string& op = r[1];
  if (isConst(n->child(2), v) && v == 0) {
    genExpr(n->child(0), t, nf);
    return;
  }
  if (op == "PLUS" && isConst(n->child(0), v) && v == 0) {
    genTerm(n->child(2), t, nf);
    return;
  }
  if (op == "MINUS" && n->child(0)->rhs.size() == 1 && n->child(2)->rhs.size() == 1) {
    const Node *t0 = n->child(0)->child(0), *t2 = n->child(2)->child(0);
    if (t0->rhs.size() == 1 && t0->rhs[0] == "ID" && t2->rhs.size() == 1 && t2->rhs[0] == "ID" &&
        t0->child(0)->lexeme == t2->child(0)->lexeme) {
      emitZeroReg(t);
      return;
    }
  }
  string tL = typeOf(n->child(0)), tR = typeOf(n->child(2));
  if (op == "PLUS") {
    if (tL == "long*" && isConst(n->child(2), v)) {
      genExpr(n->child(0), t, nf);
      if (v == 0) return;
      if (v == 1) {
        emitLoadConst(9, 8);
        emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9");
      } else {
        emitLoadConst(9, v * 8);
        emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9");
      }
      return;
    }
    if (tR == "long*" && isConst(n->child(0), v)) {
      genExpr(n->child(2), t, nf);
      if (v == 0) return;
      if (v == 1) {
        emitLoadConst(9, 8);
        emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9");
      } else {
        emitLoadConst(9, v * 8);
        emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9");
      }
      return;
    }
  } else if (op == "MINUS") {
    if (tL == "long*" && isConst(n->child(2), v)) {
      genExpr(n->child(0), t, nf);
      if (v == 0) return;
      if (v == 1) {
        emitLoadConst(9, 8);
        emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x9");
      } else {
        emitLoadConst(9, v * 8);
        emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x9");
      }
      return;
    }
  }
  int l = t, r2 = nf;
  bool sp = (r2 > 7);
  int aR = sp ? 12 : r2, pNF = sp ? 8 : nf + 1;
  if (sp) {
    genExpr(n->child(0), l, pNF);
    emitSubSpImm(16);
    emit("  stur x" + to_string(l) + ", [sp, 0]");
    genTerm(n->child(2), l, nf);
    emitMovReg(aR, l);
    emit("  ldur x" + to_string(l) + ", [sp, 0]");
    emitAddSpImm(16);
  } else {
    genExpr(n->child(0), l, pNF);
    genTerm(n->child(2), aR, pNF);
  }
  if (op == "PLUS") {
    if (tL == "long" && tR == "long")
      emit("  add x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
    else {
      int ptrReg = (tL == "long*" ? l : aR);
      int longReg = (tL == "long*" ? aR : l);
      emitLoadConst(11, 8);
      emit("  mul x9, x" + to_string(longReg) + ", x11");
      emit("  add x" + to_string(t) + ", x" + to_string(ptrReg) + ", x9");
    }
  } else {
    if (tL == "long" && tR == "long")
      emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
    else if (tL == "long*" && tR == "long") {
      emitLoadConst(11, 8);
      emit("  mul x9, x" + to_string(aR) + ", x11");
      emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x9");
    } else {
      emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
      emitLoadConst(9, 8);
      emit("  sdiv x" + to_string(t) + ", x" + to_string(t) + ", x9");
    }
  }
}

void CodeGen::genLvalueAddress(const Node* n, int t) {
  const vector<string>& r = n->rhs;
  if (r[0] == "ID") {
    emitLoadConst(9, symTab.at(n->child(0)->lexeme));
    emit("  add x" + to_string(t) + ", x29, x9");
  } else if (r[0] == "LPAREN") {
    genLvalueAddress(n->child(1), t);
  } else if (r[0] == "STAR") {
    const Node* b;
    long o;
    if (isPtrOffset(n->child(1), b, o)) {
      genExpr(b, t, t + 1);
      if (o != 0) {
        emitLoadConst(9, abs(o));
        emit("  " + string(o > 0 ? "add" : "sub") + " x" + to_string(t) + ", x" + to_string(t) + ", x9");
      }
    } else {
      genFactor(n->child(1), t, t + 1);
    }
  }
}

void CodeGen::genLvalue(const Node* n, int t) {
  const vector<string>& r = n->rhs;
  if (r[0] == "ID") {
    string id = n->child(0)->lexeme;
    if (regTab.count(id)) emitMovReg(regTab[id], t);
    else emitStoreToFrame(t, symTab.at(id));
  } else if (r[0] == "LPAREN") {
    genLvalue(n->child(1), t);
  } else {
    const Node* b;
    long o = 0;
    emitStoreToFrame(t, assignRhsSaveOff());
    int baseReg = 1;
    if (isPtrOffset(n->child(1), b, o)) {
      genExpr(b, 1, 2);
      baseReg = 1;
    } else {
      genFactor(n->child(1), 1, 2);
      baseReg = 1;
      o = 0;
    }
    emitLoadFromFrame(0, assignRhsSaveOff());
    emit("  " + string(o >= kFrameOffsetImmMin && o <= kFrameOffsetImmMax ? "stur" : "str") + " x0, [x" + to_string(baseReg) + ", " +
         to_string(o) + "]");
  }
}

void CodeGen::genDcls(const Node* n) {
  vector<const Node*> ds;
  while (n && !isEmptySt(n)) {
    ds.push_back(n);
    n = n->child(0);
  }
  for (int j = (int)ds.size() - 1; j >= 0; --j) {
    const Node* d = ds[j]->child(1);
    string id = d->child(1)->lexeme;
    const Node* i = ds[j]->child(3);
    if (i->kind == "NULL") {
      emitLoadConst(2, 1);
    } else {
      long v = stol(i->lexeme);
      if (v == 0) emitZeroReg(2);
      else emitLoadConst(2, v);
    }
    if (regTab.count(id)) emitMovReg(regTab[id], 2);
    else emitStoreToFrame(2, symTab.at(id));
  }
}
void CodeGen::genTest(const Node* n, const string& L) {
  genExpr(n->child(0), 0, 1);
  genExpr(n->child(2), 1, 2);
  emit("  cmp x0, x1");
  bool u = (typeOf(n->child(0)) == "long*" || typeOf(n->child(2)) == "long*");
  const string& op = n->child(1)->kind;
  if (u) {
    if (op == "EQ") emit("  b.eq " + L);
    else if (op == "NE") emit("  b.ne " + L);
    else if (op == "LT") emit("  b.lo " + L);
    else if (op == "LE") emit("  b.ls " + L);
    else if (op == "GE") emit("  b.hs " + L);
    else if (op == "GT") emit("  b.hi " + L);
  } else {
    auto it = kTestBranch.find(op);
    if (it != kTestBranch.end()) emit("  b." + string(it->second) + " " + L);
  }
}
void CodeGen::genInvertedTest(const Node* n, const string& L) {
  genExpr(n->child(0), 0, 1);
  genExpr(n->child(2), 1, 2);
  emit("  cmp x0, x1");
  bool u = (typeOf(n->child(0)) == "long*" || typeOf(n->child(2)) == "long*");
  const string& op = n->child(1)->kind;
  string inv;
  if (op == "EQ") inv = "ne";
  else if (op == "NE") inv = "eq";
  else if (op == "LT") inv = u ? "hs" : "ge";
  else if (op == "LE") inv = u ? "hi" : "gt";
  else if (op == "GE") inv = u ? "lo" : "lt";
  else if (op == "GT") inv = u ? "ls" : "le";
  else inv = "eq";
  emit("  b." + inv + " " + L);
}
void CodeGen::genStatement(const Node* n) {
  const vector<string>& r = n->rhs;
  if (r[0] == "lvalue") {
    genExpr(n->child(2));
    genLvalue(n->child(0));
  } else if (r[0] == "IF") {
    int sid = nextStructureId();
    string endL = labelWithId("endif", sid);
    if (isEmptySt(n->child(9))) {
      genInvertedTest(n->child(2), endL);
      genStatements(n->child(5));
      emit(endL + ":");
    } else {
      string eL = labelWithId("else", sid);
      genInvertedTest(n->child(2), eL);
      genStatements(n->child(5));
      emit("  b " + endL);
      emit(eL + ":");
      genStatements(n->child(9));
      emit(endL + ":");
    }
  } else if (r[0] == "WHILE") {
    int sid = nextStructureId();
    string bL = labelWithId("wb", sid), cL = labelWithId("wc", sid), eL = labelWithId("we", sid);
    emit("  b " + cL);
    emit(bL + ":");
    genStatements(n->child(5));
    emit(cL + ":");
    genTest(n->child(2), bL);
    emit(eL + ":");
  } else if (r[0] == "PRINTLN") {
    genExpr(n->child(2));
    emitCall("print");
  } else if (r[0] == "PUTCHAR") {
    genExpr(n->child(2));
    emitCall("putchar");
  } else if (r[0] == "DELETE") {
    genExpr(n->child(3));
    string sk = freshLabel("dn");
    emitLoadConst(9, 1);
    emit("  cmp x0, x9");
    emit("  b.eq " + sk);
    emitCall("delete");
    emit(sk + ":");
  }
}
void CodeGen::genStatements(const Node* n) {
  vector<const Node*> st;
  while (n && n->kind == "statements" && n->numChildren() == 2) {
    st.push_back(n->child(1));
    n = n->child(0);
  }
  for (int i = (int)st.size() - 1; i >= 0; --i) genStatement(st[i]);
}
bool CodeGen::isPtrOffset(const Node* n, const Node*& b, long& o) {
  if (!n || n->kind != "expr") return false;
  const vector<string>& r = n->rhs;
  long v;
  if (r.size() != 3 || (r[1] != "PLUS" && r[1] != "MINUS")) return false;
  if (typeOf(n->child(0)) == "long*" && isConst(n->child(2), v)) {
    b = n->child(0);
    o = (r[1] == "PLUS" ? v : -v) * 8;
  } else if (r[1] == "PLUS" && typeOf(n->child(2)) == "long*" && isConst(n->child(0), v)) {
    b = n->child(2);
    o = v * 8;
  } else {
    return false;
  }
  return (o >= kFrameOffsetImmMin && o <= kFrameOffsetImmMax) ||
         (o >= 0 && o <= kPtrLargeOffsetMax && o % 8 == 0);
}

bool CodeGen::isTailCall(const Node* n, string& id, vector<const Node*>& args) {
  if (!n || n->kind != "expr" || n->rhs[0] != "term") return false;
  if (n->child(0)->rhs[0] != "factor") return false;
  const Node* f = n->child(0)->child(0);
  if (f->rhs.size() < 3 || f->rhs[0] != "ID" || f->rhs[1] != "LPAREN") return false;
  id = f->child(0)->lexeme;
  args.clear();
  if (f->rhs.size() == 4) {
    const Node* curr = f->child(2);
    while (1) {
      args.push_back(curr->child(0));
      if (curr->numChildren() == 3) curr = curr->child(2);
      else break;
    }
  }
  return true;
}

// Walks the procedures subtree; appends each procedure/main node to out in emission order.
static void collectProcedures(const Node* ps, vector<const Node*>& out) {
  vector<const Node*> nodes;
  while (ps && ps->kind == "procedures") {
    if (ps->numChildren() == 1) {
      nodes.push_back(ps->child(0));
      break;
    }
    nodes.push_back(ps->child(0));
    ps = ps->child(1);
  }
  for (int i = (int)nodes.size() - 1; i >= 0; --i) out.push_back(nodes[i]);
}

// Sets p/g/t/h when println, getchar, putchar, or heap (new/delete) are needed for imports.
static void scanFeatureUses(const Node* root, bool& p, bool& g, bool& t, bool& h) {
  vector<const Node*> st = {root};
  while (!st.empty()) {
    const Node* n = st.back(); st.pop_back();
    if (!n) continue;
    if (n->kind == "PRINTLN") p = true;
    else if (n->kind == "GETCHAR") g = true;
    else if (n->kind == "PUTCHAR") t = true;
    else if (n->kind == "factor" && !n->rhs.empty() && n->rhs[0] == "NEW") h = true;
    else if (n->kind == "statement" && !n->rhs.empty() && n->rhs[0] == "DELETE") h = true;
    for (int i = (int)n->numChildren() - 1; i >= 0; --i) st.push_back(n->child(i));
  }
}
// Clears and fills cg.symTab, varType, regTab for one procedure (wain if w, else user procedure).
static void buildSymbolTable(CodeGen& cg, const Node* p, bool w) {
  cg.symTab.clear();
  cg.varType.clear();
  cg.regTab.clear();
  unordered_map<string, int> use;
  set<string> addr;
  vector<const Node*> st = {p};
  while (!st.empty()) {
    const Node* n = st.back();
    st.pop_back();
    if (!n) continue;
    if (n->kind == "ID") use[n->lexeme]++;
    const vector<string>& r = n->rhs;
    if (n->kind == "factor" && r.size() == 2 && r[0] == "AMP") {
      const Node* lv = n->child(1);
      while (lv && lv->numChildren() == 3) lv = lv->child(1);
      if (lv && lv->kind == "lvalue" && lv->rhs[0] == "ID") addr.insert(lv->child(0)->lexeme);
    }
    for (int i = (int)n->numChildren() - 1; i >= 0; --i) st.push_back(n->child(i));
  }
  vector<string> loc;
  if (w) {
    string id0 = p->child(3)->child(1)->lexeme, id1 = p->child(5)->child(1)->lexeme;
    cg.symTab[id0] = 0;
    cg.symTab[id1] = 8;
    cg.varType[id0] = typeFromDcl(p->child(3));
    cg.varType[id1] = typeFromDcl(p->child(5));
  } else {
    const Node* ps = p->child(3);
    if (ps->numChildren() > 0) {
      const Node* cur = ps->child(0);
      int i = 0;
      while (1) {
        string id = cur->child(0)->child(1)->lexeme;
        cg.symTab[id] = 8 * i;
        cg.varType[id] = typeFromDcl(cur->child(0));
        if (cur->numChildren() == 3) {
          cur = cur->child(2);
          i++;
        } else
          break;
      }
    }
  }
  const Node* dclsCur = p->child(w ? 8 : 6);
  vector<const Node*> dNodes;
  while (dclsCur && dclsCur->numChildren() >= 2) {
    dNodes.push_back(dclsCur->child(1));
    dclsCur = dclsCur->child(0);
  }
  for (int j = (int)dNodes.size() - 1; j >= 0; --j) {
    string id = dNodes[j]->child(1)->lexeme;
    loc.push_back(id);
    cg.symTab[id] = -8 * (int)loc.size();
    cg.varType[id] = typeFromDcl(dNodes[j]);
  }
  vector<string> cand;
  for (const auto& id : loc)
    if (!addr.count(id)) cand.push_back(id);
  stable_sort(cand.begin(), cand.end(), [&](const string& a, const string& b) {
    if (use[a] != use[b]) return use[a] > use[b];
    return a < b;
  });
  for (int i = 0; i < min((int)cand.size(), 9); ++i) cg.regTab[cand[i]] = 19 + i;
}

// Emits one procedure body (literal pool, prologue, statements, epilogue) to stdout.
static void emitProcedure(CodeGen& cg, const Node* p, const string& /*label*/, bool w, bool heapImports) {
  cg.beginLiteralPool();
  const Node *dcls = p->child(w ? 8 : 6), *stmts = p->child(w ? 9 : 7), *ret = p->child(w ? 11 : 9);
  int nL = 0;
  const Node* dclsTmp = dcls;
  while (dclsTmp && dclsTmp->numChildren() >= 2) {
    nL++;
    dclsTmp = dclsTmp->child(0);
  }
  int nP = w ? 2 : 0;
  if (!w && p->child(3)->numChildren() > 0) {
    const Node* curr = p->child(3)->child(0);
    nP = 1;
    while (curr->numChildren() == 3) {
      curr = curr->child(2);
      nP++;
    }
  }
  cg.emitPrologue(nP, nL, w);
  for (auto& kv : cg.regTab) cg.emitLoadFromFrame(kv.second, cg.symTab[kv.first]);
  if (w && heapImports) {
    // init expects (x0=arrayPtr, x1=arrayLen) when wain(long*, long); otherwise (0,0).
    if (cg.varType[p->child(3)->child(1)->lexeme] == "long*") {
      cg.emit("  ldur x0, [x29, 0]");
      cg.emit("  ldur x1, [x29, 8]");
    } else {
      cg.emitZeroReg(0);
      cg.emitZeroReg(1);
    }
    cg.emitCall("init");
  }
  cg.genDcls(dcls);
  cg.genStatements(stmts);
  cg.genExpr(ret);
  cg.emitEpilogue();
  cg.endLiteralPool();
  cout << cg.flushBuffer();
}

int main() {
  Parser par; unique_ptr<Node> root = par.parseOne();
  if (!root) {
    cerr << "ERROR: could not parse input" << endl;
    return 1;
  }
  const Node* ps = (root->kind == "start") ? root->child(1) : root.get();
  vector<const Node*> pl;
  collectProcedures(ps, pl);
  bool p = 0, g = 0, t = 0, h = 0;
  for (const Node* n : pl) scanFeatureUses(n, p, g, t, h);
  if (h) cout << ".import init\n.import new\n.import delete\n";
  if (p) cout << ".import print\n";
  CodeGen cg; for (const Node* n : pl) {
    if (n->kind == "main") {
      cout << mangleUserProc("wain") << ":\n";
      buildSymbolTable(cg, n, 1);
      emitProcedure(cg, n, mangleUserProc("wain"), 1, h);
    } else {
      string nm = mangleUserProc(n->child(1)->lexeme);
      cout << nm << ":\n";
      buildSymbolTable(cg, n, 0);
      emitProcedure(cg, n, nm, 0, 0);
    }
  }
  if (g) { cout << "getchar:\n"; cg.beginLiteralPool(); cg.emitGetcharStub(); cg.endLiteralPool(); cout << cg.flushBuffer(); }
  if (t) { cout << "putchar:\n"; cg.beginLiteralPool(); cg.emitPutcharStub(); cg.endLiteralPool(); cout << cg.flushBuffer(); }
  return 0;
}
