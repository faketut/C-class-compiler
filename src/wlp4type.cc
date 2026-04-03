#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// --- Grammar (WLP4 CFG) ---
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

// --- Tree node ---
struct Node {
  bool isTerminal;
  string kind;   // terminal: token kind; rule: lhs
  string lexeme; // terminal: lexeme
  vector<string> rhs;
  vector<unique_ptr<Node>> children;
  string type;   // for expression nodes: "long" or "long*"

  static unique_ptr<Node> makeTerminal(string k, string l) {
    auto n = make_unique<Node>();
    n->isTerminal = true;
    n->kind = std::move(k);
    n->lexeme = std::move(l);
    return n;
  }
  static unique_ptr<Node> makeRule(string lhs, vector<string> r, vector<unique_ptr<Node>> c) {
    auto n = make_unique<Node>();
    n->isTerminal = false;
    n->kind = std::move(lhs);
    n->rhs = std::move(r);
    n->children = std::move(c);
    return n;
  }

  const Node* child(size_t i) const { return children.at(i).get(); }
  Node* child(size_t i) { return children.at(i).get(); }
  size_t numChildren() const { return children.size(); }
};

// --- .wlp4i parser ---
struct Parser {
  vector<string> lines;
  size_t idx = 0;

  void readInput() {
    string l;
    while (getline(cin, l)) lines.push_back(l);
  }

  unique_ptr<Node> parseOne() {
    while (idx < lines.size()) {
      string line = lines[idx++];
      auto toks = split(line);
      if (toks.empty()) continue;
      if (NONTERMINALS.count(toks[0])) {
        string lhs = toks[0];
        vector<string> rhs;
        if (toks.size() >= 2 && toks[1] == ".EMPTY") {
          rhs.push_back(".EMPTY");
        } else {
          for (size_t i = 1; i < toks.size(); ++i) rhs.push_back(toks[i]);
        }
        vector<unique_ptr<Node>> children;
        for (const string& sym : rhs) {
          if (sym == ".EMPTY") continue;
          children.push_back(parseOne());
          if (!children.back()) return nullptr;
        }
        return Node::makeRule(lhs, std::move(rhs), std::move(children));
      } else {
        string lex = toks.size() >= 2 ? toks[1] : "";
        return Node::makeTerminal(toks[0], lex);
      }
    }
    return nullptr;
  }
};

// --- Semantic analysis ---
static void reportError() {
  cerr << "ERROR" << endl;
}

// Get type from dcl subtree: type LONG [STAR] -> "long" or "long*"
static string typeFromDcl(const Node* dcl) {
  const Node* typeNode = dcl->child(0);
  if (typeNode->rhs.size() == 1) return "long";
  return "long*";
}

// Get ID lexeme from dcl
static string idFromDcl(const Node* dcl) {
  return dcl->child(1)->lexeme;
}

// Check if node is expression nonterminal
static bool isExprNonterminal(const string& lhs) {
  return lhs == "expr" || lhs == "term" || lhs == "factor" || lhs == "lvalue";
}

// Recursive type-check expression (returns type or "" on error). Sets node->type.
static string typecheckExpr(Node* n, const map<string, string>& symTab,
                            const map<string, pair<string, vector<string>>>& procTab,
                            bool* err);

// Type-check lvalue (for & and assignment); returns type or ""
static string typecheckLvalue(Node* n, const map<string, string>& symTab,
                               const map<string, pair<string, vector<string>>>& procTab,
                               bool* err) {
  if (!n || n->isTerminal) return "";
  if (n->kind == "lvalue") {
    const vector<string>& r = n->rhs;
    if (r.size() == 1 && r[0] == "ID") {
      string id = n->child(0)->lexeme;
      auto it = symTab.find(id);
      if (it == symTab.end()) { *err = true; return ""; }
      string t = it->second;
      n->child(0)->type = t;
      n->type = t;
      return t;
    }
    if (r.size() == 2 && r[0] == "STAR" && r[1] == "factor") {
      string t = typecheckExpr(n->child(1), symTab, procTab, err);
      if (*err || t != "long*") { *err = true; return ""; }
      n->type = "long";
      return "long";
    }
    if (r.size() == 3 && r[0] == "LPAREN" && r[1] == "lvalue" && r[2] == "RPAREN") {
      string t = typecheckLvalue(n->child(1), symTab, procTab, err);
      n->type = t;
      return t;
    }
  }
  return "";
}

static string typecheckExpr(Node* n, const map<string, string>& symTab,
                            const map<string, pair<string, vector<string>>>& procTab,
                            bool* err) {
  if (!n) return "";
  if (n->isTerminal) {
    if (n->kind == "NUM") { n->type = "long"; return "long"; }
    if (n->kind == "NULL") { n->type = "long*"; return "long*"; }
    if (n->kind == "ID") {
      auto it = symTab.find(n->lexeme);
      if (it == symTab.end()) { *err = true; return ""; }
      n->type = it->second;
      return it->second;
    }
    return "";
  }
  const string& lhs = n->kind;
  const vector<string>& r = n->rhs;
  if (lhs == "expr") {
    if (r.size() == 1 && r[0] == "term") {
      string t = typecheckExpr(n->child(0), symTab, procTab, err);
      n->type = t;
      return t;
    }
    if (r.size() == 3 && (r[1] == "PLUS" || r[1] == "MINUS")) {
      string l = typecheckExpr(n->child(0), symTab, procTab, err);
      string rht = typecheckExpr(n->child(2), symTab, procTab, err);
      if (*err) return "";
      if (r[1] == "PLUS") {
        if (l == "long" && rht == "long") { n->type = "long"; return "long"; }
        if (l == "long*" && rht == "long") { n->type = "long*"; return "long*"; }
        if (l == "long" && rht == "long*") { n->type = "long*"; return "long*"; }
        if (l == "long*" && rht == "long*") { *err = true; return ""; }
      } else {
        if (l == "long" && rht == "long") { n->type = "long"; return "long"; }
        if (l == "long*" && rht == "long") { n->type = "long*"; return "long*"; }
        if (l == "long*" && rht == "long*") { n->type = "long"; return "long"; }
        if (l == "long" && rht == "long*") { *err = true; return ""; }
      }
      *err = true;
      return "";
    }
  }
  if (lhs == "term") {
    if (r.size() == 1 && r[0] == "factor") {
      string t = typecheckExpr(n->child(0), symTab, procTab, err);
      n->type = t;
      return t;
    }
    if (r.size() == 3 && (r[1] == "STAR" || r[1] == "SLASH" || r[1] == "PCT")) {
      string l = typecheckExpr(n->child(0), symTab, procTab, err);
      string rht = typecheckExpr(n->child(2), symTab, procTab, err);
      if (*err || l != "long" || rht != "long") { *err = true; return ""; }
      n->type = "long";
      return "long";
    }
  }
  if (lhs == "factor") {
    if (r.size() == 1 && r[0] == "ID") {
      string id = n->child(0)->lexeme;
      auto it = symTab.find(id);
      if (it != symTab.end()) {
        n->child(0)->type = it->second;
        n->type = it->second;
        return it->second;
      }
      auto pt = procTab.find(id);
      if (pt != procTab.end()) { *err = true; return ""; } // procedure used as value
      *err = true;
      return "";
    }
    if (r.size() == 1 && r[0] == "NUM") {
      n->child(0)->type = "long";
      n->type = "long";
      return "long";
    }
    if (r.size() == 1 && r[0] == "NULL") {
      n->child(0)->type = "long*";
      n->type = "long*";
      return "long*";
    }
    if (r.size() == 3 && r[0] == "LPAREN" && r[1] == "expr" && r[2] == "RPAREN") {
      string t = typecheckExpr(n->child(1), symTab, procTab, err);
      n->type = t;
      return t;
    }
    if (r.size() == 2 && r[0] == "AMP" && r[1] == "lvalue") {
      string t = typecheckLvalue(n->child(1), symTab, procTab, err);
      if (*err || t != "long") { *err = true; return ""; }
      n->type = "long*";
      return "long*";
    }
    if (r.size() == 2 && r[0] == "STAR" && r[1] == "factor") {
      string t = typecheckExpr(n->child(1), symTab, procTab, err);
      if (*err || t != "long*") { *err = true; return ""; }
      n->type = "long";
      return "long";
    }
    if (r.size() == 5 && r[0] == "NEW" && r[1] == "LONG" && r[2] == "LBRACK" && r[3] == "expr" && r[4] == "RBRACK") {
      string t = typecheckExpr(n->child(3), symTab, procTab, err);
      if (*err || t != "long") { *err = true; return ""; }
      n->type = "long*";
      return "long*";
    }
    if (r.size() == 3 && r[0] == "ID" && r[1] == "LPAREN" && r[2] == "RPAREN") {
      string id = n->child(0)->lexeme;
      auto it = procTab.find(id);
      if (it == procTab.end()) {
        if (symTab.count(id)) { *err = true; return ""; } // variable called
        *err = true;
        return "";
      }
      if (!it->second.second.empty()) { *err = true; return ""; }
      n->type = it->second.first;
      return it->second.first;
    }
    if (r.size() == 4 && r[0] == "ID" && r[1] == "LPAREN" && r[2] == "arglist" && r[3] == "RPAREN") {
      string id = n->child(0)->lexeme;
      auto it = procTab.find(id);
      if (it == procTab.end()) {
        if (symTab.count(id)) { *err = true; return ""; }
        *err = true;
        return "";
      }
      vector<string> argTypes;
      const Node* al = n->child(2);
      while (true) {
        argTypes.push_back(typecheckExpr(const_cast<Node*>(al->child(0)), symTab, procTab, err));
        if (*err) return "";
        if (al->rhs.size() == 1) break;
        al = al->child(2);
      }
      if (argTypes != it->second.second) { *err = true; return ""; }
      n->type = it->second.first;
      return it->second.first;
    }
    if (r.size() == 3 && r[0] == "GETCHAR" && r[1] == "LPAREN" && r[2] == "RPAREN") {
      n->type = "long";
      return "long";
    }
  }
  if (lhs == "lvalue") {
    return typecheckLvalue(n, symTab, procTab, err);
  }
  return "";
}

// Typecheck a statements list (recursive for IF/WHILE bodies). Returns false on error.
static bool typecheckStatements(Node* statements,
    const map<string, string>& symTab,
    const map<string, pair<string, vector<string>>>& procTable,
    bool* err) {
  while (statements && !statements->isTerminal && statements->kind == "statements" && statements->rhs.size() == 2) {
    Node* stmt = statements->child(1);
    if (stmt->rhs[0] == "lvalue") {
      string lv = typecheckLvalue(stmt->child(0), symTab, procTable, err);
      string ex = typecheckExpr(stmt->child(2), symTab, procTable, err);
      if (*err || lv != ex) return false;
    } else if (stmt->rhs[0] == "IF") {
      Node* testNode = stmt->child(2);
      string t1 = typecheckExpr(testNode->child(0), symTab, procTable, err);
      if (*err) return false;
      string t2 = typecheckExpr(testNode->child(2), symTab, procTable, err);
      if (*err || t1 != t2) return false;
      if (!typecheckStatements(stmt->child(5), symTab, procTable, err)) return false;
      if (!typecheckStatements(stmt->child(9), symTab, procTable, err)) return false;
    } else if (stmt->rhs[0] == "WHILE") {
      Node* testNode = stmt->child(2);
      string t1 = typecheckExpr(testNode->child(0), symTab, procTable, err);
      if (*err) return false;
      string t2 = typecheckExpr(testNode->child(2), symTab, procTable, err);
      if (*err || t1 != t2) return false;
      if (!typecheckStatements(stmt->child(5), symTab, procTable, err)) return false;
    } else if (stmt->rhs[0] == "PRINTLN" || stmt->rhs[0] == "PUTCHAR") {
      string t = typecheckExpr(stmt->child(2), symTab, procTable, err);
      if (*err || t != "long") return false;
    } else if (stmt->rhs[0] == "DELETE") {
      string t = typecheckExpr(stmt->child(3), symTab, procTable, err);
      if (*err || t != "long*") return false;
    }
    statements = statements->child(0);
  }
  return true;
}

// Process procedures: add to table in order (call-before-declaration check), then check each body
static bool semanticAnalysis(Node* root) {
  if (!root || root->isTerminal) return false;
  Node* procedures = nullptr;
  if (root->kind == "start") procedures = root->child(1);
  else if (root->kind == "procedures") procedures = root;
  else return false;
  map<string, pair<string, vector<string>>> procTable;
  vector<Node*> procedureNodes;
  for (Node* p = procedures; p && !p->isTerminal && p->kind == "procedures"; ) {
    Node* proc = p->child(0);
    procedureNodes.push_back(proc);
    p = (p->rhs.size() == 2) ? p->child(1) : nullptr;
  }
  bool err = false;
  for (Node* proc : procedureNodes) {
    if (proc->kind == "main") {
      if (procTable.count("wain")) { reportError(); return false; }
      string t1 = typeFromDcl(proc->child(3));
      string t2 = typeFromDcl(proc->child(5));
      proc->child(3)->child(1)->type = t1;
      proc->child(5)->child(1)->type = t2;
      if (idFromDcl(proc->child(3)) == idFromDcl(proc->child(5))) {
        reportError(); return false;
      }
      if (t2 != "long") { reportError(); return false; }
      procTable["wain"] = {"long", {t1, t2}};
    } else {
      string name = proc->child(1)->lexeme;
      if (procTable.count(name)) { reportError(); return false; }
      vector<string> paramTypes;
      Node* paramsNode = proc->child(3);
      if (paramsNode->rhs.size() == 1 && paramsNode->rhs[0] == "paramlist") {
        Node* pl = paramsNode->child(0);
        while (true) {
          paramTypes.push_back(typeFromDcl(pl->child(0)));
          if (pl->rhs.size() == 1) break;
          pl = pl->child(2);
        }
      }
      procTable[name] = {"long", paramTypes};
    }
    map<string, string> symTab;
    if (proc->kind == "main") {
      symTab[idFromDcl(proc->child(3))] = typeFromDcl(proc->child(3));
      symTab[idFromDcl(proc->child(5))] = typeFromDcl(proc->child(5));
    } else {
      Node* paramsNode = proc->child(3);
      if (paramsNode->rhs.size() == 1 && paramsNode->rhs[0] == "paramlist") {
        Node* pl = paramsNode->child(0);
        while (true) {
          Node* dcl = pl->child(0);
          string id = idFromDcl(dcl);
          if (symTab.count(id)) { reportError(); return false; }
          string pt = typeFromDcl(dcl);
          symTab[id] = pt;
          dcl->child(1)->type = pt;
          if (pl->rhs.size() == 1) break;
          pl = pl->child(2);
        }
      }
    }
    int dclsIdx = (proc->kind == "main") ? 8 : 6;
    int stmtsIdx = (proc->kind == "main") ? 9 : 7;
    int retIdx = (proc->kind == "main") ? 11 : 9;
    Node* dcls = proc->child(dclsIdx);
    while (dcls && !dcls->isTerminal && dcls->kind == "dcls" && dcls->rhs.size() >= 5) {
      Node* dcl = dcls->child(1);
      string id = idFromDcl(dcl);
      if (symTab.count(id)) { reportError(); return false; }
      string declType = typeFromDcl(dcl);
      if (dcls->rhs[3] == "NUM") {
        if (declType != "long") { reportError(); return false; }
        dcls->child(3)->type = "long";
      } else {
        if (declType != "long*") { reportError(); return false; }
        dcls->child(3)->type = "long*";
      }
      symTab[id] = declType;
      dcl->child(1)->type = declType;
      dcls = dcls->child(0);
    }
    Node* statements = proc->child(stmtsIdx);
    if (!typecheckStatements(statements, symTab, procTable, &err)) {
      reportError();
      return false;
    }
    Node* returnExpr = proc->child(retIdx);
    string retType = typecheckExpr(returnExpr, symTab, procTable, &err);
    if (err || retType != "long") { reportError(); return false; }
  }
  return true;
}

// --- Print .wlp4ti (preorder, annotate expression nodes) ---
static void printTree(const Node* n) {
  if (!n) return;
  if (n->isTerminal) {
    cout << n->kind << " " << n->lexeme;
    if (!n->type.empty()) cout << " : " << n->type;
    cout << "\n";
    return;
  }
  cout << n->kind;
  for (const auto& s : n->rhs) cout << " " << s;
  if (isExprNonterminal(n->kind) && !n->type.empty()) cout << " : " << n->type;
  cout << "\n";
  for (const auto& c : n->children) printTree(c.get());
}

int main() {
  cin.tie(nullptr);
  Parser parser;
  parser.readInput();
  unique_ptr<Node> root = parser.parseOne();
  if (!root) {
    reportError();
    return 1;
  }
  if (!semanticAnalysis(root.get())) {
    return 1;
  }
  printTree(root.get());
  cout.flush();
  return 0;
}
