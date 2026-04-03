#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const std::string WLP4_CFG = R"END(.CFG
start BOF procedures EOF
procedures procedure procedures
procedures main
procedure LONG ID LPAREN params RPAREN LBRACE dcls statements RETURN expr SEMI RBRACE
main LONG WAIN LPAREN dcl COMMA dcl RPAREN LBRACE dcls statements RETURN expr SEMI RBRACE
params .EMPTY
params paramlist
paramlist dcl
paramlist dcl COMMA paramlist
type LONG
type LONG STAR
dcls .EMPTY
dcls dcls dcl BECOMES NUM SEMI
dcls dcls dcl BECOMES NULL SEMI
dcl type ID
statements .EMPTY
statements statements statement
statement lvalue BECOMES expr SEMI
statement IF LPAREN test RPAREN LBRACE statements RBRACE ELSE LBRACE statements RBRACE
statement WHILE LPAREN test RPAREN LBRACE statements RBRACE
statement PRINTLN LPAREN expr RPAREN SEMI
statement PUTCHAR LPAREN expr RPAREN SEMI
statement DELETE LBRACK RBRACK expr SEMI
test expr EQ expr
test expr NE expr
test expr LT expr
test expr LE expr
test expr GE expr
test expr GT expr
expr term
expr expr PLUS term
expr expr MINUS term
term factor
term term STAR factor
term term SLASH factor
term term PCT factor
factor ID
factor NUM
factor NULL
factor LPAREN expr RPAREN
factor AMP lvalue
factor STAR factor
factor NEW LONG LBRACK expr RBRACK
factor ID LPAREN RPAREN
factor ID LPAREN arglist RPAREN
factor GETCHAR LPAREN RPAREN
arglist expr
arglist expr COMMA arglist
lvalue ID
lvalue STAR factor
lvalue LPAREN lvalue RPAREN
)END";

const std::string WLP4_TRANSITIONS = R"END(.TRANSITIONS
0 BOF 1
1 procedures 4
1 main 3
1 LONG 5
1 procedure 2
2 main 3
2 procedures 6
2 LONG 5
2 procedure 2
4 EOF 7
5 WAIN 9
5 ID 8
8 LPAREN 10
9 LPAREN 11
10 type 16
10 LONG 15
10 dcl 14
10 paramlist 13
10 params 12
11 type 16
11 LONG 15
11 dcl 17
12 RPAREN 18
14 COMMA 19
15 STAR 20
16 ID 21
17 COMMA 22
18 LBRACE 23
19 type 16
19 LONG 15
19 paramlist 24
19 dcl 14
22 type 16
22 LONG 15
22 dcl 25
23 dcls 26
25 RPAREN 27
26 type 16
26 dcl 29
26 LONG 15
26 statements 28
27 LBRACE 30
28 LPAREN 41
28 PUTCHAR 37
28 PRINTLN 36
28 DELETE 38
28 WHILE 35
28 ID 39
28 IF 34
28 statement 33
28 lvalue 32
28 STAR 40
28 RETURN 31
29 BECOMES 42
30 dcls 43
31 NEW 53
31 STAR 52
31 LPAREN 50
31 GETCHAR 54
31 NULL 49
31 NUM 48
31 ID 47
31 factor 46
31 term 45
31 AMP 51
31 expr 44
32 BECOMES 55
34 LPAREN 56
35 LPAREN 57
36 LPAREN 58
37 LPAREN 59
38 LBRACK 60
40 NEW 53
40 STAR 52
40 AMP 51
40 LPAREN 50
40 GETCHAR 54
40 NULL 49
40 NUM 48
40 factor 61
40 ID 47
41 lvalue 62
41 LPAREN 41
41 STAR 40
41 ID 39
42 NULL 64
42 NUM 63
43 type 16
43 dcl 29
43 LONG 15
43 statements 65
44 MINUS 68
44 PLUS 67
44 SEMI 66
45 PCT 71
45 SLASH 70
45 STAR 69
47 LPAREN 72
50 NEW 53
50 STAR 52
50 LPAREN 50
50 GETCHAR 54
50 NULL 49
50 NUM 48
50 ID 47
50 factor 46
50 AMP 51
50 expr 73
50 term 45
51 LPAREN 41
51 STAR 40
51 ID 39
51 lvalue 74
52 NEW 53
52 STAR 52
52 AMP 51
52 LPAREN 50
52 GETCHAR 54
52 NULL 49
52 NUM 48
52 factor 75
52 ID 47
53 LONG 76
54 LPAREN 77
55 NEW 53
55 STAR 52
55 LPAREN 50
55 GETCHAR 54
55 NULL 49
55 NUM 48
55 ID 47
55 factor 46
55 term 45
55 AMP 51
55 expr 78
56 NEW 53
56 STAR 52
56 LPAREN 50
56 NUM 48
56 ID 47
56 factor 46
56 term 45
56 AMP 51
56 expr 80
56 GETCHAR 54
56 NULL 49
56 test 79
57 NEW 53
57 STAR 52
57 LPAREN 50
57 NUM 48
57 ID 47
57 factor 46
57 term 45
57 AMP 51
57 expr 80
57 GETCHAR 54
57 NULL 49
57 test 81
58 NEW 53
58 STAR 52
58 LPAREN 50
58 GETCHAR 54
58 NULL 49
58 NUM 48
58 ID 47
58 factor 46
58 term 45
58 AMP 51
58 expr 82
59 NEW 53
59 STAR 52
59 LPAREN 50
59 GETCHAR 54
59 NULL 49
59 NUM 48
59 ID 47
59 factor 46
59 term 45
59 AMP 51
59 expr 83
60 RBRACK 84
62 RPAREN 85
63 SEMI 86
64 SEMI 87
65 LPAREN 41
65 PUTCHAR 37
65 PRINTLN 36
65 DELETE 38
65 WHILE 35
65 ID 39
65 IF 34
65 statement 33
65 lvalue 32
65 STAR 40
65 RETURN 88
66 RBRACE 89
67 NEW 53
67 STAR 52
67 AMP 51
67 LPAREN 50
67 GETCHAR 54
67 NULL 49
67 NUM 48
67 ID 47
67 factor 46
67 term 90
68 NEW 53
68 STAR 52
68 AMP 51
68 LPAREN 50
68 GETCHAR 54
68 NULL 49
68 NUM 48
68 ID 47
68 factor 46
68 term 91
69 NEW 53
69 STAR 52
69 AMP 51
69 LPAREN 50
69 GETCHAR 54
69 NULL 49
69 NUM 48
69 ID 47
69 factor 92
70 NEW 53
70 STAR 52
70 AMP 51
70 LPAREN 50
70 GETCHAR 54
70 NULL 49
70 NUM 48
70 ID 47
70 factor 93
71 NEW 53
71 STAR 52
71 AMP 51
71 LPAREN 50
71 GETCHAR 54
71 NULL 49
71 NUM 48
71 ID 47
71 factor 94
72 arglist 97
72 NEW 53
72 STAR 52
72 LPAREN 50
72 GETCHAR 54
72 NULL 49
72 RPAREN 96
72 NUM 48
72 ID 47
72 factor 46
72 AMP 51
72 expr 95
72 term 45
73 RPAREN 98
73 MINUS 68
73 PLUS 67
76 LBRACK 99
77 RPAREN 100
78 MINUS 68
78 PLUS 67
78 SEMI 101
79 RPAREN 102
80 MINUS 68
80 PLUS 67
80 GT 108
80 GE 107
80 LE 106
80 LT 105
80 NE 104
80 EQ 103
81 RPAREN 109
82 MINUS 68
82 PLUS 67
82 RPAREN 110
83 MINUS 68
83 PLUS 67
83 RPAREN 111
84 NEW 53
84 STAR 52
84 LPAREN 50
84 GETCHAR 54
84 NULL 49
84 NUM 48
84 ID 47
84 factor 46
84 term 45
84 AMP 51
84 expr 112
88 NEW 53
88 STAR 52
88 LPAREN 50
88 GETCHAR 54
88 NULL 49
88 NUM 48
88 ID 47
88 factor 46
88 term 45
88 AMP 51
88 expr 113
90 PCT 71
90 SLASH 70
90 STAR 69
91 PCT 71
91 SLASH 70
91 STAR 69
95 COMMA 114
95 MINUS 68
95 PLUS 67
97 RPAREN 115
99 NEW 53
99 STAR 52
99 LPAREN 50
99 GETCHAR 54
99 NULL 49
99 NUM 48
99 ID 47
99 factor 46
99 AMP 51
99 expr 116
99 term 45
102 LBRACE 117
103 NEW 53
103 STAR 52
103 LPAREN 50
103 GETCHAR 54
103 NULL 49
103 NUM 48
103 ID 47
103 factor 46
103 term 45
103 AMP 51
103 expr 118
104 NEW 53
104 STAR 52
104 LPAREN 50
104 GETCHAR 54
104 NULL 49
104 NUM 48
104 ID 47
104 factor 46
104 term 45
104 AMP 51
104 expr 119
105 NEW 53
105 STAR 52
105 LPAREN 50
105 GETCHAR 54
105 NULL 49
105 NUM 48
105 ID 47
105 factor 46
105 term 45
105 AMP 51
105 expr 120
106 NEW 53
106 STAR 52
106 LPAREN 50
106 GETCHAR 54
106 NULL 49
106 NUM 48
106 ID 47
106 factor 46
106 term 45
106 AMP 51
106 expr 121
107 NEW 53
107 STAR 52
107 LPAREN 50
107 GETCHAR 54
107 NULL 49
107 NUM 48
107 ID 47
107 factor 46
107 term 45
107 AMP 51
107 expr 122
108 NEW 53
108 STAR 52
108 LPAREN 50
108 GETCHAR 54
108 NULL 49
108 NUM 48
108 ID 47
108 factor 46
108 term 45
108 AMP 51
108 expr 123
109 LBRACE 124
110 SEMI 125
111 SEMI 126
112 MINUS 68
112 PLUS 67
112 SEMI 127
113 MINUS 68
113 PLUS 67
113 SEMI 128
114 arglist 129
114 NEW 53
114 STAR 52
114 LPAREN 50
114 GETCHAR 54
114 NULL 49
114 NUM 48
114 ID 47
114 factor 46
114 AMP 51
114 expr 95
114 term 45
116 RBRACK 130
116 MINUS 68
116 PLUS 67
117 statements 131
118 MINUS 68
118 PLUS 67
119 MINUS 68
119 PLUS 67
120 MINUS 68
120 PLUS 67
121 MINUS 68
121 PLUS 67
122 MINUS 68
122 PLUS 67
123 MINUS 68
123 PLUS 67
124 statements 132
128 RBRACE 133
131 LPAREN 41
131 PUTCHAR 37
131 PRINTLN 36
131 DELETE 38
131 WHILE 35
131 STAR 40
131 RBRACE 134
131 ID 39
131 IF 34
131 statement 33
131 lvalue 32
132 LPAREN 41
132 PUTCHAR 37
132 PRINTLN 36
132 STAR 40
132 RBRACE 135
132 DELETE 38
132 WHILE 35
132 ID 39
132 IF 34
132 statement 33
132 lvalue 32
134 ELSE 136
136 LBRACE 137
137 statements 138
138 LPAREN 41
138 PUTCHAR 37
138 PRINTLN 36
138 DELETE 38
138 WHILE 35
138 STAR 40
138 RBRACE 139
138 ID 39
138 IF 34
138 statement 33
138 lvalue 32
)END";

const std::string WLP4_REDUCTIONS = R"END(.REDUCTIONS
3 2 EOF
6 1 EOF
7 0 .ACCEPT
10 5 RPAREN
13 6 RPAREN
14 7 RPAREN
15 9 ID
20 10 ID
21 14 COMMA
21 14 RPAREN
21 14 BECOMES
23 11 DELETE
23 11 WHILE
23 11 LONG
23 11 PUTCHAR
23 11 PRINTLN
23 11 IF
23 11 ID
23 11 STAR
23 11 RETURN
23 11 LPAREN
24 8 RPAREN
26 15 RETURN
26 15 STAR
26 15 RBRACE
26 15 LPAREN
26 15 ID
26 15 IF
26 15 PRINTLN
26 15 PUTCHAR
26 15 WHILE
26 15 DELETE
30 11 DELETE
30 11 WHILE
30 11 LONG
30 11 PUTCHAR
30 11 PRINTLN
30 11 IF
30 11 ID
30 11 STAR
30 11 RETURN
30 11 LPAREN
33 16 RETURN
33 16 STAR
33 16 RBRACE
33 16 LPAREN
33 16 ID
33 16 IF
33 16 PRINTLN
33 16 PUTCHAR
33 16 WHILE
33 16 DELETE
39 48 LE
39 48 SLASH
39 48 EQ
39 48 GE
39 48 STAR
39 48 GT
39 48 RPAREN
39 48 BECOMES
39 48 LT
39 48 MINUS
39 48 PLUS
39 48 SEMI
39 48 NE
39 48 COMMA
39 48 RBRACK
39 48 PCT
43 15 RETURN
43 15 STAR
43 15 RBRACE
43 15 LPAREN
43 15 ID
43 15 IF
43 15 PRINTLN
43 15 PUTCHAR
43 15 WHILE
43 15 DELETE
45 29 COMMA
45 29 NE
45 29 RPAREN
45 29 RBRACK
45 29 SEMI
45 29 GT
45 29 MINUS
45 29 PLUS
45 29 GE
45 29 LE
45 29 LT
45 29 EQ
46 32 LE
46 32 SLASH
46 32 GT
46 32 MINUS
46 32 PLUS
46 32 SEMI
46 32 NE
46 32 COMMA
46 32 RPAREN
46 32 RBRACK
46 32 PCT
46 32 STAR
46 32 GE
46 32 EQ
46 32 LT
47 36 BECOMES
47 36 GE
47 36 STAR
47 36 LT
47 36 EQ
47 36 RPAREN
47 36 RBRACK
47 36 PCT
47 36 COMMA
47 36 SLASH
47 36 LE
47 36 NE
47 36 SEMI
47 36 GT
47 36 PLUS
47 36 MINUS
48 37 BECOMES
48 37 GE
48 37 STAR
48 37 LT
48 37 EQ
48 37 RPAREN
48 37 RBRACK
48 37 PCT
48 37 COMMA
48 37 SLASH
48 37 LE
48 37 NE
48 37 SEMI
48 37 GT
48 37 PLUS
48 37 MINUS
49 38 BECOMES
49 38 GE
49 38 STAR
49 38 LT
49 38 EQ
49 38 RPAREN
49 38 RBRACK
49 38 PCT
49 38 COMMA
49 38 SLASH
49 38 LE
49 38 NE
49 38 SEMI
49 38 GT
49 38 PLUS
49 38 MINUS
61 49 LE
61 49 SLASH
61 49 EQ
61 49 GE
61 49 STAR
61 49 GT
61 49 RPAREN
61 49 BECOMES
61 49 LT
61 49 MINUS
61 49 PLUS
61 49 SEMI
61 49 NE
61 49 COMMA
61 49 RBRACK
61 49 PCT
74 40 BECOMES
74 40 GE
74 40 STAR
74 40 LT
74 40 EQ
74 40 RPAREN
74 40 RBRACK
74 40 PCT
74 40 COMMA
74 40 SLASH
74 40 LE
74 40 NE
74 40 SEMI
74 40 GT
74 40 PLUS
74 40 MINUS
75 41 BECOMES
75 41 GE
75 41 STAR
75 41 LT
75 41 EQ
75 41 RPAREN
75 41 RBRACK
75 41 PCT
75 41 COMMA
75 41 SLASH
75 41 LE
75 41 NE
75 41 SEMI
75 41 GT
75 41 PLUS
75 41 MINUS
85 50 LE
85 50 SLASH
85 50 EQ
85 50 GE
85 50 STAR
85 50 GT
85 50 RPAREN
85 50 BECOMES
85 50 LT
85 50 MINUS
85 50 PLUS
85 50 SEMI
85 50 NE
85 50 COMMA
85 50 RBRACK
85 50 PCT
86 12 DELETE
86 12 WHILE
86 12 LONG
86 12 PUTCHAR
86 12 PRINTLN
86 12 IF
86 12 ID
86 12 STAR
86 12 RETURN
86 12 LPAREN
87 13 DELETE
87 13 WHILE
87 13 LONG
87 13 PUTCHAR
87 13 PRINTLN
87 13 IF
87 13 ID
87 13 STAR
87 13 RETURN
87 13 LPAREN
89 3 LONG
90 30 COMMA
90 30 NE
90 30 RPAREN
90 30 RBRACK
90 30 SEMI
90 30 GT
90 30 MINUS
90 30 PLUS
90 30 GE
90 30 LE
90 30 LT
90 30 EQ
91 31 COMMA
91 31 NE
91 31 RPAREN
91 31 RBRACK
91 31 SEMI
91 31 GT
91 31 MINUS
91 31 PLUS
91 31 GE
91 31 LE
91 31 LT
91 31 EQ
92 33 LE
92 33 SLASH
92 33 GT
92 33 MINUS
92 33 PLUS
92 33 SEMI
92 33 NE
92 33 COMMA
92 33 RPAREN
92 33 RBRACK
92 33 PCT
92 33 STAR
92 33 GE
92 33 EQ
92 33 LT
93 34 LE
93 34 SLASH
93 34 GT
93 34 MINUS
93 34 PLUS
93 34 SEMI
93 34 NE
93 34 COMMA
93 34 RPAREN
93 34 RBRACK
93 34 PCT
93 34 STAR
93 34 GE
93 34 EQ
93 34 LT
94 35 LE
94 35 SLASH
94 35 GT
94 35 MINUS
94 35 PLUS
94 35 SEMI
94 35 NE
94 35 COMMA
94 35 RPAREN
94 35 RBRACK
94 35 PCT
94 35 STAR
94 35 GE
94 35 EQ
94 35 LT
95 46 RPAREN
96 43 BECOMES
96 43 GE
96 43 STAR
96 43 LT
96 43 EQ
96 43 RPAREN
96 43 RBRACK
96 43 PCT
96 43 COMMA
96 43 SLASH
96 43 LE
96 43 NE
96 43 SEMI
96 43 GT
96 43 PLUS
96 43 MINUS
98 39 BECOMES
98 39 GE
98 39 STAR
98 39 LT
98 39 EQ
98 39 RPAREN
98 39 RBRACK
98 39 PCT
98 39 COMMA
98 39 SLASH
98 39 LE
98 39 NE
98 39 SEMI
98 39 GT
98 39 PLUS
98 39 MINUS
100 45 BECOMES
100 45 GE
100 45 STAR
100 45 LT
100 45 EQ
100 45 RPAREN
100 45 RBRACK
100 45 PCT
100 45 COMMA
100 45 SLASH
100 45 LE
100 45 NE
100 45 SEMI
100 45 GT
100 45 PLUS
100 45 MINUS
101 17 DELETE
101 17 WHILE
101 17 PUTCHAR
101 17 PRINTLN
101 17 IF
101 17 ID
101 17 LPAREN
101 17 STAR
101 17 RETURN
101 17 RBRACE
115 44 BECOMES
115 44 GE
115 44 STAR
115 44 LT
115 44 EQ
115 44 RPAREN
115 44 RBRACK
115 44 PCT
115 44 COMMA
115 44 SLASH
115 44 LE
115 44 NE
115 44 SEMI
115 44 GT
115 44 PLUS
115 44 MINUS
117 15 RETURN
117 15 STAR
117 15 RBRACE
117 15 LPAREN
117 15 ID
117 15 IF
117 15 PRINTLN
117 15 PUTCHAR
117 15 WHILE
117 15 DELETE
118 23 RPAREN
119 24 RPAREN
120 25 RPAREN
121 26 RPAREN
122 27 RPAREN
123 28 RPAREN
124 15 RETURN
124 15 STAR
124 15 RBRACE
124 15 LPAREN
124 15 ID
124 15 IF
124 15 PRINTLN
124 15 PUTCHAR
124 15 WHILE
124 15 DELETE
125 20 DELETE
125 20 WHILE
125 20 PUTCHAR
125 20 PRINTLN
125 20 IF
125 20 ID
125 20 LPAREN
125 20 STAR
125 20 RETURN
125 20 RBRACE
126 21 DELETE
126 21 WHILE
126 21 PUTCHAR
126 21 PRINTLN
126 21 IF
126 21 ID
126 21 LPAREN
126 21 STAR
126 21 RETURN
126 21 RBRACE
127 22 DELETE
127 22 WHILE
127 22 PUTCHAR
127 22 PRINTLN
127 22 IF
127 22 ID
127 22 LPAREN
127 22 STAR
127 22 RETURN
127 22 RBRACE
129 47 RPAREN
130 42 BECOMES
130 42 GE
130 42 STAR
130 42 LT
130 42 EQ
130 42 RPAREN
130 42 RBRACK
130 42 PCT
130 42 COMMA
130 42 SLASH
130 42 LE
130 42 NE
130 42 SEMI
130 42 GT
130 42 PLUS
130 42 MINUS
133 4 EOF
135 19 DELETE
135 19 WHILE
135 19 PUTCHAR
135 19 PRINTLN
135 19 IF
135 19 ID
135 19 LPAREN
135 19 STAR
135 19 RETURN
135 19 RBRACE
137 15 RETURN
137 15 STAR
137 15 RBRACE
137 15 LPAREN
137 15 ID
137 15 IF
137 15 PRINTLN
137 15 PUTCHAR
137 15 WHILE
137 15 DELETE
139 18 DELETE
139 18 WHILE
139 18 PUTCHAR
139 18 PRINTLN
139 18 IF
139 18 ID
139 18 LPAREN
139 18 STAR
139 18 RETURN
139 18 RBRACE
)END";

const std::string WLP4_COMBINED = WLP4_CFG+WLP4_TRANSITIONS+WLP4_REDUCTIONS+".END\n";


struct Rule {
  string lhs;
  vector<string> rhs;
};

struct TreeNode {
  virtual ~TreeNode() = default;
  virtual void printPreorder() const = 0;
};

struct LeafNode : TreeNode {
  string kind;
  string lexeme;
  LeafNode(string k, string l) : kind(std::move(k)), lexeme(std::move(l)) {}
  void printPreorder() const override {
    cout << kind << " " << lexeme << "\n";
  }
};

struct InternalNode : TreeNode {
  string lhs;
  vector<string> rhs;
  vector<unique_ptr<TreeNode>> children;
  InternalNode(string l, vector<string> r, vector<unique_ptr<TreeNode>> c)
      : lhs(std::move(l)), rhs(std::move(r)), children(std::move(c)) {}
  void printPreorder() const override {
    cout << lhs;
    for (const auto& s : rhs) cout << " " << s;
    cout << "\n";
    for (const auto& ch : children) ch->printPreorder();
  }
};

static bool isHeaderLine(const string& line) {
  for (char c : line) {
    if (!isspace(static_cast<unsigned char>(c))) return c == '.';
  }
  return false;
}

static vector<string> splitWs(const string& line) {
  istringstream iss(line);
  vector<string> out;
  for (string s; iss >> s;) out.push_back(s);
  return out;
}

int main() {
  cin.tie(nullptr);

  vector<Rule> rules;
  map<pair<int, string>, int> transitions;
  map<pair<int, string>, int> reductions;

  {
    vector<string> cfgLines;
    size_t i = 0;
    istringstream cfgStream(WLP4_CFG);
    string line;
    while (getline(cfgStream, line)) cfgLines.push_back(line);
    while (i < cfgLines.size() && splitWs(cfgLines[i]).empty()) ++i;
    if (i < cfgLines.size()) {
      auto h = splitWs(cfgLines[i]);
      if (h.size() == 1 && h[0] == ".CFG") ++i;
    }
    while (i < cfgLines.size() && !isHeaderLine(cfgLines[i])) {
      auto toks = splitWs(cfgLines[i]);
      ++i;
      if (toks.empty()) continue;
      Rule r;
      r.lhs = toks[0];
      if (toks.size() >= 2 && toks[1] == ".EMPTY") {
        r.rhs.assign(1, ".EMPTY");
      } else {
        for (size_t k = 1; k < toks.size(); ++k) r.rhs.push_back(toks[k]);
      }
      rules.push_back(std::move(r));
    }
  }

  {
    istringstream trStream(WLP4_TRANSITIONS);
    string line;
    while (getline(trStream, line)) {
      auto toks = splitWs(line);
      if (toks.size() < 3) continue;
      if (toks[0][0] == '.') continue;
      int fromState = stoi(toks[0]);
      string symbol = toks[1];
      int toState = stoi(toks[2]);
      transitions[{fromState, symbol}] = toState;
    }
  }

  {
    istringstream redStream(WLP4_REDUCTIONS);
    string line;
    while (getline(redStream, line)) {
      auto toks = splitWs(line);
      if (toks.size() < 3) continue;
      if (toks[0][0] == '.') continue;
      int stateNum = stoi(toks[0]);
      int ruleNum = stoi(toks[1]);
      string tag = toks[2];
      reductions[{stateNum, tag}] = ruleNum;
    }
  }

  deque<pair<string, string>> inputTokens;
  for (string line; getline(cin, line);) {
    auto toks = splitWs(line);
    if (toks.empty()) continue;
    string kind = toks[0];
    string lexeme = toks.size() >= 2 ? toks[1] : "";
    inputTokens.push_back({kind, lexeme});
  }

  deque<string> input;
  deque<pair<string, string>> inputWithLexeme;
  input.push_back("BOF");
  inputWithLexeme.push_back({"BOF", "BOF"});
  for (const auto& p : inputTokens) {
    input.push_back(p.first);
    inputWithLexeme.push_back(p);
  }
  input.push_back("EOF");
  inputWithLexeme.push_back({"EOF", "EOF"});

  vector<int> stateStack;
  stateStack.push_back(0);
  vector<unique_ptr<TreeNode>> treeStack;
  int numRealTokensShifted = 0;

  for (;;) {
    int curState = stateStack.back();
    string lookahead = input.empty() ? ".ACCEPT" : input.front();

    auto itRed = reductions.find({curState, lookahead});
    if (itRed != reductions.end()) {
      int ruleNum = itRed->second;
      const Rule& r = rules.at(static_cast<size_t>(ruleNum));
      size_t nPop = (r.rhs.size() == 1 && r.rhs[0] == ".EMPTY") ? 0 : r.rhs.size();
      vector<unique_ptr<TreeNode>> children;
      for (size_t k = 0; k < nPop; ++k) {
        stateStack.pop_back();
        children.insert(children.begin(), std::move(treeStack.back()));
        treeStack.pop_back();
      }
      treeStack.push_back(make_unique<InternalNode>(r.lhs, r.rhs, std::move(children)));
      stateStack.push_back(transitions[{stateStack.back(), r.lhs}]);
      if (ruleNum == 0 && lookahead == ".ACCEPT") break;
      continue;
    }

    if (input.empty()) {
      cerr << "ERROR at " << (numRealTokensShifted + 1) << "\n";
      return 1;
    }
    string sym = input.front();
    auto itTrans = transitions.find({curState, sym});
    if (itTrans != transitions.end()) {
      input.pop_front();
      auto tok = inputWithLexeme.front();
      inputWithLexeme.pop_front();
      if (sym != "BOF" && sym != "EOF") ++numRealTokensShifted;
      treeStack.push_back(make_unique<LeafNode>(tok.first, tok.second));
      stateStack.push_back(itTrans->second);
      continue;
    }

    cerr << "ERROR at " << (numRealTokensShifted + 1) << "\n";
    return 1;
  }

  if (!treeStack.empty()) {
    TreeNode* root = treeStack.back().get();
    auto* internal = dynamic_cast<InternalNode*>(root);
    if (!internal) {
      root->printPreorder();
      return 0;
    }
    if (internal->children.size() == 3 &&
        dynamic_cast<LeafNode*>(internal->children[0].get()) &&
        dynamic_cast<LeafNode*>(internal->children[2].get())) {
      // children[1] should be the real grammar root (procedures)
      internal->children[1]->printPreorder();
    } else {
      internal->printPreorder();
    }
  }
  return 0;
}
