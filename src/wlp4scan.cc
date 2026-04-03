// WLP4 Scanner (Problem 3a)
// Reads WLP4 source from stdin, outputs one token per line: TOKEN_KIND lexeme
// Uses Simplified Maximal Munch. Skips whitespace and // comments.

#include <iostream>
#include <string>
#include <map>
#include <stdexcept>

using namespace std;

static bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlnum(char c) {
  return isLetter(c) || isDigit(c);
}

static bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

// Keyword lexeme -> token kind (all reserved words)
static const map<string, string> keywords = {
  {"wain", "WAIN"},
  {"long", "LONG"},
  {"if", "IF"},
  {"else", "ELSE"},
  {"while", "WHILE"},
  {"println", "PRINTLN"},
  {"putchar", "PUTCHAR"},
  {"getchar", "GETCHAR"},
  {"return", "RETURN"},
  {"NULL", "NULL"},
  {"new", "NEW"},
  {"delete", "DELETE"},
};

// Two-char symbols: lexeme -> token kind
static const map<string, string> twoChar = {
  {"==", "EQ"},
  {"!=", "NE"},
  {"<=", "LE"},
  {">=", "GE"},
};

// Single-char symbols
static const map<char, string> oneChar = {
  {'(', "LPAREN"},
  {')', "RPAREN"},
  {'{', "LBRACE"},
  {'}', "RBRACE"},
  {'=', "BECOMES"},
  {'<', "LT"},
  {'>', "GT"},
  {'+', "PLUS"},
  {'-', "MINUS"},
  {'*', "STAR"},
  {'/', "SLASH"},
  {'%', "PCT"},
  {',', "COMMA"},
  {';', "SEMI"},
  {'[', "LBRACK"},
  {']', "RBRACK"},
  {'&', "AMP"},
};

int main() {
  string input;
  string line;
  while (getline(cin, line)) {
    if (!input.empty()) input += '\n';
    input += line;
  }

  size_t pos = 0;
  const size_t n = input.size();
  int status = 0;

  while (pos < n) {
    // Skip whitespace
    while (pos < n && isWhitespace(input[pos])) ++pos;
    if (pos >= n) break;

    // Skip // comment (to end of line)
    if (pos + 1 < n && input[pos] == '/' && input[pos + 1] == '/') {
      while (pos < n && input[pos] != '\n') ++pos;
      continue;
    }

    size_t longest = 0;
    string bestKind;
    string bestLexeme;

    // Try two-char symbols
    if (pos + 2 <= n) {
      string two = input.substr(pos, 2);
      auto it = twoChar.find(two);
      if (it != twoChar.end()) {
        longest = 2;
        bestKind = it->second;
        bestLexeme = two;
      }
    }

    // Try one-char symbols
    {
      auto it = oneChar.find(input[pos]);
      if (it != oneChar.end() && 1 > longest) {
        longest = 1;
        bestKind = it->second;
        bestLexeme = string(1, it->first);
      }
    }

    // Try NUM: single '0' or '1'..'9' followed by digits
    if (isDigit(input[pos])) {
      size_t numLen = 0;
      if (input[pos] == '0') {
        numLen = 1;
      } else {
        while (pos + numLen < n && isDigit(input[pos + numLen])) ++numLen;
      }
      if (numLen > 0 && numLen >= longest) {
        string numStr = input.substr(pos, numLen);
        try {
          (void) stoll(numStr);  // throws out_of_range if overflow
          longest = numLen;
          bestKind = "NUM";
          bestLexeme = numStr;
        } catch (const out_of_range&) {
          cerr << "ERROR: numeric value out of range" << endl;
          status = 1;
          break;
        }
      }
    }

    // Try ID / keyword: letter then (letter|digit)*
    if (isLetter(input[pos])) {
      size_t idLen = 0;
      while (pos + idLen < n && isAlnum(input[pos + idLen])) ++idLen;
      if (idLen >= longest) {
        string lex = input.substr(pos, idLen);
        auto it = keywords.find(lex);
        string kind = (it != keywords.end()) ? it->second : "ID";
        longest = idLen;
        bestKind = kind;
        bestLexeme = lex;
      }
    }

    if (longest == 0) {
      cerr << "ERROR: invalid token" << endl;
      status = 1;
      break;
    }

    cout << bestKind << " " << bestLexeme << "\n";
    pos += longest;
  }

  return status;
}
