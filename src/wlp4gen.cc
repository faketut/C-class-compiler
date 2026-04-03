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

using namespace std;

static const set<string> NONTERMINALS = {
    "start", "procedures", "procedure", "main", "params", "paramlist",
    "type", "dcl", "dcls", "statements", "statement", "test",
    "expr", "term", "factor", "arglist", "lvalue"
};

static vector<string> split(const string& s) {
  istringstream iss(s);
  vector<string> out;
  for (string t; iss >> t;) out.push_back(t);
  return out;
}

static void stripTypeSuffix(vector<string>& toks, string& outType) {
  outType.clear();
  for (size_t i = 0; i < toks.size(); ++i) {
    if (toks[i] == ":") {
      for (size_t j = i + 1; j < toks.size(); ++j) {
        if (!outType.empty()) outType += " ";
        outType += toks[j];
      }
      toks.resize(i);
      return;
    }
  }
}

static string mangleUserProc(const string& userName) { return "P" + userName; }
static int align16Up(int n) { return (n % 16 == 0) ? n : (n / 16 + 1) * 16; }

static const map<string, const char*> kTestBranch = {{"EQ", "eq"}, {"NE", "ne"}, {"LT", "lt"},
                                                     {"LE", "le"}, {"GE", "ge"}, {"GT", "gt"}};

struct Node {
  bool isTerminal;
  string kind, lexeme, type;
  vector<string> rhs;
  vector<unique_ptr<Node>> children;

  // Memoization fields
  mutable optional<pair<bool, long>> memoConst;
  mutable optional<bool> memoPure;
  mutable int memoSu = -1;
  mutable optional<size_t> memoHash;

  static unique_ptr<Node> makeTerminal(string k, string l) {
    auto n = make_unique<Node>(); n->isTerminal = true; n->kind = std::move(k); n->lexeme = std::move(l); return n;
  }
  static unique_ptr<Node> makeRule(string lhs, vector<string> r, vector<unique_ptr<Node>> c) {
    auto n = make_unique<Node>(); n->isTerminal = false; n->kind = std::move(lhs); n->rhs = std::move(r); n->children = std::move(c); return n;
  }

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
//     epilogue add sp, x29, paramsBytes (or add sp,x29,xzr).
// A3. Constants loaded with emitLoadConst(targetReg, val) — no extra mov; 0 => sub xT,xT,xT.
// A4. Call sites: emitSaveTempsForCall(nf, targetReg) saves only r<nf excluding target; 8-byte slots;
//     emitRestoreTempsAfterCall restores non-x0 first, then result to target, then x0, then pop.

struct CodeGen {
  map<string, int> symTab;
  map<string, string> varType;
  map<string, int> regTab;
  int numLocalsInFrame = 0, numParamsInFrame = 0, labelCounter = 0, structureCounter = 0, totalFrameBytes = 0;
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
  void runPeephole();
  string flushBuffer() {
    runPeephole();
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
  void emitLoadConst(int reg, long value) {
    if (value == 0) { emit("  sub x" + to_string(reg) + ", x" + to_string(reg) + ", x" + to_string(reg)); return; }
    if (value == 1 && pinned1Valid) { emit("  add x" + to_string(reg) + ", x" + to_string(kPin1) + ", xzr"); return; }
    if (value == 8 && pinned8Valid) { emit("  add x" + to_string(reg) + ", x" + to_string(kPin8) + ", xzr"); return; }
    if (value == 16 && pinned16Valid) { emit("  add x" + to_string(reg) + ", x" + to_string(kPin16) + ", xzr"); return; }
    if (value == 32 && pinned32Valid) { emit("  add x" + to_string(reg) + ", x" + to_string(kPin32) + ", xzr"); return; }
    emitLoadLitPayload(reg, to_string(value));
  }
  void emitLoadSymbolAddr(int reg, const string& symbol) { emitLoadLitPayload(reg, symbol); }
  void emitSubSpImm(long bytes) {
    if (bytes == 16 && pinned16Valid) emit("  sub sp, sp, x" + to_string(kPin16));
    else if (bytes == 32 && pinned32Valid) emit("  sub sp, sp, x" + to_string(kPin32));
    else { emitLoadConst(9, bytes); emit("  sub sp, sp, x9"); }
  }
  void emitAddSpImm(long bytes) {
    if (bytes == 16 && pinned16Valid) emit("  add sp, sp, x" + to_string(kPin16));
    else if (bytes == 32 && pinned32Valid) emit("  add sp, sp, x" + to_string(kPin32));
    else { emitLoadConst(9, bytes); emit("  add sp, sp, x9"); }
  }
  void emitCall(const string& symbol) { emitLoadSymbolAddr(8, symbol); emit("  blr x8"); invalidatePinnedConstsAfterCall(); stmtCseBaseReg.clear(); }
  int assignRhsSaveOff() const { return -(8 * numLocalsInFrame + 32); }

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
    int belowFpBytes = align16Up(localsSize + SAVED_REG_BYTES + 8 * nToSave + scratchBytes);
    totalFrameBytes = 8 * numParamsInFrame + belowFpBytes;
    emit("  add x10, x29, xzr"); if (totalFrameBytes > 0) emitSubSpImm(totalFrameBytes);
    if (belowFpBytes == 0) emit("  add x29, sp, xzr"); else { emitLoadConst(9, belowFpBytes); emit("  add x29, sp, x9"); }
    if (numParamsInFrame > 0) {
      static const char* argRegs[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
      for (int i = 0; i < min(numParamsInFrame, 8); ++i) emit("  stur " + string(argRegs[i]) + ", [x29, " + to_string(8 * i) + "]");
    }
    emitStoreToFrame(30, saveOff); emitStoreToFrame(10, linkOff);
    for (int i = 0; i < nToSave; ++i) emitStoreToFrame(19 + i, -(localsSize + 16 + 8 * (i + 1)));
  }
  void emitEpilogue() { emitEpilogueWithoutReturn(); emit("  br x30"); }
  void emitEpilogueWithoutReturn() {
    int nToSave = min((int)regTab.size(), 9);
    for (int i = 0; i < nToSave; ++i) emitLoadFromFrame(19 + i, -(8 * numLocalsInFrame + 16 + 8 * (i + 1)));
    emitLoadFromFrame(30, -(8 * numLocalsInFrame + 8)); emitLoadFromFrame(10, -(8 * numLocalsInFrame + 16));
    if (numParamsInFrame > 0) { emitLoadConst(9, 8 * numParamsInFrame); emit("  add sp, x29, x9"); } else emit("  add sp, x29, xzr");
    emit("  add x29, x10, xzr");
  }
  void emitStoreToFrame(int reg, int offset) {
    if (offset >= -256 && offset <= 255) emit("  stur x" + to_string(reg) + ", [x29, " + to_string(offset) + "]");
    else { emitLoadConst(9, offset); emit("  add x10, x29, x9"); emit("  stur x" + to_string(reg) + ", [x10, 0]"); }
  }
  void emitLoadFromFrame(int reg, int offset) {
    if (offset >= -256 && offset <= 255) emit("  ldur x" + to_string(reg) + ", [x29, " + to_string(offset) + "]");
    else { emitLoadConst(9, offset); emit("  add x10, x29, x9"); emit("  ldur x" + to_string(reg) + ", [x10, 0]"); }
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
  void resetProcedureCodegenState() { pinned1Valid = pinned8Valid = pinned16Valid = pinned32Valid = false; }
  void invalidatePinnedConstsAfterCall() { pinned1Valid = pinned8Valid = pinned16Valid = pinned32Valid = false; }
  void emitEnsureConst1() { if (!pinned1Valid) { emitLoadConst(kPin1, 1); pinned1Valid = true; } }
  void emitEnsureConst8() { if (!pinned8Valid) { emitLoadConst(kPin8, 8); pinned8Valid = true; } }
  static constexpr int kPin1 = 14, kPin8 = 15, kPin16 = 12, kPin32 = 13;
  bool pinned1Valid = false, pinned8Valid = false, pinned16Valid = false, pinned32Valid = false, isLeaf = false;
  map<pair<size_t, long>, int> stmtCseBaseReg;
  void clearStmtCse() { stmtCseBaseReg.clear(); }
  void invalidateCseReg(int r);
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

static int suNeedRegs(const Node* n) {
  if (!n || n->isTerminal) return 0;
  if (n->memoSu != -1) return n->memoSu;
  int res = 2;
  if (n->kind == "factor") {
  const vector<string>& r = n->rhs;
    if (r[0] == "NUM" || r[0] == "NULL" || (r[0] == "ID" && r.size() == 1)) res = 0;
    else if (r[0] == "LPAREN") res = suNeedRegs(n->child(1));
    else if (r[0] == "AMP") res = 1;
    else if (r[0] == "STAR") res = suNeedRegs(n->child(1)) + 1;
    else if (r[0] == "GETCHAR" || r[0] == "NEW" || (r[0] == "ID" && r.size() >= 2 && r[1] == "LPAREN")) res = 4;
  } else if (n->kind == "term" || n->kind == "expr") {
    if (n->rhs.size() == 1) res = suNeedRegs(n->child(0));
    else if (n->rhs.size() == 3) {
      int a = suNeedRegs(n->child(0)), b = suNeedRegs(n->child(2));
      res = (a == b) ? a + 1 : max(a, b);
    }
  }
  return n->memoSu = res;
}

static size_t hashExprTree(const Node* n) {
  if (!n) return 14695981039346656037ULL;
  if (n->memoHash.has_value()) return *n->memoHash;
  size_t h = hash<string>{}(n->kind) ^ (hash<string>{}(n->type) << 1);
  if (n->isTerminal) {
    h ^= hash<string>{}(n->lexeme) << 1;
  } else {
    for (const string& s : n->rhs) h ^= hash<string>{}(s) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    for (size_t i = 0; i < n->numChildren(); ++i) h ^= hashExprTree(n->child(i)) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  }
  n->memoHash = h;
  return h;
}

static bool isTestConst(const Node* n, bool& r) {
  if (!n || n->kind != "test") return false;
  long v1, v2; if (isConst(n->child(0), v1) && isConst(n->child(2), v2)) {
    const string& op = n->child(1)->kind;
    if (op == "EQ") r = (v1 == v2); else if (op == "NE") r = (v1 != v2); else if (op == "LT") r = (v1 < v2);
    else if (op == "LE") r = (v1 <= v2); else if (op == "GT") r = (v1 > v2); else if (op == "GE") r = (v1 >= v2);
    return true;
  }
  return false;
}

void CodeGen::runPeephole() {
  vector<string> n; n.reserve(instBuffer.size());
  for (size_t i = 0; i < instBuffer.size(); ++i) {
    const string& s = instBuffer[i];
    if (s.find("add x") != string::npos && s.find(", xzr") != string::npos) {
       size_t f = s.find("x"), c = s.find(",", f), s2 = s.find("x", c);
       if (f != string::npos && c != string::npos && s2 != string::npos && s.substr(f, c - f) == s.substr(s2)) continue;
    }
    if (i + 1 < instBuffer.size()) {
       const string& s1 = instBuffer[i], &s2 = instBuffer[i+1];
       if (s1.find("stur x") == 2 && s2.find("ldur x") == 2 && s1.size() > 6 && s2.size() > 6 && s1.substr(6) == s2.substr(6)) {
         n.push_back(s1); i++; continue;
       }
    }
    n.push_back(s);
  }
  instBuffer = std::move(n);
}

void CodeGen::finalizeLiteralPool() {
  if (idToPayload.empty()) return;
  vector<string>& lines = instBuffer;
  vector<int> addr(lines.size(), 0); int cur = 0; for (int i = 0; i < (int)lines.size(); ++i) { addr[i] = cur; cur += asmLineSizeBytes(lines[i]); }
  if ((cur % 8) != 0) { lines.push_back("  add x0, x0, xzr"); cur += 4; }
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
    if (t != 0) emit("  add x" + to_string(t) + ", x0, xzr");
    return;
  }
  bool savedX0 = false;
  for (size_t i = 0; i < sv.size(); ++i) {
    if (sv[i] == 0) savedX0 = true;
    else emit("  ldur x" + to_string(sv[i]) + ", [sp, " + to_string(8 * i) + "]");
  }
  if (t != 0) emit("  add x" + to_string(t) + ", x0, xzr");
  if (savedX0) {
    for (size_t i = 0; i < sv.size(); ++i) {
      if (sv[i] != 0) continue;
      emit("  ldur x0, [sp, " + to_string(8 * i) + "]");
      break;
    }
  }
  if (sb > 0) emitAddSpImm(sb);
}

void CodeGen::invalidateCseReg(int r) {
  vector<pair<size_t, long>> toErase;
  for (const auto& kv : stmtCseBaseReg)
    if (kv.second == r) toErase.push_back(kv.first);
  for (auto k : toErase) stmtCseBaseReg.erase(k);
}

void CodeGen::genFactor(const Node* n, int t, int nf) {
  invalidateCseReg(t);
  const vector<string>& r = n->rhs;
  if (r[0] == "NUM") { long v = stol(n->child(0)->lexeme); if (v == 0) emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); else emitLoadConst(t, v); }
  else if (r[0] == "NULL") { emitEnsureConst1(); emit("  add x" + to_string(t) + ", x" + to_string(kPin1) + ", xzr"); }
  else if (r[0] == "ID" && r.size() == 1) { string id = n->child(0)->lexeme; if (regTab.count(id)) emit("  add x" + to_string(t) + ", x" + to_string(regTab[id]) + ", xzr"); else emitLoadFromFrame(t, symTab.at(id)); }
  else if (r[0] == "LPAREN") genExpr(n->child(1), t, nf);
  else if (r[0] == "GETCHAR") { vector<int> sv; int sb; emitSaveTempsForCall(nf, t, sv, sb); emitCall("getchar"); emitRestoreTempsAfterCall(sv, sb, t); }
  else if (r[0] == "AMP") genLvalueAddress(n->child(1), t);
  else if (r[0] == "STAR") {
    const Node* b; long o;
    if (isPtrOffset(n->child(1), b, o)) {
      pair<size_t, long> key{hashExprTree(b), o};
      auto cached = stmtCseBaseReg.find(key);
      string ldOp = (o >= -256 && o <= 255) ? "ldur" : "ldr";
      if (cached != stmtCseBaseReg.end()) {
        int br = cached->second;
        emit("  " + ldOp + " x" + to_string(t) + ", [x" + to_string(br) + ", " + to_string(o) + "]");
    return;
  }
      int baseReg = t;
      if (nf < 8 && nf != t) {
        baseReg = nf;
        genExpr(b, baseReg, nf + 1);
        stmtCseBaseReg[key] = baseReg;
        emit("  " + ldOp + " x" + to_string(t) + ", [x" + to_string(baseReg) + ", " + to_string(o) + "]");
    } else {
        genExpr(b, t, nf);
        stmtCseBaseReg[key] = t;
        emit("  " + ldOp + " x" + to_string(t) + ", [x" + to_string(t) + ", " + to_string(o) + "]");
        invalidateCseReg(t);
      }
    } else {
      genFactor(n->child(1), t, nf);
      emit("  ldur x" + to_string(t) + ", [x" + to_string(t) + ", 0]");
    }
  }
  else if (r[0] == "ID" && r[1] == "LPAREN") {
    string id = n->child(0)->lexeme; vector<const Node*> args; if (r.size() == 4) { const Node* curr = n->child(2); while (1) { args.push_back(curr->child(0)); if (curr->numChildren() == 3) curr = curr->child(2); else break; } }
    vector<int> sv; int sb; emitSaveTempsForCall(nf, t, sv, sb); for (int i = 0; i < (int)args.size() && i < 8; ++i) genExpr(args[i], i, i + 1);
    emitCall(mangleUserProc(id)); emitRestoreTempsAfterCall(sv, sb, t);
  } else if (r[0] == "NEW") { vector<int> sv; int sb; emitSaveTempsForCall(nf, t, sv, sb); genExpr(n->child(3), 0, 1); emitCall("new"); string k = freshLabel("nok"); emit("  cmp x0, xzr"); emit("  b.ne " + k); emitEnsureConst1(); emit("  add x0, x" + to_string(kPin1) + ", xzr"); emit(k + ":"); emitRestoreTempsAfterCall(sv, sb, t); }
}

void CodeGen::genTerm(const Node* n, int t, int nf) {
  invalidateCseReg(t);
  long v; if (isConst(n, v)) { if (v == 0) emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); else emitLoadConst(t, v); return; }
  const vector<string>& r = n->rhs; if (r.size() == 1) { genFactor(n->child(0), t, nf); return; }
  const string& op = r[1];
  if (op == "STAR") {
    if (isConst(n->child(2), v) && v == 1) { genTerm(n->child(0), t, nf); return; }
    if (isConst(n->child(0), v) && v == 1) { genFactor(n->child(2), t, nf); return; }
    if (isConst(n->child(2), v) && v == 0 && isPure(n->child(0))) { emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); return; }
    if (isConst(n->child(0), v) && v == 0 && isPure(n->child(2))) { emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); return; }
    if (isConst(n->child(2), v) && v == 2) { genTerm(n->child(0), t, nf); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); return; }
  }
  int l = t, r2 = nf; bool sp = (r2 > 7); int aR = sp ? 10 : r2, pNF = sp ? 8 : nf + 1;
  bool swapStar = (!sp && op == "STAR" && typeOf(n->child(0)) == "long" && typeOf(n->child(2)) == "long" && isPure(n->child(0)) && isPure(n->child(2)) && suNeedRegs(n->child(2)) > suNeedRegs(n->child(0)));
  if (sp) { genTerm(n->child(0), l, pNF); emitSubSpImm(16); emit("  stur x" + to_string(l) + ", [sp, 0]"); genFactor(n->child(2), l, nf); emit("  add x" + to_string(aR) + ", x" + to_string(l) + ", xzr"); emit("  ldur x" + to_string(l) + ", [sp, 0]"); emitAddSpImm(16); }
  else if (swapStar) { genFactor(n->child(2), l, pNF); genTerm(n->child(0), aR, pNF); }
  else { genTerm(n->child(0), l, pNF); genFactor(n->child(2), aR, pNF); }
  if (op == "STAR") emit("  mul x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
  else if (op == "SLASH") emit("  sdiv x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
  else { emit("  sdiv x9, x" + to_string(l) + ", x" + to_string(aR)); emit("  mul x10, x9, x" + to_string(aR)); emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x10"); }
}

void CodeGen::genExpr(const Node* n, int t, int nf) {
  invalidateCseReg(t);
  long v; if (isConst(n, v)) { if (v == 0) emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); else emitLoadConst(t, v); return; }
  const vector<string>& r = n->rhs; if (r.size() == 1) { genTerm(n->child(0), t, nf); return; }
  const string& op = r[1];
  if (isConst(n->child(2), v) && v == 0) { genExpr(n->child(0), t, nf); return; }
  if (op == "PLUS" && isConst(n->child(0), v) && v == 0) { genTerm(n->child(2), t, nf); return; }
  if (op == "MINUS" && n->child(0)->rhs.size() == 1 && n->child(2)->rhs.size() == 1) {
    const Node* t0 = n->child(0)->child(0), *t2 = n->child(2)->child(0);
    if (t0->rhs.size() == 1 && t0->rhs[0] == "ID" && t2->rhs.size() == 1 && t2->rhs[0] == "ID" && t0->child(0)->lexeme == t2->child(0)->lexeme) { emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(t)); return; }
  }
  string tL = typeOf(n->child(0)), tR = typeOf(n->child(2));
  if (op == "PLUS") {
    if (tL == "long*" && isConst(n->child(2), v)) { genExpr(n->child(0), t, nf); if (v == 0) return; if (v == 1) { emitEnsureConst8(); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); } else { emitLoadConst(9, v * 8); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9"); } return; }
    if (tR == "long*" && isConst(n->child(0), v)) { genExpr(n->child(2), t, nf); if (v == 0) return; if (v == 1) { emitEnsureConst8(); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); } else { emitLoadConst(9, v * 8); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x9"); } return; }
      } else {
    if (tL == "long*" && isConst(n->child(2), v)) { genExpr(n->child(0), t, nf); if (v == 0) return; if (v == 1) { emitEnsureConst8(); emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); } else { emitLoadConst(9, v * 8); emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x9"); } return; }
  }
  int l = t, r2 = nf; bool sp = (r2 > 7); int aR = sp ? 10 : r2, pNF = sp ? 8 : nf+1;
  bool swapPlus = false;
  if (!sp && op == "PLUS") {
    if (tL == "long" && tR == "long" && isPure(n->child(0)) && isPure(n->child(2)) && suNeedRegs(n->child(2)) > suNeedRegs(n->child(0))) swapPlus = true;
    else if (((tL == "long*" && tR == "long") || (tL == "long" && tR == "long*")) && isPure(n->child(0)) && isPure(n->child(2)) && suNeedRegs(n->child(2)) > suNeedRegs(n->child(0))) swapPlus = true;
  }
  if (sp) { genExpr(n->child(0), l, pNF); emitSubSpImm(16); emit("  stur x" + to_string(l) + ", [sp, 0]"); genTerm(n->child(2), l, nf); emit("  add x" + to_string(aR) + ", x" + to_string(l) + ", xzr"); emit("  ldur x" + to_string(l) + ", [sp, 0]"); emitAddSpImm(16); }
  else if (swapPlus) { genTerm(n->child(2), l, pNF); genExpr(n->child(0), aR, pNF); }
  else { genExpr(n->child(0), l, pNF); genTerm(n->child(2), aR, pNF); }
  if (op == "PLUS") {
    if (tL == "long" && tR == "long") emit("  add x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
    else {
      int ptrReg, longReg;
      if (tL == "long*" && tR == "long") { ptrReg = swapPlus ? aR : l; longReg = swapPlus ? l : aR; }
      else { ptrReg = swapPlus ? l : aR; longReg = swapPlus ? aR : l; }
        emitEnsureConst8();
      emit("  mul x9, x" + to_string(longReg) + ", x" + to_string(kPin8));
      emit("  add x" + to_string(t) + ", x" + to_string(ptrReg) + ", x9");
    }
  } else {
    if (tL == "long" && tR == "long") emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR));
    else if (tL == "long*" && tR == "long") { emitEnsureConst8(); emit("  mul x9, x" + to_string(aR) + ", x" + to_string(kPin8)); emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x9"); }
    else { emit("  sub x" + to_string(t) + ", x" + to_string(l) + ", x" + to_string(aR)); emitEnsureConst8(); emit("  sdiv x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); }
  }
}

void CodeGen::genLvalueAddress(const Node* n, int t) {
  const vector<string>& r = n->rhs;
  if (r[0] == "ID") { emitLoadConst(9, symTab.at(n->child(0)->lexeme)); emit("  add x" + to_string(t) + ", x29, x9"); }
  else if (r[0] == "LPAREN") genLvalueAddress(n->child(1), t);
  else if (r[0] == "STAR") {
    const Node* b; long o;
    if (isPtrOffset(n->child(1), b, o)) {
      pair<size_t, long> key{hashExprTree(b), o};
      auto it = stmtCseBaseReg.find(key);
      if (it != stmtCseBaseReg.end()) {
        int br = it->second;
        if (br != t) emit("  add x" + to_string(t) + ", x" + to_string(br) + ", xzr");
        } else {
        int baseFree = -1;
        for (int rr = 2; rr < 8; ++rr) if (rr != t) { baseFree = rr; break; }
        if (baseFree >= 0) {
          genExpr(b, baseFree, baseFree + 1);
          stmtCseBaseReg[key] = baseFree;
          emit("  add x" + to_string(t) + ", x" + to_string(baseFree) + ", xzr");
        } else {
          genExpr(b, t, t + 1);
          stmtCseBaseReg[key] = t;
        }
      }
      if (o != 0) {
        if (o == 8) { emitEnsureConst8(); emit("  add x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); }
        else if (o == -8) { emitEnsureConst8(); emit("  sub x" + to_string(t) + ", x" + to_string(t) + ", x" + to_string(kPin8)); }
        else { emitLoadConst(9, abs(o)); emit("  " + string(o > 0 ? "add" : "sub") + " x" + to_string(t) + ", x" + to_string(t) + ", x9"); }
      }
      if (o != 0) invalidateCseReg(t);
    } else genFactor(n->child(1), t, t + 1);
  }
}

void CodeGen::genLvalue(const Node* n, int t) {
    const vector<string>& r = n->rhs;
  if (r[0] == "ID") { string id = n->child(0)->lexeme; if (regTab.count(id)) emit("  add x" + to_string(regTab[id]) + ", x" + to_string(t) + ", xzr"); else emitStoreToFrame(t, symTab.at(id)); }
  else if (r[0] == "LPAREN") genLvalue(n->child(1), t);
  else {
    const Node* b; long o = 0;
    emitStoreToFrame(t, assignRhsSaveOff());
    int baseReg = 1;
    if (isPtrOffset(n->child(1), b, o)) {
      pair<size_t, long> key{hashExprTree(b), o};
      auto it = stmtCseBaseReg.find(key);
      if (it != stmtCseBaseReg.end()) {
        baseReg = it->second;
        if (baseReg != 1) emit("  add x1, x" + to_string(baseReg) + ", xzr");
      } else {
        int baseFree = -1;
        for (int rr = 2; rr < 8; ++rr) if (rr != 1) { baseFree = rr; break; }
        if (baseFree >= 0) {
          genExpr(b, baseFree, baseFree + 1);
          stmtCseBaseReg[key] = baseFree;
          baseReg = baseFree;
        } else {
          genExpr(b, 1, 2);
          stmtCseBaseReg[key] = 1;
        }
      }
    } else {
      genFactor(n->child(1), 1, 2);
      baseReg = 1;
      o = 0;
    }
      emitLoadFromFrame(0, assignRhsSaveOff());
    emit("  " + string(o >= -256 && o <= 255 ? "stur" : "str") + " x0, [x" + to_string(baseReg) + ", " + to_string(o) + "]");
  }
}

void CodeGen::genDcls(const Node* n) { vector<const Node*> ds; while (n && !isEmptySt(n)) { ds.push_back(n); n = n->child(0); } for (int j = (int)ds.size() - 1; j >= 0; --j) { const Node* d = ds[j]->child(1); string id = d->child(1)->lexeme; const Node* i = ds[j]->child(3); if (i->kind == "NULL") { emitEnsureConst1(); emit("  add x2, x" + to_string(kPin1) + ", xzr"); } else { long v = stol(i->lexeme); if (v == 0) emit("  sub x2, x2, x2"); else emitLoadConst(2, v); } if (regTab.count(id)) emit("  add x" + to_string(regTab[id]) + ", x2, xzr"); else emitStoreToFrame(2, symTab.at(id)); } }
void CodeGen::genTest(const Node* n, const string& L) {
  // Pinned x14/x15 may be logically invalid across compare operands; always reload before mul/add that use kPin8.
  invalidatePinnedConstsAfterCall();
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
  } else
    emit("  b." + string(kTestBranch.at(op)) + " " + L);
}
void CodeGen::genInvertedTest(const Node* n, const string& L) {
  invalidatePinnedConstsAfterCall();
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
  emit("  b." + inv + " " + L);
}
void CodeGen::genStatement(const Node* n) {
  clearStmtCse();
  const vector<string>& r = n->rhs;
  if (r[0] == "lvalue") { genExpr(n->child(2)); genLvalue(n->child(0)); }
  else if (r[0] == "IF") { bool tv; if (isTestConst(n->child(2), tv)) { if (tv) genStatements(n->child(5)); else genStatements(n->child(9)); return; } int sid = nextStructureId(); string endL = labelWithId("endif", sid); if (isEmptySt(n->child(9))) { genInvertedTest(n->child(2), endL); genStatements(n->child(5)); emit(endL + ":"); } else { string eL = labelWithId("else", sid); genInvertedTest(n->child(2), eL); genStatements(n->child(5)); emit("  b " + endL); emit(eL + ":"); genStatements(n->child(9)); emit(endL + ":"); } }
  else if (r[0] == "WHILE") { bool tv; if (isTestConst(n->child(2), tv)) { if (!tv) return; string sL = freshLabel("wL"); emit(sL + ":"); genStatements(n->child(5)); emit("  b " + sL); return; } int sid = nextStructureId(); string bL = labelWithId("wb", sid), cL = labelWithId("wc", sid), eL = labelWithId("we", sid); emit("  b " + cL); emit(bL + ":"); genStatements(n->child(5)); emit(cL + ":"); genTest(n->child(2), bL); emit(eL + ":"); }
  else if (r[0] == "PRINTLN") { genExpr(n->child(2)); emitCall("print"); } else if (r[0] == "PUTCHAR") { genExpr(n->child(2)); emitCall("putchar"); }
  else if (r[0] == "DELETE") { genExpr(n->child(3)); string sk = freshLabel("dn"); emitEnsureConst1(); emit("  cmp x0, x" + to_string(kPin1)); emit("  b.eq " + sk); emitCall("delete"); emit(sk + ":"); }
}
void CodeGen::genStatements(const Node* n) {
  vector<const Node*> st;
  while (n && n->kind == "statements" && n->numChildren() == 2) {
    st.push_back(n->child(1));
    n = n->child(0);
  }
  for (int i = (int)st.size() - 1; i >= 0; --i) genStatement(st[i]);
}
bool CodeGen::isPtrOffset(const Node* n, const Node*& b, long& o) { if (!n || n->kind != "expr") return false; const vector<string>& r = n->rhs; long v; if (r.size() == 3 && (r[1] == "PLUS" || r[1] == "MINUS")) { if (typeOf(n->child(0)) == "long*" && isConst(n->child(2), v)) { b = n->child(0); o = (r[1] == "PLUS" ? v : -v) * 8; } else if (r[1] == "PLUS" && typeOf(n->child(2)) == "long*" && isConst(n->child(0), v)) { b = n->child(2); o = v * 8; } else return false; return (o >= -256 && o <= 255) || (o >= 0 && o <= 32760 && o % 8 == 0); } return false; }
bool CodeGen::isTailCall(const Node* n, string& id, vector<const Node*>& args) { if (!n || n->kind != "expr" || n->rhs[0] != "term") return false; if (n->child(0)->rhs[0] != "factor") return false; const Node* f = n->child(0)->child(0); if (f->rhs.size() >= 3 && f->rhs[0] == "ID" && f->rhs[1] == "LPAREN") { id = f->child(0)->lexeme; args.clear(); if (f->rhs.size() == 4) { const Node* curr = f->child(2); while (1) { args.push_back(curr->child(0)); if (curr->numChildren() == 3) curr = curr->child(2); else break; } } return true; } return false; }

static void collectPl(const Node* ps, vector<const Node*>& out) {
  vector<const Node*> nodes;
  while (ps && ps->kind == "procedures") {
    if (ps->numChildren() == 1) { nodes.push_back(ps->child(0)); break; }
    nodes.push_back(ps->child(0)); ps = ps->child(1);
  }
  for (int i = (int)nodes.size() - 1; i >= 0; --i) out.push_back(nodes[i]);
}
static void scanT(const Node* root, bool& p, bool& g, bool& t, bool& h) {
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
static void buildST(CodeGen& cg, const Node* p, bool w) {
  cg.symTab.clear(); cg.varType.clear(); cg.regTab.clear(); cg.isLeaf = true; unordered_map<string, int> use; set<string> addr;
  vector<const Node*> st = {p};
  while (!st.empty()) {
    const Node* n = st.back(); st.pop_back(); if (!n) continue;
    if (n->kind == "ID") use[n->lexeme]++;
    const vector<string>& r = n->rhs;
    if (n->kind == "factor" && r.size() >= 1 && (r[0] == "NEW" || r[0] == "GETCHAR" || (r.size() >= 2 && r[1] == "LPAREN"))) cg.isLeaf = false;
    if (n->kind == "statement" && r.size() >= 1 && (r[0] == "DELETE" || r[0] == "PUTCHAR")) cg.isLeaf = false;
    if (n->kind == "factor" && r.size() == 2 && r[0] == "AMP") { const Node* lv = n->child(1); while(lv && lv->numChildren() == 3) lv = lv->child(1); if (lv && lv->kind == "lvalue" && lv->rhs[0] == "ID") addr.insert(lv->child(0)->lexeme); }
    for (int i = (int)n->numChildren() - 1; i >= 0; --i) st.push_back(n->child(i));
  }
  vector<string> loc; if (w) { string id0 = p->child(3)->child(1)->lexeme, id1 = p->child(5)->child(1)->lexeme; cg.symTab[id0] = 0; cg.symTab[id1] = 8; cg.varType[id0] = typeFromDcl(p->child(3)); cg.varType[id1] = typeFromDcl(p->child(5)); }
  else { const Node* ps = p->child(3); if (ps->numChildren() > 0) { const Node* cur = ps->child(0); int i = 0; while (1) { string id = cur->child(0)->child(1)->lexeme; cg.symTab[id] = 8 * i; cg.varType[id] = typeFromDcl(cur->child(0)); if (cur->numChildren() == 3) { cur = cur->child(2); i++; } else break; } } }
  const Node* dclsCur = p->child(w ? 8 : 6); vector<const Node*> dNodes; while (dclsCur && dclsCur->numChildren() >= 2) { dNodes.push_back(dclsCur->child(1)); dclsCur = dclsCur->child(0); }
  for (int j = (int)dNodes.size() - 1; j >= 0; --j) { string id = dNodes[j]->child(1)->lexeme; loc.push_back(id); cg.symTab[id] = -8 * (int)loc.size(); cg.varType[id] = typeFromDcl(dNodes[j]); }
  vector<string> cand; for (const auto& id : loc) if (!addr.count(id)) cand.push_back(id); sort(cand.begin(), cand.end(), [&](const string& a, const string& b) { return use[a] > use[b]; });
  for (int i = 0; i < min((int)cand.size(), 9); ++i) cg.regTab[cand[i]] = 19 + i;
}
static void emitP(CodeGen& cg, const Node* p, const string& /*l*/, bool w, bool h) {
  cg.beginLiteralPool(); const Node* dcls = p->child(w ? 8 : 6), *stmts = p->child(w ? 9 : 7), *ret = p->child(w ? 11 : 9);
  int nL = 0; const Node* dclsTmp = dcls; while (dclsTmp && dclsTmp->numChildren() >= 2) { nL++; dclsTmp = dclsTmp->child(0); }
  int nP = w ? 2 : 0; if (!w && p->child(3)->numChildren() > 0) { const Node* curr = p->child(3)->child(0); nP = 1; while (curr->numChildren() == 3) { curr = curr->child(2); nP++; } }
  cg.emitPrologue(nP, nL, w); for (auto& kv : cg.regTab) cg.emitStoreToFrame(kv.second, cg.symTab[kv.first]);
  if (w && h) { if (cg.varType[p->child(3)->child(1)->lexeme] == "long*") cg.emit("  ldur x0, [x29, 8]"); else cg.emit("  sub x0, x0, x0"); cg.emitCall("init"); }
  cg.genDcls(dcls); cg.genStatements(stmts);
  string cid; vector<const Node*> args;
  if (!w && cg.isTailCall(ret, cid, args)) {
    for (int i = 0; i < min((int)args.size(), 8); ++i) cg.genExpr(args[i], i, i + 1);
    cg.emitLoadSymbolAddr(8, mangleUserProc(cid));
    cg.emitEpilogueWithoutReturn();
    cg.emit("  br x8");
  } else {
    cg.genExpr(ret);
    cg.emitEpilogue();
  }
  cg.endLiteralPool(); cout << cg.flushBuffer();
}

int main() {
  Parser par; unique_ptr<Node> root = par.parseOne(); if (!root) return 1;
  const Node* ps = (root->kind == "start") ? root->child(1) : root.get(); vector<const Node*> pl; collectPl(ps, pl);
  bool p=0, g=0, t=0, h=0; for (const Node* n : pl) scanT(n, p, g, t, h);
  if (h) cout << ".import init\n.import new\n.import delete\n";
  if (p) cout << ".import print\n";
  CodeGen cg; for (const Node* n : pl) {
    if (n->kind == "main") { cout << mangleUserProc("wain") << ":\n"; buildST(cg, n, 1); emitP(cg, n, mangleUserProc("wain"), 1, h); }
    else { string nm = mangleUserProc(n->child(1)->lexeme); cout << nm << ":\n"; buildST(cg, n, 0); emitP(cg, n, nm, 0, 0); }
  }
  if (g) { cout << "getchar:\n"; cg.beginLiteralPool(); cg.emitGetcharStub(); cg.endLiteralPool(); cout << cg.flushBuffer(); }
  if (t) { cout << "putchar:\n"; cg.beginLiteralPool(); cg.emitPutcharStub(); cg.endLiteralPool(); cout << cg.flushBuffer(); }
  return 0;
}
