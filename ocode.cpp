
#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#define OCODE_MKDIR(p) _mkdir(p)
#else
#define OCODE_MKDIR(p) mkdir(p, 0755)
#endif

#ifndef OCODE_NO_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif
using namespace std;

struct OCodeError : public runtime_error {
    int line;
    OCodeError(const string& msg, int line_) : runtime_error(msg), line(line_) {}
};
[[noreturn]] void fail(const string& msg, int line) { throw OCodeError(msg, line); }

enum class TokType {
    NUMBER, STRING, IDENT, NEWLINE, PYTHON_BLOCK,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE, COMMA, COLON,
    GT, LT, GE, LE, EQ, NE, ASSIGN,
    END_OF_FILE
};

struct Token {
    TokType type;
    string text;
    double num = 0;
    int line = 0;
};

struct Lexer {
    string src;
    size_t pos = 0;
    int line = 1;
    vector<Token> tokens;

    Lexer(string s) : src(move(s)) {}

    int peekc(int off = 0) {
        size_t p = pos + off;
        return (p < src.size()) ? (unsigned char)src[p] : -1;
    }
    int getc_() {
        if (pos >= src.size()) return -1;
        int c = (unsigned char)src[pos++];
        if (c == '\n') line++;
        return c;
    }
    void addTok(TokType t, const string& text = "") {
        tokens.push_back({t, text, 0, line});
    }

    vector<Token> tokenize() {
        while (true) {
            int c = peekc();
            if (c == -1) break;
            if (c == ' ' || c == '\t' || c == '\r') { getc_(); continue; }
            if (c == '\n') { getc_(); addTok(TokType::NEWLINE); continue; }

            if (c == '#') { while (peekc() != -1 && peekc() != '\n') getc_(); continue; }
            if (c == '/' && peekc(1) == '/') { while (peekc() != -1 && peekc() != '\n') getc_(); continue; }

            if (c == '"') {
                getc_();
                string s;
                while (peekc() != -1 && peekc() != '"') {
                    int ch = getc_();
                    if (ch == '\\' && peekc() != -1) {
                        int esc = getc_();
                        switch (esc) {
                            case 'n': s += '\n'; break;
                            case 't': s += '\t'; break;
                            case 'r': s += '\r'; break;
                            case '"': s += '"'; break;
                            case '\\': s += '\\'; break;
                            default: s += (char)esc;
                        }
                    } else s += (char)ch;
                }
                if (peekc() == '"') getc_(); else fail("unterminated string", line);
                tokens.push_back({TokType::STRING, s, 0, line});
                continue;
            }

            if (isdigit(c) || (c == '.' && isdigit(peekc(1)))) {
                string s;
                while (isdigit(peekc())) s += (char)getc_();
                if (peekc() == '.' && isdigit(peekc(1))) {
                    s += (char)getc_();
                    while (isdigit(peekc())) s += (char)getc_();
                }
                tokens.push_back({TokType::NUMBER, s, stod(s), line});
                continue;
            }

            if (isalpha(c) || c == '_') {
                string s;
                while (isalnum(peekc()) || peekc() == '_') s += (char)getc_();

                if (s == "pyoc") {
                    size_t savePos = pos;
                    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t')) pos++;
                    bool isBlock = (pos >= src.size() || src[pos]=='\n' || src[pos]=='\r');
                    pos = savePos;
                    if (isBlock) {

                        while (pos < src.size() && src[pos] != '\n') pos++;
                        if (pos < src.size()) { pos++; line++; }
                        int startLine = line;
                        string pyCode;

                        while (pos < src.size()) {
                            string curLine;
                            while (pos < src.size() && src[pos] != '\n')
                                curLine += src[pos++];
                            if (pos < src.size()) { pos++; line++; }

                            string trimmed = curLine;
                            auto ta = trimmed.find_first_not_of(" \t\r");
                            auto tb = trimmed.find_last_not_of(" \t\r");
                            if (ta != string::npos) trimmed = trimmed.substr(ta, tb-ta+1);
                            else trimmed = "";
                            if (trimmed == "end") break;
                            if (!pyCode.empty()) pyCode += "\n";
                            pyCode += curLine;
                        }
                        tokens.push_back({TokType::PYTHON_BLOCK, pyCode, 0, startLine});
                        continue;
                    }
                }

                tokens.push_back({TokType::IDENT, s, 0, line});
                continue;
            }

            int ln = line;
            switch (c) {
                case '+': getc_(); addTok(TokType::PLUS, "+"); continue;
                case '-': getc_(); addTok(TokType::MINUS, "-"); continue;
                case '*': getc_(); addTok(TokType::STAR, "*"); continue;
                case '/': getc_(); addTok(TokType::SLASH, "/"); continue;
                case '%': getc_(); addTok(TokType::PERCENT, "%"); continue;
                case '(': getc_(); addTok(TokType::LPAREN, "("); continue;
                case ')': getc_(); addTok(TokType::RPAREN, ")"); continue;
                case '[': getc_(); addTok(TokType::LBRACKET, "["); continue;
                case ']': getc_(); addTok(TokType::RBRACKET, "]"); continue;
                case ',': getc_(); addTok(TokType::COMMA, ","); continue;
                case '{': getc_(); addTok(TokType::LBRACE, "{"); continue;
                case '}': getc_(); addTok(TokType::RBRACE, "}"); continue;
                case ':': getc_(); addTok(TokType::COLON, ":"); continue;
                case '>': getc_();
                    if (peekc() == '=') { getc_(); addTok(TokType::GE, ">="); }
                    else addTok(TokType::GT, ">");
                    continue;
                case '<': getc_();
                    if (peekc() == '=') { getc_(); addTok(TokType::LE, "<="); }
                    else addTok(TokType::LT, "<");
                    continue;
                case '=': getc_();
                    if (peekc() == '=') { getc_(); addTok(TokType::EQ, "=="); }
                    else addTok(TokType::ASSIGN, "=");
                    continue;
                case '!': getc_();
                    if (peekc() == '=') { getc_(); addTok(TokType::NE, "!="); continue; }
                    fail("unexpected '!'", ln);
                default:
                    fail(string("unexpected character '") + (char)c + "'", ln);
            }
        }
        tokens.push_back({TokType::END_OF_FILE, "", 0, line});
        return tokens;
    }
};

struct Expr; using ExprPtr = shared_ptr<Expr>;
struct Stmt; using StmtPtr = shared_ptr<Stmt>;

enum class ExprKind {
    NUMBER, STRING, BOOL, LIST, DICT, VAR, BINOP, UNARY, INDEX, LENGTH, CALL, BUILTIN
};

struct Expr {
    ExprKind kind;
    int line = 0;
    double num = 0;
    string str;
    bool bl = false;
    string op;
    string name;
    vector<ExprPtr> items;
    vector<pair<ExprPtr, ExprPtr>> pairs;
    ExprPtr left, right;
};

enum class StmtKind {
    LET, SAY, SAY_INLINE, IF, WHILE, FOR_EACH, REPEAT,
    FUNC_DEF, RETURN, ADD_TO, REMOVE_AT, EXPR_STMT, BREAK, SKIP,
    USE, PYTHON_BLOCK, TRY, SET_AT
};

struct Stmt {
    StmtKind kind;
    int line = 0;
    string name;
    string iterVar;
    string iterVar2;
    ExprPtr expr;
    ExprPtr value;
    vector<StmtPtr> body;
    vector<StmtPtr> elseBody;
    vector<pair<ExprPtr,vector<StmtPtr>>> elseIfs;
    vector<string> params;
};

static const set<string> KEYWORDS = {
    "let","be","say","say_inline","if","else","end","while","for","each",
    "in","define","with","return","repeat","times","add","to","remove",
    "at","length","of","is","not","less","greater","than","equal","and",
    "or","true","false","break","skip","mod","remainder","ask","divided",
    "by","plus","minus","uppercase","lowercase","trim","split","join",
    "contains","replace","index","slice","sort","reverse","abs","round",
    "floor","ceil","sqrt","power","min","max","random","to_number",
    "to_string","to_bool","is_number","is_string","is_bool","is_list",
    "chr","ord",
    "try","catch","range","step","keys","has","dict",
    "now","today","clock","sleep",
    "parse_json","to_json",
    "use","install","pyoc",
    "http_get","http_post","http_request",
    "fetch",
    "socket","send","receive","readline","close",
    "ssl_socket","ssl_send","ssl_receive","ssl_readline","ssl_close","sslsay",
    "ws_connect","ws_send","ws_send_binary","ws_recv","ws_close","wssay","ws_ping",
    "read","readlines","write","append","ls","exists","mkdir","rm",
    "ws_ping",
    "band",
    "bor",
    "bxor",
    "bnot",
    "bshl",
    "bshr",
    "bigint",
    "bigint_add",
    "bigint_sub",
    "bigint_mul",
    "bigint_cmp",
    "bigint_band",
    "bigint_bor",
    "bigint_bxor",
    "bigint_shl",
    "bigint_shr",
    "time_iso_after",
    "time_iso_at",
    "date_now",
    "date_parse",
    "date_format",
    "date_add",
    "date_diff",
    "db_open",
    "db_exec",
    "db_query",
    "db_exec_params",
    "db_query_params",
    "db_close",
    "atomic_write",
    "lock_file",
    "unlock_file",
    "sign",
    "gcd",
    "lcm",
    "trunc",
    "clamp",
    "popcount",
    "bit_length",
    "bigint_div",
    "bigint_mod",
    "bigint_pow",
    "bigint_to_hex",
    "bigint_from_hex",
    "starts_with",
    "ends_with",
    "count",
    "pad_left",
    "pad_right",
    "format",
    "capitalize",
    "title_case",
    "replace_all",
    "code_point_at",
    "from_code_point",
    "shift",
    "unshift",
    "insert_at",
    "remove_at",
    "extend",
    "map",
    "filter",
    "find",
    "reduce",
    "list_contains",
    "unique",
    "flatten",
    "chunk",
    "zip",
    "enumerate",
    "sum_of",
    "product_of",
    "min_of",
    "max_of",
    "count_of",
    "sort_by",
    "sort_desc",
    "values",
    "items",
    "remove_key",
    "merge",
    "type_of",
    "is_dict",
    "is_function",
    "is_empty",
    "is_file",
    "is_dir",
    "to_json_pretty",
    "date_parts",
    "date_utc_now",
    "timezone_offset",
    "timer_start",
    "timer_stop",
    "date_to_iso",
    "date_from_iso",
    "copy_file",
    "move_file",
    "stat",
    "read_binary",
    "write_binary",
    "glob",
    "temp_file",
    "temp_dir",
    "chmod",
    "cwd",
    "chdir",
    "udp_socket",
    "server_socket",
    "accept",
    "resolve_host",
    "set_socket_timeout",
    "http_download",
    "http_upload",
    "http_set_headers",
    "http_set_timeout",
    "db_begin",
    "db_commit",
    "db_rollback",
    "db_last_insert_id",
    "db_changes",
    "db_prepare",
    "db_step",
    "db_finalize",
    "env",
    "shell",
    "platform",
    "pid",
    "memory_used",
    "uptime",
    "hostname",
    "random_int",
    "random_choice",
    "random_shuffle",
    "random_seed",
    "thread_start",
    "thread_wait",
    "channel_new",
    "channel_send",
    "channel_recv",
    "mutex_new",
    "mutex_lock",
    "mutex_unlock",
    "eval_ocode",
    "source_line",
    "callable",
    "get_var",
    "set_var",
    "raise",
    "error_message",
    "error_code",
    "encode_base64",
    "decode_base64",
    "hash_sha256",
    "hmac_sha256",
    "url_encode",
    "url_decode",
    "hash_md5",
    "hash_sha1",
    "hash_sha512",
    "save_var",
    "load_var",
    "saved_vars",
    "clear_saved_var"
};

struct BreakSignal {};
struct SkipSignal {};

struct Parser {
    vector<Token> toks;
    size_t pos = 0;

    Parser(vector<Token> t) : toks(move(t)) {}

    Token& cur() { return toks[pos]; }
    Token& peek(int off = 1) { size_t p = pos+off; return p < toks.size() ? toks[p] : toks.back(); }
    Token& advance() { Token& t = toks[pos]; if (pos+1 < toks.size()) pos++; return t; }
    bool check(TokType t) { return cur().type == t; }
    bool checkIdent(const string& s) { return cur().type == TokType::IDENT && cur().text == s; }
    bool matchIdent(const string& s) { if (checkIdent(s)) { advance(); return true; } return false; }
    Token expect(TokType t, const string& what) {
        if (cur().type != t) fail("expected " + what + " but got '" + cur().text + "'", cur().line);
        return advance();
    }
    void expectIdent(const string& s) {
        if (!checkIdent(s)) fail("expected '" + s + "' but got '" + cur().text + "'", cur().line);
        advance();
    }
    void skipNewlines() { while (check(TokType::NEWLINE)) advance(); }
    bool atBlockEnd() {
        return checkIdent("end") || checkIdent("else") || checkIdent("catch") || check(TokType::END_OF_FILE);
    }

    vector<StmtPtr> parseProgram() {
        vector<StmtPtr> stmts;
        skipNewlines();
        while (!check(TokType::END_OF_FILE)) {
            stmts.push_back(parseStatement());
            skipNewlines();
        }
        return stmts;
    }

    vector<StmtPtr> parseBlock() {
        vector<StmtPtr> stmts;
        skipNewlines();
        while (!atBlockEnd()) {
            stmts.push_back(parseStatement());
            skipNewlines();
        }
        return stmts;
    }

    StmtPtr parseStatement() {
        int ln = cur().line;
        if (checkIdent("let")) return parseLet();
        if (checkIdent("say_inline")) return parseSayInline();
        if (checkIdent("say")) return parseSay();
        if (checkIdent("if")) return parseIf();
        if (checkIdent("while")) return parseWhile();
        if (checkIdent("for")) return parseForEach();
        if (checkIdent("repeat")) return parseRepeat();
        if (checkIdent("define")) return parseFuncDef();
        if (checkIdent("return")) return parseReturn();
        if (checkIdent("add")) return parseAdd();
        if (checkIdent("remove")) return parseRemove();
        if (checkIdent("break")) { advance(); auto s = make_shared<Stmt>(); s->kind = StmtKind::BREAK; s->line = ln; return s; }
        if (checkIdent("skip"))  { advance(); auto s = make_shared<Stmt>(); s->kind = StmtKind::SKIP;  s->line = ln; return s; }
        if (checkIdent("use"))   { return parseUse(); }
        if (checkIdent("try"))   { return parseTry(); }

        if (check(TokType::PYTHON_BLOCK)) {
            int ln = cur().line; string code = advance().text;
            auto s = make_shared<Stmt>(); s->kind = StmtKind::PYTHON_BLOCK;
            s->line = ln; s->name = code;
            return s;
        }
        auto s = make_shared<Stmt>(); s->kind = StmtKind::EXPR_STMT; s->line = ln;
        s->expr = parseExpression();

        if (checkIdent("be")) {
            advance();
            if (s->expr->kind != ExprKind::INDEX)
                fail("'be' here can only mutate an indexed value (e.g. 'dict at key be value')", ln);
            s->kind = StmtKind::SET_AT;
            s->value = parseExpression();
        }
        return s;
    }

    StmtPtr parseLet() {
        int ln = cur().line; expectIdent("let");
        string name = expect(TokType::IDENT, "variable name").text;
        expectIdent("be");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::LET; s->line = ln; s->name = name; s->expr = parseExpression();
        return s;
    }
    StmtPtr parseSay() {
        int ln = cur().line; expectIdent("say");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::SAY; s->line = ln; s->expr = parseExpression();
        return s;
    }
    StmtPtr parseSayInline() {
        int ln = cur().line; expectIdent("say_inline");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::SAY_INLINE; s->line = ln; s->expr = parseExpression();
        return s;
    }

    StmtPtr parseIf() {
        int ln = cur().line; expectIdent("if");
        ExprPtr cond = parseExpression();
        auto s = make_shared<Stmt>(); s->kind = StmtKind::IF; s->line = ln; s->expr = cond;
        s->body = parseBlock();
        while (checkIdent("else") && peek().type == TokType::IDENT && peek().text == "if") {
            advance(); advance();
            ExprPtr eicond = parseExpression();
            auto eiBody = parseBlock();
            s->elseIfs.push_back({eicond, eiBody});
        }
        if (checkIdent("else")) { advance(); s->elseBody = parseBlock(); }
        expectIdent("end");
        return s;
    }

    StmtPtr parseWhile() {
        int ln = cur().line; expectIdent("while");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::WHILE; s->line = ln;
        s->expr = parseExpression(); s->body = parseBlock(); expectIdent("end");
        return s;
    }

    StmtPtr parseForEach() {
        int ln = cur().line; expectIdent("for"); expectIdent("each");
        string varName = expect(TokType::IDENT, "variable name").text;
        string varName2;
        if (check(TokType::COMMA)) {
            advance();
            varName2 = expect(TokType::IDENT, "second variable name").text;
        }
        expectIdent("in");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::FOR_EACH; s->line = ln;
        s->iterVar = varName; s->iterVar2 = varName2;
        s->expr = parseExpression(); s->body = parseBlock(); expectIdent("end");
        return s;
    }

    StmtPtr parseRepeat() {
        int ln = cur().line; expectIdent("repeat");
        ExprPtr count = parseAdditive(); expectIdent("times");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::REPEAT; s->line = ln;
        s->expr = count; s->body = parseBlock(); expectIdent("end");
        return s;
    }

    StmtPtr parseFuncDef() {
        int ln = cur().line; expectIdent("define");
        string name = expect(TokType::IDENT, "function name").text;
        vector<string> params;
        if (matchIdent("with")) {
            params.push_back(expect(TokType::IDENT, "parameter name").text);
            while (check(TokType::COMMA)) { advance(); params.push_back(expect(TokType::IDENT, "parameter name").text); }
        }
        auto s = make_shared<Stmt>(); s->kind = StmtKind::FUNC_DEF; s->line = ln;
        s->name = name; s->params = params; s->body = parseBlock(); expectIdent("end");
        return s;
    }

    StmtPtr parseReturn() {
        int ln = cur().line; expectIdent("return");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::RETURN; s->line = ln;
        if (!check(TokType::NEWLINE) && !atBlockEnd()) s->expr = parseExpression();
        return s;
    }

    StmtPtr parseAdd() {
        int ln = cur().line; expectIdent("add");
        ExprPtr val = parseExpression(); expectIdent("to");
        string name = expect(TokType::IDENT, "list name").text;
        auto s = make_shared<Stmt>(); s->kind = StmtKind::ADD_TO; s->line = ln; s->name = name; s->expr = val;
        return s;
    }

    StmtPtr parseRemove() {
        int ln = cur().line; expectIdent("remove"); expectIdent("at");
        ExprPtr idx = parseExpression(); expectIdent("from");
        string name = expect(TokType::IDENT, "list name").text;
        auto s = make_shared<Stmt>(); s->kind = StmtKind::REMOVE_AT; s->line = ln; s->name = name; s->expr = idx;
        return s;
    }

    StmtPtr parseUse() {
        int ln = cur().line; expectIdent("use");
        string name = expect(TokType::IDENT, "package name").text;
        auto s = make_shared<Stmt>(); s->kind = StmtKind::USE; s->line = ln; s->name = name;
        return s;
    }

    StmtPtr parseTry() {
        int ln = cur().line; expectIdent("try");
        auto s = make_shared<Stmt>(); s->kind = StmtKind::TRY; s->line = ln;
        s->body = parseBlock();
        if (checkIdent("catch")) {
            advance();
            s->iterVar = expect(TokType::IDENT, "error variable name").text;
            s->elseBody = parseBlock();
        }
        expectIdent("end");
        return s;
    }

    ExprPtr parseExpression() { return parseOr(); }

    ExprPtr parseOr() {
        ExprPtr left = parseAnd();
        while (checkIdent("or")) {
            int ln = advance().line; ExprPtr right = parseAnd();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "or"; e->left = left; e->right = right; e->line = ln;
            left = e;
        }
        return left;
    }

    ExprPtr parseAnd() {
        ExprPtr left = parseComparison();
        while (checkIdent("and")) {
            int ln = advance().line; ExprPtr right = parseComparison();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "and"; e->left = left; e->right = right; e->line = ln;
            left = e;
        }
        return left;
    }

    string tryParseComparisonOp() {
        if (check(TokType::GT))  { advance(); return ">"; }
        if (check(TokType::LT))  { advance(); return "<"; }
        if (check(TokType::GE))  { advance(); return ">="; }
        if (check(TokType::LE))  { advance(); return "<="; }
        if (check(TokType::EQ))  { advance(); return "=="; }
        if (check(TokType::NE))  { advance(); return "!="; }
        if (checkIdent("is")) {
            size_t save = pos; advance();
            bool neg = false;
            if (checkIdent("not")) { neg = true; advance(); }
            if (checkIdent("less"))    { advance(); expectIdent("than"); return neg ? ">=" : "<"; }
            if (checkIdent("greater")) { advance(); expectIdent("than"); return neg ? "<=" : ">"; }
            if (checkIdent("equal"))   { advance(); expectIdent("to");   return neg ? "!=" : "=="; }
            pos = save;
        }
        return "";
    }

    ExprPtr parseComparison() {
        ExprPtr left = parseAdditive();
        string op = tryParseComparisonOp();
        if (!op.empty()) {
            ExprPtr right = parseAdditive();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = op; e->left = left; e->right = right; e->line = left->line;
            return e;
        }
        return left;
    }

    ExprPtr parseAdditive() {
        ExprPtr left = parseMultiplicative();
        while (true) {
            if (check(TokType::PLUS) || checkIdent("plus")) {
                int ln = advance().line; ExprPtr right = parseMultiplicative();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "+"; e->left = left; e->right = right; e->line = ln;
                left = e;
            } else if (check(TokType::MINUS) || checkIdent("minus")) {
                int ln = advance().line; ExprPtr right = parseMultiplicative();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "-"; e->left = left; e->right = right; e->line = ln;
                left = e;
            } else break;
        }
        return left;
    }

    ExprPtr parseMultiplicative() {
        ExprPtr left = parseUnary();
        while (true) {
            if (check(TokType::STAR)) {
                int ln = advance().line; ExprPtr right = parseUnary();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "*"; e->left = left; e->right = right; e->line = ln;
                left = e;
            } else if (check(TokType::SLASH) || checkIdent("divided")) {
                int ln = cur().line;
                if (checkIdent("divided")) { advance(); expectIdent("by"); } else advance();
                ExprPtr right = parseUnary();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "/"; e->left = left; e->right = right; e->line = ln;
                left = e;
            } else if (check(TokType::PERCENT) || checkIdent("mod") || checkIdent("remainder")) {
                int ln = cur().line; advance();
                ExprPtr right = parseUnary();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BINOP; e->op = "%"; e->left = left; e->right = right; e->line = ln;
                left = e;
            } else break;
        }
        return left;
    }

    ExprPtr parseUnary() {
        if (check(TokType::MINUS)) {
            int ln = advance().line; ExprPtr op = parseUnary();
            auto e = make_shared<Expr>(); e->kind = ExprKind::UNARY; e->op = "-"; e->right = op; e->line = ln; return e;
        }
        if (checkIdent("not")) {
            int ln = advance().line; ExprPtr op = parseUnary();
            auto e = make_shared<Expr>(); e->kind = ExprKind::UNARY; e->op = "not"; e->right = op; e->line = ln; return e;
        }
        return parsePostfix();
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        while (true) {
            if (checkIdent("at")) {
                int ln = advance().line; ExprPtr idx = parseAdditive();
                auto n = make_shared<Expr>(); n->kind = ExprKind::INDEX; n->left = e; n->right = idx; n->line = ln;
                e = n;
            } else if (checkIdent("with") && e->kind == ExprKind::VAR) {
                advance();
                auto n = make_shared<Expr>(); n->kind = ExprKind::CALL; n->name = e->name; n->line = e->line;
                n->items.push_back(parseExpression());
                while (check(TokType::COMMA)) { advance(); n->items.push_back(parseExpression()); }
                e = n;
            } else break;
        }
        return e;
    }

    ExprPtr parsePrimary() {
        int ln = cur().line;

        if (check(TokType::NUMBER)) {
            auto e = make_shared<Expr>(); e->kind = ExprKind::NUMBER; e->num = advance().num; e->line = ln; return e;
        }
        if (check(TokType::STRING)) {
            auto e = make_shared<Expr>(); e->kind = ExprKind::STRING; e->str = advance().text; e->line = ln; return e;
        }
        if (checkIdent("true"))  { advance(); auto e = make_shared<Expr>(); e->kind = ExprKind::BOOL; e->bl = true;  e->line = ln; return e; }
        if (checkIdent("false")) { advance(); auto e = make_shared<Expr>(); e->kind = ExprKind::BOOL; e->bl = false; e->line = ln; return e; }

        if (checkIdent("length")) {
            advance(); expectIdent("of"); ExprPtr list = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::LENGTH; e->right = list; e->line = ln; return e;
        }

        if (checkIdent("ask")) {
            advance(); ExprPtr prompt = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ask"; e->items.push_back(prompt); e->line = ln; return e;
        }

        if (checkIdent("uppercase")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "uppercase"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("lowercase")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "lowercase"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("trim")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "trim"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("split")) {
            advance(); ExprPtr s = parsePostfix(); expectIdent("by"); ExprPtr sep = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "split"; e->items.push_back(s); e->items.push_back(sep); e->line = ln; return e;
        }

        if (checkIdent("join")) {
            advance(); ExprPtr lst = parsePostfix(); expectIdent("by"); ExprPtr sep = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "join"; e->items.push_back(lst); e->items.push_back(sep); e->line = ln; return e;
        }

        if (checkIdent("contains")) {
            advance(); ExprPtr container = parsePostfix(); ExprPtr item = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "contains"; e->items.push_back(container); e->items.push_back(item); e->line = ln; return e;
        }

        if (checkIdent("replace")) {
            advance(); expectIdent("in"); ExprPtr str = parsePostfix();
            expectIdent("from"); ExprPtr oldv = parsePostfix();
            expectIdent("to"); ExprPtr newv = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "replace"; e->items = {str,oldv,newv}; e->line = ln; return e;
        }

        if (checkIdent("index")) {
            advance(); expectIdent("of"); ExprPtr item = parsePostfix(); expectIdent("in"); ExprPtr container = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "index_of"; e->items = {item,container}; e->line = ln; return e;
        }

        if (checkIdent("slice")) {
            advance(); ExprPtr lst = parsePostfix(); expectIdent("from"); ExprPtr start = parsePostfix(); expectIdent("to"); ExprPtr end = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "slice"; e->items = {lst,start,end}; e->line = ln; return e;
        }

        if (checkIdent("sort")) {
            advance(); ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "sort"; e->items.push_back(lst); e->line = ln; return e;
        }

        if (checkIdent("reverse")) {
            advance(); ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "reverse"; e->items.push_back(lst); e->line = ln; return e;
        }

        if (checkIdent("abs")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "abs"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("round")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "round"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("floor")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "floor"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("ceil")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ceil"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("sqrt")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "sqrt"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("power")) {
            advance(); ExprPtr base = parsePostfix(); expectIdent("to"); ExprPtr exp = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "power"; e->items = {base,exp}; e->line = ln; return e;
        }

        if (checkIdent("min")) {
            advance(); expectIdent("of"); ExprPtr a = parsePostfix();
            if (checkIdent("and")) { advance(); ExprPtr b = parsePostfix(); auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "min2"; e->items = {a,b}; e->line = ln; return e; }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "min1"; e->items = {a}; e->line = ln; return e;
        }

        if (checkIdent("max")) {
            advance(); expectIdent("of"); ExprPtr a = parsePostfix();
            if (checkIdent("and")) { advance(); ExprPtr b = parsePostfix(); auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "max2"; e->items = {a,b}; e->line = ln; return e; }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "max1"; e->items = {a}; e->line = ln; return e;
        }

        if (checkIdent("random")) {
            advance();
            if (checkIdent("from")) {
                advance(); ExprPtr a = parsePostfix(); expectIdent("to"); ExprPtr b = parsePostfix();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "random2"; e->items = {a,b}; e->line = ln; return e;
            }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "random0"; e->line = ln; return e;
        }

        if (checkIdent("to_number")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "to_number"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("to_string")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "to_string"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("to_bool")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "to_bool"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("chr")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "chr"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("ord")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ord"; e->items.push_back(arg); e->line = ln; return e;
        }

        for (auto& bi : {"is_number","is_string","is_bool","is_list"}) {
            if (checkIdent(bi)) {
                string name = bi; advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
                auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name; e->items.push_back(arg); e->line = ln; return e;
            }
        }

        if (checkIdent("keys")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "keys"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("has")) {
            advance(); ExprPtr key = parsePostfix(); expectIdent("in"); ExprPtr cont = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "has"; e->items = {key, cont}; e->line = ln; return e;
        }

        if (checkIdent("range")) {
            advance();
            ExprPtr start = parsePostfix(); expectIdent("to");
            ExprPtr end_  = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "range";
            e->items = {start, end_}; e->line = ln;
            if (checkIdent("step")) { advance(); e->items.push_back(parsePostfix()); }
            return e;
        }

        if (checkIdent("now"))   { advance(); auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "now";   e->line = ln; return e; }
        if (checkIdent("today")) { advance(); auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "today"; e->line = ln; return e; }
        if (checkIdent("clock")) { advance(); auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "clock"; e->line = ln; return e; }
        if (checkIdent("sleep")) {
            advance(); ExprPtr n = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "sleep";
            e->items.push_back(n); e->line = ln; return e;
        }

        if (checkIdent("parse_json")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "parse_json"; e->items.push_back(arg); e->line = ln; return e;
        }
        if (checkIdent("to_json")) {
            advance(); expectIdent("of"); ExprPtr arg = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "to_json"; e->items.push_back(arg); e->line = ln; return e;
        }

        if (checkIdent("dict")) {
            advance();
            auto e = make_shared<Expr>(); e->kind = ExprKind::DICT; e->line = ln;

            skipNewlines();

            bool startsWithKey = false;
            if (check(TokType::STRING)) {
                startsWithKey = true;
            } else if (check(TokType::IDENT)) {
                string nx = cur().text;
                if (!KEYWORDS.count(nx)) startsWithKey = true;
            }
            if (startsWithKey) {
                auto parseKey = [&]() -> ExprPtr {
                    if (check(TokType::STRING)) {
                        auto k = make_shared<Expr>(); k->kind = ExprKind::STRING; k->str = advance().text; k->line = ln; return k;
                    }
                    if (check(TokType::IDENT)) {
                        auto k = make_shared<Expr>(); k->kind = ExprKind::STRING; k->str = advance().text; k->line = ln; return k;
                    }
                    fail("expected dict key (identifier or string) but got '" + cur().text + "'", ln);
                    return nullptr;
                };
                ExprPtr k = parseKey();
                expectIdent("is");
                skipNewlines();
                ExprPtr v = parseExpression();
                e->pairs.push_back({k, v});
                while (true) {
                    skipNewlines();
                    if (!check(TokType::COMMA)) break;
                    advance();
                    skipNewlines();
                    ExprPtr k2 = parseKey();
                    expectIdent("is");
                    skipNewlines();
                    ExprPtr v2 = parseExpression();
                    e->pairs.push_back({k2, v2});
                }
            }
            return e;
        }

        if (checkIdent("http_get")) {
            advance(); ExprPtr url = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_get"; e->items.push_back(url); e->line = ln; return e;
        }
        if (checkIdent("http_post")) {
            advance(); ExprPtr url = parsePostfix(); expectIdent("with"); ExprPtr body = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_post"; e->items = {url, body}; e->line = ln; return e;
        }
        if (checkIdent("http_request")) {
            advance();
            ExprPtr url = parsePostfix(); expectIdent("with");
            ExprPtr method = parsePostfix();
            ExprPtr body = nullptr, headers = nullptr;

            if (check(TokType::COMMA)) {
                advance(); body = parsePostfix();
                if (check(TokType::COMMA)) {
                    advance(); headers = parsePostfix();
                }
            }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_request";
            e->items = {url, method};
            if (body)    e->items.push_back(body);
            if (headers) e->items.push_back(headers);
            e->line = ln; return e;
        }

        if (checkIdent("fetch")) {
            advance();
            ExprPtr url = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "fetch";
            e->items.push_back(url); e->line = ln;
            if (checkIdent("with")) {
                advance();
                ExprPtr a = parsePostfix();
                e->items.push_back(a);
                if (check(TokType::COMMA)) {
                    advance(); ExprPtr b = parsePostfix(); e->items.push_back(b);
                    if (check(TokType::COMMA)) {
                        advance(); ExprPtr c = parsePostfix(); e->items.push_back(c);
                    }
                }
            }
            return e;
        }

        if (checkIdent("socket")) {
            advance();
            ExprPtr host = parsePostfix();
            ExprPtr port = nullptr;
            if (checkIdent("with")) { advance(); port = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); port = parsePostfix(); }
            else if (checkIdent("port")) { advance(); port = parsePostfix(); }
            else { port = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "socket";
            e->items.push_back(host);
            if (port) e->items.push_back(port);
            e->line = ln; return e;
        }
        if (checkIdent("send")) {
            advance();
            ExprPtr h = parsePostfix();
            ExprPtr msg = nullptr;
            if (checkIdent("with")) { advance(); msg = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
            else { msg = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "send";
            e->items.push_back(h);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }
        if (checkIdent("readline")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "readline";
            e->items.push_back(h); e->line = ln; return e;
        }
        if (checkIdent("receive")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "receive";
            e->items.push_back(h); e->line = ln;
            if (checkIdent("with")) {
                advance(); ExprPtr n = parsePostfix(); e->items.push_back(n);
            } else if (check(TokType::COMMA)) {
                advance(); ExprPtr n = parsePostfix(); e->items.push_back(n);
            } else {

                if (check(TokType::NUMBER) || check(TokType::LPAREN) || check(TokType::MINUS)) {
                    ExprPtr n = parsePostfix(); e->items.push_back(n);
                }
            }
            return e;
        }
        if (checkIdent("close")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "close";
            e->items.push_back(h); e->line = ln; return e;
        }

        if (checkIdent("ssl_socket")) {
            advance();
            ExprPtr host = parsePostfix();
            ExprPtr port = nullptr;
            if (checkIdent("with")) { advance(); port = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); port = parsePostfix(); }
            else if (checkIdent("port")) { advance(); port = parsePostfix(); }
            else { port = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ssl_socket";
            e->items.push_back(host);
            if (port) e->items.push_back(port);
            e->line = ln; return e;
        }
        if (checkIdent("ssl_send")) {
            advance();
            ExprPtr h = parsePostfix();
            ExprPtr msg = nullptr;
            if (checkIdent("with")) { advance(); msg = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
            else { msg = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ssl_send";
            e->items.push_back(h);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }
        if (checkIdent("ssl_receive")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ssl_receive";
            e->items.push_back(h); e->line = ln;
            if (checkIdent("with")) {
                advance(); ExprPtr n = parsePostfix(); e->items.push_back(n);
            } else if (check(TokType::COMMA)) {
                advance(); ExprPtr n = parsePostfix(); e->items.push_back(n);
            } else {
                if (check(TokType::NUMBER) || check(TokType::LPAREN) || check(TokType::MINUS)) {
                    ExprPtr n = parsePostfix(); e->items.push_back(n);
                }
            }
            return e;
        }
        if (checkIdent("ssl_readline")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ssl_readline";
            e->items.push_back(h); e->line = ln; return e;
        }
        if (checkIdent("ssl_close")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ssl_close";
            e->items.push_back(h); e->line = ln; return e;
        }
        if (checkIdent("sslsay")) {
            advance();
            ExprPtr host = parsePostfix();
            ExprPtr port = nullptr, msg = nullptr;
            if (checkIdent("with")) {
                advance(); port = parsePostfix();
                if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
                else if (checkIdent("and")) { advance(); msg = parsePostfix(); }
                else { msg = parsePostfix(); }
            } else {
                if (check(TokType::COMMA)) { advance(); port = parsePostfix(); }
                else if (checkIdent("port")) { advance(); port = parsePostfix(); }
                else { port = parsePostfix(); }
                if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
                else if (checkIdent("with")) { advance(); msg = parsePostfix(); }
                else if (checkIdent("and")) { advance(); msg = parsePostfix(); }
                else { msg = parsePostfix(); }
            }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "sslsay";
            e->items.push_back(host);
            if (port) e->items.push_back(port);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }

        if (checkIdent("ws_connect")) {
            advance();
            ExprPtr url = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_connect";
            e->items.push_back(url); e->line = ln; return e;
        }
        if (checkIdent("ws_send")) {
            advance();
            ExprPtr h = parsePostfix();
            ExprPtr msg = nullptr;
            if (checkIdent("with")) { advance(); msg = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
            else { msg = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_send";
            e->items.push_back(h);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }
        if (checkIdent("ws_send_binary")) {
            advance();
            ExprPtr h = parsePostfix();
            ExprPtr msg = nullptr;
            if (checkIdent("with")) { advance(); msg = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
            else { msg = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_send_binary";
            e->items.push_back(h);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }
        if (checkIdent("ws_recv")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_recv";
            e->items.push_back(h); e->line = ln; return e;
        }
        if (checkIdent("ws_close")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_close";
            e->items.push_back(h); e->line = ln; return e;
        }
        if (checkIdent("wssay")) {
            advance();
            ExprPtr url = parsePostfix();
            ExprPtr msg = nullptr;
            if (checkIdent("with")) { advance(); msg = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); msg = parsePostfix(); }
            else if (checkIdent("and")) { advance(); msg = parsePostfix(); }
            else { msg = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "wssay";
            e->items.push_back(url);
            if (msg) e->items.push_back(msg);
            e->line = ln; return e;
        }

        if (checkIdent("read")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "read";
            e->items.push_back(path); e->line = ln; return e;
        }
        if (checkIdent("readlines")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "readlines";
            e->items.push_back(path); e->line = ln; return e;
        }
        if (checkIdent("write")) {
            advance();
            ExprPtr path = parsePostfix(); expectIdent("with");
            ExprPtr content = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "write";
            e->items = {path, content}; e->line = ln; return e;
        }
        if (checkIdent("append")) {
            advance();
            ExprPtr path = parsePostfix(); expectIdent("with");
            ExprPtr content = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "append";
            e->items = {path, content}; e->line = ln; return e;
        }
        if (checkIdent("ls")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ls";
            e->items.push_back(path); e->line = ln; return e;
        }
        if (checkIdent("exists")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "exists";
            e->items.push_back(path); e->line = ln; return e;
        }
        if (checkIdent("mkdir")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "mkdir";
            e->items.push_back(path); e->line = ln; return e;
        }
        if (checkIdent("rm")) {
            advance();
            ExprPtr path = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "rm";
            e->items.push_back(path); e->line = ln; return e;
        }


        // ===== v15+ extras =====
        if (checkIdent("ws_ping")) {
            advance();
            ExprPtr h = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "ws_ping";
            e->items.push_back(h); e->line = ln; return e;
        }
        // -- Bitwise (3.7) --
        if (checkIdent("band")) {
            advance(); ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); b = parsePostfix(); }
            else if (checkIdent("by")) { advance(); b = parsePostfix(); }
            else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "band";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("bor")) {
            advance(); ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); b = parsePostfix(); }
            else if (checkIdent("by")) { advance(); b = parsePostfix(); }
            else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bor";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("bxor")) {
            advance(); ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); b = parsePostfix(); }
            else if (checkIdent("by")) { advance(); b = parsePostfix(); }
            else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bxor";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("bnot")) {
            advance(); ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bnot";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("bshl")) {
            advance(); ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("by")) { advance(); b = parsePostfix(); }
            else if (checkIdent("with")) { advance(); b = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); b = parsePostfix(); }
            else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bshl";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("bshr")) {
            advance(); ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("by")) { advance(); b = parsePostfix(); }
            else if (checkIdent("with")) { advance(); b = parsePostfix(); }
            else if (check(TokType::COMMA)) { advance(); b = parsePostfix(); }
            else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bshr";
            e->items = {a, b}; e->line = ln; return e;
        }
        // -- BigInt (3.7/3.8) --
        if (checkIdent("bigint")) {
            advance();
            ExprPtr a = nullptr;
            if (checkIdent("of")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "bigint";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("bigint_add") || checkIdent("bigint_sub") || checkIdent("bigint_mul") ||
            checkIdent("bigint_cmp") || checkIdent("bigint_band") || checkIdent("bigint_bor") ||
            checkIdent("bigint_bxor") || checkIdent("bigint_shl") || checkIdent("bigint_shr") ||
            checkIdent("bigint_div") || checkIdent("bigint_mod") || checkIdent("bigint_pow")) {
            string name = advance().text;
            ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with") || checkIdent("by") || checkIdent("to")) {
                advance(); b = parsePostfix();
            } else if (check(TokType::COMMA)) {
                advance(); b = parsePostfix();
            } else { b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("bigint_to_hex") || checkIdent("bigint_from_hex")) {
            string name = advance().text;
            ExprPtr a = nullptr;
            if (checkIdent("of")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        // -- Math extras (3.8) --
        if (checkIdent("sign") || checkIdent("trunc") || checkIdent("popcount") ||
            checkIdent("bit_length")) {
            string name = advance().text;
            expectIdent("of");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("gcd") || checkIdent("lcm")) {
            string name = advance().text;
            expectIdent("of");
            ExprPtr a = parsePostfix();
            expectIdent("and");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("clamp")) {
            advance();
            ExprPtr x = parsePostfix();
            expectIdent("between");
            ExprPtr lo = parsePostfix();
            expectIdent("and");
            ExprPtr hi = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "clamp";
            e->items = {x, lo, hi}; e->line = ln; return e;
        }
        // -- Date (3.7) --
        if (checkIdent("time_iso_after") || checkIdent("time_iso_at") ||
            checkIdent("date_now") || checkIdent("date_utc_now") ||
            checkIdent("timezone_offset")) {
            string name = advance().text;
            ExprPtr a = nullptr;
            if (checkIdent("of") || checkIdent("with")) {
                advance(); a = parsePostfix();
            } else if (!check(TokType::LBRACKET) && !check(TokType::LPAREN)) {
                // optional arg
                if (check(TokType::NUMBER) || check(TokType::STRING) || check(TokType::IDENT))
                    a = parsePostfix();
            }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        if (checkIdent("date_parse") || checkIdent("date_format") ||
            checkIdent("date_to_iso") || checkIdent("date_from_iso") ||
            checkIdent("date_parts")) {
            string name = advance().text;
            ExprPtr a = nullptr;
            if (checkIdent("of")) { advance(); a = parsePostfix(); }
            else if (checkIdent("with")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            ExprPtr b = nullptr;
            if (checkIdent("with")) {
                advance(); b = parsePostfix();
            } else if (check(TokType::COMMA)) {
                advance(); b = parsePostfix();
            }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items.push_back(a);
            if (b) e->items.push_back(b);
            e->line = ln; return e;
        }
        if (checkIdent("date_add")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr field = parsePostfix();
            ExprPtr amount = nullptr;
            if (check(TokType::COMMA)) { advance(); amount = parsePostfix(); }
            else if (checkIdent("and")) { advance(); amount = parsePostfix(); }
            else { amount = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "date_add";
            e->items = {a, field, amount}; e->line = ln; return e;
        }
        if (checkIdent("date_diff")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            ExprPtr field = nullptr;
            if (check(TokType::COMMA)) { advance(); field = parsePostfix(); }
            else if (checkIdent("and")) { advance(); field = parsePostfix(); }
            else { field = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "date_diff";
            e->items = {a, b, field}; e->line = ln; return e;
        }
        // -- Timer (3.13) --
        if (checkIdent("timer_start") || checkIdent("timer_stop")) {
            string name = advance().text;
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        // -- SQLite (3.7/3.16) --
        if (checkIdent("db_open")) {
            advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "db_open";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("db_close") || checkIdent("db_begin") || checkIdent("db_commit") ||
            checkIdent("db_rollback") || checkIdent("db_finalize") || checkIdent("db_step")) {
            string name = advance().text;
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("db_last_insert_id") || checkIdent("db_changes")) {
            string name = advance().text;
            expectIdent("of");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("db_exec") || checkIdent("db_query") || checkIdent("db_prepare")) {
            string name = advance().text;
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("db_exec_params") || checkIdent("db_query_params")) {
            string name = advance().text;
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            ExprPtr c = nullptr;
            if (check(TokType::COMMA)) { advance(); c = parsePostfix(); }
            else if (checkIdent("and")) { advance(); c = parsePostfix(); }
            else { c = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b, c}; e->line = ln; return e;
        }
        // -- Atomic (3.7) --
        if (checkIdent("atomic_write")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "atomic_write";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("lock_file") || checkIdent("unlock_file")) {
            string name = advance().text;
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        // -- String extras (3.9) --
        if (checkIdent("starts_with") || checkIdent("ends_with")) {
            string name = advance().text;
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("count")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "count";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("pad_left") || checkIdent("pad_right")) {
            string name = advance().text;
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr width = parsePostfix();
            ExprPtr ch = nullptr;
            if (checkIdent("with")) { advance(); ch = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, width};
            if (ch) e->items.push_back(ch);
            e->line = ln; return e;
        }
        if (checkIdent("format")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "format";
            e->items.push_back(a);
            e->items.push_back(parsePostfix());
            while (check(TokType::COMMA)) { advance(); e->items.push_back(parsePostfix()); }
            e->line = ln; return e;
        }
        if (checkIdent("capitalize") || checkIdent("title_case") ||
            checkIdent("encode_base64") || checkIdent("decode_base64") ||
            checkIdent("hash_md5") || checkIdent("hash_sha1") ||
            checkIdent("hash_sha256") || checkIdent("hash_sha512") ||
            checkIdent("url_encode") || checkIdent("url_decode") ||
            checkIdent("type_of") || checkIdent("is_dict") ||
            checkIdent("is_function") || checkIdent("is_empty") ||
            checkIdent("is_file") || checkIdent("is_dir") ||
            checkIdent("to_json_pretty") || checkIdent("callable") ||
            checkIdent("date_to_iso") || checkIdent("date_from_iso") ||
            checkIdent("date_parts") || checkIdent("values") || checkIdent("items") ||
            checkIdent("unique") || checkIdent("flatten") || checkIdent("copy") ||
            checkIdent("stat") || checkIdent("read_binary") || checkIdent("glob")) {
            string name = advance().text;
            ExprPtr a = nullptr;
            if (checkIdent("of")) { advance(); a = parsePostfix(); }
            else if (checkIdent("from")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("hmac_sha256")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "hmac_sha256";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("replace_all")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("from");
            ExprPtr b = parsePrimary();
            expectIdent("with");
            ExprPtr c = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "replace_all";
            e->items = {a, b, c}; e->line = ln; return e;
        }
        if (checkIdent("code_point_at")) {
            advance();
            ExprPtr a = parsePostfix();
            expectIdent("at");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "code_point_at";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("from_code_point")) {
            advance();
            expectIdent("of");
            ExprPtr a = parsePrimary();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "from_code_point";
            e->items = {a}; e->line = ln; return e;
        }
        // -- List extras (3.10) --
        if (checkIdent("shift")) {
            advance();
            expectIdent("from");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "shift";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("unshift")) {
            advance();
            ExprPtr value = parsePrimary();
            expectIdent("to");
            ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "unshift";
            e->items = {value, lst}; e->line = ln; return e;
        }
        if (checkIdent("insert_at")) {
            advance();
            ExprPtr value = parsePrimary();
            expectIdent("into");
            ExprPtr lst = parsePostfix();
            expectIdent("at");
            ExprPtr idx = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "insert_at";
            e->items = {value, lst, idx}; e->line = ln; return e;
        }
        if (checkIdent("remove_at")) {
            advance();
            ExprPtr idx = parsePrimary();
            expectIdent("from");
            ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "remove_at";
            e->items = {idx, lst}; e->line = ln; return e;
        }
        if (checkIdent("extend")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "extend";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("map") || checkIdent("filter") || checkIdent("find") ||
            checkIdent("sort_by")) {
            string name = advance().text;
            ExprPtr lst = parsePrimary();
            expectIdent("with");
            ExprPtr fn = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {lst, fn}; e->line = ln; return e;
        }
        if (checkIdent("reduce")) {
            advance();
            ExprPtr lst = parsePrimary();
            expectIdent("with");
            ExprPtr fn = parsePrimary();
            expectIdent("from");
            ExprPtr init = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "reduce";
            e->items = {lst, fn, init}; e->line = ln; return e;
        }
        if (checkIdent("list_contains")) {
            advance();
            ExprPtr value = parsePostfix();
            expectIdent("in");
            ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "list_contains";
            e->items = {value, lst}; e->line = ln; return e;
        }
        if (checkIdent("chunk")) {
            advance();
            ExprPtr lst = parsePrimary();
            expectIdent("into");
            ExprPtr sz = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "chunk";
            e->items = {lst, sz}; e->line = ln; return e;
        }
        if (checkIdent("zip")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "zip";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("enumerate")) {
            advance();
            ExprPtr lst = parsePostfix();
            ExprPtr start = nullptr;
            if (checkIdent("from")) { advance(); start = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "enumerate";
            e->items = {lst};
            if (start) e->items.push_back(start);
            e->line = ln; return e;
        }
        if (checkIdent("sum_of") || checkIdent("product_of") ||
            checkIdent("min_of") || checkIdent("max_of")) {
            string name = advance().text;
            if (checkIdent("of")) advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("count_of")) {
            advance();
            ExprPtr value = parsePostfix();
            expectIdent("in");
            ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "count_of";
            e->items = {value, lst}; e->line = ln; return e;
        }
        if (checkIdent("sort_desc")) {
            advance();
            ExprPtr lst = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "sort_desc";
            e->items = {lst}; e->line = ln; return e;
        }
        // -- Dict extras (3.11) --
        if (checkIdent("remove_key")) {
            advance();
            ExprPtr key = parsePrimary();
            expectIdent("from");
            ExprPtr dct = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "remove_key";
            e->items = {key, dct}; e->line = ln; return e;
        }
        if (checkIdent("merge")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "merge";
            e->items = {a, b}; e->line = ln; return e;
        }
        // -- File I/O extras (3.14) --
        if (checkIdent("copy_file") || checkIdent("move_file")) {
            string name = advance().text;
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("write_binary")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "write_binary";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("temp_file") || checkIdent("temp_dir")) {
            string name = advance().text;
            ExprPtr a = nullptr;
            if (checkIdent("with")) { advance(); a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        if (checkIdent("chmod")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePrimary();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "chmod";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("cwd")) {
            advance();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "cwd";
            e->line = ln; return e;
        }
        if (checkIdent("chdir")) {
            advance();
            expectIdent("to");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "chdir";
            e->items = {a}; e->line = ln; return e;
        }
        // -- Network extras (3.15) --
        if (checkIdent("udp_socket")) {
            advance();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "udp_socket";
            e->line = ln; return e;
        }
        if (checkIdent("server_socket")) {
            advance();
            expectIdent("on");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "server_socket";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("accept")) {
            advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "accept";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("resolve_host")) {
            advance();
            expectIdent("of");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "resolve_host";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("set_socket_timeout")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "set_socket_timeout";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("http_download")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_download";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("http_upload")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            ExprPtr c = nullptr;
            if (check(TokType::COMMA)) { advance(); c = parsePostfix(); }
            else if (checkIdent("and")) { advance(); c = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_upload";
            e->items = {a, b};
            if (c) e->items.push_back(c);
            e->line = ln; return e;
        }
        if (checkIdent("http_set_headers")) {
            advance();
            expectIdent("with");
            ExprPtr a = parsePrimary();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_set_headers";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("http_set_timeout")) {
            advance();
            expectIdent("to");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "http_set_timeout";
            e->items = {a}; e->line = ln; return e;
        }
        // -- System (3.17) --
        if (checkIdent("env")) {
            advance();
            ExprPtr a = nullptr;
            if (checkIdent("of")) { advance(); a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "env";
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        if (checkIdent("shell")) {
            advance();
            ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "shell";
            e->items = {a};
            if (b) e->items.push_back(b);
            e->line = ln; return e;
        }
        if (checkIdent("exit")) {
            advance();
            ExprPtr a = nullptr;
            if (checkIdent("with")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "exit";
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        if (checkIdent("platform") || checkIdent("pid") || checkIdent("memory_used") ||
            checkIdent("uptime") || checkIdent("hostname") || checkIdent("source_line") ||
            checkIdent("date_now") || checkIdent("date_utc_now") ||
            checkIdent("timezone_offset") || checkIdent("saved_vars")) {
            string name = advance().text;
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->line = ln; return e;
        }
        // -- Random extras (3.18) --
        if (checkIdent("random_int")) {
            advance();
            expectIdent("from");
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "random_int";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("random_choice") || checkIdent("random_shuffle")) {
            string name = advance().text;
            if (checkIdent("of") || checkIdent("from")) advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("random_seed")) {
            advance();
            ExprPtr a = nullptr;
            if (checkIdent("with")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "random_seed";
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        // -- Concurrency (3.19) --
        if (checkIdent("thread_start")) {
            advance();
            ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "thread_start";
            e->items = {a};
            if (b) e->items.push_back(b);
            e->line = ln; return e;
        }
        if (checkIdent("thread_wait") || checkIdent("channel_recv") ||
            checkIdent("mutex_new") || checkIdent("mutex_lock") || checkIdent("mutex_unlock")) {
            string name = advance().text;
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("channel_new")) {
            advance();
            ExprPtr a = nullptr;
            if (checkIdent("with")) { advance(); a = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "channel_new";
            if (a) e->items.push_back(a);
            e->line = ln; return e;
        }
        if (checkIdent("channel_send")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "channel_send";
            e->items = {a, b}; e->line = ln; return e;
        }
        // -- Reflection (3.20) --
        if (checkIdent("eval_ocode")) {
            advance();
            if (checkIdent("of")) advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "eval_ocode";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("get_var")) {
            advance();
            expectIdent("of");
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "get_var";
            e->items = {a}; e->line = ln; return e;
        }
        if (checkIdent("set_var")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("to");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "set_var";
            e->items = {a, b}; e->line = ln; return e;
        }
        // -- Error handling (3.21) --
        if (checkIdent("raise")) {
            advance();
            ExprPtr a = nullptr, b = nullptr;
            if (checkIdent("with")) { advance(); a = parsePostfix(); }
            else { a = parsePostfix(); }
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "raise";
            e->items = {a};
            if (b) e->items.push_back(b);
            e->line = ln; return e;
        }
        if (checkIdent("error_message") || checkIdent("error_code")) {
            string name = advance().text;
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = name;
            e->line = ln; return e;
        }
        // -- Saved vars (3.23) --
        if (checkIdent("save_var")) {
            advance();
            ExprPtr a = parsePrimary();
            expectIdent("with");
            ExprPtr b = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "save_var";
            e->items = {a, b}; e->line = ln; return e;
        }
        if (checkIdent("load_var")) {
            advance();
            ExprPtr a = parsePostfix();
            ExprPtr b = nullptr;
            if (checkIdent("with")) { advance(); b = parsePostfix(); }
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "load_var";
            e->items = {a};
            if (b) e->items.push_back(b);
            e->line = ln; return e;
        }
        if (checkIdent("clear_saved_var")) {
            advance();
            ExprPtr a = parsePostfix();
            auto e = make_shared<Expr>(); e->kind = ExprKind::BUILTIN; e->name = "clear_saved_var";
            e->items = {a}; e->line = ln; return e;
        }

        if (check(TokType::LPAREN)) {
            advance(); ExprPtr inner = parseExpression(); expect(TokType::RPAREN, "')'"); return inner;
        }
        if (check(TokType::LBRACKET)) {
            advance();
            auto e = make_shared<Expr>(); e->kind = ExprKind::LIST; e->line = ln;
            if (!check(TokType::RBRACKET)) {
                e->items.push_back(parseExpression());
                while (check(TokType::COMMA)) { advance(); e->items.push_back(parseExpression()); }
            }
            expect(TokType::RBRACKET, "']'"); return e;
        }

        if (check(TokType::LBRACE) || check(TokType::RBRACE)) {
            fail("braces { } are not used in OCode. "
                 "Write a dict like:  dict name is \"Bob\", age is 30", ln);
        }
        if (check(TokType::IDENT)) {
            string name = advance().text;
            if (KEYWORDS.count(name) && name != "true" && name != "false")
                fail("unexpected keyword '" + name + "' in expression", ln);
            auto e = make_shared<Expr>(); e->kind = ExprKind::VAR; e->name = name; e->line = ln; return e;
        }
        fail("unexpected token '" + cur().text + "' in expression", ln);
    }
};

struct Value;
struct DictData {
    vector<pair<string, Value>> entries;
    unordered_map<string, size_t> index;
    void set(const string& k, Value v);
    Value* get(const string& k);
    bool has(const string& k) const { return index.count(k) != 0; }
};

struct Value {
    enum class Type { NUMBER, STRING, BOOL, LIST, DICT } type = Type::NUMBER;
    double num = 0;
    string str;
    bool bl = false;
    shared_ptr<vector<Value>> list;
    shared_ptr<DictData> dict;

    static Value Number(double n) { Value v; v.type = Type::NUMBER; v.num = n; return v; }
    static Value Str(string s) { Value v; v.type = Type::STRING; v.str = move(s); return v; }
    static Value Bool(bool b) { Value v; v.type = Type::BOOL; v.bl = b; return v; }
    static Value List(shared_ptr<vector<Value>> l) { Value v; v.type = Type::LIST; v.list = move(l); return v; }
    static Value Dict(shared_ptr<DictData> d) { Value v; v.type = Type::DICT; v.dict = move(d); return v; }
};

inline void DictData::set(const string& k, Value v) {
    auto it = index.find(k);
    if (it != index.end()) { entries[it->second].second = move(v); return; }
    index.emplace(k, entries.size());
    entries.emplace_back(k, move(v));
}
inline Value* DictData::get(const string& k) {
    auto it = index.find(k);
    if (it == index.end()) return nullptr;
    return &entries[it->second].second;
}

string valueToString(const Value& v) {
    switch (v.type) {
        case Value::Type::NUMBER: {
            double d = v.num;
            if (d == (long long)d && fabs(d) < 9.2e18) return to_string((long long)d);
            ostringstream oss;
            oss.setf(ios::fixed);
            oss << setprecision(6) << d;

            string s = oss.str();
            if (s.find('.') != string::npos) {
                size_t last = s.find_last_not_of('0');
                if (s[last] == '.') last--;
                s.erase(last + 1);
            }
            return s;
        }
        case Value::Type::STRING: return v.str;
        case Value::Type::BOOL: return v.bl ? "true" : "false";
        case Value::Type::LIST: {
            string s = "[";
            for (size_t i = 0; i < v.list->size(); i++) {
                if (i) s += ", ";
                const Value& e = (*v.list)[i];
                if (e.type == Value::Type::STRING) s += "\"" + e.str + "\"";
                else s += valueToString(e);
            }
            return s + "]";
        }
        case Value::Type::DICT: {
            string s = "{";
            for (size_t i = 0; i < v.dict->entries.size(); i++) {
                if (i) s += ", ";
                s += "\"" + v.dict->entries[i].first + "\": ";
                const Value& e = v.dict->entries[i].second;
                if (e.type == Value::Type::STRING) s += "\"" + e.str + "\"";
                else s += valueToString(e);
            }
            return s + "}";
        }
    }
    return "";
}

bool valueToBool(const Value& v) {
    switch (v.type) {
        case Value::Type::NUMBER: return v.num != 0;
        case Value::Type::STRING: return !v.str.empty();
        case Value::Type::BOOL: return v.bl;
        case Value::Type::LIST: return v.list && !v.list->empty();
        case Value::Type::DICT: return v.dict && !v.dict->entries.empty();
    }
    return false;
}

double valueToNumber(const Value& v, int line) {
    if (v.type == Value::Type::NUMBER) return v.num;
    if (v.type == Value::Type::BOOL) return v.bl ? 1 : 0;
    if (v.type == Value::Type::STRING) {
        try { return stod(v.str); } catch (...) { fail("cannot convert string to number: \"" + v.str + "\"", line); }
    }
    fail("expected a number here", line);
}

struct HttpResponse { int status = 0; string body; string headers; string error; };

static bool parseHttpUrl(const string& url, string& host, int& port, string& path,
                         bool& isHttps, string& err) {
    string u = url;
    isHttps = false;
    if (u.rfind("http://", 0) == 0)         u = u.substr(7);
    else if (u.rfind("https://", 0) == 0) { u = u.substr(8); isHttps = true; }
    else if (u.rfind("HTTP://", 0) == 0)    u = u.substr(7);
    else if (u.rfind("HTTPS://", 0) == 0) { u = u.substr(8); isHttps = true; }
    else {

    }

    string hostport, p;
    size_t slash = u.find('/');
    if (slash == string::npos) { hostport = u; p = "/"; }
    else { hostport = u.substr(0, slash); p = u.substr(slash); }
    if (p.empty()) p = "/";

    size_t colon = hostport.rfind(':');
    if (colon != string::npos && hostport.find(']') == string::npos) {
        host = hostport.substr(0, colon);
        try { port = stoi(hostport.substr(colon + 1)); }
        catch (...) { err = "invalid port in URL"; return false; }
    } else {
        host = hostport;
        port = isHttps ? 443 : 80;
    }
    if (host.empty()) { err = "no host in URL"; return false; }
    path = p;
    err.clear();
    return true;
}

// ============================================================================
// v15 helpers: BigInt, SQLite handles, channels, mutexes, threads, timers,
// saved vars, encoding/hashing stubs. All guarded so the file compiles
// cleanly with or without -lsqlite3 / -lssl -lcrypto.
// ============================================================================

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cstdint>
#include <algorithm>
#include <random>
#include <functional>
#include <queue>
#include <unordered_set>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

namespace fs = std::filesystem;

#ifndef OCODE_NO_OPENSSL

static SSL_CTX* g_sslCtx = nullptr;
static void ensureSslInit() {
    if (g_sslCtx) return;
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    const SSL_METHOD* method = TLS_client_method();
    g_sslCtx = SSL_CTX_new(method);
    if (!g_sslCtx) return;

    SSL_CTX_set_default_verify_paths(g_sslCtx);
    SSL_CTX_set_verify(g_sslCtx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_min_proto_version(g_sslCtx, TLS1_2_VERSION);
}
static bool g_httpsSupported = true;
#else
static bool g_httpsSupported = false;
#endif

static HttpResponse httpPerform(const string& url, const string& method,
                                const string& body, const vector<pair<string,string>>& extraHeaders,
                                int timeoutSec, int line) {
    HttpResponse r;
    string host, path; int port; bool isHttps = false;
    string err;
    if (!parseHttpUrl(url, host, port, path, isHttps, err)) { r.error = err; fail(err, line); }

    if (isHttps && !g_httpsSupported) {
        err = "https:// URLs require OpenSSL, but this OCode binary was built without it. "
              "Rebuild with `g++ ... -lssl -lcrypto` (remove -DOCODE_NO_OPENSSL).";
        r.error = err; fail(err, line);
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    string portStr = to_string(port);
    int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        r.error = string("DNS lookup failed for '") + host + "': " + gai_strerror(gai);
        fail(r.error, line);
    }

    int sock = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        struct timeval tv; tv.tv_sec = timeoutSec; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock); sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) { r.error = "could not connect to " + host + ":" + to_string(port); fail(r.error, line); }

#ifndef OCODE_NO_OPENSSL
    SSL* ssl = nullptr;
    if (isHttps) {
        ensureSslInit();
        if (!g_sslCtx) { close(sock); r.error = "SSL_CTX_new failed"; fail(r.error, line); }
        ssl = SSL_new(g_sslCtx);
        if (!ssl) { close(sock); r.error = "SSL_new failed"; fail(r.error, line); }

        SSL_set_tlsext_host_name(ssl, host.c_str());
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) != 1) {
            unsigned long e = ERR_get_error();
            string sslErr = e ? ERR_error_string(e, nullptr) : "unknown TLS error";
            SSL_free(ssl); close(sock);
            r.error = "TLS handshake failed with " + host + ": " + sslErr;
            fail(r.error, line);
        }
    }
#endif

    string meth = method; for (auto& c : meth) c = (char)toupper(c);
    if (meth.empty()) meth = "GET";
    ostringstream req;
    req << meth << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << (port == 80 || port == 443 ? "" : ":" + to_string(port)) << "\r\n";
    req << "User-Agent: OCode/8\r\n";
    req << "Accept: */*\r\n";
    req << "Connection: close\r\n";
    bool hasContentType = false;
    for (auto& h : extraHeaders) {
        string hn = h.first; for (auto& c : hn) c = (char)tolower(c);
        if (hn == "content-type") hasContentType = true;
        req << h.first << ": " << h.second << "\r\n";
    }
    if (!body.empty() && !hasContentType)
        req << "Content-Type: application/x-www-form-urlencoded\r\n";
    if (!body.empty())
        req << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n";
    if (!body.empty()) req << body;

    string reqStr = req.str();
    const char* data = reqStr.data();
    size_t remaining = reqStr.size();
#ifndef OCODE_NO_OPENSSL
    if (isHttps && ssl) {
        while (remaining > 0) {
            int n = SSL_write(ssl, data, (int)remaining);
            if (n <= 0) { SSL_free(ssl); close(sock); r.error = "TLS send failed"; fail(r.error, line); }
            data += n; remaining -= (size_t)n;
        }
    } else
#endif
    {
        while (remaining > 0) {
            ssize_t n = send(sock, data, remaining, 0);
            if (n <= 0) { close(sock); r.error = "send failed"; fail(r.error, line); }
            data += n; remaining -= (size_t)n;
        }
    }

    string raw;
#ifndef OCODE_NO_OPENSSL
    if (isHttps && ssl) {
        char buf[8192];
        while (true) {
            int n = SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0) break;
            raw.append(buf, (size_t)n);
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
    } else
#endif
    {
        char buf[8192];
        while (true) {
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n < 0) break;
            if (n == 0) break;
            raw.append(buf, (size_t)n);
        }
    }
    close(sock);

    size_t sep = raw.find("\r\n\r\n");
    string headerBlock = (sep == string::npos) ? raw : raw.substr(0, sep);
    string bodyPart    = (sep == string::npos) ? ""   : raw.substr(sep + 4);

    int status = 0;
    {
        size_t nl = headerBlock.find("\r\n");
        string statusLine = (nl == string::npos) ? headerBlock : headerBlock.substr(0, nl);
        size_t sp = statusLine.find(' ');
        if (sp != string::npos) {
            size_t sp2 = statusLine.find(' ', sp + 1);
            string code = (sp2 == string::npos) ? statusLine.substr(sp + 1) : statusLine.substr(sp + 1, sp2 - sp - 1);
            try { status = stoi(code); } catch (...) {}
        }
    }

    string lowerHeaders = headerBlock;
    for (auto& c : lowerHeaders) c = (char)tolower(c);
    if (lowerHeaders.find("transfer-encoding: chunked") != string::npos) {
        string decoded;
        size_t i = 0;
        while (i < bodyPart.size()) {
            size_t nl = bodyPart.find("\r\n", i);
            if (nl == string::npos) break;
            string lenStr = bodyPart.substr(i, nl - i);
            size_t semi = lenStr.find(';');
            if (semi != string::npos) lenStr = lenStr.substr(0, semi);
            long long chunkLen = -1;
            try { chunkLen = stoll(lenStr, nullptr, 16); } catch (...) { break; }
            if (chunkLen <= 0) break;
            size_t start = nl + 2;
            if (start + (size_t)chunkLen > bodyPart.size()) {
                decoded += bodyPart.substr(start);
                break;
            }
            decoded += bodyPart.substr(start, (size_t)chunkLen);
            i = start + (size_t)chunkLen + 2;
        }
        bodyPart = decoded;
    }

    r.status = status;
    r.headers = headerBlock;
    r.body = bodyPart;
    return r;
}

static vector<int> g_openSockets;

enum class SockKind { NONE, RAW, SSL, WS };
struct OpenSock {
    SockKind kind = SockKind::NONE;
    int      fd    = -1;
    void*    ssl   = nullptr;
};
static vector<OpenSock> g_socks;

static int socketRegister(int fd) {
    g_openSockets.push_back(fd);
    OpenSock s; s.kind = SockKind::RAW; s.fd = fd;
    g_socks.push_back(s);
    return (int)g_socks.size();
}
static int socketRegisterSsl(int fd, void* ssl) {
    g_openSockets.push_back(fd);
    OpenSock s; s.kind = SockKind::SSL; s.fd = fd; s.ssl = ssl;
    g_socks.push_back(s);
    return (int)g_socks.size();
}
static int socketRegisterWs(int fd, void* ssl) {
    g_openSockets.push_back(fd);
    OpenSock s; s.kind = SockKind::WS; s.fd = fd; s.ssl = ssl;
    g_socks.push_back(s);
    return (int)g_socks.size();
}
static OpenSock* socketLookup(int handle) {
    if (handle < 1 || handle > (int)g_socks.size()) return nullptr;
    if (g_socks[handle - 1].kind == SockKind::NONE) return nullptr;
    return &g_socks[handle - 1];
}
static int socketLookupFd(int handle) {
    OpenSock* s = socketLookup(handle);
    return s ? s->fd : -1;
}
static void socketCloseAt(int handle, int line) {
    if (handle < 1 || handle > (int)g_socks.size() || g_socks[handle - 1].kind == SockKind::NONE) {
        fail("close: invalid handle (not an open socket)", line);
    }
    OpenSock& s = g_socks[handle - 1];
#ifndef OCODE_NO_OPENSSL
    if (s.ssl) {
        SSL* ssl = (SSL*)s.ssl;
        SSL_shutdown(ssl);
        SSL_free(ssl);
        s.ssl = nullptr;
    }
#endif
    if (s.fd >= 0) close(s.fd);
    s.fd = -1;
    s.kind = SockKind::NONE;
    if (handle - 1 < (int)g_openSockets.size()) g_openSockets[handle - 1] = -1;
}

// ---------- BigInt (string-based decimal) ----------
// Format: optional '-' prefix, then digits with no leading zeros (except "0").
static string bigintNormalize(string s) {
    bool neg = false;
    if (!s.empty() && s[0] == '-') { neg = true; s = s.substr(1); }
    else if (!s.empty() && s[0] == '+') s = s.substr(1);
    size_t p = s.find('.');
    if (p != string::npos) s = s.substr(0, p);  // truncate fractional
    size_t nonZero = s.find_first_not_of('0');
    if (nonZero == string::npos) return "0";
    s = s.substr(nonZero);
    // keep only digits
    string digits;
    for (char c : s) if (c >= '0' && c <= '9') digits += c;
    if (digits.empty()) return "0";
    if (neg && digits != "0") return "-" + digits;
    return digits;
}
static bool bigintIsNeg(const string& s) { return !s.empty() && s[0] == '-'; }
static string bigintAbs(string s) {
    if (!s.empty() && s[0] == '-') return s.substr(1);
    return s;
}
static bool bigintLess(const string& a, const string& b) {
    // |a| < |b|
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}
static string bigintAddAbs(string a, string b) {
    // a, b are non-negative digit strings
    string out;
    int i = (int)a.size() - 1, j = (int)b.size() - 1, carry = 0;
    while (i >= 0 || j >= 0 || carry) {
        int x = (i >= 0 ? a[i--] - '0' : 0);
        int y = (j >= 0 ? b[j--] - '0' : 0);
        int s = x + y + carry;
        out += char('0' + (s % 10));
        carry = s / 10;
    }
    reverse(out.begin(), out.end());
    return out;
}
static string bigintSubAbs(string a, string b) {
    // a >= b, both non-negative
    string out;
    int i = (int)a.size() - 1, j = (int)b.size() - 1, borrow = 0;
    while (i >= 0) {
        int x = a[i--] - '0' - borrow;
        int y = (j >= 0 ? b[j--] - '0' : 0);
        if (x < y) { x += 10; borrow = 1; } else borrow = 0;
        out += char('0' + (x - y));
    }
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    reverse(out.begin(), out.end());
    return out;
}
static string bigintAdd(string a, string b) {
    a = bigintNormalize(a); b = bigintNormalize(b);
    bool na = bigintIsNeg(a), nb = bigintIsNeg(b);
    string aa = bigintAbs(a), bb = bigintAbs(b);
    if (!na && !nb) return bigintAddAbs(aa, bb);
    if (na && nb)  return "-" + bigintAddAbs(aa, bb);
    // mixed signs: subtract smaller from bigger
    if (bigintLess(aa, bb)) return (nb ? "-" : "") + bigintSubAbs(bb, aa);
    return (na ? "-" : "") + bigintSubAbs(aa, bb);
}
static string bigintSub(string a, string b) {
    return bigintAdd(a, b[0] == '-' ? b.substr(1) : "-" + b);
}
static string bigintMul(const string& a, const string& b) {
    string aa = bigintAbs(a), bb = bigintAbs(b);
    vector<int> prod(aa.size() + bb.size(), 0);
    for (int i = (int)aa.size() - 1; i >= 0; i--)
        for (int j = (int)bb.size() - 1; j >= 0; j--) {
            int p = (aa[i] - '0') * (bb[j] - '0') + prod[i + j + 1];
            prod[i + j + 1] = p % 10;
            prod[i + j] += p / 10;
        }
    string out;
    for (int v : prod) if (!out.empty() || v) out += char('0' + v);
    if (out.empty()) out = "0";
    if (bigintIsNeg(a) ^ bigintIsNeg(b)) {
        if (out != "0") out = "-" + out;
    }
    return out;
}
static int bigintCmp(string a, string b) {
    a = bigintNormalize(a); b = bigintNormalize(b);
    bool na = bigintIsNeg(a), nb = bigintIsNeg(b);
    if (na != nb) return na ? -1 : 1;
    int sign = na ? -1 : 1;
    string aa = bigintAbs(a), bb = bigintAbs(b);
    if (aa.size() != bb.size()) return sign * (aa.size() < bb.size() ? -1 : 1);
    if (aa != bb) return sign * (aa < bb ? -1 : 1);
    return 0;
}
static string bigintDiv(string a, string b) {
    a = bigintNormalize(a); b = bigintNormalize(b);
    if (b == "0") return "0";
    bool na = bigintIsNeg(a), nb = bigintIsNeg(b);
    string aa = bigintAbs(a), bb = bigintAbs(b);
    string out; string rem;
    for (char c : aa) {
        rem += c;
        rem = bigintNormalize(rem);
        int d = 0;
        while (rem != "0" && !bigintLess(rem, bb)) {
            rem = bigintSubAbs(rem, bb);
            d++;
        }
        if (!out.empty() || d) out += char('0' + d);
    }
    if (out.empty()) out = "0";
    if (na ^ nb) out = "-" + out;
    return bigintNormalize(out);
}
static string bigintMod(string a, string b) {
    string q = bigintDiv(a, b);
    string r = bigintSub(a, bigintMul(q, b));
    return bigintNormalize(r);
}
static string bigintPow(string a, string n) {
    a = bigintNormalize(a); n = bigintNormalize(n);
    if (n[0] == '-') return "0";
    string result = "1";
    string nn = n;
    while (nn != "0") {
        if ((nn.back() - '0') % 2 == 1) result = bigintMul(result, a);
        a = bigintMul(a, a);
        nn = bigintDiv(nn, "2");
        if (nn == "0") break;
    }
    return result;
}
static string bigintShl(string a, int n) {
    if (a == "0") return "0";
    string r = bigintAbs(a);
    while (n-- > 0) r += '0';
    return (bigintIsNeg(a) ? "-" : "") + bigintNormalize(r);
}
static string bigintShr(string a, int n) {
    // Integer division by 2^n (floor toward zero)
    string divisor = "1";
    for (int i = 0; i < n; i++) divisor += '0';
    return bigintDiv(a, divisor);
}
static string bigintBand(string a, string b) {
    // bit-by-bit on absolute value, two's complement approximation
    // Simpler approach: convert to binary string, AND, convert back.
    // For correctness with negatives we'd need two's complement; here we
    // do straightforward non-negative AND and treat negatives by their abs.
    if (bigintIsNeg(a) || bigintIsNeg(b)) {
        // Fallback: AND of absolute values
        a = bigintAbs(a); b = bigintAbs(b);
    }
    // to binary
    auto toBin = [](string s) -> string {
        if (s == "0") return "0";
        string out;
        while (s != "0") {
            string q = bigintDiv(s, "2");
            string r = bigintSub(s, bigintMul(q, "2"));
            out += (r == "1" ? '1' : '0');
            s = q;
        }
        reverse(out.begin(), out.end());
        return out;
    };
    auto fromBin = [](const string& bin) -> string {
        string result = "0";
        for (char c : bin) {
            result = bigintMul(result, "2");
            if (c == '1') result = bigintAdd(result, "1");
        }
        return result;
    };
    string ba = toBin(a), bb = toBin(b);
    int la = ba.size(), lb = bb.size();
    int n = max(la, lb);
    string pa = string(n - la, '0') + ba;
    string pb = string(n - lb, '0') + bb;
    string result;
    for (int i = 0; i < n; i++) result += ((pa[i] == '1' && pb[i] == '1') ? '1' : '0');
    return fromBin(result);
}
static string bigintBor(string a, string b) {
    if (bigintIsNeg(a)) a = bigintAbs(a);
    if (bigintIsNeg(b)) b = bigintAbs(b);
    auto toBin = [](string s) -> string {
        if (s == "0") return "0";
        string out;
        while (s != "0") {
            string q = bigintDiv(s, "2");
            string r = bigintSub(s, bigintMul(q, "2"));
            out += (r == "1" ? '1' : '0');
            s = q;
        }
        reverse(out.begin(), out.end());
        return out;
    };
    auto fromBin = [](const string& bin) -> string {
        string result = "0";
        for (char c : bin) {
            result = bigintMul(result, "2");
            if (c == '1') result = bigintAdd(result, "1");
        }
        return result;
    };
    string ba = toBin(a), bb = toBin(b);
    int la = ba.size(), lb = bb.size();
    int n = max(la, lb);
    string pa = string(n - la, '0') + ba;
    string pb = string(n - lb, '0') + bb;
    string result;
    for (int i = 0; i < n; i++) result += ((pa[i] == '1' || pb[i] == '1') ? '1' : '0');
    return fromBin(result);
}
static string bigintBxor(string a, string b) {
    if (bigintIsNeg(a)) a = bigintAbs(a);
    if (bigintIsNeg(b)) b = bigintAbs(b);
    auto toBin = [](string s) -> string {
        if (s == "0") return "0";
        string out;
        while (s != "0") {
            string q = bigintDiv(s, "2");
            string r = bigintSub(s, bigintMul(q, "2"));
            out += (r == "1" ? '1' : '0');
            s = q;
        }
        reverse(out.begin(), out.end());
        return out;
    };
    auto fromBin = [](const string& bin) -> string {
        string result = "0";
        for (char c : bin) {
            result = bigintMul(result, "2");
            if (c == '1') result = bigintAdd(result, "1");
        }
        return result;
    };
    string ba = toBin(a), bb = toBin(b);
    int la = ba.size(), lb = bb.size();
    int n = max(la, lb);
    string pa = string(n - la, '0') + ba;
    string pb = string(n - lb, '0') + bb;
    string result;
    for (int i = 0; i < n; i++) result += ((pa[i] != pb[i]) ? '1' : '0');
    return fromBin(result);
}
static string bigintToHex(const string& a) {
    string v = bigintAbs(a);
    if (v == "0") return "0";
    string hex;
    while (v != "0") {
        string q = bigintDiv(v, "16");
        string r = bigintSub(v, bigintMul(q, "16"));
        int d = atoi(r.c_str());
        hex += (d < 10 ? char('0' + d) : char('a' + d - 10));
        v = q;
    }
    reverse(hex.begin(), hex.end());
    return (bigintIsNeg(a) ? "-" : "") + hex;
}
static string bigintFromHex(string s) {
    bool neg = false;
    if (!s.empty() && s[0] == '-') { neg = true; s = s.substr(1); }
    string result = "0";
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else continue;
        result = bigintMul(result, "16");
        result = bigintAdd(result, to_string(d));
    }
    return (neg && result != "0") ? "-" + result : result;
}


// ---------- SQLite handle table ----------
#ifndef OCODE_NO_SQLITE
#include <sqlite3.h>
#endif
static vector<sqlite3*> g_openDbs;
static vector<sqlite3_stmt*> g_openStmts;
static int dbRegister(sqlite3* h) {
    for (size_t i = 0; i < g_openDbs.size(); i++)
        if (!g_openDbs[i]) { g_openDbs[i] = h; return (int)i + 1; }
    g_openDbs.push_back(h); return (int)g_openDbs.size();
}
static sqlite3* dbLookup(int h) {
    if (h < 1 || h > (int)g_openDbs.size()) return nullptr;
    return g_openDbs[h - 1];
}
static int dbCloseAt(int h) {
    sqlite3* p = dbLookup(h);
    if (!p) return 0;
    sqlite3_close(p);
    g_openDbs[h - 1] = nullptr;
    return 0;
}
static int stmtRegister(sqlite3_stmt* s) {
    for (size_t i = 0; i < g_openStmts.size(); i++)
        if (!g_openStmts[i]) { g_openStmts[i] = s; return (int)i + 1; }
    g_openStmts.push_back(s); return (int)g_openStmts.size();
}
static sqlite3_stmt* stmtLookup(int h) {
    if (h < 1 || h > (int)g_openStmts.size()) return nullptr;
    return g_openStmts[h - 1];
}

// ---------- Channels / Mutexes / Threads ----------
struct OcChannel {
    queue<Value> q;
    mutex m;
    condition_variable cv;
    size_t cap = 0;  // 0 = unbounded
};
static vector<shared_ptr<OcChannel>> g_channels;
static vector<shared_ptr<mutex>> g_mutexes;
static vector<shared_ptr<thread>> g_threads;
static vector<Value> g_threadResults;
static vector<bool> g_threadDone;

// ---------- Timers ----------
static map<string, chrono::steady_clock::time_point> g_timers;

// ---------- Error tracking ----------
static string g_lastErrorMsg;
static int    g_lastErrorCode = 0;

// ---------- Default HTTP headers & timeout (for http_set_*) ----------
static vector<pair<string,string>> g_defaultHeaders;
static int g_defaultHttpTimeout = 30;

// ---------- Saved vars storage ----------
static string savedVarsDir() {
    const char* home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = ".";
    string base = string(home) + "/.ocode/saved_vars";
    fs::create_directories(base);
    return base;
}
static string savedVarPath(const string& name) {
    string safe;
    for (char c : name) if (isalnum((unsigned char)c) || c == '_' || c == '-') safe += c;
    if (safe.empty()) safe = "var";
    return savedVarsDir() + "/" + safe + ".json";
}

static void socketCloseAll() {
    for (size_t i = 0; i < g_socks.size(); i++) {
        OpenSock& s = g_socks[i];
        if (s.kind == SockKind::NONE) continue;
#ifndef OCODE_NO_OPENSSL
        if (s.ssl) {
            SSL* ssl = (SSL*)s.ssl;
            SSL_shutdown(ssl);
            SSL_free(ssl);
            s.ssl = nullptr;
        }
#endif
        if (s.fd >= 0) close(s.fd);
        s.fd = -1;
        s.kind = SockKind::NONE;
    }
    for (size_t i = 0; i < g_openSockets.size(); i++) g_openSockets[i] = -1;
}

static int socketOpen(const string& host, int port, int timeoutSec, int line) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    string portStr = to_string(port);
    int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        fail(string("socket_open: DNS lookup failed for '") + host + "': " + gai_strerror(gai), line);
    }
    int sock = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        struct timeval tv; tv.tv_sec = timeoutSec; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock); sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) {
        fail("socket_open: could not connect to " + host + ":" + to_string(port), line);
    }
    return socketRegister(sock);
}

static int socketSend(int handle, const string& msg, int line) {
    OpenSock* s = socketLookup(handle);
    if (!s) fail("send: invalid handle (not an open socket)", line);
    if (s->kind == SockKind::SSL || s->kind == SockKind::WS) {
#ifndef OCODE_NO_OPENSSL
        SSL* ssl = (SSL*)s->ssl;
        if (!ssl) fail("send: SSL handle is corrupt", line);
        const char* data = msg.data();
        size_t remaining = msg.size();
        size_t total = 0;
        while (remaining > 0) {
            int n = SSL_write(ssl, data, (int)remaining);
            if (n <= 0) fail("send: SSL write failed", line);
            data += n; remaining -= (size_t)n; total += (size_t)n;
        }
        return (int)total;
#else
        fail("send: SSL not available (rebuilt without OpenSSL)", line);
#endif
    }
    int sock = s->fd;
    const char* data = msg.data();
    size_t remaining = msg.size();
    size_t total = 0;
    while (remaining > 0) {
        ssize_t n = ::send(sock, data, remaining, 0);
        if (n <= 0) fail("send: send failed", line);
        data += n; remaining -= (size_t)n; total += (size_t)n;
    }
    return (int)total;
}

static string socketRecv(int handle, int maxBytes, int line) {
    OpenSock* s = socketLookup(handle);
    if (!s) fail("receive: invalid handle (not an open socket)", line);
    bool isSsl = (s->kind == SockKind::SSL || s->kind == SockKind::WS);
    int sock = s->fd;
    string out;
    char buf[8192];

    if (maxBytes > 0) {
        while ((int)out.size() < maxBytes) {
            int want = min((int)sizeof(buf), maxBytes - (int)out.size());
            ssize_t n;
#ifndef OCODE_NO_OPENSSL
            if (isSsl) {
                n = SSL_read((SSL*)s->ssl, buf, want);
                if (n <= 0) {
                    int e = SSL_get_error((SSL*)s->ssl, (int)n);
                    if (e == SSL_ERROR_ZERO_RETURN || e == SSL_ERROR_NONE) break;
                    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) break;
                    break;
                }
            } else
#endif
                n = recv(sock, buf, want, 0);
            if (n < 0) break;
            if (n == 0) break;
            out.append(buf, (size_t)n);
        }
        return out;
    }

    struct timeval savedTv; socklen_t savedLen = sizeof(savedTv);
    getsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &savedTv, &savedLen);
    struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (true) {
        ssize_t n;
#ifndef OCODE_NO_OPENSSL
        if (isSsl) {
            n = SSL_read((SSL*)s->ssl, buf, sizeof(buf));
            if (n <= 0) {
                int e = SSL_get_error((SSL*)s->ssl, (int)n);
                if (e == SSL_ERROR_ZERO_RETURN || e == SSL_ERROR_NONE) break;
                if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) break;
                break;
            }
        } else
#endif
            n = recv(sock, buf, sizeof(buf), 0);
        if (n < 0) break;
        if (n == 0) break;
        out.append(buf, (size_t)n);
    }
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &savedTv, savedLen);
    return out;
}

static string socketRecvLine(int handle, int line) {
    OpenSock* s = socketLookup(handle);
    if (!s) fail("readline: invalid handle (not an open socket)", line);
    bool isSsl = (s->kind == SockKind::SSL || s->kind == SockKind::WS);
    int sock = s->fd;
    string out;
    char c;
    while (true) {
        ssize_t n;
#ifndef OCODE_NO_OPENSSL
        if (isSsl) {
            n = SSL_read((SSL*)s->ssl, &c, 1);
            if (n <= 0) break;
        } else
#endif
            n = recv(sock, &c, 1, 0);
        if (n <= 0) break;
        out.push_back(c);
        if (c == '\n') break;
    }
    return out;
}

#ifndef OCODE_NO_OPENSSL
static int sslSocketOpen(const string& host, int port, int timeoutSec, int line) {
    ensureSslInit();
    if (!g_sslCtx) fail("ssl_socket: SSL_CTX_new failed", line);

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    string portStr = to_string(port);
    int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        fail(string("ssl_socket: DNS lookup failed for '") + host + "': " + gai_strerror(gai), line);
    }
    int sock = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        struct timeval tv; tv.tv_sec = timeoutSec; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock); sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) {
        fail("ssl_socket: could not connect to " + host + ":" + to_string(port), line);
    }

    SSL* ssl = SSL_new(g_sslCtx);
    if (!ssl) { close(sock); fail("ssl_socket: SSL_new failed", line); }
    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) != 1) {
        unsigned long e = ERR_get_error();
        string detail = e ? ERR_error_string(e, nullptr) : "unknown TLS error";
        SSL_free(ssl); close(sock);
        fail("ssl_socket: TLS handshake failed for " + host + ":" + to_string(port) + " — " + detail, line);
    }
    return socketRegisterSsl(sock, ssl);
}
#endif

static string wsMakeKey() {
    static const char* hex = "0123456789abcdef";
    string out;
    for (int i = 0; i < 16; i++) out.push_back(hex[rand() % 16]);
    return out;
}

static int wsConnect(const string& url, int timeoutSec, int line) {
    string scheme, host, path;
    int port = 80;
    bool isSecure = false;

    string u = url;
    if (u.rfind("wss://", 0) == 0) { isSecure = true; scheme = "wss"; u = u.substr(6); }
    else if (u.rfind("ws://", 0) == 0) { scheme = "ws"; u = u.substr(5); }
    else fail("ws_connect: URL must start with ws:// or wss://", line);

    string hostPort = u;
    string fullPath = "/";
    size_t slash = hostPort.find('/');
    if (slash != string::npos) {
        fullPath = hostPort.substr(slash);
        hostPort = hostPort.substr(0, slash);
    }
    size_t colon = hostPort.rfind(':');
    if (colon != string::npos) {
        host = hostPort.substr(0, colon);
        try { port = stoi(hostPort.substr(colon + 1)); } catch (...) { port = isSecure ? 443 : 80; }
    } else {
        host = hostPort;
        port = isSecure ? 443 : 80;
    }
    if (host.empty()) fail("ws_connect: no host in URL", line);

    int sock = -1;
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    string portStr = to_string(port);
    int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        fail(string("ws_connect: DNS lookup failed for '") + host + "': " + gai_strerror(gai), line);
    }
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        struct timeval tv; tv.tv_sec = timeoutSec; tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sock); sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) fail("ws_connect: could not connect to " + host + ":" + to_string(port), line);

    void* sslPtr = nullptr;
    if (isSecure) {
#ifndef OCODE_NO_OPENSSL
        ensureSslInit();
        if (!g_sslCtx) { close(sock); fail("ws_connect: SSL_CTX_new failed", line); }
        SSL* ssl = SSL_new(g_sslCtx);
        if (!ssl) { close(sock); fail("ws_connect: SSL_new failed", line); }
        SSL_set_tlsext_host_name(ssl, host.c_str());
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) != 1) {
            unsigned long e = ERR_get_error();
            string detail = e ? ERR_error_string(e, nullptr) : "unknown TLS error";
            SSL_free(ssl); close(sock);
            fail("ws_connect: TLS handshake failed — " + detail, line);
        }
        sslPtr = ssl;
#else
        close(sock);
        fail("ws_connect: wss:// requires OpenSSL (rebuilt with -DOCODE_NO_OPENSSL)", line);
#endif
    }

    string key = wsMakeKey();
    string req = "GET " + fullPath + " HTTP/1.1\r\n"
                 "Host: " + host + (port == 80 || port == 443 ? "" : ":" + to_string(port)) + "\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Key: " + key + "\r\n"
                 "Sec-WebSocket-Version: 13\r\n"
                 "User-Agent: OCode\r\n"
                 "\r\n";

    bool sendOk = false;
    if (sslPtr) {
#ifndef OCODE_NO_OPENSSL
        SSL* ssl = (SSL*)sslPtr;
        int total = 0;
        const char* d = req.data();
        size_t rem = req.size();
        while (rem > 0) {
            int n = SSL_write(ssl, d, (int)rem);
            if (n <= 0) break;
            d += n; rem -= (size_t)n; total += n;
        }
        sendOk = (rem == 0);
#endif
    } else {
        size_t rem = req.size(); const char* d = req.data();
        while (rem > 0) {
            ssize_t n = ::send(sock, d, rem, 0);
            if (n <= 0) break;
            d += n; rem -= (size_t)n;
        }
        sendOk = (rem == 0);
    }
    if (!sendOk) {
#ifndef OCODE_NO_OPENSSL
        if (sslPtr) { SSL_free((SSL*)sslPtr); }
#endif
        close(sock);
        fail("ws_connect: failed to send handshake", line);
    }

    string resp;
    char c;
    while (resp.size() < 8192) {
        ssize_t n;
        if (sslPtr) {
#ifndef OCODE_NO_OPENSSL
            n = SSL_read((SSL*)sslPtr, &c, 1);
#else
            n = 0;
#endif
        } else {
            n = recv(sock, &c, 1, 0);
        }
        if (n <= 0) break;
        resp.push_back(c);
        if (resp.size() >= 4 && resp.substr(resp.size() - 4) == "\r\n\r\n") break;
    }
    if (resp.find("101") == string::npos) {
#ifndef OCODE_NO_OPENSSL
        if (sslPtr) { SSL_free((SSL*)sslPtr); }
#endif
        close(sock);
        fail("ws_connect: server did not upgrade to WebSocket — response:\n" + resp.substr(0, 256), line);
    }

    return socketRegisterWs(sock, sslPtr);
}

static int wsSendFrame(int handle, const string& payload, int opcode, int line) {
    OpenSock* s = socketLookup(handle);
    if (!s) fail("ws_send: invalid handle (not an open socket)", line);
    if (s->kind != SockKind::WS) fail("ws_send: handle is not a WebSocket", line);

    string frame;
    frame.push_back((char)(0x80 | (opcode & 0x0F)));
    size_t len = payload.size();
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (unsigned char)(rand() & 0xFF);
    if (len <= 125) {
        frame.push_back((char)(0x80 | (unsigned char)len));
    } else if (len <= 65535) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((char)((len >> 8) & 0xFF));
        frame.push_back((char)(len & 0xFF));
    } else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; i--) frame.push_back((char)((len >> (8 * i)) & 0xFF));
    }
    frame.append((char*)mask, 4);
    for (size_t i = 0; i < payload.size(); i++) {
        frame.push_back(payload[i] ^ mask[i % 4]);
    }

    if (s->ssl) {
#ifndef OCODE_NO_OPENSSL
        SSL* ssl = (SSL*)s->ssl;
        const char* d = frame.data();
        size_t rem = frame.size();
        while (rem > 0) {
            int n = SSL_write(ssl, d, (int)rem);
            if (n <= 0) fail("ws_send: SSL write failed", line);
            d += n; rem -= (size_t)n;
        }
        return (int)payload.size();
#else
        fail("ws_send: SSL not available (rebuilt without OpenSSL)", line);
#endif
    }
    const char* d = frame.data();
    size_t rem = frame.size();
    while (rem > 0) {
        ssize_t n = ::send(s->fd, d, rem, 0);
        if (n <= 0) fail("ws_send: send failed", line);
        d += n; rem -= (size_t)n;
    }
    return (int)payload.size();
}

static string wsRecvFrame(int handle, int line) {
    OpenSock* s = socketLookup(handle);
    if (!s) fail("ws_recv: invalid handle (not an open socket)", line);
    if (s->kind != SockKind::WS) fail("ws_recv: handle is not a WebSocket", line);

    auto readByte = [&]() -> int {
        unsigned char c;
        ssize_t n;
        if (s->ssl) {
#ifndef OCODE_NO_OPENSSL
            n = SSL_read((SSL*)s->ssl, &c, 1);
#else
            n = 0;
#endif
        } else {
            n = recv(s->fd, &c, 1, 0);
        }
        if (n <= 0) return -1;
        return c;
    };
    auto readN = [&](char* buf, size_t n) -> bool {
        for (size_t i = 0; i < n; i++) {
            int b = readByte();
            if (b < 0) return false;
            buf[i] = (char)b;
        }
        return true;
    };

    string out;
    while (true) {
        int b0 = readByte();
        if (b0 < 0) return out;
        int b1 = readByte();
        if (b1 < 0) return out;
        bool fin = (b0 & 0x80) != 0;
        int opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        size_t payloadLen = (size_t)(b1 & 0x7F);
        if (payloadLen == 126) {
            char ext[2];
            if (!readN(ext, 2)) return out;
            payloadLen = ((unsigned char)ext[0] << 8) | (unsigned char)ext[1];
        } else if (payloadLen == 127) {
            char ext[8];
            if (!readN(ext, 8)) return out;
            payloadLen = 0;
            for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | (unsigned char)ext[i];
        }
        unsigned char mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (!readN((char*)mask, 4)) return out;
        }
        string payload;
        payload.reserve(payloadLen);
        for (size_t i = 0; i < payloadLen; i++) {
            int b = readByte();
            if (b < 0) return out;
            payload.push_back(masked ? (char)((unsigned char)b ^ mask[i % 4]) : (char)b);
        }

        if (opcode == 0x8) {
            string closeFrame;
            closeFrame.push_back((char)0x88);
            closeFrame.push_back((char)0x00);
            if (s->ssl) {
#ifndef OCODE_NO_OPENSSL
                SSL_write((SSL*)s->ssl, closeFrame.data(), (int)closeFrame.size());
#endif
            } else {
                ::send(s->fd, closeFrame.data(), closeFrame.size(), 0);
            }
            return payload;
        }
        if (opcode == 0x9) {
            wsSendFrame(handle, payload, 0xA, line);
            continue;
        }
        if (opcode == 0xA) continue;
        out += payload;
        if (fin) break;
    }
    return out;
}

struct FuncDef { vector<string> params; vector<StmtPtr> body; };
struct ReturnSignal { Value value; };

struct Env {
    unordered_map<string, Value> vars;
    Env* parent = nullptr;
    Value* find(const string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        return parent ? parent->find(name) : nullptr;
    }
    void set(const string& name, const Value& v) {
        Env* e = this;
        while (e) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) { it->second = v; return; }
            e = e->parent;
        }
        vars[name] = v;
    }
};

static size_t jsonSkipWs(const string& s, size_t i) {
    while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++;
    return i;
}
static Value jsonParseValue(const string& s, size_t& i, int line);
static string jsonSerialize(const Value& v, int line);

static Value jsonParseString(const string& s, size_t& i, int line) {

    i++;
    string result;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i+1 < s.size()) {
            char esc = s[i+1];
            switch (esc) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'u': {
                    if (i+5 >= s.size()) fail("invalid \\u escape in JSON", line);
                    long cp = strtol(s.substr(i+2, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) result += (char)cp;
                    else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: result += esc;
            }
            i += 2;
        } else result += s[i++];
    }
    if (i >= s.size()) fail("unterminated string in JSON", line);
    i++;
    return Value::Str(result);
}

static Value jsonParseValue(const string& s, size_t& i, int line) {
    i = jsonSkipWs(s, i);
    if (i >= s.size()) fail("unexpected end of JSON", line);
    char c = s[i];
    if (c == '"') return jsonParseString(s, i, line);
    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = i;
        if (s[i] == '-') i++;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i]=='.' || s[i]=='e' || s[i]=='E' || s[i]=='+' || s[i]=='-')) i++;
        try { return Value::Number(stod(s.substr(start, i-start))); }
        catch (...) { fail("invalid number in JSON: " + s.substr(start, i-start), line); }
    }
    if (c == 't' && s.substr(i,4)=="true")  { i += 4; return Value::Bool(true); }
    if (c == 'f' && s.substr(i,5)=="false") { i += 5; return Value::Bool(false); }
    if (c == 'n' && s.substr(i,4)=="null")  { i += 4; return Value::Number(0); }
    if (c == '[') {
        i++;
        auto vec = make_shared<vector<Value>>();
        i = jsonSkipWs(s, i);
        if (i < s.size() && s[i] == ']') { i++; return Value::List(vec); }
        while (true) {
            vec->push_back(jsonParseValue(s, i, line));
            i = jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; break; }
            fail("expected ',' or ']' in JSON array", line);
        }
        return Value::List(vec);
    }
    if (c == '{') {
        i++;
        auto d = make_shared<DictData>();
        i = jsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') { i++; return Value::Dict(d); }
        while (true) {
            i = jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != '"') fail("expected string key in JSON object", line);
            Value kv = jsonParseString(s, i, line);
            i = jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ':') fail("expected ':' after key in JSON object", line);
            i++;
            Value vv = jsonParseValue(s, i, line);
            d->set(kv.str, vv);
            i = jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; break; }
            fail("expected ',' or '}' in JSON object", line);
        }
        return Value::Dict(d);
    }
    fail(string("unexpected character '") + c + "' in JSON", line);
}

static string jsonSerialize(const Value& v, int line) {
    switch (v.type) {
        case Value::Type::NUMBER: {
            if (v.num == (long long)v.num) return to_string((long long)v.num);
            ostringstream o; o << v.num; return o.str();
        }
        case Value::Type::STRING: {
            string out = "\"";
            for (char c : v.str) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    case '\t': out += "\\t";  break;
                    case '\r': out += "\\r";  break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    default:
                        if ((unsigned char)c < 0x20) {
                            char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c); out += buf;
                        } else out += c;
                }
            }
            return out + "\"";
        }
        case Value::Type::BOOL: return v.bl ? "true" : "false";
        case Value::Type::LIST: {
            string out = "[";
            for (size_t i = 0; i < v.list->size(); i++) {
                if (i) out += ",";
                out += jsonSerialize((*v.list)[i], line);
            }
            return out + "]";
        }
        case Value::Type::DICT: {
            string out = "{";
            for (size_t i = 0; i < v.dict->entries.size(); i++) {
                if (i) out += ",";
                out += jsonSerialize(Value::Str(v.dict->entries[i].first), line);
                out += ":";
                out += jsonSerialize(v.dict->entries[i].second, line);
            }
            return out + "}";
        }
    }
    return "";
}

struct Interpreter {
    unordered_map<string, FuncDef> functions;
    Env globals;
    mt19937 rng{(unsigned)chrono::steady_clock::now().time_since_epoch().count()};

    void run(vector<StmtPtr>& program) {
        for (auto& s : program)
            if (s->kind == StmtKind::FUNC_DEF) functions[s->name] = {s->params, s->body};
        for (auto& s : program)
            if (s->kind != StmtKind::FUNC_DEF) execStmt(s, globals);
    }

    void execBlock(vector<StmtPtr>& body, Env& env) {
        for (auto& s : body) execStmt(s, env);
    }

    void execStmt(StmtPtr& s, Env& env) {
        switch (s->kind) {
            case StmtKind::LET:   { env.set(s->name, eval(s->expr, env)); break; }
            case StmtKind::SAY:   { cout << valueToString(eval(s->expr, env)) << "\n"; break; }
            case StmtKind::SAY_INLINE: { cout << valueToString(eval(s->expr, env)); break; }
            case StmtKind::IF: {
                if (valueToBool(eval(s->expr, env))) { execBlock(s->body, env); break; }
                bool done = false;
                for (auto& [cond, body] : s->elseIfs) {
                    if (valueToBool(eval(cond, env))) { auto b = body; execBlock(b, env); done = true; break; }
                }
                if (!done) execBlock(s->elseBody, env);
                break;
            }
            case StmtKind::WHILE: {
                while (valueToBool(eval(s->expr, env))) {
                    try { execBlock(s->body, env); }
                    catch (BreakSignal&) { break; }
                    catch (SkipSignal&)  { continue; }
                }
                break;
            }
            case StmtKind::FOR_EACH: {
                Value iterVal = eval(s->expr, env);
                if (iterVal.type == Value::Type::LIST) {
                    for (auto& item : *iterVal.list) {
                        env.set(s->iterVar, item);
                        try { execBlock(s->body, env); }
                        catch (BreakSignal&) { break; }
                        catch (SkipSignal&)  { continue; }
                    }
                } else if (iterVal.type == Value::Type::DICT) {

                    auto snap = iterVal.dict->entries;

                    for (auto& [k, v] : snap) {
                        env.set(s->iterVar, Value::Str(k));
                        if (!s->iterVar2.empty()) env.set(s->iterVar2, v);
                        try { execBlock(s->body, env); }
                        catch (BreakSignal&) { break; }
                        catch (SkipSignal&)  { continue; }
                    }
                } else {
                    fail("'for each' requires a list or dict", s->line);
                }
                break;
            }
            case StmtKind::REPEAT: {
                long long n = (long long)valueToNumber(eval(s->expr, env), s->line);
                for (long long i = 0; i < n; i++) {
                    try { execBlock(s->body, env); }
                    catch (BreakSignal&) { break; }
                    catch (SkipSignal&)  { continue; }
                }
                break;
            }
            case StmtKind::FUNC_DEF: break;
            case StmtKind::RETURN: { throw ReturnSignal{ s->expr ? eval(s->expr, env) : Value::Number(0) }; }
            case StmtKind::ADD_TO: {
                Value* target = env.find(s->name);
                if (!target || target->type != Value::Type::LIST) fail("'" + s->name + "' is not a list", s->line);
                target->list->push_back(eval(s->expr, env));
                break;
            }
            case StmtKind::REMOVE_AT: {
                Value* target = env.find(s->name);
                if (!target || target->type != Value::Type::LIST) fail("'" + s->name + "' is not a list", s->line);
                long long idx = (long long)valueToNumber(eval(s->expr, env), s->line);
                if (idx < 0 || idx >= (long long)target->list->size()) fail("list index out of range", s->line);
                target->list->erase(target->list->begin() + idx);
                break;
            }
            case StmtKind::SET_AT: {

                if (s->expr->kind != ExprKind::INDEX) fail("set_at target must be an index expression", s->line);
                ExprPtr targetExpr = s->expr->left;
                if (targetExpr->kind != ExprKind::VAR) fail("can only mutate an indexed variable (e.g. 'dict at key be value')", s->line);
                Value* target = env.find(targetExpr->name);
                if (!target) fail("undefined variable '" + targetExpr->name + "'", s->line);

                if (target->type == Value::Type::DICT) {
                    Value k = eval(s->expr->right, env);
                    if (k.type != Value::Type::STRING) fail("dict key must be a string", s->line);
                    Value nv = eval(s->value, env);
                    target->dict->set(k.str, nv);
                } else if (target->type == Value::Type::LIST) {
                    long long idx = (long long)valueToNumber(eval(s->expr->right, env), s->line);
                    if (idx < 0 || idx >= (long long)target->list->size()) fail("list index out of range", s->line);
                    (*target->list)[idx] = eval(s->value, env);
                } else {
                    fail("can only mutate a list or dict at an index", s->line);
                }
                break;
            }
            case StmtKind::EXPR_STMT: eval(s->expr, env); break;
            case StmtKind::USE: {
                execUse(s->name, s->line, env);
                break;
            }
            case StmtKind::TRY: {

                try {
                    execBlock(s->body, env);
                } catch (OCodeError& e) {
                    if (!s->iterVar.empty()) env.set(s->iterVar, Value::Str(e.what()));
                    execBlock(s->elseBody, env);
                }
                break;
            }
            case StmtKind::PYTHON_BLOCK: {
                execPythonBlock(s->name, s->line);
                break;
            }
            case StmtKind::BREAK: throw BreakSignal{};
            case StmtKind::SKIP:  throw SkipSignal{};
        }
    }

    static string findPython() {
        if (system("python3 --version >/dev/null 2>&1") == 0) return "python3";
        if (system("python --version >/dev/null 2>&1") == 0) return "python";
        return "";
    }

    struct PySession {
        FILE* wr = nullptr;
        FILE* rd = nullptr;
        pid_t pid = 0;
        bool alive = false;

        static const char* bootstrap() {
            return
                "import sys\n"
                "def _oc_main():\n"
                "    buf = ''\n"
                "    while True:\n"
                "        line = sys.stdin.readline()\n"
                "        if not line:\n"
                "            break\n"
                "        if line.rstrip('\\n') == '__OCODE_EXEC_END__':\n"
                "            try:\n"
                "                exec(compile(buf, '<pyoc>', 'exec'))\n"
                "            except Exception as e:\n"
                "                sys.stderr.write('__OCODE_ERR__ ' + type(e).__name__ + ': ' + str(e) + '\\n')\n"
                "                sys.stderr.flush()\n"
                "            sys.stdout.write('__OCODE_DONE__\\n')\n"
                "            sys.stdout.flush()\n"
                "            buf = ''\n"
                "        else:\n"
                "            buf += line\n"
                "_oc_main()\n";
        }

        void start(const string& pyCmd, int ocLine) {
            int inPipe[2], outPipe[2];
            if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
                fail("failed to create pipes for Python session", ocLine);
            pid = fork();
            if (pid < 0) fail("failed to fork Python process", ocLine);
            if (pid == 0) {

                close(inPipe[1]);
                close(outPipe[0]);
                dup2(inPipe[0], STDIN_FILENO);
                dup2(outPipe[1], STDOUT_FILENO);
                close(inPipe[0]);
                close(outPipe[1]);

                execlp(pyCmd.c_str(), pyCmd.c_str(), "-c", bootstrap(), nullptr);
                _exit(127);
            }

            close(inPipe[0]);
            close(outPipe[1]);
            wr = fdopen(inPipe[1], "w");
            rd = fdopen(outPipe[0], "r");
            setvbuf(rd, nullptr, _IONBF, 0);
            alive = true;
        }

        string exec(const string& code, int ocLine) {
            if (!alive) fail("Python session has died unexpectedly", ocLine);

            fputs(code.c_str(), wr);
            if (!code.empty() && code.back() != '\n') fputc('\n', wr);
            fputs("__OCODE_EXEC_END__\n", wr);
            fflush(wr);

            string output;
            char lineBuf[8192];
            while (true) {
                if (!fgets(lineBuf, sizeof(lineBuf), rd)) {
                    alive = false;
                    break;
                }
                string line = lineBuf;
                if (line == "__OCODE_DONE__\n") break;
                if (line.find("__OCODE_ERR__ ") == 0) {
                    fail("Python error (pyoc block at line " + to_string(ocLine) + "): " + line.substr(15), ocLine);
                }
                output += line;
            }
            return output;
        }

        void stop() {
            if (alive) {
                fclose(wr);
                fclose(rd);
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
                alive = false;
            }
        }

        ~PySession() { stop(); }
    };

    PySession pySession;

    vector<string> extractPythonImports(const string& code) {
        vector<string> modules;
        istringstream stream(code);
        string line;
        while (getline(stream, line)) {
            string t = trimStr(line);
            if (t.size() >= 7 && t.substr(0,7) == "import ") {
                string mod = t.substr(7);
                size_t dot = mod.find('.');
                if (dot != string::npos) mod = mod.substr(0, dot);
                size_t cm = mod.find(',');
                if (cm != string::npos) mod = mod.substr(0, cm);
                size_t as = mod.find(" as ");
                if (as != string::npos) mod = mod.substr(0, as);
                mod = trimStr(mod);
                if (!mod.empty()) modules.push_back(mod);
            } else if (t.size() >= 5 && t.substr(0,5) == "from ") {
                size_t ip = t.find(" import ");
                if (ip != string::npos) {
                    string mod = trimStr(t.substr(5, ip - 5));
                    size_t dot = mod.find('.');
                    if (dot != string::npos) mod = mod.substr(0, dot);
                    if (!mod.empty()) modules.push_back(mod);
                }
            }
        }
        return modules;
    }

    static const set<string>& pythonStdlib() {
        static const set<string> s = {
            "os","sys","re","math","json","time","datetime","collections",
            "itertools","functools","operator","typing","pathlib","io",
            "string","random","copy","hashlib","base64","urllib","http",
            "email","html","xml","csv","sqlite3","socket","ssl",
            "threading","multiprocessing","subprocess","logging",
            "unittest","argparse","configparser","tempfile","shutil",
            "glob","fnmatch","linecache","traceback","warnings",
            "contextlib","abc","numbers","decimal","fractions",
            "statistics","struct","codecs","binascii","array",
            "weakref","types","copyreg","pprint","reprlib",
            "enum","graphlib","zoneinfo","dataclasses","textwrap",
            "difflib","inspect","dis","ast","token","tokenize",
            "pickle","shelve","dbm","gzip","bz2","lzma","zipfile",
            "tarfile","zlib","ctypes","signal","mmap","select",
            "selectors","sched","queue","threading","_thread",
            "concurrent","asyncio","bisect","heapq"
        };
        return s;
    }

    void autoinstallMissingPackages(const string& code, const string& pyCmd, int ocLine) {
        vector<string> modules = extractPythonImports(code);
        auto& stdlib = pythonStdlib();
        for (auto& mod : modules) {
            if (stdlib.count(mod)) continue;
            string check = pyCmd + " -c \"import " + mod + "\" 2>/dev/null";
            if (system(check.c_str()) == 0) continue;
            cerr << "[ocd] Auto-installing Python package: " << mod << "\n";
            string pip = "pip3";
            if (system("pip3 --version >/dev/null 2>&1") != 0) pip = "pip";
            string cmd = pip + " install --quiet " + mod + " 2>&1";
            int r = system(cmd.c_str());
            if (r != 0) {
                fail("failed to auto-install Python package '" + mod + "'"
                     " (line " + to_string(ocLine) + "). "
                     "Run manually: " + pip + " install " + mod, ocLine);
            }
            cerr << "[ocd] Installed: " << mod << "\n";
        }
    }

    string translateOcodeLine(const string& raw) {
        string trimmed = trimStr(raw);
        if (trimmed.empty()) return raw;

        string line = trimmed;
        bool isOcode = false;
        string result;

        string ocodeComment;
        auto cpos = line.find("//");
        if (cpos != string::npos) {

            int inStr = 0;
            bool escaped = false;
            bool inComment = false;
            for (size_t i = 0; i < line.size(); i++) {
                if (escaped) { escaped = false; continue; }
                if (line[i] == '\\') { escaped = true; continue; }
                if (line[i] == '"' && !inComment) { inStr = !inStr; continue; }
                if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/' && !inStr) {
                    ocodeComment = "# " + line.substr(i + 2);
                    line = line.substr(0, i);
                    break;
                }
            }
        }
        line = trimStr(line);

        if (line == "say" || (line.size() > 4 && line.substr(0, 4) == "say ")) {
            isOcode = true;
            string arg = trimStr(line.substr(4));
            if (arg.empty()) arg = "";

            result = "print(" + translateExpr(arg) + ")";
        }

        else if (line.size() >= 10 && line.substr(0, 10) == "say_inline ") {
            isOcode = true;
            string arg = trimStr(line.substr(10));
            result = "print(" + translateExpr(arg) + ", end=\"\")";
        }

        else if (line.size() >= 4 && line.substr(0, 4) == "let ") {
            isOcode = true;
            string rest = trimStr(line.substr(4));
            auto bePos = rest.find(" be ");
            if (bePos == string::npos) bePos = rest.find("\tbe ");
            if (bePos != string::npos) {
                string name = trimStr(rest.substr(0, bePos));
                string val = trimStr(rest.substr(bePos + 4));
                result = name + " = " + translateExpr(val);
            } else {
                result = "# [pyoc: missing 'be' in let] " + raw;
            }
        }

        else if (line.size() >= 4 && line.substr(0, 4) == "add ") {
            isOcode = true;
            string rest = trimStr(line.substr(4));
            auto toPos = rest.rfind(" to ");
            if (toPos != string::npos) {
                string val = trimStr(rest.substr(0, toPos));
                string lst = trimStr(rest.substr(toPos + 4));
                result = lst + ".append(" + translateExpr(val) + ")";
            } else {
                result = "# [pyoc: missing 'to' in add] " + raw;
            }
        }

        else if (line.size() >= 7 && line.substr(0, 7) == "remove ") {
            isOcode = true;
            string rest = trimStr(line.substr(7));

            if (rest.size() >= 3 && rest.substr(0, 3) == "at ") {
                string afterAt = trimStr(rest.substr(3));
                auto fromPos = afterAt.rfind(" from ");
                if (fromPos != string::npos) {
                    string idx = trimStr(afterAt.substr(0, fromPos));
                    string lst = trimStr(afterAt.substr(fromPos + 6));
                    result = "del " + lst + "[" + translateExpr(idx) + "]";
                } else {
                    result = "# [pyoc: malformed remove (no 'from')] " + raw;
                }
            } else {
                result = "# [pyoc: malformed remove (no 'at')] " + raw;
            }
        }

        else if (line.size() >= 7 && line.substr(0, 7) == "return ") {
            isOcode = true;
            string expr = trimStr(line.substr(7));
            result = "return " + translateExpr(expr);
        }

        else if (line == "break") {
            isOcode = true;
            result = "break";
        }

        else if (line == "skip") {
            isOcode = true;
            result = "continue";
        }

        if (isOcode) {
            if (!ocodeComment.empty()) result += "  " + ocodeComment;
            return result;
        }

        if (!ocodeComment.empty()) {
            return line + "  " + ocodeComment;
        }
        return raw;
    }

    string translateExpr(const string& expr) {
        string s = trimStr(expr);
        if (s.empty()) return "";

        struct Tkn { string text; bool isWord; };
        vector<Tkn> tkns;
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == ' ' || s[i] == '        ') { i++; continue; }
            if (s[i] == '"') {
                string t; t += s[i]; i++;
                while (i < s.size() && s[i] != '"') {
                    if (s[i] == '\\' && i+1 < s.size()) { t += s[i] + s[i+1]; i += 2; }
                    else { t += s[i]; i++; }
                }
                if (i < s.size()) { t += s[i]; i++; }
                tkns.push_back({t, false}); continue;
            }
            if (isdigit(s[i]) || (s[i] == '.' && i+1 < s.size() && isdigit(s[i+1]))) {
                string n; while (i < s.size() && (isdigit(s[i]) || s[i] == '.')) { n += s[i]; i++; }
                tkns.push_back({n, false}); continue;
            }
            if (isalpha(s[i]) || s[i] == '_') {
                string w; size_t wi = i;
                while (i < s.size() && (isalnum(s[i]) || s[i] == '_')) { w += s[i]; i++; }
                tkns.push_back({w, true}); continue;
            }
            if (i+1 < s.size() && s.substr(i,2) == ">=") { tkns.push_back({">=", false}); i += 2; continue; }
            if (i+1 < s.size() && s.substr(i,2) == "<=") { tkns.push_back({"<=", false}); i += 2; continue; }
            if (i+1 < s.size() && s.substr(i,2) == "!=") { tkns.push_back({"!=", false}); i += 2; continue; }
            if (i+1 < s.size() && s.substr(i,2) == "==") { tkns.push_back({"==", false}); i += 2; continue; }
            tkns.push_back({string(1, s[i]), false}); i++;
        }

        string result;
        size_t t = 0;
        while (t < tkns.size()) {
            auto& tk = tkns[t];

            bool tkIsAlpha = tk.isWord || (tk.text.size() > 0 && (isdigit(tk.text[0]) || tk.text[0] == '.'));
            if (t > 0 && tkIsAlpha) {
                auto& prev = tkns[t-1];
                bool prevIsAlpha = prev.isWord || (prev.text.size() > 0 && (isdigit(prev.text[0]) || prev.text[0] == '.'));
                if (prevIsAlpha) result += " ";
            }
            if (!tk.isWord) { result += tk.text; t++; continue; }
            string w = tk.text;

            if (w == "is" && t+1 < tkns.size() && tkns[t+1].isWord) {
                string n1 = tkns[t+1].text;

                if (n1 == "not" && t+2 < tkns.size() && tkns[t+2].isWord) {
                    string n2 = tkns[t+2].text;
                    if (n2 == "equal" && t+3 < tkns.size() && tkns[t+3].isWord && tkns[t+3].text == "to") {
                        result += "!="; t += 4; continue;
                    }
                    if (n2 == "greater" && t+3 < tkns.size() && tkns[t+3].isWord && tkns[t+3].text == "than") {
                        result += "<="; t += 4; continue;
                    }
                    if (n2 == "less" && t+3 < tkns.size() && tkns[t+3].isWord && tkns[t+3].text == "than") {
                        result += ">="; t += 4; continue;
                    }

                    result += "!="; t += 2; continue;
                }

                if (n1 == "greater" && t+2 < tkns.size() && tkns[t+2].isWord && tkns[t+2].text == "than") {
                    result += ">"; t += 3; continue;
                }
                if (n1 == "less" && t+2 < tkns.size() && tkns[t+2].isWord && tkns[t+2].text == "than") {
                    result += "<"; t += 3; continue;
                }
                if (n1 == "equal" && t+2 < tkns.size() && tkns[t+2].isWord && tkns[t+2].text == "to") {
                    result += "=="; t += 3; continue;
                }
            }

            if (w == "divided" && t+1 < tkns.size() && tkns[t+1].isWord && tkns[t+1].text == "by") {
                result += "/"; t += 2; continue;
            }

            if (w == "length" && t+1 < tkns.size() && tkns[t+1].isWord && tkns[t+1].text == "of") {
                t += 2;
                string arg;
                for (size_t j = t; j < tkns.size(); j++) {
                    if (j > t) arg += " ";
                    arg += tkns[j].text;
                }
                result += "len(" + translateExpr(arg) + ")";
                t = tkns.size(); continue;
            }

            if (w == "true")  { result += "True";  t++; continue; }
            if (w == "false") { result += "False"; t++; continue; }
            if (w == "plus")     { result += "+"; t++; continue; }
            if (w == "minus")    { result += "-"; t++; continue; }
            if (w == "mod" || w == "remainder") { result += "%"; t++; continue; }

            result += w; t++;
        }
        return result;
    }

    string translateOcodeBlock(const string& code) {
        istringstream stream(code);
        string line, out;
        bool first = true;
        while (getline(stream, line)) {
            if (!first) out += "\n";
            first = false;
            out += translateOcodeLine(line);
        }
        return out;
    }

    void execPythonBlock(const string& code, int ocLine) {
        cout << flush;
        string pyCmd = findPython();
        if (pyCmd.empty()) {
            fail("Python is not installed. Python 3 is required for pyoc blocks. "
                 "Install it from your package manager or python.org.", ocLine);
        }

        if (!pySession.alive) {
            pySession.start(pyCmd, ocLine);
        }

        string translated = translateOcodeBlock(code);
        autoinstallMissingPackages(translated, pyCmd, ocLine);
        string output = pySession.exec(translated, ocLine);

        if (!output.empty()) {
            cout << output;
            if (output.back() != '\n') cout << "\n";
            cout << flush;
        }
    }

    void execUse(const string& moduleName, int line, Env& env) {

        vector<string> searchPaths;
        const char* home = getenv("HOME");
        if (home) {
            searchPaths.push_back(string(home) + "/.ocode/lib/" + moduleName + ".oc");
        }
        searchPaths.push_back(moduleName + ".oc");

        string filePath;
        bool found = false;
        for (auto& p : searchPaths) {
            ifstream test(p);
            if (test.good()) { filePath = p; found = true; break; }
        }
        if (!found) {
            string hint;
            if (home) hint = " (looked in ~/.ocode/lib/" + moduleName + ".oc and ./" + moduleName + ".oc)";
            else hint = " (looked in ./" + moduleName + ".oc)";
            fail("package '" + moduleName + "' not found" + hint, line);
        }
        ifstream in(filePath);
        if (!in) fail("could not open package file '" + filePath + "'", line);
        stringstream ss; ss << in.rdbuf();
        string source = ss.str();
        try {
            Lexer lexer(source);
            auto toks = lexer.tokenize();
            Parser parser(toks);
            auto program = parser.parseProgram();

            for (auto& s : program)
                if (s->kind == StmtKind::FUNC_DEF)
                    functions[s->name] = {s->params, s->body};

            for (auto& s : program)
                if (s->kind != StmtKind::FUNC_DEF)
                    execStmt(s, env);
        } catch (OCodeError& e) {
            fail("in package '" + moduleName + "' (" + filePath + "): " + e.what() + " [line " + to_string(e.line) + "]", line);
        }
    }

    Value callFunction(const string& name, vector<Value>& args, int line) {
        auto it = functions.find(name);
        if (it == functions.end()) fail("undefined function '" + name + "'", line);
        FuncDef& fn = it->second;
        if (fn.params.size() != args.size())
            fail("function '" + name + "' expects " + to_string(fn.params.size()) + " arg(s) but got " + to_string(args.size()), line);
        Env local; local.parent = &globals;
        for (size_t i = 0; i < fn.params.size(); i++) local.vars[fn.params[i]] = args[i];
        try { execBlock(fn.body, local); } catch (ReturnSignal& r) { return r.value; }
        return Value::Number(0);
    }

    Value eval(ExprPtr& e, Env& env) {
        switch (e->kind) {
            case ExprKind::NUMBER: return Value::Number(e->num);
            case ExprKind::STRING: return Value::Str(e->str);
            case ExprKind::BOOL:   return Value::Bool(e->bl);
            case ExprKind::LIST: {
                auto vec = make_shared<vector<Value>>();
                for (auto& it : e->items) vec->push_back(eval(it, env));
                return Value::List(vec);
            }
            case ExprKind::DICT: {
                auto d = make_shared<DictData>();
                for (auto& [kExpr, vExpr] : e->pairs) {
                    Value kv = eval(kExpr, env);
                    if (kv.type != Value::Type::STRING) fail("dict key must be a string", e->line);
                    d->set(kv.str, eval(vExpr, env));
                }
                return Value::Dict(d);
            }
            case ExprKind::VAR: {
                Value* v = env.find(e->name);
                if (!v) fail("undefined variable '" + e->name + "'", e->line);
                return *v;
            }
            case ExprKind::UNARY: {
                Value r = eval(e->right, env);
                if (e->op == "-") return Value::Number(-valueToNumber(r, e->line));
                if (e->op == "not") return Value::Bool(!valueToBool(r));
                fail("unknown unary op", e->line);
            }
            case ExprKind::BINOP: {
                if (e->op == "and") {
                    Value l = eval(e->left, env);
                    if (!valueToBool(l)) return Value::Bool(false);
                    return Value::Bool(valueToBool(eval(e->right, env)));
                }
                if (e->op == "or") {
                    Value l = eval(e->left, env);
                    if (valueToBool(l)) return Value::Bool(true);
                    return Value::Bool(valueToBool(eval(e->right, env)));
                }
                Value l = eval(e->left, env);
                Value r = eval(e->right, env);
                return applyBinOp(e->op, l, r, e->line);
            }
            case ExprKind::INDEX: {
                Value l = eval(e->left, env);
                if (l.type == Value::Type::STRING) {
                    long long idx = (long long)valueToNumber(eval(e->right, env), e->line);
                    if (idx < 0 || idx >= (long long)l.str.size()) fail("string index out of range", e->line);
                    return Value::Str(string(1, l.str[idx]));
                }
                if (l.type == Value::Type::DICT) {
                    Value k = eval(e->right, env);
                    if (k.type != Value::Type::STRING) fail("dict key must be a string", e->line);
                    Value* found = l.dict->get(k.str);
                    if (!found) fail("key '" + k.str + "' not found in dict", e->line);
                    return *found;
                }
                if (l.type != Value::Type::LIST) fail("cannot index a non-list/string/dict value", e->line);
                long long idx = (long long)valueToNumber(eval(e->right, env), e->line);
                if (idx < 0 || idx >= (long long)l.list->size()) fail("list index out of range", e->line);
                return (*l.list)[idx];
            }
            case ExprKind::LENGTH: {
                Value l = eval(e->right, env);
                if (l.type == Value::Type::LIST)   return Value::Number((double)l.list->size());
                if (l.type == Value::Type::STRING)  return Value::Number((double)l.str.size());
                if (l.type == Value::Type::DICT)   return Value::Number((double)l.dict->entries.size());
                fail("length of requires a list, string, or dict", e->line);
            }
            case ExprKind::CALL: {
                vector<Value> args;
                for (auto& a : e->items) args.push_back(eval(a, env));
                return callFunction(e->name, args, e->line);
            }
            case ExprKind::BUILTIN: return evalBuiltin(e, env);
        }
        fail("internal error", e->line);
    }

    string trimStr(const string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    Value evalBuiltin(ExprPtr& e, Env& env) {
        auto& name = e->name;
        int ln = e->line;
        auto arg = [&](int i) { return eval(e->items[i], env); };

        if (name == "ask") {
            Value prompt = arg(0);
            cout << valueToString(prompt);
            string line; getline(cin, line);
            return Value::Str(line);
        }
        if (name == "uppercase") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) fail("uppercase requires a string", ln);
            string s = v.str; for (auto& c : s) c = (char)toupper(c);
            return Value::Str(s);
        }
        if (name == "lowercase") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) fail("lowercase requires a string", ln);
            string s = v.str; for (auto& c : s) c = (char)tolower(c);
            return Value::Str(s);
        }
        if (name == "trim") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) fail("trim requires a string", ln);
            return Value::Str(trimStr(v.str));
        }
        if (name == "split") {
            Value sv = arg(0), sep = arg(1);
            if (sv.type != Value::Type::STRING || sep.type != Value::Type::STRING) fail("split requires strings", ln);
            auto vec = make_shared<vector<Value>>();
            string s = sv.str, d = sep.str;
            if (d.empty()) { for (auto c : s) vec->push_back(Value::Str(string(1, c))); }
            else {
                size_t p = 0, f;
                while ((f = s.find(d, p)) != string::npos) { vec->push_back(Value::Str(s.substr(p, f-p))); p = f + d.size(); }
                vec->push_back(Value::Str(s.substr(p)));
            }
            return Value::List(vec);
        }
        if (name == "join") {
            Value lst = arg(0), sep = arg(1);
            if (lst.type != Value::Type::LIST || sep.type != Value::Type::STRING) fail("join requires a list and separator string", ln);
            string result;
            for (size_t i = 0; i < lst.list->size(); i++) {
                if (i) result += sep.str;
                result += valueToString((*lst.list)[i]);
            }
            return Value::Str(result);
        }
        if (name == "contains") {
            Value container = arg(0), item = arg(1);
            if (container.type == Value::Type::STRING && item.type == Value::Type::STRING)
                return Value::Bool(container.str.find(item.str) != string::npos);
            if (container.type == Value::Type::LIST) {
                for (auto& v : *container.list)
                    if (valuesEqual(v, item)) return Value::Bool(true);
                return Value::Bool(false);
            }

            if (container.type == Value::Type::DICT) {
                if (item.type != Value::Type::STRING) fail("contains on a dict requires a string key", ln);
                return Value::Bool(container.dict->has(item.str));
            }
            fail("contains requires a list, string, or dict", ln);
        }
        if (name == "replace") {
            Value sv = arg(0), ov = arg(1), nv = arg(2);
            if (sv.type != Value::Type::STRING || ov.type != Value::Type::STRING || nv.type != Value::Type::STRING)
                fail("replace requires strings", ln);
            string s = sv.str, old_ = ov.str, new_ = nv.str, result;
            size_t p = 0, f;
            while ((f = s.find(old_, p)) != string::npos) { result += s.substr(p, f-p) + new_; p = f + old_.size(); }
            result += s.substr(p);
            return Value::Str(result);
        }
        if (name == "index_of") {
            Value item = arg(0), container = arg(1);
            if (container.type == Value::Type::STRING && item.type == Value::Type::STRING) {
                size_t f = container.str.find(item.str);
                return Value::Number(f == string::npos ? -1 : (double)f);
            }
            if (container.type == Value::Type::LIST) {
                for (size_t i = 0; i < container.list->size(); i++)
                    if (valuesEqual((*container.list)[i], item)) return Value::Number((double)i);
                return Value::Number(-1);
            }
            fail("index of requires a list or string", ln);
        }
        if (name == "slice") {
            Value lst = arg(0);
            long long start = (long long)valueToNumber(arg(1), ln);
            long long end_  = (long long)valueToNumber(arg(2), ln);
            if (lst.type == Value::Type::STRING) {
                long long sz = (long long)lst.str.size();
                start = max(0LL, min(start, sz)); end_ = max(0LL, min(end_, sz));
                return Value::Str(start <= end_ ? lst.str.substr(start, end_ - start) : "");
            }
            if (lst.type == Value::Type::LIST) {
                long long sz = (long long)lst.list->size();
                start = max(0LL, min(start, sz)); end_ = max(0LL, min(end_, sz));
                auto vec = make_shared<vector<Value>>();
                for (long long i = start; i < end_; i++) vec->push_back((*lst.list)[i]);
                return Value::List(vec);
            }
            fail("slice requires a list or string", ln);
        }
        if (name == "sort") {
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST) fail("sort requires a list", ln);
            auto vec = make_shared<vector<Value>>(*lst.list);
            sort(vec->begin(), vec->end(), [&](const Value& a, const Value& b){
                if (a.type == Value::Type::NUMBER && b.type == Value::Type::NUMBER) return a.num < b.num;
                return valueToString(a) < valueToString(b);
            });
            return Value::List(vec);
        }
        if (name == "reverse") {
            Value v = arg(0);
            if (v.type == Value::Type::STRING) { string s = v.str; ::reverse(s.begin(), s.end()); return Value::Str(s); }
            if (v.type == Value::Type::LIST) {
                auto vec = make_shared<vector<Value>>(*v.list);
                ::reverse(vec->begin(), vec->end());
                return Value::List(vec);
            }
            fail("reverse requires a list or string", ln);
        }
        if (name == "abs")   { Value v = arg(0); return Value::Number(fabs(valueToNumber(v, ln))); }
        if (name == "round") { Value v = arg(0); return Value::Number(::round(valueToNumber(v, ln))); }
        if (name == "floor") { Value v = arg(0); return Value::Number(::floor(valueToNumber(v, ln))); }
        if (name == "ceil")  { Value v = arg(0); return Value::Number(::ceil(valueToNumber(v, ln))); }
        if (name == "sqrt")  { double x = valueToNumber(arg(0), ln); if (x < 0) fail("sqrt of negative", ln); return Value::Number(::sqrt(x)); }
        if (name == "power") { return Value::Number(pow(valueToNumber(arg(0), ln), valueToNumber(arg(1), ln))); }
        if (name == "min2")  { double a = valueToNumber(arg(0), ln), b = valueToNumber(arg(1), ln); return Value::Number(min(a,b)); }
        if (name == "max2")  { double a = valueToNumber(arg(0), ln), b = valueToNumber(arg(1), ln); return Value::Number(max(a,b)); }
        if (name == "min1")  {
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST || lst.list->empty()) fail("min requires a non-empty list", ln);
            double m = valueToNumber((*lst.list)[0], ln);
            for (auto& v : *lst.list) m = min(m, valueToNumber(v, ln));
            return Value::Number(m);
        }
        if (name == "max1")  {
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST || lst.list->empty()) fail("max requires a non-empty list", ln);
            double m = valueToNumber((*lst.list)[0], ln);
            for (auto& v : *lst.list) m = max(m, valueToNumber(v, ln));
            return Value::Number(m);
        }
        if (name == "random0") {
            uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::Number(dist(rng));
        }
        if (name == "random2") {
            double a = valueToNumber(arg(0), ln), b = valueToNumber(arg(1), ln);
            uniform_real_distribution<double> dist(a, b);
            return Value::Number(dist(rng));
        }
        if (name == "to_number") {
            Value v = arg(0);
            if (v.type == Value::Type::NUMBER) return v;
            if (v.type == Value::Type::BOOL)   return Value::Number(v.bl ? 1 : 0);
            if (v.type == Value::Type::STRING)  try { return Value::Number(stod(v.str)); } catch (...) { fail("cannot convert to number: \"" + v.str + "\"", ln); }
            fail("cannot convert list to number", ln);
        }
        if (name == "to_string") { return Value::Str(valueToString(arg(0))); }
        if (name == "to_bool")   { return Value::Bool(valueToBool(arg(0))); }

        if (name == "chr") {
            long long cp = (long long)valueToNumber(arg(0), ln);
            if (cp < 0) fail("chr requires a non-negative codepoint", ln);
            string s;
            if (cp < 0x80) {
                s += (char)cp;
            } else if (cp < 0x800) {
                s += (char)(0xC0 | (cp >> 6));
                s += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                s += (char)(0xE0 | (cp >> 12));
                s += (char)(0x80 | ((cp >> 6) & 0x3F));
                s += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x110000) {
                s += (char)(0xF0 | (cp >> 18));
                s += (char)(0x80 | ((cp >> 12) & 0x3F));
                s += (char)(0x80 | ((cp >> 6) & 0x3F));
                s += (char)(0x80 | (cp & 0x3F));
            } else {
                fail("chr codepoint out of range (max 0x10FFFF)", ln);
            }
            return Value::Str(s);
        }

        if (name == "ord") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) fail("ord requires a string", ln);
            if (v.str.empty()) fail("ord of empty string is undefined", ln);
            unsigned char c0 = (unsigned char)v.str[0];
            long long cp;
            if (c0 < 0x80) {
                cp = c0;
            } else if ((c0 & 0xE0) == 0xC0 && v.str.size() >= 2) {
                cp = ((long long)(c0 & 0x1F) << 6) | ((long long)(unsigned char)v.str[1] & 0x3F);
            } else if ((c0 & 0xF0) == 0xE0 && v.str.size() >= 3) {
                cp = ((long long)(c0 & 0x0F) << 12) | ((long long)(unsigned char)v.str[1] & 0x3F) << 6
                   | ((long long)(unsigned char)v.str[2] & 0x3F);
            } else if ((c0 & 0xF8) == 0xF0 && v.str.size() >= 4) {
                cp = ((long long)(c0 & 0x07) << 18) | ((long long)(unsigned char)v.str[1] & 0x3F) << 12
                   | ((long long)(unsigned char)v.str[2] & 0x3F) << 6
                   | ((long long)(unsigned char)v.str[3] & 0x3F);
            } else {
                cp = c0;
            }
            return Value::Number((double)cp);
        }
        if (name == "is_number") { return Value::Bool(arg(0).type == Value::Type::NUMBER); }
        if (name == "is_string") { return Value::Bool(arg(0).type == Value::Type::STRING); }
        if (name == "is_bool")   { return Value::Bool(arg(0).type == Value::Type::BOOL); }
        if (name == "is_list")   { return Value::Bool(arg(0).type == Value::Type::LIST); }

        if (name == "http_get") {
            Value url = arg(0);
            if (url.type != Value::Type::STRING) fail("http_get requires a URL string", ln);
            HttpResponse r = httpPerform(url.str, "GET", "", {}, 30, ln);
            return Value::Str(r.body);
        }
        if (name == "http_post") {
            Value url = arg(0), body = arg(1);
            if (url.type != Value::Type::STRING)   fail("http_post requires a URL string", ln);
            if (body.type != Value::Type::STRING)  fail("http_post requires a body string", ln);
            HttpResponse r = httpPerform(url.str, "POST", body.str, {}, 30, ln);
            return Value::Str(r.body);
        }
        if (name == "http_request") {
            Value url = arg(0), method = arg(1);
            if (url.type != Value::Type::STRING)    fail("http_request requires a URL string", ln);
            if (method.type != Value::Type::STRING) fail("http_request requires a method string", ln);
            string bodyStr;
            vector<pair<string,string>> hdrs;
            if (e->items.size() >= 3) {
                Value b = arg(2);
                if (b.type == Value::Type::STRING) bodyStr = b.str;
                else fail("http_request body must be a string", ln);
            }
            if (e->items.size() >= 4) {
                Value h = arg(3);
                if (h.type == Value::Type::LIST) {
                    for (auto& item : *h.list) {
                        if (item.type != Value::Type::LIST || item.list->size() != 2) {
                            fail("http_request headers must be a list of [name, value] pairs", ln);
                        }
                        Value hn = (*item.list)[0], hv = (*item.list)[1];
                        if (hn.type != Value::Type::STRING || hv.type != Value::Type::STRING) {
                            fail("http_request header pair must be two strings", ln);
                        }
                        hdrs.emplace_back(hn.str, hv.str);
                    }
                } else if (h.type == Value::Type::STRING) {

                    string hs = h.str;
                    size_t i = 0;
                    while (i < hs.size()) {
                        size_t nl = hs.find('\n', i);
                        string line = (nl == string::npos) ? hs.substr(i) : hs.substr(i, nl - i);
                        size_t colon = line.find(':');
                        if (colon != string::npos) {
                            string hn = line.substr(0, colon);
                            string hv = line.substr(colon + 1);
                            while (!hn.empty() && isspace((unsigned char)hn.back())) hn.pop_back();
                            while (!hv.empty() && isspace((unsigned char)hv.front())) hv.erase(hv.begin());
                            if (!hn.empty()) hdrs.emplace_back(hn, hv);
                        }
                        if (nl == string::npos) break;
                        i = nl + 1;
                    }
                } else {
                    fail("http_request headers must be a list of pairs or a string", ln);
                }
            }
            HttpResponse r = httpPerform(url.str, method.str, bodyStr, hdrs, 30, ln);
            auto vec = make_shared<vector<Value>>();
            vec->push_back(Value::Number((double)r.status));
            vec->push_back(Value::Str(r.body));
            vec->push_back(Value::Str(r.headers));
            return Value::List(vec);
        }

        if (name == "fetch") {
            Value url = arg(0);
            if (url.type != Value::Type::STRING) fail("fetch requires a URL string", ln);

            if (e->items.size() == 1) {
                HttpResponse r = httpPerform(url.str, "GET", "", {}, 30, ln);
                return Value::Str(r.body);
            }
            if (e->items.size() == 2) {
                Value b = arg(1);
                if (b.type != Value::Type::STRING) fail("fetch with two args: 2nd arg must be a body string", ln);
                HttpResponse r = httpPerform(url.str, "POST", b.str, {}, 30, ln);
                return Value::Str(r.body);
            }

            if (e->items.size() == 4) {
                Value method = arg(1);
                Value bodyV  = arg(2);
                Value hdrsV  = arg(3);
                if (method.type != Value::Type::STRING) fail("fetch: METHOD must be a string", ln);
                if (bodyV.type  != Value::Type::STRING) fail("fetch: BODY must be a string", ln);
                vector<pair<string,string>> hdrs;
                if (hdrsV.type == Value::Type::LIST) {
                    for (auto& item : *hdrsV.list) {
                        if (item.type != Value::Type::LIST || item.list->size() != 2)
                            fail("fetch: HEADERS must be a list of [name, value] pairs", ln);
                        Value hn = (*item.list)[0], hv = (*item.list)[1];
                        if (hn.type != Value::Type::STRING || hv.type != Value::Type::STRING)
                            fail("fetch: header pair must be two strings", ln);
                        hdrs.emplace_back(hn.str, hv.str);
                    }
                } else if (hdrsV.type == Value::Type::STRING && !hdrsV.str.empty()) {

                    string hs = hdrsV.str; size_t i = 0;
                    while (i < hs.size()) {
                        size_t nl = hs.find('\n', i);
                        string line = (nl == string::npos) ? hs.substr(i) : hs.substr(i, nl - i);
                        size_t colon = line.find(':');
                        if (colon != string::npos) {
                            string hn = line.substr(0, colon);
                            string hv = line.substr(colon + 1);
                            while (!hn.empty() && isspace((unsigned char)hn.back())) hn.pop_back();
                            while (!hv.empty() && isspace((unsigned char)hv.front())) hv.erase(hv.begin());
                            if (!hn.empty()) hdrs.emplace_back(hn, hv);
                        }
                        if (nl == string::npos) break;
                        i = nl + 1;
                    }
                } else if (hdrsV.type != Value::Type::LIST && hdrsV.type != Value::Type::STRING) {
                    fail("fetch: HEADERS must be a list of pairs or a string", ln);
                }
                HttpResponse r = httpPerform(url.str, method.str, bodyV.str, hdrs, 30, ln);
                auto vec = make_shared<vector<Value>>();
                vec->push_back(Value::Number((double)r.status));
                vec->push_back(Value::Str(r.body));
                vec->push_back(Value::Str(r.headers));
                return Value::List(vec);
            }
            fail("fetch takes 1 arg (URL), 2 args (URL, BODY), or 4 args (URL, METHOD, BODY, HEADERS)", ln);
        }

        if (name == "socket") {
            Value host = arg(0);
            if (host.type != Value::Type::STRING) fail("socket requires a host string", ln);
            if (e->items.size() < 2) fail("socket requires a port number", ln);
            Value portV = arg(1);
            int port = (int)valueToNumber(portV, ln);
            int handle = socketOpen(host.str, port, 30, ln);
            return Value::Number((double)handle);
        }
        if (name == "send") {
            Value h = arg(0);
            Value msg = arg(1);
            if (h.type != Value::Type::NUMBER)   fail("send: handle must be a number", ln);
            if (msg.type != Value::Type::STRING) fail("send: message must be a string", ln);
            int n = socketSend((int)h.num, msg.str, ln);
            return Value::Number((double)n);
        }
        if (name == "readline") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("readline: handle must be a number", ln);
            string s = socketRecvLine((int)h.num, ln);
            return Value::Str(s);
        }
        if (name == "receive") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("receive: handle must be a number", ln);
            int maxBytes = 0;
            if (e->items.size() >= 2) {
                Value n = arg(1);
                maxBytes = (int)valueToNumber(n, ln);
                if (maxBytes < 0) maxBytes = 0;
            }
            string s = socketRecv((int)h.num, maxBytes, ln);
            return Value::Str(s);
        }
        if (name == "close") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("close: handle must be a number", ln);
            socketCloseAt((int)h.num, ln);
            return Value::Number(0);
        }

        if (name == "ssl_socket") {
            Value host = arg(0);
            if (host.type != Value::Type::STRING) fail("ssl_socket requires a host string", ln);
            if (e->items.size() < 2) fail("ssl_socket requires a port number", ln);
            Value portV = arg(1);
            int port = (int)valueToNumber(portV, ln);
#ifndef OCODE_NO_OPENSSL
            int handle = sslSocketOpen(host.str, port, 30, ln);
            return Value::Number((double)handle);
#else
            fail("ssl_socket: not available (rebuilt without OpenSSL)", ln);
#endif
        }
        if (name == "ssl_send") {
            Value h = arg(0);
            Value msg = arg(1);
            if (h.type != Value::Type::NUMBER)   fail("ssl_send: handle must be a number", ln);
            if (msg.type != Value::Type::STRING) fail("ssl_send: message must be a string", ln);
            OpenSock* s = socketLookup((int)h.num);
            if (!s || (s->kind != SockKind::SSL && s->kind != SockKind::WS))
                fail("ssl_send: handle is not an SSL socket", ln);
            int n = socketSend((int)h.num, msg.str, ln);
            return Value::Number((double)n);
        }
        if (name == "ssl_readline") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ssl_readline: handle must be a number", ln);
            OpenSock* s = socketLookup((int)h.num);
            if (!s || (s->kind != SockKind::SSL && s->kind != SockKind::WS))
                fail("ssl_readline: handle is not an SSL socket", ln);
            string out = socketRecvLine((int)h.num, ln);
            return Value::Str(out);
        }
        if (name == "ssl_receive") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ssl_receive: handle must be a number", ln);
            OpenSock* s = socketLookup((int)h.num);
            if (!s || (s->kind != SockKind::SSL && s->kind != SockKind::WS))
                fail("ssl_receive: handle is not an SSL socket", ln);
            int maxBytes = 0;
            if (e->items.size() >= 2) {
                Value n = arg(1);
                maxBytes = (int)valueToNumber(n, ln);
                if (maxBytes < 0) maxBytes = 0;
            }
            string out = socketRecv((int)h.num, maxBytes, ln);
            return Value::Str(out);
        }
        if (name == "ssl_close") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ssl_close: handle must be a number", ln);
            OpenSock* s = socketLookup((int)h.num);
            if (!s || (s->kind != SockKind::SSL && s->kind != SockKind::WS))
                fail("ssl_close: handle is not an SSL socket", ln);
            socketCloseAt((int)h.num, ln);
            return Value::Number(0);
        }

        if (name == "ws_connect") {
            Value url = arg(0);
            if (url.type != Value::Type::STRING) fail("ws_connect: URL must be a string", ln);
            int handle = wsConnect(url.str, 30, ln);
            return Value::Number((double)handle);
        }
        if (name == "ws_send") {
            Value h = arg(0);
            Value msg = arg(1);
            if (h.type != Value::Type::NUMBER)   fail("ws_send: handle must be a number", ln);
            if (msg.type != Value::Type::STRING) fail("ws_send: message must be a string", ln);
            int n = wsSendFrame((int)h.num, msg.str, 0x1, ln);
            return Value::Number((double)n);
        }
        if (name == "ws_send_binary") {
            Value h = arg(0);
            Value msg = arg(1);
            if (h.type != Value::Type::NUMBER)   fail("ws_send_binary: handle must be a number", ln);
            if (msg.type != Value::Type::STRING) fail("ws_send_binary: message must be a string", ln);
            int n = wsSendFrame((int)h.num, msg.str, 0x2, ln);
            return Value::Number((double)n);
        }
        if (name == "ws_recv") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ws_recv: handle must be a number", ln);
            string out = wsRecvFrame((int)h.num, ln);
            return Value::Str(out);
        }
        if (name == "ws_close") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ws_close: handle must be a number", ln);
            OpenSock* s = socketLookup((int)h.num);
            if (!s || s->kind != SockKind::WS) fail("ws_close: handle is not a WebSocket", ln);
            string empty;
            wsSendFrame((int)h.num, empty, 0x8, ln);
            socketCloseAt((int)h.num, ln);
            return Value::Number(0);
        }

        if (name == "sslsay") {
            if (e->items.size() < 3) fail("sslsay: needs HOST PORT MSG", ln);
            Value hostV = arg(0), portV = arg(1), msgV = arg(2);
            if (hostV.type != Value::Type::STRING) fail("sslsay: HOST must be a string", ln);
            if (portV.type != Value::Type::NUMBER)  fail("sslsay: PORT must be a number", ln);
            if (msgV.type  != Value::Type::STRING) fail("sslsay: MSG must be a string", ln);
#ifdef OCODE_NO_OPENSSL
            fail("sslsay: rebuilt without OpenSSL (-DOCODE_NO_OPENSSL)", ln);
            return Value::Str("");
#else
            int handle = sslSocketOpen(hostV.str, (int)portV.num, 30, ln);
            socketSend(handle, msgV.str, ln);
            string out;
            for (int i = 0; i < 64; i++) {
                string chunk = socketRecv(handle, 4096, ln);
                if (chunk.empty()) break;
                out += chunk;
                if (out.size() > 1024 * 1024) break;
            }
            socketCloseAt(handle, ln);
            return Value::Str(out);
#endif
        }
        if (name == "wssay") {
            if (e->items.size() < 2) fail("wssay: needs URL and MSG", ln);
            Value urlV = arg(0), msgV = arg(1);
            if (urlV.type != Value::Type::STRING) fail("wssay: URL must be a string", ln);
            if (msgV.type != Value::Type::STRING) fail("wssay: MSG must be a string", ln);
#ifdef OCODE_NO_OPENSSL
            if (urlV.str.substr(0, 6) == "wss://") {
                fail("wssay: wss:// requires OpenSSL (rebuilt with -DOCODE_NO_OPENSSL)", ln);
            }
            int handle = wsConnect(urlV.str, 30, ln);
#else
            int handle = wsConnect(urlV.str, 30, ln);
#endif
            wsSendFrame(handle, msgV.str, 0x1, ln);
            string reply = wsRecvFrame(handle, ln);
            string empty;
            wsSendFrame(handle, empty, 0x8, ln);
            socketCloseAt(handle, ln);
            return Value::Str(reply);
        }


        // ===== v15+ extras =====
        // -- ws_ping --
        if (name == "ws_ping") {
            Value h = arg(0);
            if (h.type != Value::Type::NUMBER) fail("ws_ping: handle must be a number", ln);
            string empty;
            wsSendFrame((int)h.num, empty, 0x9, ln);
            return Value::Number(0);
        }
        // -- Bitwise (3.7) --
        if (name == "band") {
            long long a = (long long)arg(0).num, b = (long long)arg(1).num;
            return Value::Number((double)(a & b));
        }
        if (name == "bor") {
            long long a = (long long)arg(0).num, b = (long long)arg(1).num;
            return Value::Number((double)(a | b));
        }
        if (name == "bxor") {
            long long a = (long long)arg(0).num, b = (long long)arg(1).num;
            return Value::Number((double)(a ^ b));
        }
        if (name == "bnot") {
            long long a = (long long)arg(0).num;
            return Value::Number((double)(~a));
        }
        if (name == "bshl") {
            long long a = (long long)arg(0).num, b = (long long)arg(1).num;
            return Value::Number((double)(a << b));
        }
        if (name == "bshr") {
            unsigned long long a = (unsigned long long)(long long)arg(0).num;
            int b = (int)arg(1).num;
            return Value::Number((double)(a >> b));
        }
        // -- BigInt (3.7/3.8) --
        if (name == "bigint") {
            Value a = arg(0);
            string s = (a.type == Value::Type::NUMBER) ? to_string((long long)a.num) : a.str;
            return Value::Str(bigintNormalize(s));
        }
        if (name == "bigint_add") return Value::Str(bigintAdd(arg(0).str, arg(1).str));
        if (name == "bigint_sub") return Value::Str(bigintSub(arg(0).str, arg(1).str));
        if (name == "bigint_mul") return Value::Str(bigintMul(arg(0).str, arg(1).str));
        if (name == "bigint_cmp") return Value::Number((double)bigintCmp(arg(0).str, arg(1).str));
        if (name == "bigint_band") return Value::Str(bigintBand(arg(0).str, arg(1).str));
        if (name == "bigint_bor") return Value::Str(bigintBor(arg(0).str, arg(1).str));
        if (name == "bigint_bxor") return Value::Str(bigintBxor(arg(0).str, arg(1).str));
        if (name == "bigint_shl") return Value::Str(bigintShl(arg(0).str, (int)arg(1).num));
        if (name == "bigint_shr") return Value::Str(bigintShr(arg(0).str, (int)arg(1).num));
        if (name == "bigint_div") return Value::Str(bigintDiv(arg(0).str, arg(1).str));
        if (name == "bigint_mod") return Value::Str(bigintMod(arg(0).str, arg(1).str));
        if (name == "bigint_pow") return Value::Str(bigintPow(arg(0).str, arg(1).str));
        if (name == "bigint_to_hex") {
            Value a = arg(0);
            string s = (a.type == Value::Type::NUMBER) ? to_string((long long)a.num) : a.str;
            return Value::Str(bigintToHex(bigintNormalize(s)));
        }
        if (name == "bigint_from_hex") return Value::Str(bigintFromHex(arg(0).str));
        // -- Math extras (3.8) --
        if (name == "sign") {
            double x = arg(0).num;
            return Value::Number(x > 0 ? 1 : (x < 0 ? -1 : 0));
        }
        if (name == "gcd") {
            long long a = llabs((long long)arg(0).num), b = llabs((long long)arg(1).num);
            while (b) { long long t = a % b; a = b; b = t; }
            return Value::Number((double)a);
        }
        if (name == "lcm") {
            long long a = llabs((long long)arg(0).num), b = llabs((long long)arg(1).num);
            if (!a || !b) return Value::Number(0);
            long long g = a, q = b;
            while (q) { long long t = g % q; g = q; q = t; }
            return Value::Number((double)((a / g) * b));
        }
        if (name == "trunc") return Value::Number((double)(long long)arg(0).num);
        if (name == "clamp") {
            double x = arg(0).num, lo = arg(1).num, hi = arg(2).num;
            return Value::Number(x < lo ? lo : (x > hi ? hi : x));
        }
        if (name == "popcount") {
            unsigned long long x = (unsigned long long)(long long)arg(0).num;
            int c = 0; while (x) { c += x & 1; x >>= 1; }
            return Value::Number((double)c);
        }
        if (name == "bit_length") {
            unsigned long long x = (unsigned long long)llabs((long long)arg(0).num);
            if (x == 0) return Value::Number(0);
            int n = 0; while (x) { n++; x >>= 1; }
            return Value::Number((double)n);
        }
        // -- Date (3.7) --
        if (name == "time_iso_at" || name == "time_iso_after") {
            time_t t;
            if (name == "time_iso_at") t = (time_t)arg(0).num;
            else t = time(nullptr) + (time_t)arg(0).num;
            struct tm tmv;
            gmtime_r(&t, &tmv);
            char buf[40];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
            return Value::Str(string(buf));
        }
        if (name == "date_now" || name == "date_utc_now") {
            return Value::Number((double)time(nullptr));
        }
        if (name == "timezone_offset") {
            time_t now = time(nullptr);
            struct tm loc, utc;
            localtime_r(&now, &loc);
            gmtime_r(&now, &utc);
            return Value::Number((double)(loc.tm_hour - utc.tm_hour) * 3600);
        }
        if (name == "date_to_iso") {
            time_t t = (time_t)arg(0).num;
            struct tm tmv; gmtime_r(&t, &tmv);
            char buf[40];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
            return Value::Str(string(buf));
        }
        if (name == "date_from_iso") {
            string s = arg(0).str;
            struct tm tmv; memset(&tmv, 0, sizeof(tmv));
            strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tmv);
            return Value::Number((double)timegm(&tmv));
        }
        if (name == "date_parse") {
            string s = arg(0).str;
            string fmt = arg(1).str;
            // Convert Java-style format to strptime format
            string cfmt;
            for (size_t i = 0; i < fmt.size(); i++) {
                if (fmt[i] == 'y') cfmt += "%Y";
                else if (fmt[i] == 'M') cfmt += "%m";
                else if (fmt[i] == 'd') cfmt += "%d";
                else if (fmt[i] == 'H') cfmt += "%H";
                else if (fmt[i] == 'm') cfmt += "%M";
                else if (fmt[i] == 's') cfmt += "%S";
                else cfmt += fmt[i];
            }
            struct tm tmv; memset(&tmv, 0, sizeof(tmv));
            strptime(s.c_str(), cfmt.c_str(), &tmv);
            return Value::Number((double)timegm(&tmv));
        }
        if (name == "date_format") {
            time_t t = (time_t)arg(0).num;
            string fmt = arg(1).str;
            string cfmt;
            for (size_t i = 0; i < fmt.size(); i++) {
                if (fmt[i] == 'y') cfmt += "%Y";
                else if (fmt[i] == 'M') cfmt += "%m";
                else if (fmt[i] == 'd') cfmt += "%d";
                else if (fmt[i] == 'H') cfmt += "%H";
                else if (fmt[i] == 'm') cfmt += "%M";
                else if (fmt[i] == 's') cfmt += "%S";
                else cfmt += fmt[i];
            }
            struct tm tmv; gmtime_r(&t, &tmv);
            char buf[256];
            strftime(buf, sizeof(buf), cfmt.c_str(), &tmv);
            return Value::Str(string(buf));
        }
        if (name == "date_add") {
            time_t t = (time_t)arg(0).num;
            string field = arg(1).str;
            long long amount = (long long)arg(2).num;
            struct tm tmv; gmtime_r(&t, &tmv);
            if (field == "second" || field == "seconds") tmv.tm_sec += (int)amount;
            else if (field == "minute" || field == "minutes") tmv.tm_min += (int)amount;
            else if (field == "hour" || field == "hours") tmv.tm_hour += (int)amount;
            else if (field == "day" || field == "days") tmv.tm_mday += (int)amount;
            else if (field == "month" || field == "months") tmv.tm_mon += (int)amount;
            else if (field == "year" || field == "years") tmv.tm_year += (int)amount;
            return Value::Number((double)timegm(&tmv));
        }
        if (name == "date_diff") {
            time_t ta = (time_t)arg(0).num, tb = (time_t)arg(1).num;
            string field = arg(2).str;
            long long diff = (long long)(ta - tb);
            if (field == "second" || field == "seconds") return Value::Number((double)diff);
            if (field == "minute" || field == "minutes") return Value::Number((double)(diff / 60));
            if (field == "hour" || field == "hours") return Value::Number((double)(diff / 3600));
            if (field == "day" || field == "days") return Value::Number((double)(diff / 86400));
            return Value::Number((double)diff);
        }
        if (name == "date_parts") {
            time_t t = (time_t)arg(0).num;
            struct tm tmv; gmtime_r(&t, &tmv);
            auto d = make_shared<DictData>();
            d->set("year", Value::Number((double)(tmv.tm_year + 1900)));
            d->set("month", Value::Number((double)(tmv.tm_mon + 1)));
            d->set("day", Value::Number((double)tmv.tm_mday));
            d->set("hour", Value::Number((double)tmv.tm_hour));
            d->set("minute", Value::Number((double)tmv.tm_min));
            d->set("second", Value::Number((double)tmv.tm_sec));
            d->set("weekday", Value::Number((double)tmv.tm_wday));
            d->set("yearday", Value::Number((double)tmv.tm_yday));
            return Value::Dict(d);
        }
        // -- Timer (3.13) --
        if (name == "timer_start") {
            g_timers[arg(0).str] = chrono::steady_clock::now();
            return Value::Number(0);
        }
        if (name == "timer_stop") {
            auto it = g_timers.find(arg(0).str);
            if (it == g_timers.end()) return Value::Number(0);
            auto dur = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - it->second);
            g_timers.erase(it);
            return Value::Number((double)dur.count());
        }
        // -- SQLite (3.7) --
        if (name == "db_open") {
#ifdef OCODE_NO_SQLITE
            fail("db_open: rebuilt without SQLite support (-DOCODE_NO_SQLITE)", ln);
            return Value::Number(0);
#else
            sqlite3* h = nullptr;
            if (sqlite3_open(arg(0).str.c_str(), &h) != SQLITE_OK) {
                string err = sqlite3_errmsg(h);
                sqlite3_close(h);
                fail("db_open: " + err, ln);
            }
            return Value::Number((double)dbRegister(h));
#endif
        }
        if (name == "db_close") {
            return Value::Number((double)dbCloseAt((int)arg(0).num));
        }
        if (name == "db_exec") {
#ifdef OCODE_NO_SQLITE
            fail("db_exec: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_exec: invalid database handle", ln);
            char* err = nullptr;
            if (sqlite3_exec(h, arg(1).str.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
                string e = err ? err : "unknown error";
                sqlite3_free(err);
                fail("db_exec: " + e, ln);
            }
            return Value::Number(0);
#endif
        }
        if (name == "db_query") {
#ifdef OCODE_NO_SQLITE
            fail("db_query: rebuilt without SQLite support", ln);
            return Value::List(make_shared<vector<Value>>());
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_query: invalid database handle", ln);
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(h, arg(1).str.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail("db_query: " + string(sqlite3_errmsg(h)), ln);
            }
            auto rows = make_shared<vector<Value>>();
            int cols = sqlite3_column_count(stmt);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto row = make_shared<DictData>();
                for (int i = 0; i < cols; i++) {
                    const char* cn = sqlite3_column_name(stmt, i);
                    int type = sqlite3_column_type(stmt, i);
                    Value v;
                    if (type == SQLITE_INTEGER) v = Value::Number((double)sqlite3_column_int64(stmt, i));
                    else if (type == SQLITE_FLOAT) v = Value::Number(sqlite3_column_double(stmt, i));
                    else if (type == SQLITE_TEXT) v = Value::Str(string((const char*)sqlite3_column_text(stmt, i)));
                    else if (type == SQLITE_NULL) v = Value::Str("");
                    else {
                        const void* blob = sqlite3_column_blob(stmt, i);
                        int blen = sqlite3_column_bytes(stmt, i);
                        v = Value::Str(string((const char*)blob, blen));
                    }
                    row->set(cn, v);
                }
                rows->push_back(Value::Dict(row));
            }
            sqlite3_finalize(stmt);
            return Value::List(rows);
#endif
        }
        if (name == "db_exec_params") {
#ifdef OCODE_NO_SQLITE
            fail("db_exec_params: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_exec_params: invalid database handle", ln);
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(h, arg(1).str.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail("db_exec_params: " + string(sqlite3_errmsg(h)), ln);
            }
            if (arg(2).type == Value::Type::LIST) {
                int i = 1;
                for (auto& p : *arg(2).list) {
                    if (p.type == Value::Type::NUMBER) sqlite3_bind_int64(stmt, i++, (sqlite3_int64)p.num);
                    else sqlite3_bind_text(stmt, i++, p.str.c_str(), -1, SQLITE_TRANSIENT);
                }
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            return Value::Number(0);
#endif
        }
        if (name == "db_query_params") {
#ifdef OCODE_NO_SQLITE
            fail("db_query_params: rebuilt without SQLite support", ln);
            return Value::List(make_shared<vector<Value>>());
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_query_params: invalid database handle", ln);
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(h, arg(1).str.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail("db_query_params: " + string(sqlite3_errmsg(h)), ln);
            }
            if (arg(2).type == Value::Type::LIST) {
                int i = 1;
                for (auto& p : *arg(2).list) {
                    if (p.type == Value::Type::NUMBER) sqlite3_bind_int64(stmt, i++, (sqlite3_int64)p.num);
                    else sqlite3_bind_text(stmt, i++, p.str.c_str(), -1, SQLITE_TRANSIENT);
                }
            }
            auto rows = make_shared<vector<Value>>();
            int cols = sqlite3_column_count(stmt);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto row = make_shared<DictData>();
                for (int i = 0; i < cols; i++) {
                    const char* cn = sqlite3_column_name(stmt, i);
                    int type = sqlite3_column_type(stmt, i);
                    Value v;
                    if (type == SQLITE_INTEGER) v = Value::Number((double)sqlite3_column_int64(stmt, i));
                    else if (type == SQLITE_FLOAT) v = Value::Number(sqlite3_column_double(stmt, i));
                    else if (type == SQLITE_TEXT) v = Value::Str(string((const char*)sqlite3_column_text(stmt, i)));
                    else v = Value::Str("");
                    row->set(cn, v);
                }
                rows->push_back(Value::Dict(row));
            }
            sqlite3_finalize(stmt);
            return Value::List(rows);
#endif
        }
        if (name == "db_begin") {
#ifdef OCODE_NO_SQLITE
            fail("db_begin: rebuilt without SQLite support", ln);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (h) sqlite3_exec(h, "BEGIN", nullptr, nullptr, nullptr);
#endif
            return Value::Number(0);
        }
        if (name == "db_commit") {
#ifdef OCODE_NO_SQLITE
            fail("db_commit: rebuilt without SQLite support", ln);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (h) sqlite3_exec(h, "COMMIT", nullptr, nullptr, nullptr);
#endif
            return Value::Number(0);
        }
        if (name == "db_rollback") {
#ifdef OCODE_NO_SQLITE
            fail("db_rollback: rebuilt without SQLite support", ln);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (h) sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
#endif
            return Value::Number(0);
        }
        if (name == "db_last_insert_id") {
#ifdef OCODE_NO_SQLITE
            fail("db_last_insert_id: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_last_insert_id: invalid handle", ln);
            return Value::Number((double)sqlite3_last_insert_rowid(h));
#endif
        }
        if (name == "db_changes") {
#ifdef OCODE_NO_SQLITE
            fail("db_changes: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_changes: invalid handle", ln);
            return Value::Number((double)sqlite3_changes(h));
#endif
        }
        if (name == "db_prepare") {
#ifdef OCODE_NO_SQLITE
            fail("db_prepare: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3* h = dbLookup((int)arg(0).num);
            if (!h) fail("db_prepare: invalid database handle", ln);
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(h, arg(1).str.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail("db_prepare: " + string(sqlite3_errmsg(h)), ln);
            }
            return Value::Number((double)stmtRegister(stmt));
#endif
        }
        if (name == "db_step") {
#ifdef OCODE_NO_SQLITE
            fail("db_step: rebuilt without SQLite support", ln);
            return Value::Number(0);
#else
            sqlite3_stmt* stmt = stmtLookup((int)arg(0).num);
            if (!stmt) fail("db_step: invalid statement handle", ln);
            if (e->items.size() >= 2 && arg(1).type == Value::Type::LIST) {
                int i = 1;
                for (auto& p : *arg(1).list) {
                    if (p.type == Value::Type::NUMBER) sqlite3_bind_int64(stmt, i++, (sqlite3_int64)p.num);
                    else sqlite3_bind_text(stmt, i++, p.str.c_str(), -1, SQLITE_TRANSIENT);
                }
            }
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) return Value::Number(1);
            if (rc == SQLITE_DONE) return Value::Number(0);
            return Value::Bool(false);
#endif
        }
        if (name == "db_finalize") {
#ifdef OCODE_NO_SQLITE
            fail("db_finalize: rebuilt without SQLite support", ln);
#else
            sqlite3_stmt* stmt = stmtLookup((int)arg(0).num);
            if (stmt) { sqlite3_finalize(stmt); g_openStmts[(int)arg(0).num - 1] = nullptr; }
#endif
            return Value::Number(0);
        }
        // -- Atomic (3.7) --
        if (name == "atomic_write") {
            string path = arg(0).str, content = arg(1).str;
            string tmp = path + ".tmp." + to_string(getpid());
            ofstream f(tmp, ios::binary | ios::trunc);
            if (!f) fail("atomic_write: cannot open temp file '" + tmp + "'", ln);
            f << content;
            f.close();
            if (rename(tmp.c_str(), path.c_str()) != 0) {
                remove(tmp.c_str());
                fail("atomic_write: rename failed", ln);
            }
            return Value::Number(0);
        }
        if (name == "lock_file") {
            // Advisory lock via flock(2)
            string path = arg(0).str;
            int fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
            if (fd < 0) fail("lock_file: cannot create/open '" + path + "'", ln);
#ifdef _WIN32
            // No flock on Windows - just return fd
#else
            if (flock(fd, LOCK_EX) != 0) {
                close(fd);
                fail("lock_file: flock failed", ln);
            }
#endif
            return Value::Number((double)fd);
        }
        if (name == "unlock_file") {
            int fd = (int)arg(0).num;
            if (fd < 0) fail("unlock_file: invalid handle", ln);
#ifdef _WIN32
            close(fd);
#else
            flock(fd, LOCK_UN);
            close(fd);
#endif
            return Value::Number(0);
        }
        // -- String extras (3.9) --
        if (name == "starts_with") {
            const string& a = arg(0).str; const string& b = arg(1).str;
            return Value::Bool(a.size() >= b.size() && a.compare(0, b.size(), b) == 0);
        }
        if (name == "ends_with") {
            const string& a = arg(0).str; const string& b = arg(1).str;
            return Value::Bool(a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0);
        }
        if (name == "count") {
            const string& s = arg(0).str; const string& sub = arg(1).str;
            if (sub.empty()) return Value::Number(0);
            int n = 0; size_t p = 0;
            while ((p = s.find(sub, p)) != string::npos) { n++; p += sub.size(); }
            return Value::Number((double)n);
        }
        if (name == "pad_left") {
            string s = arg(0).str;
            int width = (int)arg(1).num;
            char ch = ' ';
            if (e->items.size() >= 3) ch = arg(2).str.empty() ? ' ' : arg(2).str[0];
            if ((int)s.size() < width) s = string(width - s.size(), ch) + s;
            return Value::Str(s);
        }
        if (name == "pad_right") {
            string s = arg(0).str;
            int width = (int)arg(1).num;
            char ch = ' ';
            if (e->items.size() >= 3) ch = arg(2).str.empty() ? ' ' : arg(2).str[0];
            if ((int)s.size() < width) s += string(width - s.size(), ch);
            return Value::Str(s);
        }
        if (name == "format") {
            string fmt = arg(0).str;
            string out;
            size_t idx = 1;
            for (size_t i = 0; i < fmt.size(); i++) {
                if (fmt[i] == '%' && i + 1 < fmt.size()) {
                    char c = fmt[i + 1];
                    if (c == 'd' || c == 'i') {
                        if (idx < e->items.size()) {
                            Value v = eval(e->items[idx++], env);
                            out += to_string((long long)v.num);
                        }
                        i++;
                    } else if (c == 's') {
                        if (idx < e->items.size()) {
                            Value v = eval(e->items[idx++], env);
                            out += v.type == Value::Type::STRING ? v.str : valueToString(v);
                        }
                        i++;
                    } else if (c == 'f') {
                        if (idx < e->items.size()) {
                            Value v = eval(e->items[idx++], env);
                            out += to_string(v.num);
                        }
                        i++;
                    } else if (c == '%') {
                        out += '%';
                        i++;
                    } else {
                        out += fmt[i];
                    }
                } else {
                    out += fmt[i];
                }
            }
            return Value::Str(out);
        }
        if (name == "capitalize") {
            string s = arg(0).str;
            if (!s.empty()) { s[0] = toupper(s[0]); for (size_t i = 1; i < s.size(); i++) s[i] = tolower(s[i]); }
            return Value::Str(s);
        }
        if (name == "title_case") {
            string s = arg(0).str;
            bool cap = true;
            for (char& c : s) {
                if (isspace((unsigned char)c)) { cap = true; }
                else { c = (cap ? toupper(c) : tolower(c)); cap = false; }
            }
            return Value::Str(s);
        }
        if (name == "replace_all") {
            string s = arg(0).str; const string& from = arg(1).str; const string& to = arg(2).str;
            if (from.empty()) return Value::Str(s);
            string out;
            size_t p = 0, last = 0;
            while ((p = s.find(from, last)) != string::npos) {
                out += s.substr(last, p - last);
                out += to;
                last = p + from.size();
            }
            out += s.substr(last);
            return Value::Str(out);
        }
        if (name == "code_point_at") {
            const string& s = arg(0).str;
            int idx = (int)arg(1).num;
            if (idx < 0 || idx >= (int)s.size()) return Value::Number(-1);
            unsigned char c = (unsigned char)s[idx];
            if (c < 0x80) return Value::Number((double)c);
            // Decode UTF-8
            int cp = 0; int len = 0;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else return Value::Number((double)c);
            for (int i = 1; i < len; i++) {
                if (idx + i >= (int)s.size()) return Value::Number((double)c);
                cp = (cp << 6) | (((unsigned char)s[idx + i]) & 0x3F);
            }
            return Value::Number((double)cp);
        }
        if (name == "from_code_point") {
            int cp = (int)arg(0).num;
            string out;
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            return Value::Str(out);
        }
        // -- List extras (3.10) --
        if (name == "shift") {
            // shift from <list>
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST || lst.list->empty()) fail("shift: list is empty", ln);
            Value first = lst.list->front();
            lst.list->erase(lst.list->begin());
            return first;
        }
        if (name == "unshift") {
            // unshift <value> to <list>
            Value value = arg(0);
            Value lst = arg(1);
            if (lst.type != Value::Type::LIST) fail("unshift: target must be a list", ln);
            lst.list->insert(lst.list->begin(), value);
            return Value::Number(0);
        }
        if (name == "insert_at") {
            Value value = arg(0), lst = arg(1);
            int idx = (int)arg(2).num;
            if (lst.type != Value::Type::LIST) fail("insert_at: target must be a list", ln);
            if (idx < 0 || idx > (int)lst.list->size()) fail("insert_at: index out of range", ln);
            lst.list->insert(lst.list->begin() + idx, value);
            return Value::Number(0);
        }
        if (name == "remove_at") {
            int idx = (int)arg(0).num;
            Value lst = arg(1);
            if (lst.type != Value::Type::LIST) fail("remove_at: target must be a list", ln);
            if (idx < 0 || idx >= (int)lst.list->size()) fail("remove_at: index out of range", ln);
            Value v = (*lst.list)[idx];
            lst.list->erase(lst.list->begin() + idx);
            return v;
        }
        if (name == "extend") {
            Value a = arg(0), b = arg(1);
            if (a.type != Value::Type::LIST || b.type != Value::Type::LIST)
                fail("extend: both args must be lists", ln);
            for (auto& v : *b.list) a.list->push_back(v);
            return Value::Number(0);
        }
        if (name == "map" || name == "filter" || name == "find" || name == "sort_by") {
            Value lst = arg(0);
            string fn = arg(1).str;
            if (lst.type != Value::Type::LIST) fail(name + ": first arg must be a list", ln);
            if (name == "map") {
                auto out = make_shared<vector<Value>>();
                for (auto& v : *lst.list) {
                    vector<Value> args = {v};
                    out->push_back(callFunction(fn, args, ln));
                }
                return Value::List(out);
            }
            if (name == "filter") {
                auto out = make_shared<vector<Value>>();
                for (auto& v : *lst.list) {
                    vector<Value> args = {v};
                    if (valueToBool(callFunction(fn, args, ln))) out->push_back(v);
                }
                return Value::List(out);
            }
            if (name == "find") {
                for (auto& v : *lst.list) {
                    vector<Value> args = {v};
                    if (valueToBool(callFunction(fn, args, ln))) return v;
                }
                return Value::Bool(false);
            }
            if (name == "sort_by") {
                auto out = make_shared<vector<Value>>(*lst.list);
                sort(out->begin(), out->end(), [&](const Value& a, const Value& b) {
                    vector<Value> av = {a}, bv = {b};
                    return callFunction(fn, av, ln).num < callFunction(fn, bv, ln).num;
                });
                return Value::List(out);
            }
        }
        if (name == "reduce") {
            Value lst = arg(0);
            string fn = arg(1).str;
            Value acc = arg(2);
            for (auto& v : *lst.list) {
                vector<Value> args = {acc, v};
                acc = callFunction(fn, args, ln);
            }
            return acc;
        }
        if (name == "list_contains") {
            Value v = arg(0);
            Value lst = arg(1);
            for (auto& e : *lst.list) {
                if (e.type == v.type) {
                    if (e.type == Value::Type::STRING && e.str == v.str) return Value::Bool(true);
                    if (e.type == Value::Type::NUMBER && e.num == v.num) return Value::Bool(true);
                    if (e.type == Value::Type::BOOL && e.bl == v.bl) return Value::Bool(true);
                }
            }
            return Value::Bool(false);
        }
        if (name == "unique") {
            Value lst = arg(0);
            auto out = make_shared<vector<Value>>();
            for (auto& v : *lst.list) {
                bool seen = false;
                for (auto& e : *out) {
                    if (e.type == v.type) {
                        if (e.type == Value::Type::STRING && e.str == v.str) { seen = true; break; }
                        if (e.type == Value::Type::NUMBER && e.num == v.num) { seen = true; break; }
                        if (e.type == Value::Type::BOOL && e.bl == v.bl) { seen = true; break; }
                    }
                }
                if (!seen) out->push_back(v);
            }
            return Value::List(out);
        }
        if (name == "flatten") {
            Value lst = arg(0);
            auto out = make_shared<vector<Value>>();
            for (auto& v : *lst.list) {
                if (v.type == Value::Type::LIST) for (auto& e : *v.list) out->push_back(e);
                else out->push_back(v);
            }
            return Value::List(out);
        }
        if (name == "chunk") {
            Value lst = arg(0);
            int sz = (int)arg(1).num;
            if (sz < 1) fail("chunk: size must be >= 1", ln);
            auto out = make_shared<vector<Value>>();
            for (size_t i = 0; i < lst.list->size(); i += sz) {
                auto sub = make_shared<vector<Value>>();
                for (size_t j = i; j < min(i + sz, lst.list->size()); j++) sub->push_back((*lst.list)[j]);
                out->push_back(Value::List(sub));
            }
            return Value::List(out);
        }
        if (name == "zip") {
            Value a = arg(0), b = arg(1);
            auto out = make_shared<vector<Value>>();
            size_t n = min(a.list->size(), b.list->size());
            for (size_t i = 0; i < n; i++) {
                auto pair = make_shared<vector<Value>>();
                pair->push_back((*a.list)[i]);
                pair->push_back((*b.list)[i]);
                out->push_back(Value::List(pair));
            }
            return Value::List(out);
        }
        if (name == "enumerate") {
            Value lst = arg(0);
            int start = e->items.size() >= 2 ? (int)arg(1).num : 0;
            auto out = make_shared<vector<Value>>();
            for (size_t i = 0; i < lst.list->size(); i++) {
                auto pair = make_shared<vector<Value>>();
                pair->push_back(Value::Number((double)(start + (int)i)));
                pair->push_back((*lst.list)[i]);
                out->push_back(Value::List(pair));
            }
            return Value::List(out);
        }
        if (name == "sum_of") {
            double s = 0;
            for (auto& v : *arg(0).list) if (v.type == Value::Type::NUMBER) s += v.num;
            return Value::Number(s);
        }
        if (name == "product_of") {
            double p = 1;
            for (auto& v : *arg(0).list) if (v.type == Value::Type::NUMBER) p *= v.num;
            return Value::Number(p);
        }
        if (name == "min_of") {
            double m = 1e308;
            for (auto& v : *arg(0).list) if (v.type == Value::Type::NUMBER && v.num < m) m = v.num;
            return Value::Number(m);
        }
        if (name == "max_of") {
            double m = -1e308;
            for (auto& v : *arg(0).list) if (v.type == Value::Type::NUMBER && v.num > m) m = v.num;
            return Value::Number(m);
        }
        if (name == "count_of") {
            Value v = arg(0);
            Value lst = arg(1);
            int n = 0;
            for (auto& e : *lst.list) {
                if (e.type == v.type) {
                    if (e.type == Value::Type::STRING && e.str == v.str) n++;
                    else if (e.type == Value::Type::NUMBER && e.num == v.num) n++;
                    else if (e.type == Value::Type::BOOL && e.bl == v.bl) n++;
                }
            }
            return Value::Number((double)n);
        }
        if (name == "sort_desc") {
            Value lst = arg(0);
            auto out = make_shared<vector<Value>>(*lst.list);
            sort(out->begin(), out->end(), [](const Value& a, const Value& b) {
                if (a.type == Value::Type::NUMBER && b.type == Value::Type::NUMBER) return a.num > b.num;
                if (a.type == Value::Type::STRING && b.type == Value::Type::STRING) return a.str > b.str;
                return false;
            });
            return Value::List(out);
        }
        // -- Dict extras (3.11) --
        if (name == "values") {
            Value d = arg(0);
            if (d.type != Value::Type::DICT) fail("values: arg must be a dict", ln);
            auto out = make_shared<vector<Value>>();
            for (auto& [k, v] : d.dict->entries) out->push_back(v);
            return Value::List(out);
        }
        if (name == "items") {
            Value d = arg(0);
            if (d.type != Value::Type::DICT) fail("items: arg must be a dict", ln);
            auto out = make_shared<vector<Value>>();
            for (auto& [k, v] : d.dict->entries) {
                auto pair = make_shared<vector<Value>>();
                pair->push_back(Value::Str(k));
                pair->push_back(v);
                out->push_back(Value::List(pair));
            }
            return Value::List(out);
        }
        if (name == "remove_key") {
            string key = arg(0).str;
            Value d = arg(1);
            if (d.type != Value::Type::DICT) fail("remove_key: arg must be a dict", ln);
            Value* cur = d.dict->get(key);
            Value old = cur ? *cur : Value::Bool(false);
            d.dict->entries.erase(d.dict->entries.begin() + d.dict->index[key]);
            d.dict->index.erase(key);
            return old;
        }
        if (name == "merge") {
            Value a = arg(0), b = arg(1);
            if (a.type != Value::Type::DICT || b.type != Value::Type::DICT)
                fail("merge: both args must be dicts", ln);
            auto d = make_shared<DictData>();
            for (auto& [k, v] : a.dict->entries) d->set(k, v);
            for (auto& [k, v] : b.dict->entries) d->set(k, v);
            return Value::Dict(d);
        }
        if (name == "copy") {
            Value v = arg(0);
            if (v.type == Value::Type::LIST) {
                auto out = make_shared<vector<Value>>(*v.list);
                return Value::List(out);
            }
            if (v.type == Value::Type::DICT) {
                auto out = make_shared<DictData>();
                for (auto& [k, val] : v.dict->entries) out->set(k, val);
                return Value::Dict(out);
            }
            return v;
        }
        // -- Type introspection (3.12) --
        if (name == "type_of") {
            Value v = arg(0);
            switch (v.type) {
                case Value::Type::NUMBER: return Value::Str("number");
                case Value::Type::STRING: return Value::Str("string");
                case Value::Type::BOOL:   return Value::Str("bool");
                case Value::Type::LIST:    return Value::Str("list");
                case Value::Type::DICT:    return Value::Str("dict");
            }
            return Value::Str("unknown");
        }
        if (name == "is_dict")   return Value::Bool(arg(0).type == Value::Type::DICT);
        if (name == "is_function") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) return Value::Bool(false);
            return Value::Bool(functions.count(v.str) != 0);
        }
        if (name == "callable") {
            Value v = arg(0);
            if (v.type != Value::Type::STRING) return Value::Bool(false);
            return Value::Bool(functions.count(v.str) != 0);
        }
        if (name == "is_empty") {
            Value v = arg(0);
            if (v.type == Value::Type::STRING) return Value::Bool(v.str.empty());
            if (v.type == Value::Type::LIST)   return Value::Bool(v.list->empty());
            if (v.type == Value::Type::DICT)    return Value::Bool(v.dict->entries.empty());
            if (v.type == Value::Type::NUMBER) return Value::Bool(v.num == 0);
            if (v.type == Value::Type::BOOL)   return Value::Bool(!v.bl);
            return Value::Bool(false);
        }
        if (name == "is_file")   return Value::Bool(fs::is_regular_file(arg(0).str));
        if (name == "is_dir")    return Value::Bool(fs::is_directory(arg(0).str));
        if (name == "to_json_pretty") {
            string s = jsonSerialize(arg(0), ln);
            // indent by 2 spaces - simple pretty-print
            string out;
            int depth = 0;
            for (size_t i = 0; i < s.size(); i++) {
                char c = s[i];
                if (c == '{' || c == '[') {
                    out += c; out += '\n';
                    depth++; out += string(depth * 2, ' ');
                } else if (c == '}' || c == ']') {
                    out += '\n'; depth--; out += string(depth * 2, ' '); out += c;
                } else if (c == ',') {
                    out += c; out += '\n'; out += string(depth * 2, ' ');
                } else if (c == ':') {
                    out += c; out += ' ';
                } else {
                    out += c;
                }
            }
            return Value::Str(out);
        }
        // -- File I/O extras (3.14) --
        if (name == "copy_file") {
            fs::copy_file(arg(0).str, arg(1).str, fs::copy_options::overwrite_existing);
            return Value::Number(0);
        }
        if (name == "move_file") {
            fs::rename(arg(0).str, arg(1).str);
            return Value::Number(0);
        }
        if (name == "stat") {
            string path = arg(0).str;
            auto d = make_shared<DictData>();
            if (!fs::exists(path)) {
                d->set("size", Value::Number(0));
                d->set("is_dir", Value::Bool(false));
                d->set("is_file", Value::Bool(false));
                d->set("readable", Value::Bool(false));
                d->set("writable", Value::Bool(false));
                d->set("mtime", Value::Number(0));
                return Value::Dict(d);
            }
            std::error_code ec;
            auto perms = fs::status(path, ec).permissions();
            d->set("size", Value::Number((double)fs::file_size(path, ec)));
            d->set("is_dir", Value::Bool(fs::is_directory(path)));
            d->set("is_file", Value::Bool(fs::is_regular_file(path)));
            d->set("readable", Value::Bool((perms & (fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read)) != fs::perms::none));
            d->set("writable", Value::Bool((perms & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) != fs::perms::none));
            struct stat st;
            stat(path.c_str(), &st);
            d->set("mtime", Value::Number((double)st.st_mtime));
            return Value::Dict(d);
        }
        if (name == "read_binary") {
            string path = arg(0).str;
            ifstream f(path, ios::binary);
            if (!f) fail("read_binary: cannot open '" + path + "'", ln);
            string s((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
            auto out = make_shared<vector<Value>>();
            for (unsigned char c : s) out->push_back(Value::Number((double)c));
            return Value::List(out);
        }
        if (name == "write_binary") {
            string path = arg(0).str;
            Value data = arg(1);
            string s;
            for (auto& v : *data.list) s += (char)(unsigned char)v.num;
            ofstream f(path, ios::binary | ios::trunc);
            f << s;
            return Value::Number(0);
        }
        if (name == "glob") {
            string pattern = arg(0).str;
            auto out = make_shared<vector<Value>>();
            fs::path p(pattern);
            fs::path dir = p.parent_path().empty() ? "." : p.parent_path();
            string fname = p.filename().string();
            // Simple wildcard match: * and ?
            auto matches = [](const string& pat, const string& name) -> bool {
                size_t i = 0, j = 0, star = string::npos, starmatch = 0;
                while (j < name.size()) {
                    if (i < pat.size() && (pat[i] == '?' || pat[i] == name[j])) { i++; j++; }
                    else if (i < pat.size() && pat[i] == '*') { star = i; starmatch = j; i++; }
                    else if (star != string::npos) { i = star + 1; starmatch++; j = starmatch; }
                    else return false;
                }
                while (i < pat.size() && pat[i] == '*') i++;
                return i == pat.size();
            };
            std::error_code ec;
            if (fs::exists(dir, ec)) {
                for (auto& entry : fs::directory_iterator(dir, ec)) {
                    if (matches(fname, entry.path().filename().string())) {
                        out->push_back(Value::Str((dir / entry.path().filename()).string()));
                    }
                }
            }
            return Value::List(out);
        }
        if (name == "temp_file") {
            string prefix = e->items.size() >= 1 ? arg(0).str : "ocode";
            char tmpl[] = "/tmp/ocodeXXXXXX";
            int fd = mkstemp(tmpl);
            if (fd < 0) fail("temp_file: mkstemp failed", ln);
            close(fd);
            return Value::Str(string(tmpl));
        }
        if (name == "temp_dir") {
            string prefix = e->items.size() >= 1 ? arg(0).str : "ocode";
            char tmpl[] = "/tmp/ocodeXXXXXX";
            char* d = mkdtemp(tmpl);
            if (!d) fail("temp_dir: mkdtemp failed", ln);
            return Value::Str(string(d));
        }
        if (name == "chmod") {
            string path = arg(0).str;
            int mode = (int)arg(1).num;
            std::error_code ec;
            fs::permissions(path, (fs::perms)mode, ec);
            return Value::Number(0);
        }
        if (name == "cwd") {
            return Value::Str(fs::current_path().string());
        }
        if (name == "chdir") {
            fs::current_path(arg(0).str);
            return Value::Number(0);
        }
        // -- Network extras (3.15) --
        if (name == "udp_socket") {
            int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0) fail("udp_socket: cannot create socket", ln);
            return Value::Number((double)socketRegister(fd));
        }
        if (name == "server_socket") {
            int port = (int)arg(0).num;
            int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) fail("server_socket: cannot create socket", ln);
            int yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);
            if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(fd); fail("server_socket: bind failed on port " + to_string(port), ln);
            }
            if (listen(fd, 5) < 0) { close(fd); fail("server_socket: listen failed", ln); }
            return Value::Number((double)socketRegister(fd));
        }
        if (name == "accept") {
            int sfd = socketLookupFd((int)arg(0).num);
            if (sfd < 0) fail("accept: invalid server handle", ln);
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = (int)accept(sfd, (struct sockaddr*)&caddr, &clen);
            if (cfd < 0) fail("accept: no incoming connection", ln);
            return Value::Number((double)socketRegister(cfd));
        }
        if (name == "resolve_host") {
            string hostname = arg(0).str;
            struct addrinfo* res = nullptr;
            auto out = make_shared<vector<Value>>();
            if (getaddrinfo(hostname.c_str(), nullptr, nullptr, &res) == 0) {
                for (struct addrinfo* p = res; p; p = p->ai_next) {
                    char ip[INET6_ADDRSTRLEN];
                    if (p->ai_family == AF_INET) {
                        inet_ntop(AF_INET, &((struct sockaddr_in*)p->ai_addr)->sin_addr, ip, sizeof(ip));
                        out->push_back(Value::Str(string(ip)));
                    } else if (p->ai_family == AF_INET6) {
                        inet_ntop(AF_INET6, &((struct sockaddr_in6*)p->ai_addr)->sin6_addr, ip, sizeof(ip));
                        out->push_back(Value::Str(string(ip)));
                    }
                }
                freeaddrinfo(res);
            }
            return Value::List(out);
        }
        if (name == "set_socket_timeout") {
            int fd = socketLookupFd((int)arg(0).num);
            if (fd < 0) fail("set_socket_timeout: invalid handle", ln);
            int ms = (int)arg(1).num;
            struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            return Value::Number(0);
        }
        if (name == "http_download") {
            string url = arg(0).str, path = arg(1).str;
            HttpResponse r = httpPerform(url, "GET", "", g_defaultHeaders, g_defaultHttpTimeout, ln);
            ofstream f(path, ios::binary | ios::trunc);
            if (!f) fail("http_download: cannot write to '" + path + "'", ln);
            f << r.body;
            return Value::Number(0);
        }
        if (name == "http_upload") {
            string url = arg(0).str, filePath = arg(1).str;
            string fieldName = e->items.size() >= 3 ? arg(2).str : "file";
            ifstream f(filePath, ios::binary);
            if (!f) fail("http_upload: cannot open '" + filePath + "'", ln);
            string body((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
            HttpResponse r = httpPerform(url, "POST", body, g_defaultHeaders, g_defaultHttpTimeout, ln);
            return Value::Str(r.body);
        }
        if (name == "http_set_headers") {
            Value v = arg(0);
            g_defaultHeaders.clear();
            if (v.type == Value::Type::LIST) {
                for (auto& item : *v.list) {
                    if (item.type == Value::Type::LIST && item.list->size() == 2) {
                        g_defaultHeaders.emplace_back((*item.list)[0].str, (*item.list)[1].str);
                    }
                }
            } else if (v.type == Value::Type::DICT) {
                for (auto& [k, val] : v.dict->entries) g_defaultHeaders.emplace_back(k, val.str);
            }
            return Value::Number(0);
        }
        if (name == "http_set_timeout") {
            g_defaultHttpTimeout = (int)arg(0).num;
            return Value::Number(0);
        }
        // -- System (3.17) --
        if (name == "env") {
            if (e->items.empty()) {
                auto d = make_shared<DictData>();
                extern char** environ;
                for (char** p = environ; *p; p++) {
                    string entry(*p);
                    size_t eq = entry.find('=');
                    if (eq != string::npos) d->set(entry.substr(0, eq), Value::Str(entry.substr(eq + 1)));
                }
                return Value::Dict(d);
            }
            const char* v = getenv(arg(0).str.c_str());
            return v ? Value::Str(string(v)) : Value::Bool(false);
        }
        if (name == "shell") {
            string cmd = arg(0).str;
            string input;
            if (e->items.size() >= 2) input = arg(1).str;
            string out, err;
            string fullCmd = cmd;
            if (!input.empty()) fullCmd = "echo '" + input + "' | " + cmd;
            FILE* pipe = popen(fullCmd.c_str(), "r");
            if (!pipe) fail("shell: popen failed for '" + cmd + "'", ln);
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe)) out += buf;
            int code = pclose(pipe);
            auto d = make_shared<DictData>();
            d->set("stdout", Value::Str(out));
            d->set("stderr", Value::Str(""));
            d->set("exit", Value::Number((double)code));
            return Value::Dict(d);
        }
        if (name == "platform") {
#ifdef _WIN32
            return Value::Str("windows");
#elif defined(__APPLE__)
            return Value::Str("macos");
#elif defined(__ANDROID__)
            return Value::Str("android");
#elif defined(__linux__)
            return Value::Str("linux");
#else
            return Value::Str("unix");
#endif
        }
        if (name == "pid") return Value::Number((double)getpid());
        if (name == "memory_used") {
            // Best-effort - return resident set size from /proc/self/statm
            ifstream f("/proc/self/statm");
            if (f) {
                size_t rss; size_t dummy;
                f >> dummy >> rss;
                return Value::Number((double)(rss * 4096));
            }
            return Value::Number(0);
        }
        if (name == "uptime") {
            static auto start = chrono::steady_clock::now();
            auto dur = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start);
            return Value::Number((double)dur.count());
        }
        if (name == "hostname") {
            char h[256];
            if (gethostname(h, sizeof(h)) == 0) return Value::Str(string(h));
            return Value::Str("");
        }
        // -- Random extras (3.18) --
        if (name == "random_int") {
            long long lo = (long long)arg(0).num, hi = (long long)arg(1).num;
            if (lo > hi) swap(lo, hi);
            uniform_int_distribution<long long> dist(lo, hi);
            return Value::Number((double)dist(rng));
        }
        if (name == "random_choice") {
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST || lst.list->empty()) fail("random_choice: list is empty", ln);
            uniform_int_distribution<size_t> dist(0, lst.list->size() - 1);
            return (*lst.list)[dist(rng)];
        }
        if (name == "random_shuffle") {
            Value lst = arg(0);
            if (lst.type != Value::Type::LIST) fail("random_shuffle: arg must be a list", ln);
            shuffle(lst.list->begin(), lst.list->end(), rng);
            return Value::Number(0);
        }
        if (name == "random_seed") {
            unsigned seed = (unsigned)arg(0).num;
            rng.seed(seed);
            return Value::Number(0);
        }
        // -- Concurrency (3.19) --
        if (name == "thread_start") {
            string fn = arg(0).str;
            Value argVal = e->items.size() >= 2 ? arg(1) : Value::Number(0);
            int handle = (int)g_threads.size() + 1;
            g_threads.push_back(nullptr);
            g_threadResults.push_back(Value::Number(0));
            g_threadDone.push_back(false);
            // For simplicity, we run synchronously (no real thread spawn in this build)
            // because the interpreter is not thread-safe. This stub still returns
            // a "handle" so users can write code; thread_wait just returns the result.
            try {
                vector<Value> args = {argVal};
                g_threadResults[handle - 1] = callFunction(fn, args, ln);
                g_threadDone[handle - 1] = true;
            } catch (...) {
                g_threadResults[handle - 1] = Value::Number(0);
                g_threadDone[handle - 1] = true;
            }
            return Value::Number((double)handle);
        }
        if (name == "thread_wait") {
            int h = (int)arg(0).num;
            if (h < 1 || h > (int)g_threadResults.size()) fail("thread_wait: invalid handle", ln);
            return g_threadResults[h - 1];
        }
        if (name == "channel_new") {
            auto ch = make_shared<OcChannel>();
            if (e->items.size() >= 1) ch->cap = (size_t)arg(0).num;
            g_channels.push_back(ch);
            return Value::Number((double)g_channels.size());
        }
        if (name == "channel_send") {
            int h = (int)arg(0).num;
            if (h < 1 || h > (int)g_channels.size()) fail("channel_send: invalid handle", ln);
            Value v = arg(1);
            auto& ch = g_channels[h - 1];
            unique_lock<mutex> lk(ch->m);
            if (ch->cap > 0) {
                ch->cv.wait(lk, [&] { return ch->q.size() < ch->cap; });
            }
            ch->q.push(v);
            ch->cv.notify_one();
            return Value::Number(0);
        }
        if (name == "channel_recv") {
            int h = (int)arg(0).num;
            if (h < 1 || h > (int)g_channels.size()) fail("channel_recv: invalid handle", ln);
            auto& ch = g_channels[h - 1];
            unique_lock<mutex> lk(ch->m);
            ch->cv.wait(lk, [&] { return !ch->q.empty(); });
            Value v = ch->q.front(); ch->q.pop();
            ch->cv.notify_one();
            return v;
        }
        if (name == "mutex_new") {
            auto m = make_shared<mutex>();
            g_mutexes.push_back(m);
            return Value::Number((double)g_mutexes.size());
        }
        if (name == "mutex_lock") {
            int h = (int)arg(0).num;
            if (h < 1 || h > (int)g_mutexes.size()) fail("mutex_lock: invalid handle", ln);
            g_mutexes[h - 1]->lock();
            return Value::Number(0);
        }
        if (name == "mutex_unlock") {
            int h = (int)arg(0).num;
            if (h < 1 || h > (int)g_mutexes.size()) fail("mutex_unlock: invalid handle", ln);
            g_mutexes[h - 1]->unlock();
            return Value::Number(0);
        }
        // -- Reflection (3.20) --
        if (name == "eval_ocode") {
            // Evaluate OCode source in a sub-runtime: re-parse the source string
            // and run it. For simplicity, we just execute the source as a single
            // expression statement and return its value.
            string src = arg(0).str;
            // Stub: re-run as a block in the same interpreter
            // (a full re-entrant eval requires lexer/parser instantiation which
            // is not thread-safe in this build; return parse of "let _eval = src"
            // and run it.)
            return Value::Str(src);
        }
        if (name == "source_line") return Value::Number((double)ln);
        if (name == "get_var") {
            Value* v = env.find(arg(0).str);
            return v ? *v : Value::Bool(false);
        }
        if (name == "set_var") {
            env.set(arg(0).str, arg(1));
            return Value::Number(0);
        }
        // -- Error handling (3.21) --
        if (name == "raise") {
            string msg = arg(0).str;
            int code = 0;
            if (e->items.size() >= 2) code = (int)arg(1).num;
            g_lastErrorMsg = msg;
            g_lastErrorCode = code;
            throw OCodeError("raise: " + msg, ln);
        }
        if (name == "error_message") return Value::Str(g_lastErrorMsg);
        if (name == "error_code") return Value::Number((double)g_lastErrorCode);
        // -- Encoding & Hashing (3.22/3.23) --
        if (name == "encode_base64") {
            string s = arg(0).str;
            static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            string out;
            size_t i = 0;
            while (i < s.size()) {
                size_t startI = i;
                int a = (unsigned char)s[i++];
                int b = i < s.size() ? (unsigned char)s[i++] : 0;
                int c = i < s.size() ? (unsigned char)s[i++] : 0;
                int consumed = (int)(i - startI);
                out += b64[a >> 2];
                out += b64[((a & 3) << 4) | (b >> 4)];
                out += (consumed >= 2) ? b64[((b & 15) << 2) | (c >> 6)] : '=';
                out += (consumed >= 3) ? b64[c & 63] : '=';
            }
            return Value::Str(out);
        }
        if (name == "decode_base64") {
            string s = arg(0).str;
            static int b64idx[256];
            static bool init = false;
            if (!init) {
                for (int i = 0; i < 256; i++) b64idx[i] = -1;
                const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (int i = 0; i < 64; i++) b64idx[(unsigned char)b64[i]] = i;
                init = true;
            }
            string out;
            int bits = 0, val = 0;
            for (char c : s) {
                if (c == '=') break;
                if (b64idx[(unsigned char)c] == -1) continue;
                val = (val << 6) | b64idx[(unsigned char)c];
                bits += 6;
                if (bits >= 8) { bits -= 8; out += (char)((val >> bits) & 0xFF); }
            }
            return Value::Str(out);
        }
        if (name == "hash_sha256" || name == "hash_sha1" || name == "hash_sha512" ||
            name == "hash_md5" || name == "hmac_sha256") {
#ifndef OCODE_NO_OPENSSL
            const EVP_MD* md = nullptr;
            if (name == "hash_sha256" || name == "hmac_sha256") md = EVP_sha256();
            else if (name == "hash_sha1")   md = EVP_sha1();
            else if (name == "hash_sha512") md = EVP_sha512();
            else if (name == "hash_md5")    md = EVP_md5();
            if (!md) fail(name + ": hash algorithm not available", ln);
            string s = arg(0).str;
            unsigned char hash[EVP_MAX_MD_SIZE];
            unsigned int hashLen = 0;
            if (name == "hmac_sha256") {
                string key = arg(0).str, msg = arg(1).str;
                HMAC(md, key.c_str(), (int)key.size(),
                     (const unsigned char*)msg.c_str(), msg.size(),
                     hash, &hashLen);
            } else {
                EVP_MD_CTX* ctx = EVP_MD_CTX_new();
                EVP_DigestInit_ex(ctx, md, nullptr);
                EVP_DigestUpdate(ctx, s.c_str(), s.size());
                EVP_DigestFinal_ex(ctx, hash, &hashLen);
                EVP_MD_CTX_free(ctx);
            }
            stringstream ss;
            ss << hex << setfill('0');
            for (unsigned int i = 0; i < hashLen; i++) ss << setw(2) << (int)hash[i];
            return Value::Str(ss.str());
#else
            fail(name + ": rebuilt without OpenSSL (-DOCODE_NO_OPENSSL)", ln);
            return Value::Str("");
#endif
        }
        if (name == "url_encode") {
            string s = arg(0).str;
            static const char hex[] = "0123456789ABCDEF";
            string out;
            for (char c : s) {
                if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
                else { out += '%'; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF]; }
            }
            return Value::Str(out);
        }
        if (name == "url_decode") {
            string s = arg(0).str;
            string out;
            for (size_t i = 0; i < s.size(); i++) {
                if (s[i] == '%' && i + 2 < s.size()) {
                    int hi = (s[i+1] >= 'A') ? (s[i+1] - 'A' + 10) : (s[i+1] - '0');
                    int lo = (s[i+2] >= 'A') ? (s[i+2] - 'A' + 10) : (s[i+2] - '0');
                    out += (char)((hi << 4) | lo);
                    i += 2;
                } else if (s[i] == '+') out += ' ';
                else out += s[i];
            }
            return Value::Str(out);
        }
        // -- Saved vars (3.23) --
        if (name == "save_var") {
            string n = arg(0).str;
            Value v = arg(1);
            string path = savedVarPath(n);
            ofstream f(path, ios::trunc);
            if (!f) fail("save_var: cannot write '" + path + "'", ln);
            f << jsonSerialize(v, ln);
            return Value::Bool(true);
        }
        if (name == "load_var") {
            string n = arg(0).str;
            string path = savedVarPath(n);
            ifstream f(path);
            if (!f) {
                if (e->items.size() >= 2) return arg(1);
                return Value::Bool(false);
            }
            string s((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
            size_t i = 0;
            return jsonParseValue(s, i, ln);
        }
        if (name == "saved_vars") {
            auto out = make_shared<vector<Value>>();
            string dir = savedVarsDir();
            for (auto& entry : fs::directory_iterator(dir)) {
                string p = entry.path().filename().string();
                if (p.size() > 5 && p.substr(p.size() - 5) == ".json") {
                    out->push_back(Value::Str(p.substr(0, p.size() - 5)));
                }
            }
            return Value::List(out);
        }
        if (name == "clear_saved_var") {
            string path = savedVarPath(arg(0).str);
            fs::remove(path);
            return Value::Bool(true);
        }


        if (name == "read") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("read: path must be a string", ln);
            ifstream f(p.str, ios::binary);
            if (!f) fail("read: cannot open '" + p.str + "'", ln);
            ostringstream ss; ss << f.rdbuf();
            return Value::Str(ss.str());
        }
        if (name == "readlines") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("readlines: path must be a string", ln);
            ifstream f(p.str);
            if (!f) fail("readlines: cannot open '" + p.str + "'", ln);
            auto vec = make_shared<vector<Value>>();
            string line;
            while (getline(f, line)) {

                if (!line.empty() && line.back() == '\r') line.pop_back();
                vec->push_back(Value::Str(line));
            }
            return Value::List(vec);
        }
        if (name == "write") {
            Value p = arg(0), c = arg(1);
            if (p.type != Value::Type::STRING) fail("write: path must be a string", ln);
            if (c.type != Value::Type::STRING) fail("write: content must be a string", ln);
            ofstream f(p.str, ios::binary | ios::trunc);
            if (!f) fail("write: cannot open '" + p.str + "' for writing", ln);
            f << c.str;
            if (!f) fail("write: failed to write to '" + p.str + "'", ln);
            return Value::Number(0);
        }
        if (name == "append") {
            Value p = arg(0), c = arg(1);
            if (p.type != Value::Type::STRING) fail("append: path must be a string", ln);
            if (c.type != Value::Type::STRING) fail("append: content must be a string", ln);
            ofstream f(p.str, ios::binary | ios::app);
            if (!f) fail("append: cannot open '" + p.str + "' for appending", ln);
            f << c.str;
            if (!f) fail("append: failed to write to '" + p.str + "'", ln);
            return Value::Number(0);
        }
        if (name == "ls") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("ls: path must be a string", ln);
            DIR* d = opendir(p.str.c_str());
            if (!d) fail("ls: cannot open directory '" + p.str + "'", ln);
            auto vec = make_shared<vector<Value>>();
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                string n = ent->d_name;
                if (n == "." || n == "..") continue;
                vec->push_back(Value::Str(n));
            }
            closedir(d);

            sort(vec->begin(), vec->end(),
                [](const Value& a, const Value& b) { return a.str < b.str; });
            return Value::List(vec);
        }
        if (name == "exists") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("exists: path must be a string", ln);
            struct stat st;
            return Value::Bool(stat(p.str.c_str(), &st) == 0);
        }
        if (name == "mkdir") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("mkdir: path must be a string", ln);
            if (OCODE_MKDIR(p.str.c_str()) != 0)
                fail("mkdir: cannot create directory '" + p.str + "'", ln);
            return Value::Number(0);
        }
        if (name == "rm") {
            Value p = arg(0);
            if (p.type != Value::Type::STRING) fail("rm: path must be a string", ln);
            if (std::remove(p.str.c_str()) != 0)
                fail("rm: cannot remove '" + p.str + "'", ln);
            return Value::Number(0);
        }

        if (name == "keys") {
            Value d = arg(0);
            if (d.type != Value::Type::DICT) fail("keys of requires a dict", ln);
            auto vec = make_shared<vector<Value>>();
            vec->reserve(d.dict->entries.size());
            for (auto& [k, v] : d.dict->entries) vec->push_back(Value::Str(k));
            return Value::List(vec);
        }

        if (name == "has") {
            Value k = arg(0), d = arg(1);
            if (k.type != Value::Type::STRING) fail("has requires a string key", ln);
            if (d.type != Value::Type::DICT)   fail("has requires a dict as the second argument", ln);
            return Value::Bool(d.dict->has(k.str));
        }

        if (name == "range") {
            double start = valueToNumber(arg(0), ln);
            double end_  = valueToNumber(arg(1), ln);
            double step  = (e->items.size() >= 3) ? valueToNumber(arg(2), ln) : 1.0;
            if (step == 0) fail("range step cannot be zero", ln);
            auto vec = make_shared<vector<Value>>();
            if (step > 0) for (double v = start; v <= end_ + 1e-9; v += step) vec->push_back(Value::Number(v));
            else          for (double v = start; v >= end_ - 1e-9; v += step) vec->push_back(Value::Number(v));
            return Value::List(vec);
        }

        if (name == "now") {

            auto dur = chrono::system_clock::now().time_since_epoch();
            return Value::Number(chrono::duration<double>(dur).count());
        }
        if (name == "today") {
            time_t t = time(nullptr); struct tm* lt = localtime(&t);
            char buf[16]; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday);
            return Value::Str(buf);
        }
        if (name == "clock") {
            time_t t = time(nullptr); struct tm* lt = localtime(&t);
            char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
            return Value::Str(buf);
        }
        if (name == "sleep") {
            double s = valueToNumber(arg(0), ln);
            if (s < 0) fail("sleep requires a non-negative number of seconds", ln);
            this_thread::sleep_for(chrono::duration<double>(s));
            return Value::Number(0);
        }

        if (name == "parse_json") {
            Value sv = arg(0);
            if (sv.type != Value::Type::STRING) fail("parse_json requires a string", ln);
            size_t i = 0;
            Value v = jsonParseValue(sv.str, i, ln);
            i = jsonSkipWs(sv.str, i);
            if (i < sv.str.size()) fail("trailing characters in JSON", ln);
            return v;
        }
        if (name == "to_json") {
            return Value::Str(jsonSerialize(arg(0), ln));
        }

        fail("unknown builtin '" + name + "'", ln);
    }

    Value applyBinOp(const string& op, const Value& l, const Value& r, int line) {
        if (op == "+") {
            if (l.type == Value::Type::STRING || r.type == Value::Type::STRING)
                return Value::Str(valueToString(l) + valueToString(r));
            if (l.type == Value::Type::LIST && r.type == Value::Type::LIST) {
                auto vec = make_shared<vector<Value>>(*l.list);
                vec->insert(vec->end(), r.list->begin(), r.list->end());
                return Value::List(vec);
            }

            if (l.type == Value::Type::DICT && r.type == Value::Type::DICT) {
                auto merged = make_shared<DictData>();

                for (auto& [k, v] : l.dict->entries) merged->set(k, v);

                for (auto& [k, v] : r.dict->entries) merged->set(k, v);
                return Value::Dict(merged);
            }
            return Value::Number(valueToNumber(l,line) + valueToNumber(r,line));
        }
        if (op == "-") return Value::Number(valueToNumber(l,line) - valueToNumber(r,line));
        if (op == "*") return Value::Number(valueToNumber(l,line) * valueToNumber(r,line));
        if (op == "/") {
            double rn = valueToNumber(r,line);
            if (rn == 0) fail("division by zero", line);
            return Value::Number(valueToNumber(l,line) / rn);
        }
        if (op == "%") {
            double rn = valueToNumber(r,line);
            if (rn == 0) fail("modulo by zero", line);
            return Value::Number(fmod(valueToNumber(l,line), rn));
        }
        if (op == "==") return Value::Bool(valuesEqual(l,r));
        if (op == "!=") return Value::Bool(!valuesEqual(l,r));
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            double a = valueToNumber(l,line), b = valueToNumber(r,line);
            if (op == "<")  return Value::Bool(a < b);
            if (op == ">")  return Value::Bool(a > b);
            if (op == "<=") return Value::Bool(a <= b);
            return Value::Bool(a >= b);
        }
        fail("unknown operator '" + op + "'", line);
    }

    bool valuesEqual(const Value& l, const Value& r) {
        if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER) return l.num == r.num;
        if (l.type == Value::Type::STRING && r.type == Value::Type::STRING) return l.str == r.str;
        if (l.type == Value::Type::BOOL   && r.type == Value::Type::BOOL)   return l.bl == r.bl;
        if (l.type == Value::Type::LIST   && r.type == Value::Type::LIST) {
            if (l.list->size() != r.list->size()) return false;
            for (size_t i = 0; i < l.list->size(); i++)
                if (!valuesEqual((*l.list)[i], (*r.list)[i])) return false;
            return true;
        }

        if (l.type == Value::Type::DICT && r.type == Value::Type::DICT) {
            if (l.dict->entries.size() != r.dict->entries.size()) return false;
            for (auto& [lk, lv] : l.dict->entries) {
                Value* rv = r.dict->get(lk);
                if (!rv || !valuesEqual(lv, *rv)) return false;
            }
            return true;
        }
        return false;
    }
};

static string ocodeLibDir() {
    const char* home = getenv("HOME");
    if (!home) return ".";
    return string(home) + "/.ocode/lib";
}

static int ocdInstall(const string& srcPath) {

    ifstream in(srcPath);
    if (!in) { cerr << "ocd error: cannot open '" << srcPath << "'\n"; return 1; }
    stringstream ss; ss << in.rdbuf();
    string source = ss.str();
    in.close();

    try {
        Lexer lexer(source);
        auto toks = lexer.tokenize();
        Parser parser(toks);
        parser.parseProgram();
    } catch (OCodeError& e) {
        cerr << "ocd error: '" << srcPath << "' has a syntax error (line " << e.line << "): " << e.what() << "\n";
        return 1;
    }

    string fileName = srcPath;
    size_t slashPos = fileName.find_last_of("/\\");
    if (slashPos != string::npos) fileName = fileName.substr(slashPos + 1);

    string libDir = ocodeLibDir();
    filesystem::create_directories(libDir);

    string destPath = libDir + "/" + fileName;
    ifstream src(srcPath, ios::binary);
    ofstream dst(destPath, ios::binary);
    if (!dst) { cerr << "ocd error: cannot write to '" << destPath << "'\n"; return 1; }
    dst << src.rdbuf();
    src.close(); dst.close();

    string pkgName = fileName;
    if (pkgName.size() > 3 && pkgName.substr(pkgName.size() - 3) == ".oc")
        pkgName = pkgName.substr(0, pkgName.size() - 3);

    cout << "Installed '" << pkgName << "' to " << destPath << "\n";
    cout << "Now you can use it with: use " << pkgName << "\n";
    return 0;
}

static int ocdList() {
    string libDir = ocodeLibDir();
    if (!filesystem::exists(libDir)) {
        cout << "No packages installed.\n";
        return 0;
    }
    vector<string> packages;
    for (auto& entry : filesystem::directory_iterator(libDir)) {
        if (entry.is_regular_file()) {
            string name = entry.path().filename().string();
            if (name.size() > 3 && name.substr(name.size() - 3) == ".oc") {
                packages.push_back(name.substr(0, name.size() - 3));
            }
        }
    }
    if (packages.empty()) {
        cout << "No packages installed.\n";
    } else {
        cout << "Installed packages (" << packages.size() << "):\n";
        for (auto& p : packages) cout << "  - " << p << "\n";
    }
    return 0;
}

static int ocdRemove(const string& pkgName) {
    string libDir = ocodeLibDir();
    string filePath = libDir + "/" + pkgName + ".oc";
    if (!filesystem::exists(filePath)) {
        cerr << "ocd error: package '" << pkgName << "' is not installed.\n";
        return 1;
    }
    filesystem::remove(filePath);
    cout << "Removed '" << pkgName << "'.\n";
    return 0;
}

static void printOcdHelp() {
    cout << "ocd - OCode package manager\n";
    cout << "\n";
    cout << "Usage:\n";
    cout << "  ocd install <file.oc>     Install an OCode package (.oc file)\n";
    cout << "  ocd install <package>      Install a Python package from PyPI\n";
    cout << "  ocd list                  List installed packages (OCode + Python)\n";
    cout << "  ocd remove <name>         Remove an OCode package\n";
    cout << "  ocd uninstall <name>      Uninstall a Python package from PyPI\n";
    cout << "  ocd help                  Show this help message\n";
    cout << "\n";
    cout << "Rules:\n";
    cout << "  - 'ocd install file.oc' installs an OCode package (ends with .oc)\n";
    cout << "  - 'ocd install requests' installs a Python package from PyPI\n";
    cout << "  - 'ocd list' shows both OCode and Python packages\n";
    cout << "  - 'ocd remove' removes OCode packages, 'ocd uninstall' removes Python packages\n";
    cout << "\n";
    cout << "Packages are stored in ~/.ocode/lib/\n";
    cout << "Use OCode packages in your code with: use <name>\n";
}

static string findPip() {
    if (system("pip3 --version >/dev/null 2>&1") == 0) return "pip3";
    if (system("pip --version >/dev/null 2>&1") == 0) return "pip";
    return "";
}

static int ocdPipInstall(const string& pkgName) {
    string pipCmd = findPip();
    if (pipCmd.empty()) {
        cerr << "ocd error: pip not found. Please install Python 3 with pip.\n";
        return 1;
    }
    string cmd = pipCmd + " install --quiet " + pkgName + " 2>&1";
    cout << "Installing Python package: " << pkgName << "\n";
    int r = system(cmd.c_str());
    if (r != 0) {
        cerr << "ocd error: failed to install Python package '" << pkgName << "'\n";
        return 1;
    }
    cout << "Installed: " << pkgName << "\n";
    return 0;
}

static int ocdPipUninstall(const string& pkgName) {
    string pipCmd = findPip();
    if (pipCmd.empty()) {
        cerr << "ocd error: pip not found. Please install Python 3 with pip.\n";
        return 1;
    }
    string cmd = pipCmd + " uninstall -y " + pkgName + " 2>&1";
    cout << "Uninstalling Python package: " << pkgName << "\n";
    int r = system(cmd.c_str());
    if (r != 0) {
        cerr << "ocd error: failed to uninstall Python package '" << pkgName << "'\n";
        return 1;
    }
    return 0;
}

static int ocdPipList() {
    string pipCmd = findPip();
    if (pipCmd.empty()) {
        cerr << "ocd error: pip not found. Please install Python 3 with pip.\n";
        return 1;
    }
    string cmd = pipCmd + " list 2>&1";
    return system(cmd.c_str()) == 0 ? 0 : 1;
}

static const char* OC_RUNTIME_CPP = R"OCRT(
#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
using namespace std;

struct OCodeError : runtime_error { int line; OCodeError(const string& m, int l):runtime_error(m),line(l){} };
[[noreturn]] static void oc_fail(const string& msg, int line) { throw OCodeError(msg, line); }
struct BreakSignal {};
struct SkipSignal {};

struct Value;

struct DictData {
    vector<pair<string, Value>> entries;
    unordered_map<string, size_t> index;
    void set(const string& k, Value v);
    Value* get(const string& k);
    bool has(const string& k) const { return index.count(k) != 0; }
};

struct Value {
    enum class Type { NUMBER, STRING, BOOL, LIST, DICT } type = Type::NUMBER;
    double num = 0;
    string str;
    bool bl = false;
    shared_ptr<vector<Value>> list;
    shared_ptr<DictData> dict;
    static Value Number(double n) { Value v; v.type = Type::NUMBER; v.num = n; return v; }
    static Value Str(string s) { Value v; v.type = Type::STRING; v.str = move(s); return v; }
    static Value Bool(bool b) { Value v; v.type = Type::BOOL; v.bl = b; return v; }
    static Value List(shared_ptr<vector<Value>> l) { Value v; v.type = Type::LIST; v.list = move(l); return v; }
    static Value Dict(shared_ptr<DictData> d) { Value v; v.type = Type::DICT; v.dict = move(d); return v; }
};

struct ReturnSignal { Value value; };

inline void DictData::set(const string& k, Value v) {
    auto it = index.find(k);
    if (it != index.end()) { entries[it->second].second = move(v); return; }
    index.emplace(k, entries.size());
    entries.emplace_back(k, move(v));
}
inline Value* DictData::get(const string& k) {
    auto it = index.find(k);
    if (it == index.end()) return nullptr;
    return &entries[it->second].second;
}

static string valueToString(const Value& v) {
    switch (v.type) {
        case Value::Type::NUMBER: {
            double d = v.num;
            if (d == (long long)d && fabs(d) < 9.2e18) return to_string((long long)d);
            ostringstream oss; oss.setf(ios::fixed);
            oss << setprecision(6) << d;
            string s = oss.str();
            if (s.find('.') != string::npos) {
                size_t last = s.find_last_not_of('0');
                if (s[last] == '.') last--;
                s.erase(last + 1);
            }
            return s;
        }
        case Value::Type::STRING: return v.str;
        case Value::Type::BOOL: return v.bl ? "true" : "false";
        case Value::Type::LIST: {
            string s = "[";
            for (size_t i = 0; i < v.list->size(); i++) {
                if (i) s += ", ";
                const Value& e = (*v.list)[i];
                if (e.type == Value::Type::STRING) s += "\"" + e.str + "\"";
                else s += valueToString(e);
            }
            return s + "]";
        }
        case Value::Type::DICT: {
            string s = "{";
            for (size_t i = 0; i < v.dict->entries.size(); i++) {
                if (i) s += ", ";
                s += "\"" + v.dict->entries[i].first + "\": ";
                const Value& e = v.dict->entries[i].second;
                if (e.type == Value::Type::STRING) s += "\"" + e.str + "\"";
                else s += valueToString(e);
            }
            return s + "}";
        }
    }
    return "";
}
static inline bool valueToBool(const Value& v) {

    if (v.type == Value::Type::NUMBER) return v.num != 0;
    if (v.type == Value::Type::STRING) return !v.str.empty();
    if (v.type == Value::Type::BOOL)   return v.bl;
    if (v.type == Value::Type::LIST)   return v.list && !v.list->empty();
    if (v.type == Value::Type::DICT)   return v.dict && !v.dict->entries.empty();
    return false;
}
static inline double valueToNumber(const Value& v, int line) {

    if (v.type == Value::Type::NUMBER) return v.num;
    if (v.type == Value::Type::BOOL)   return v.bl ? 1 : 0;
    if (v.type == Value::Type::STRING) { try { return stod(v.str); } catch (...) { oc_fail("cannot convert string to number: \"" + v.str + "\"", line); } }
    oc_fail("expected a number here", line);
}
static bool valuesEqual(const Value& l, const Value& r) {
    if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER) return l.num == r.num;
    if (l.type == Value::Type::STRING && r.type == Value::Type::STRING) return l.str == r.str;
    if (l.type == Value::Type::BOOL   && r.type == Value::Type::BOOL)   return l.bl == r.bl;
    if (l.type == Value::Type::LIST   && r.type == Value::Type::LIST) {
        if (l.list->size() != r.list->size()) return false;
        for (size_t i = 0; i < l.list->size(); i++)
            if (!valuesEqual((*l.list)[i], (*r.list)[i])) return false;
        return true;
    }
    if (l.type == Value::Type::DICT && r.type == Value::Type::DICT) {
        if (l.dict->entries.size() != r.dict->entries.size()) return false;
        for (auto& kv : l.dict->entries) {
            Value* rv = r.dict->get(kv.first);
            if (!rv || !valuesEqual(kv.second, *rv)) return false;
        }
        return true;
    }
    return false;
}

static mt19937 g_rng{(unsigned)chrono::steady_clock::now().time_since_epoch().count()};

static uint64_t g_rng_state[2] = {
    (uint64_t)chrono::steady_clock::now().time_since_epoch().count() | 1u,
    (uint64_t)(uintptr_t)&g_rng_state | 1u
};
static inline uint64_t xorshift128p() {
    uint64_t s0 = g_rng_state[0];
    uint64_t s1 = g_rng_state[1];
    uint64_t result = s0 + s1;
    s1 ^= s0;
    g_rng_state[0] = ((s0 << 55) | (s0 >> 9)) ^ s1 ^ (s1 << 14);
    g_rng_state[1] = (s1 << 36) | (s1 >> 28);
    return result;
}
static inline double xorshift_double(double lo, double hi) {

    double frac = (double)(xorshift128p() >> 11) * (1.0 / 9007199254740992.0);
    return lo + frac * (hi - lo);
}

static inline Value addValues(const Value& l, const Value& r, int line) {

    if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER)
        return Value::Number(l.num + r.num);
    if (l.type == Value::Type::STRING || r.type == Value::Type::STRING)
        return Value::Str(valueToString(l) + valueToString(r));
    if (l.type == Value::Type::LIST && r.type == Value::Type::LIST) {
        auto vec = make_shared<vector<Value>>(*l.list);
        vec->insert(vec->end(), r.list->begin(), r.list->end());
        return Value::List(vec);
    }
    if (l.type == Value::Type::DICT && r.type == Value::Type::DICT) {
        auto merged = make_shared<DictData>();
        for (auto& kv : l.dict->entries) merged->set(kv.first, kv.second);
        for (auto& kv : r.dict->entries) merged->set(kv.first, kv.second);
        return Value::Dict(merged);
    }
    return Value::Number(valueToNumber(l, line) + valueToNumber(r, line));
}

static inline void addInPlace(Value& l, const Value& r, int line) {
    if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER) {
        l.num += r.num;
        return;
    }
    if (l.type == Value::Type::STRING) {
        if (r.type == Value::Type::STRING) { l.str += r.str; return; }
        l.str += valueToString(r);
        return;
    }
    if (l.type == Value::Type::LIST && r.type == Value::Type::LIST) {
        l.list->insert(l.list->end(), r.list->begin(), r.list->end());
        return;
    }

    l = addValues(l, r, line);
}
static Value indexValue(const Value& l, const Value& idx, int line) {
    if (l.type == Value::Type::STRING) {
        long long i = (long long)valueToNumber(idx, line);
        if (i < 0 || i >= (long long)l.str.size()) oc_fail("string index out of range", line);
        return Value::Str(string(1, l.str[i]));
    }
    if (l.type == Value::Type::DICT) {
        if (idx.type != Value::Type::STRING) oc_fail("dict key must be a string", line);
        Value* f = l.dict->get(idx.str);
        if (!f) oc_fail("key '" + idx.str + "' not found in dict", line);
        return *f;
    }
    if (l.type != Value::Type::LIST) oc_fail("cannot index a non-list/string/dict value", line);
    long long i = (long long)valueToNumber(idx, line);
    if (i < 0 || i >= (long long)l.list->size()) oc_fail("list index out of range", line);
    return (*l.list)[i];
}
static double lengthOf(const Value& v, int line) {
    if (v.type == Value::Type::LIST)   return (double)v.list->size();
    if (v.type == Value::Type::STRING) return (double)v.str.size();
    if (v.type == Value::Type::DICT)   return (double)v.dict->entries.size();
    oc_fail("length of requires a list, string, or dict", line);
}

static Value _ask(Value prompt, int) {
    cout << valueToString(prompt);
    string line; getline(cin, line);
    return Value::Str(line);
}
static Value _uppercase(Value v, int line) {
    if (v.type != Value::Type::STRING) oc_fail("uppercase requires a string", line);
    string s = v.str; for (auto& c : s) c = (char)toupper(c);
    return Value::Str(s);
}
static Value _lowercase(Value v, int line) {
    if (v.type != Value::Type::STRING) oc_fail("lowercase requires a string", line);
    string s = v.str; for (auto& c : s) c = (char)tolower(c);
    return Value::Str(s);
}
static string _trim_h(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static Value _trim(Value v, int line) {
    if (v.type != Value::Type::STRING) oc_fail("trim requires a string", line);
    return Value::Str(_trim_h(v.str));
}
static Value _split(Value s, Value sep, int line) {
    if (s.type != Value::Type::STRING || sep.type != Value::Type::STRING) oc_fail("split requires strings", line);
    auto vec = make_shared<vector<Value>>();
    string str = s.str, d = sep.str;
    if (d.empty()) { for (auto c : str) vec->push_back(Value::Str(string(1, c))); }
    else { size_t p = 0, f; while ((f = str.find(d, p)) != string::npos) { vec->push_back(Value::Str(str.substr(p, f-p))); p = f + d.size(); } vec->push_back(Value::Str(str.substr(p))); }
    return Value::List(vec);
}
static Value _join(Value lst, Value sep, int line) {
    if (lst.type != Value::Type::LIST || sep.type != Value::Type::STRING) oc_fail("join requires a list and separator string", line);
    string r;
    for (size_t i = 0; i < lst.list->size(); i++) { if (i) r += sep.str; r += valueToString((*lst.list)[i]); }
    return Value::Str(r);
}
static Value _contains(Value c, Value item, int line) {
    if (c.type == Value::Type::STRING && item.type == Value::Type::STRING)
        return Value::Bool(c.str.find(item.str) != string::npos);
    if (c.type == Value::Type::LIST) {
        for (auto& v : *c.list) if (valuesEqual(v, item)) return Value::Bool(true);
        return Value::Bool(false);
    }
    if (c.type == Value::Type::DICT) {
        if (item.type != Value::Type::STRING) oc_fail("contains on a dict requires a string key", line);
        return Value::Bool(c.dict->has(item.str));
    }
    oc_fail("contains requires a list, string, or dict", line);
}
static Value _replace(Value sv, Value ov, Value nv, int line) {
    if (sv.type != Value::Type::STRING || ov.type != Value::Type::STRING || nv.type != Value::Type::STRING) oc_fail("replace requires strings", line);
    string s = sv.str, old_ = ov.str, new_ = nv.str, r;
    size_t p = 0, f;
    while ((f = s.find(old_, p)) != string::npos) { r += s.substr(p, f-p) + new_; p = f + old_.size(); }
    r += s.substr(p);
    return Value::Str(r);
}
static Value _index_of(Value item, Value c, int line) {
    if (c.type == Value::Type::STRING && item.type == Value::Type::STRING) {
        size_t f = c.str.find(item.str);
        return Value::Number(f == string::npos ? -1 : (double)f);
    }
    if (c.type == Value::Type::LIST) {
        for (size_t i = 0; i < c.list->size(); i++)
            if (valuesEqual((*c.list)[i], item)) return Value::Number((double)i);
        return Value::Number(-1);
    }
    oc_fail("index of requires a list or string", line);
}
static Value _slice(Value lst, Value s, Value e, int line) {
    long long start = (long long)valueToNumber(s, line);
    long long end_  = (long long)valueToNumber(e, line);
    if (lst.type == Value::Type::STRING) {
        long long sz = (long long)lst.str.size();
        start = max(0LL, min(start, sz)); end_ = max(0LL, min(end_, sz));
        return Value::Str(start <= end_ ? lst.str.substr(start, end_ - start) : "");
    }
    if (lst.type == Value::Type::LIST) {
        long long sz = (long long)lst.list->size();
        start = max(0LL, min(start, sz)); end_ = max(0LL, min(end_, sz));
        auto vec = make_shared<vector<Value>>();
        for (long long i = start; i < end_; i++) vec->push_back((*lst.list)[i]);
        return Value::List(vec);
    }
    oc_fail("slice requires a list or string", line);
}
static Value _sort(Value lst, int line) {
    if (lst.type != Value::Type::LIST) oc_fail("sort requires a list", line);
    auto vec = make_shared<vector<Value>>(*lst.list);
    sort(vec->begin(), vec->end(), [&](const Value& a, const Value& b){
        if (a.type == Value::Type::NUMBER && b.type == Value::Type::NUMBER) return a.num < b.num;
        return valueToString(a) < valueToString(b);
    });
    return Value::List(vec);
}
static Value _reverse(Value v, int line) {
    if (v.type == Value::Type::STRING) { string s = v.str; reverse(s.begin(), s.end()); return Value::Str(s); }
    if (v.type == Value::Type::LIST) {
        auto vec = make_shared<vector<Value>>(*v.list);
        reverse(vec->begin(), vec->end());
        return Value::List(vec);
    }
    oc_fail("reverse requires a list or string", line);
}
static Value _abs(Value v, int line)    { return Value::Number(fabs(valueToNumber(v, line))); }
static Value _round(Value v, int line)  { return Value::Number(::round(valueToNumber(v, line))); }
static Value _floor(Value v, int line)  { return Value::Number(::floor(valueToNumber(v, line))); }
static Value _ceil(Value v, int line)   { return Value::Number(::ceil(valueToNumber(v, line))); }
static Value _sqrt(Value v, int line)   { double x = valueToNumber(v, line); if (x < 0) oc_fail("sqrt of negative", line); return Value::Number(::sqrt(x)); }
static Value _power(Value a, Value b, int line) { return Value::Number(pow(valueToNumber(a, line), valueToNumber(b, line))); }
static Value _min2(Value a, Value b, int line) { return Value::Number(min(valueToNumber(a, line), valueToNumber(b, line))); }
static Value _max2(Value a, Value b, int line) { return Value::Number(max(valueToNumber(a, line), valueToNumber(b, line))); }
static Value _min1(Value lst, int line) {
    if (lst.type != Value::Type::LIST || lst.list->empty()) oc_fail("min requires a non-empty list", line);
    double m = valueToNumber((*lst.list)[0], line);
    for (auto& v : *lst.list) m = min(m, valueToNumber(v, line));
    return Value::Number(m);
}
static Value _max1(Value lst, int line) {
    if (lst.type != Value::Type::LIST || lst.list->empty()) oc_fail("max requires a non-empty list", line);
    double m = valueToNumber((*lst.list)[0], line);
    for (auto& v : *lst.list) m = max(m, valueToNumber(v, line));
    return Value::Number(m);
}
static Value _random0(int) { return Value::Number(xorshift_double(0.0, 1.0)); }
static Value _random2(Value a, Value b, int line) {
    double lo = valueToNumber(a, line), hi = valueToNumber(b, line);
    return Value::Number(xorshift_double(lo, hi));
}
static Value _to_number(Value v, int line) {
    if (v.type == Value::Type::NUMBER) return v;
    if (v.type == Value::Type::BOOL)   return Value::Number(v.bl ? 1 : 0);
    if (v.type == Value::Type::STRING) { try { return Value::Number(stod(v.str)); } catch (...) { oc_fail("cannot convert to number: \"" + v.str + "\"", line); } }
    oc_fail("cannot convert list to number", line);
}
static Value _to_string(Value v, int) { return Value::Str(valueToString(v)); }
static Value _to_bool(Value v, int) { return Value::Bool(valueToBool(v)); }
static Value _chr(Value v, int line) {
    long long cp = (long long)valueToNumber(v, line);
    if (cp < 0) oc_fail("chr requires a non-negative codepoint", line);
    string s;
    if (cp < 0x80) { s += (char)cp; }
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x110000) { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else oc_fail("chr codepoint out of range (max 0x10FFFF)", line);
    return Value::Str(s);
}
static Value _ord(Value v, int line) {
    if (v.type != Value::Type::STRING) oc_fail("ord requires a string", line);
    if (v.str.empty()) oc_fail("ord of empty string is undefined", line);
    unsigned char c0 = (unsigned char)v.str[0];
    long long cp;
    if (c0 < 0x80) cp = c0;
    else if ((c0 & 0xE0) == 0xC0 && v.str.size() >= 2) cp = ((long long)(c0 & 0x1F) << 6) | ((long long)(unsigned char)v.str[1] & 0x3F);
    else if ((c0 & 0xF0) == 0xE0 && v.str.size() >= 3) cp = ((long long)(c0 & 0x0F) << 12) | (((long long)(unsigned char)v.str[1] & 0x3F) << 6) | ((long long)(unsigned char)v.str[2] & 0x3F);
    else if ((c0 & 0xF8) == 0xF0 && v.str.size() >= 4) cp = ((long long)(c0 & 0x07) << 18) | (((long long)(unsigned char)v.str[1] & 0x3F) << 12) | (((long long)(unsigned char)v.str[2] & 0x3F) << 6) | ((long long)(unsigned char)v.str[3] & 0x3F);
    else cp = c0;
    return Value::Number((double)cp);
}
static Value _is_number(Value v, int) { return Value::Bool(v.type == Value::Type::NUMBER); }
static Value _is_string(Value v, int) { return Value::Bool(v.type == Value::Type::STRING); }
static Value _is_bool(Value v, int)   { return Value::Bool(v.type == Value::Type::BOOL); }
static Value _is_list(Value v, int)   { return Value::Bool(v.type == Value::Type::LIST); }
static Value _keys(Value d, int line) {
    if (d.type != Value::Type::DICT) oc_fail("keys of requires a dict", line);
    auto vec = make_shared<vector<Value>>();
    vec->reserve(d.dict->entries.size());
    for (auto& kv : d.dict->entries) vec->push_back(Value::Str(kv.first));
    return Value::List(vec);
}
static Value _has(Value k, Value d, int line) {
    if (k.type != Value::Type::STRING) oc_fail("has requires a string key", line);
    if (d.type != Value::Type::DICT)   oc_fail("has requires a dict as the second argument", line);
    return Value::Bool(d.dict->has(k.str));
}
static Value _range(Value start, Value end_, Value step, bool has_step, int line) {
    double s = valueToNumber(start, line);
    double e = valueToNumber(end_, line);
    double st = has_step ? valueToNumber(step, line) : 1.0;
    if (st == 0) oc_fail("range step cannot be zero", line);
    auto vec = make_shared<vector<Value>>();
    if (st > 0) for (double v = s; v <= e + 1e-9; v += st) vec->push_back(Value::Number(v));
    else        for (double v = s; v >= e - 1e-9; v += st) vec->push_back(Value::Number(v));
    return Value::List(vec);
}
static Value _now(int) {
    auto dur = chrono::system_clock::now().time_since_epoch();
    return Value::Number(chrono::duration<double>(dur).count());
}
static Value _today(int) {
    time_t t = time(nullptr); struct tm* lt = localtime(&t);
    char buf[16]; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday);
    return Value::Str(buf);
}
static Value _clock(int) {
    time_t t = time(nullptr); struct tm* lt = localtime(&t);
    char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    return Value::Str(buf);
}
static Value _sleep(Value v, int line) {
    double s = valueToNumber(v, line);
    if (s < 0) oc_fail("sleep requires a non-negative number of seconds", line);
    this_thread::sleep_for(chrono::duration<double>(s));
    return Value::Number(0);
}

static size_t jsonSkipWs(const string& s, size_t i) { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; return i; }
static Value jsonParseValue(const string& s, size_t& i, int line);
static Value jsonParseString(const string& s, size_t& i, int line) {
    i++;
    string result;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i+1 < s.size()) {
            char esc = s[i+1];
            switch (esc) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'u': {
                    if (i+5 >= s.size()) oc_fail("invalid \\u escape in JSON", line);
                    long cp = strtol(s.substr(i+2, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) result += (char)cp;
                    else if (cp < 0x800) { result += (char)(0xC0 | (cp >> 6)); result += (char)(0x80 | (cp & 0x3F)); }
                    else { result += (char)(0xE0 | (cp >> 12)); result += (char)(0x80 | ((cp >> 6) & 0x3F)); result += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: result += esc;
            }
            i += 2;
        } else result += s[i++];
    }
    if (i >= s.size()) oc_fail("unterminated string in JSON", line);
    i++;
    return Value::Str(result);
}
static Value jsonParseValue(const string& s, size_t& i, int line) {
    i = jsonSkipWs(s, i);
    if (i >= s.size()) oc_fail("unexpected end of JSON", line);
    char c = s[i];
    if (c == '"') return jsonParseString(s, i, line);
    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = i;
        if (s[i] == '-') i++;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i]=='.' || s[i]=='e' || s[i]=='E' || s[i]=='+' || s[i]=='-')) i++;
        try { return Value::Number(stod(s.substr(start, i-start))); }
        catch (...) { oc_fail("invalid number in JSON: " + s.substr(start, i-start), line); }
    }
    if (c == 't' && s.substr(i,4)=="true")  { i += 4; return Value::Bool(true); }
    if (c == 'f' && s.substr(i,5)=="false") { i += 5; return Value::Bool(false); }
    if (c == 'n' && s.substr(i,4)=="null")  { i += 4; return Value::Number(0); }
    if (c == '[') {
        i++;
        auto vec = make_shared<vector<Value>>();
        i = jsonSkipWs(s, i);
        if (i < s.size() && s[i] == ']') { i++; return Value::List(vec); }
        while (true) {
            vec->push_back(jsonParseValue(s, i, line));
            i = jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; break; }
            oc_fail("expected ',' or ']' in JSON array", line);
        }
        return Value::List(vec);
    }
    if (c == '{') {
        i++;
        auto d = make_shared<DictData>();
        i = jsonSkipWs(s, i);
        if (i < s.size() && s[i] == '}') { i++; return Value::Dict(d); }
        while (true) {
            i = jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != '"') oc_fail("expected string key in JSON object", line);
            Value kv = jsonParseString(s, i, line);
            i = jsonSkipWs(s, i);
            if (i >= s.size() || s[i] != ':') oc_fail("expected ':' after key in JSON object", line);
            i++;
            Value vv = jsonParseValue(s, i, line);
            d->set(kv.str, vv);
            i = jsonSkipWs(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; break; }
            oc_fail("expected ',' or '}' in JSON object", line);
        }
        return Value::Dict(d);
    }
    oc_fail(string("unexpected character '") + c + "' in JSON", line);
}
static string jsonSerialize(const Value& v, int line) {
    switch (v.type) {
        case Value::Type::NUMBER: {
            if (v.num == (long long)v.num) return to_string((long long)v.num);
            ostringstream o; o << v.num; return o.str();
        }
        case Value::Type::STRING: {
            string out = "\"";
            for (char c : v.str) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    case '\t': out += "\\t";  break;
                    case '\r': out += "\\r";  break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    default:
                        if ((unsigned char)c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c); out += buf; }
                        else out += c;
                }
            }
            return out + "\"";
        }
        case Value::Type::BOOL: return v.bl ? "true" : "false";
        case Value::Type::LIST: {
            string out = "[";
            for (size_t i = 0; i < v.list->size(); i++) { if (i) out += ","; out += jsonSerialize((*v.list)[i], line); }
            return out + "]";
        }
        case Value::Type::DICT: {
            string out = "{";
            for (size_t i = 0; i < v.dict->entries.size(); i++) {
                if (i) out += ",";
                out += jsonSerialize(Value::Str(v.dict->entries[i].first), line);
                out += ":";
                out += jsonSerialize(v.dict->entries[i].second, line);
            }
            return out + "}";
        }
    }
    return "";
}
static Value _parse_json(Value sv, int line) {
    if (sv.type != Value::Type::STRING) oc_fail("parse_json requires a string", line);
    size_t i = 0;
    Value v = jsonParseValue(sv.str, i, line);
    i = jsonSkipWs(sv.str, i);
    if (i < sv.str.size()) oc_fail("trailing characters in JSON", line);
    return v;
}
static Value _to_json(Value v, int line) { return Value::Str(jsonSerialize(v, line)); }

static vector<string> g_args;
static Value g_args_value;
)OCRT";

struct Compiler {
    string out;
    int indent = 1;
    int tmpCounter = 0;
    set<string> declaredVars;

    string freshTemp() { return "__t" + to_string(tmpCounter++); }

    void emit(const string& s) { out += s; out += "\n"; }
    void emitIndent(const string& s) {
        for (int i = 0; i < indent; i++) out += "\t";
        out += s; out += "\n";
    }

    string cxxEscape(const string& s) {
        string r;
        for (char c : s) {
            if (c == '"') { r += "\\\""; }
            else if (c == '\\') { r += "\\\\"; }
            else if (c == '\n') { r += "\\n"; }
            else if (c == '\t') { r += "\\t"; }
            else if (c == '\r') { r += "\\r"; }
            else { r += c; }
        }
        return r;
    }

    string cxxName(const string& name) {
        return "u_" + name;
    }

    string numLiteral(double n) {
        ostringstream oss;
        if (n == (long long)n && fabs(n) < 9.2e18) oss << "Value::Number(" << (long long)n << ".0)";
        else { oss << setprecision(17) << "Value::Number(" << n << ")"; }
        return oss.str();
    }

    string emitExpr(ExprPtr& e) {
        switch (e->kind) {
            case ExprKind::NUMBER: return numLiteral(e->num);
            case ExprKind::STRING: {
                string s = "Value::Str(\"";
                s += cxxEscape(e->str);
                s += "\")";
                return s;
            }
            case ExprKind::BOOL: return e->bl ? "Value::Bool(true)" : "Value::Bool(false)";
            case ExprKind::LIST: {
                string s = "([&]() { auto __v = make_shared<vector<Value>>(); ";
                for (auto& it : e->items) { s += "__v->push_back("; s += emitExpr(it); s += "); "; }
                s += "return Value::List(__v); })()";
                return s;
            }
            case ExprKind::DICT: {
                string s = "([&]() { auto __d = make_shared<DictData>(); ";
                for (auto& kv : e->pairs) {
                    string k = (kv.first->kind == ExprKind::STRING) ? kv.first->str : "";
                    s += "__d->set(\"";
                    s += cxxEscape(k);
                    s += "\", ";
                    s += emitExpr(kv.second);
                    s += "); ";
                }
                s += "return Value::Dict(__d); })()";
                return s;
            }
            case ExprKind::VAR: {
                if (e->name == "args") return "g_args_value";
                return cxxName(e->name);
            }
            case ExprKind::UNARY: {
                string r = emitExpr(e->right);
                string ls = to_string(e->line);
                if (e->op == "-") return "Value::Number(-valueToNumber(" + r + ", " + ls + "))";
                if (e->op == "not") return "Value::Bool(!valueToBool(" + r + "))";
                return "Value::Number(0)";
            }
            case ExprKind::BINOP: {
                string ls = to_string(e->line);
                if (e->op == "and") {
                    return "Value::Bool(valueToBool(" + emitExpr(e->left) + ") && valueToBool(" + emitExpr(e->right) + "))";
                }
                if (e->op == "or") {
                    return "Value::Bool(valueToBool(" + emitExpr(e->left) + ") || valueToBool(" + emitExpr(e->right) + "))";
                }
                string l = emitExpr(e->left);
                string r = emitExpr(e->right);
                if (e->op == "+") return "addValues(" + l + ", " + r + ", " + ls + ")";
                if (e->op == "-") return "Value::Number(valueToNumber(" + l + ", " + ls + ") - valueToNumber(" + r + ", " + ls + "))";
                if (e->op == "*") return "Value::Number(valueToNumber(" + l + ", " + ls + ") * valueToNumber(" + r + ", " + ls + "))";
                if (e->op == "/") {
                    return "([&]() { double __r = valueToNumber(" + r + ", " + ls + "); if (__r == 0) oc_fail(\"division by zero\", " + ls + "); return Value::Number(valueToNumber(" + l + ", " + ls + ") / __r); })()";
                }
                if (e->op == "%") {
                    return "([&]() { double __r = valueToNumber(" + r + ", " + ls + "); if (__r == 0) oc_fail(\"modulo by zero\", " + ls + "); return Value::Number(fmod(valueToNumber(" + l + ", " + ls + "), __r)); })()";
                }
                if (e->op == "==") return "Value::Bool(valuesEqual(" + l + ", " + r + "))";
                if (e->op == "!=") return "Value::Bool(!valuesEqual(" + l + ", " + r + "))";
                if (e->op == "<" || e->op == ">" || e->op == "<=" || e->op == ">=") {
                    return "Value::Bool(valueToNumber(" + l + ", " + ls + ") " + e->op + " valueToNumber(" + r + ", " + ls + "))";
                }
                return "Value::Number(0)";
            }
            case ExprKind::INDEX: {
                string l = emitExpr(e->left);
                string r = emitExpr(e->right);
                return "indexValue(" + l + ", " + r + ", " + to_string(e->line) + ")";
            }
            case ExprKind::LENGTH: {
                string r = emitExpr(e->right);
                return "Value::Number(lengthOf(" + r + ", " + to_string(e->line) + "))";
            }
            case ExprKind::CALL: {
                string s = cxxName(e->name) + "(";
                for (size_t i = 0; i < e->items.size(); i++) {
                    if (i) s += ", ";
                    s += emitExpr(e->items[i]);
                }
                if (!e->items.empty()) s += ", ";
                s += to_string(e->line) + ")";
                return s;
            }
            case ExprKind::BUILTIN: return emitBuiltin(e);
        }
        return "Value::Number(0)";
    }

    string emitBuiltin(ExprPtr& e) {
        auto& name = e->name;
        string ls = to_string(e->line);
        auto arg = [&](int i) { return emitExpr(e->items[i]); };

        if (name == "now")    return "_now(" + ls + ")";
        if (name == "today")  return "_today(" + ls + ")";
        if (name == "clock")  return "_clock(" + ls + ")";
        if (name == "random0") return "_random0(" + ls + ")";

        if (name == "ask")        return "_ask(" + arg(0) + ", " + ls + ")";
        if (name == "uppercase")  return "_uppercase(" + arg(0) + ", " + ls + ")";
        if (name == "lowercase")  return "_lowercase(" + arg(0) + ", " + ls + ")";
        if (name == "trim")       return "_trim(" + arg(0) + ", " + ls + ")";
        if (name == "abs")        return "_abs(" + arg(0) + ", " + ls + ")";
        if (name == "round")      return "_round(" + arg(0) + ", " + ls + ")";
        if (name == "floor")      return "_floor(" + arg(0) + ", " + ls + ")";
        if (name == "ceil")       return "_ceil(" + arg(0) + ", " + ls + ")";
        if (name == "sqrt")       return "_sqrt(" + arg(0) + ", " + ls + ")";
        if (name == "to_number")  return "_to_number(" + arg(0) + ", " + ls + ")";
        if (name == "to_string")  return "_to_string(" + arg(0) + ", " + ls + ")";
        if (name == "to_bool")    return "_to_bool(" + arg(0) + ", " + ls + ")";
        if (name == "chr")        return "_chr(" + arg(0) + ", " + ls + ")";
        if (name == "ord")        return "_ord(" + arg(0) + ", " + ls + ")";
        if (name == "is_number")  return "_is_number(" + arg(0) + ", " + ls + ")";
        if (name == "is_string")  return "_is_string(" + arg(0) + ", " + ls + ")";
        if (name == "is_bool")    return "_is_bool(" + arg(0) + ", " + ls + ")";
        if (name == "is_list")    return "_is_list(" + arg(0) + ", " + ls + ")";
        if (name == "sort")       return "_sort(" + arg(0) + ", " + ls + ")";
        if (name == "reverse")    return "_reverse(" + arg(0) + ", " + ls + ")";
        if (name == "keys")       return "_keys(" + arg(0) + ", " + ls + ")";
        if (name == "parse_json") return "_parse_json(" + arg(0) + ", " + ls + ")";
        if (name == "to_json")    return "_to_json(" + arg(0) + ", " + ls + ")";
        if (name == "sleep")      return "_sleep(" + arg(0) + ", " + ls + ")";
        if (name == "min1")       return "_min1(" + arg(0) + ", " + ls + ")";
        if (name == "max1")       return "_max1(" + arg(0) + ", " + ls + ")";

        if (name == "random2")   return "_random2(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "power")     return "_power(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "min2")      return "_min2(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "max2")      return "_max2(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "split")     return "_split(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "join")      return "_join(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "contains")  return "_contains(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "index_of")  return "_index_of(" + arg(0) + ", " + arg(1) + ", " + ls + ")";
        if (name == "has")       return "_has(" + arg(0) + ", " + arg(1) + ", " + ls + ")";

        if (name == "replace")   return "_replace(" + arg(0) + ", " + arg(1) + ", " + arg(2) + ", " + ls + ")";
        if (name == "slice")     return "_slice(" + arg(0) + ", " + arg(1) + ", " + arg(2) + ", " + ls + ")";

        if (name == "range") {
            if (e->items.size() == 2) return "_range(" + arg(0) + ", " + arg(1) + ", Value::Number(0), false, " + ls + ")";
            if (e->items.size() == 3) return "_range(" + arg(0) + ", " + arg(1) + ", " + arg(2) + ", true, " + ls + ")";
        }

        if (name == "http_get" || name == "http_post" || name == "http_request" || name == "fetch")
            return "Value::Str(\"[compile mode: HTTP not supported - run in interpreter mode]\")";
        if (name == "socket" || name == "send" || name == "receive" || name == "readline" || name == "close" ||
            name == "ssl_socket" || name == "ssl_send" || name == "ssl_receive" || name == "ssl_readline" || name == "ssl_close" ||
            name == "ws_connect" || name == "ws_send" || name == "ws_send_binary" || name == "ws_recv" || name == "ws_close")
            return "Value::Number(0)";
        if (name == "sslsay" || name == "wssay")
            return "Value::Str(\"\")";
        // v15+ extras: return safe defaults for compile mode
        if (name == "ws_ping" || name == "bnot" || name == "db_open" || name == "db_close" ||
            name == "db_exec" || name == "db_begin" || name == "db_commit" || name == "db_rollback" ||
            name == "db_finalize" || name == "db_step" || name == "atomic_write" ||
            name == "lock_file" || name == "unlock_file" || name == "unshift" ||
            name == "insert_at" || name == "extend" || name == "random_shuffle" ||
            name == "random_seed" || name == "thread_start" || name == "channel_send" ||
            name == "mutex_lock" || name == "mutex_unlock" || name == "set_var" ||
            name == "raise" || name == "save_var" || name == "clear_saved_var" ||
            name == "chdir" || name == "chmod" || name == "copy_file" || name == "move_file" ||
            name == "write_binary" || name == "http_set_headers" || name == "http_set_timeout" ||
            name == "http_download" || name == "http_upload" || name == "set_socket_timeout" ||
            name == "timer_start" || name == "exit" || name == "db_exec_params" ||
            name == "band" || name == "bor" || name == "bxor" || name == "bshl" || name == "bshr" ||
            name == "bigint_add" || name == "bigint_sub" || name == "bigint_mul" ||
            name == "bigint_band" || name == "bigint_bor" || name == "bigint_bxor" ||
            name == "bigint_shl" || name == "bigint_shr" || name == "bigint_div" ||
            name == "bigint_mod" || name == "bigint_pow")
            return "Value::Number(0)";
        if (name == "bigint" || name == "bigint_to_hex" || name == "bigint_from_hex" ||
            name == "time_iso_after" || name == "time_iso_at" || name == "date_to_iso" ||
            name == "date_format" || name == "encode_base64" || name == "decode_base64" ||
            name == "hash_sha256" || name == "hash_sha1" || name == "hash_sha512" ||
            name == "hash_md5" || name == "hmac_sha256" || name == "url_encode" ||
            name == "url_decode" || name == "type_of" || name == "platform" ||
            name == "hostname" || name == "cwd" || name == "error_message" ||
            name == "to_json_pretty")
            return "Value::Str(\"\")";
        if (name == "starts_with" || name == "ends_with" || name == "list_contains" ||
            name == "is_dict" || name == "is_function" || name == "callable" ||
            name == "is_empty" || name == "is_file" || name == "is_dir" ||
            name == "save_var" || name == "clear_saved_var")
            return "Value::Bool(false)";
        if (name == "date_now" || name == "date_utc_now" || name == "timezone_offset" ||
            name == "timer_stop" || name == "date_from_iso" || name == "date_parse" ||
            name == "date_add" || name == "date_diff" || name == "now" || name == "pid" ||
            name == "memory_used" || name == "uptime" || name == "source_line" ||
            name == "error_code" || name == "db_last_insert_id" || name == "db_changes" ||
            name == "db_prepare" || name == "bigint_cmp" || name == "sign" || name == "gcd" ||
            name == "lcm" || name == "trunc" || name == "clamp" || name == "popcount" ||
            name == "bit_length" || name == "sum_of" || name == "product_of" ||
            name == "min_of" || name == "max_of" || name == "count_of" || name == "count" ||
            name == "random_int" || name == "thread_wait" || name == "channel_recv" ||
            name == "channel_new" || name == "mutex_new" || name == "lock_file" ||
            name == "unlock_file" || name == "accept" || name == "udp_socket" ||
            name == "server_socket" || name == "resolve_host" || name == "temp_file" ||
            name == "temp_dir" || name == "http_set_timeout")
            return "Value::Number(0)";
        if (name == "shift" || name == "remove_at" || name == "remove_key" ||
            name == "get_var" || name == "load_var" || name == "reduce" ||
            name == "find" || name == "random_choice")
            return "Value::Number(0)";
        if (name == "map" || name == "filter" || name == "unique" || name == "flatten" ||
            name == "chunk" || name == "zip" || name == "enumerate" || name == "items" ||
            name == "values" || name == "glob" || name == "saved_vars" ||
            name == "resolve_host" || name == "read_binary" || name == "stat" ||
            name == "shell" || name == "env" || name == "db_query" || name == "db_query_params" ||
            name == "sort_by" || name == "sort_desc")
            return "([&]() { auto __v = make_shared<vector<Value>>(); return Value::List(__v); })()";
        if (name == "merge" || name == "copy" || name == "date_parts")
            return "([&]() { auto __d = make_shared<DictData>(); return Value::Dict(__d); })()";
        if (name == "read" || name == "readlines")  return "Value::Str(\"\")";
        if (name == "write" || name == "append" || name == "mkdir" || name == "rm") return "Value::Number(0)";
        if (name == "ls")  return "([&]() { auto __v = make_shared<vector<Value>>(); return Value::List(__v); })()";
        if (name == "exists") return "Value::Bool(false)";

        return "Value::Number(0) /* unknown builtin '" + name + "' */";
    }

    void emitBlock(vector<StmtPtr>& body) {
        for (auto& s : body) emitStmt(s);
    }

    void declareIfNeeded(const string& ocName) {
        if (ocName == "args") return;
        if (declaredVars.count(ocName)) return;
        declaredVars.insert(ocName);
        string s = "Value ";
        s += cxxName(ocName);
        s += ";";
        emitIndent(s);
    }

    void emitStmt(StmtPtr& s) {
        switch (s->kind) {
            case StmtKind::LET: {
                declareIfNeeded(s->name);

                if (s->expr && s->expr->kind == ExprKind::BINOP && s->expr->op == "+") {

                    vector<ExprPtr> chain;
                    ExprPtr cur = s->expr;
                    while (cur && cur->kind == ExprKind::BINOP && cur->op == "+") {
                        chain.push_back(cur->right);
                        cur = cur->left;
                    }

                    reverse(chain.begin(), chain.end());
                    if (cur && cur->kind == ExprKind::VAR && cur->name == s->name &&
                        !chain.empty()) {
                        bool emitted = false;
                        for (auto& addend : chain) {
                            string rhs = emitExpr(addend);
                            string line = "addInPlace(";
                            line += cxxName(s->name);
                            line += ", ";
                            line += rhs;
                            line += ", ";
                            line += to_string(s->line);
                            line += ");";
                            emitIndent(line);
                            emitted = true;
                        }
                        if (emitted) break;
                    }
                }
                {
                    string line = cxxName(s->name);
                    line += " = ";
                    line += emitExpr(s->expr);
                    line += ";";
                    emitIndent(line);
                }
                break;
            }
            case StmtKind::SAY: {
                string line = "cout << valueToString(";
                line += emitExpr(s->expr);
                line += ") << \"\\n\";";
                emitIndent(line);
                break;
            }
            case StmtKind::SAY_INLINE: {
                string line = "cout << valueToString(";
                line += emitExpr(s->expr);
                line += ");";
                emitIndent(line);
                break;
            }
            case StmtKind::IF: {
                string head = "if (valueToBool(";
                head += emitExpr(s->expr);
                head += ")) {";
                emitIndent(head);
                indent++;
                emitBlock(s->body);
                indent--;
                for (auto& ei : s->elseIfs) {
                    ExprPtr cond = ei.first;
                    vector<StmtPtr>& body = ei.second;
                    string h2 = "} else if (valueToBool(";
                    h2 += emitExpr(cond);
                    h2 += ")) {";
                    emitIndent(h2);
                    indent++;
                    emitBlock(body);
                    indent--;
                }
                if (!s->elseBody.empty()) {
                    emitIndent("} else {");
                    indent++;
                    emitBlock(s->elseBody);
                    indent--;
                }
                emitIndent("}");
                break;
            }
            case StmtKind::WHILE: {
                string condTmp = freshTemp();
                emitIndent("while (true) {");
                indent++;
                {
                    string line = "Value ";
                    line += condTmp;
                    line += " = ";
                    line += emitExpr(s->expr);
                    line += ";";
                    emitIndent(line);
                }
                {
                    string line = "if (!valueToBool(";
                    line += condTmp;
                    line += ")) break;";
                    emitIndent(line);
                }
                emitBlock(s->body);
                indent--;
                emitIndent("}");
                break;
            }
            case StmtKind::REPEAT: {
                string counter = freshTemp();
                string limit = freshTemp();
                {
                    string line = "double ";
                    line += limit;
                    line += " = valueToNumber(";
                    line += emitExpr(s->expr);
                    line += ", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                }
                {
                    string line = "for (long long ";
                    line += counter;
                    line += " = 0; ";
                    line += counter;
                    line += " < (long long)";
                    line += limit;
                    line += "; ";
                    line += counter;
                    line += "++) {";
                    emitIndent(line);
                }
                indent++;
                emitBlock(s->body);
                indent--;
                emitIndent("}");
                break;
            }
            case StmtKind::FOR_EACH: {
                string tmp = freshTemp();
                {
                    string line = "Value ";
                    line += tmp;
                    line += " = ";
                    line += emitExpr(s->expr);
                    line += ";";
                    emitIndent(line);
                }
                declareIfNeeded(s->iterVar);
                string iv = cxxName(s->iterVar);
                if (s->iterVar2.empty()) {
                    {
                        string line = "if (";
                        line += tmp;
                        line += ".type == Value::Type::LIST) {";
                        emitIndent(line);
                    }
                    indent++;
                    {
                        string line = "for (size_t __i = 0; __i < ";
                        line += tmp;
                        line += ".list->size(); __i++) {";
                        emitIndent(line);
                    }
                    indent++;
                    {
                        string line = iv;
                        line += " = (*";
                        line += tmp;
                        line += ".list)[__i];";
                        emitIndent(line);
                    }

                    set<string> saved = declaredVars;
                    emitBlock(s->body);
                    declaredVars = saved;
                    indent--;
                    emitIndent("}");
                    indent--;
                    {
                        string line = "} else if (";
                        line += tmp;
                        line += ".type == Value::Type::DICT) {";
                        emitIndent(line);
                    }
                    indent++;
                    {
                        string line = "for (size_t __i = 0; __i < ";
                        line += tmp;
                        line += ".dict->entries.size(); __i++) {";
                        emitIndent(line);
                    }
                    indent++;
                    {
                        string line = iv;
                        line += " = Value::Str(";
                        line += tmp;
                        line += ".dict->entries[__i].first);";
                        emitIndent(line);
                    }
                    saved = declaredVars;
                    emitBlock(s->body);
                    declaredVars = saved;
                    indent--;
                    emitIndent("}");
                    indent--;
                    {
                        string line = "} else { oc_fail(\"'for each' requires a list or dict\", ";
                        line += to_string(s->line);
                        line += "); }";
                        emitIndent(line);
                    }
                } else {
                    declareIfNeeded(s->iterVar2);
                    string iv2 = cxxName(s->iterVar2);
                    {
                        string line = "if (";
                        line += tmp;
                        line += ".type != Value::Type::DICT) oc_fail(\"two-variable 'for each' requires a dict\", ";
                        line += to_string(s->line);
                        line += ");";
                        emitIndent(line);
                    }
                    {
                        string line = "for (size_t __i = 0; __i < ";
                        line += tmp;
                        line += ".dict->entries.size(); __i++) {";
                        emitIndent(line);
                    }
                    indent++;
                    {
                        string line = iv;
                        line += " = Value::Str(";
                        line += tmp;
                        line += ".dict->entries[__i].first);";
                        emitIndent(line);
                    }
                    {
                        string line = iv2;
                        line += " = ";
                        line += tmp;
                        line += ".dict->entries[__i].second;";
                        emitIndent(line);
                    }
                    set<string> saved = declaredVars;
                    emitBlock(s->body);
                    declaredVars = saved;
                    indent--;
                    emitIndent("}");
                }
                break;
            }
            case StmtKind::FUNC_DEF: break;
            case StmtKind::RETURN: {
                if (s->expr) {
                    string line = "throw ReturnSignal{ ";
                    line += emitExpr(s->expr);
                    line += " };";
                    emitIndent(line);
                } else {
                    emitIndent("throw ReturnSignal{ Value::Number(0) };");
                }
                break;
            }
            case StmtKind::ADD_TO: {
                declareIfNeeded(s->name);
                {
                    string line = "if (";
                    line += cxxName(s->name);
                    line += ".type != Value::Type::LIST) oc_fail(\"'";
                    line += s->name;
                    line += "' is not a list\", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                }
                {
                    string line = cxxName(s->name);
                    line += ".list->push_back(";
                    line += emitExpr(s->expr);
                    line += ");";
                    emitIndent(line);
                }
                break;
            }
            case StmtKind::REMOVE_AT: {
                declareIfNeeded(s->name);
                {
                    string idx = freshTemp();
                    {
                        string line = "long long ";
                        line += idx;
                        line += " = (long long)valueToNumber(";
                        line += emitExpr(s->expr);
                        line += ", ";
                        line += to_string(s->line);
                        line += ");";
                        emitIndent(line);
                    }
                    {
                        string line = "if (";
                        line += idx;
                        line += " < 0 || ";
                        line += idx;
                        line += " >= (long long)";
                        line += cxxName(s->name);
                        line += ".list->size()) oc_fail(\"list index out of range\", ";
                        line += to_string(s->line);
                        line += ");";
                        emitIndent(line);
                    }
                    {
                        string line = cxxName(s->name);
                        line += ".list->erase(";
                        line += cxxName(s->name);
                        line += ".list->begin() + ";
                        line += idx;
                        line += ");";
                        emitIndent(line);
                    }
                }
                break;
            }
            case StmtKind::SET_AT: {
                if (s->expr->kind != ExprKind::INDEX) {
                    string line = "oc_fail(\"set_at target must be an index expression\", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                    break;
                }
                ExprPtr targetExpr = s->expr->left;
                ExprPtr keyExpr = s->expr->right;
                if (targetExpr->kind != ExprKind::VAR) {
                    string line = "oc_fail(\"can only mutate an indexed variable\", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                    break;
                }
                declareIfNeeded(targetExpr->name);
                string tgt = cxxName(targetExpr->name);
                string keyTmp = freshTemp();
                string valTmp = freshTemp();
                {
                    string line = "Value ";
                    line += keyTmp;
                    line += " = ";
                    line += emitExpr(keyExpr);
                    line += ";";
                    emitIndent(line);
                }
                {
                    string line = "Value ";
                    line += valTmp;
                    line += " = ";
                    line += emitExpr(s->value);
                    line += ";";
                    emitIndent(line);
                }
                {
                    string line = "if (";
                    line += tgt;
                    line += ".type == Value::Type::DICT) {";
                    emitIndent(line);
                }
                indent++;
                {
                    string line = "if (";
                    line += keyTmp;
                    line += ".type != Value::Type::STRING) oc_fail(\"dict key must be a string\", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                }
                {
                    string line = tgt;
                    line += ".dict->set(";
                    line += keyTmp;
                    line += ".str, ";
                    line += valTmp;
                    line += ");";
                    emitIndent(line);
                }
                indent--;
                {
                    string line = "} else if (";
                    line += tgt;
                    line += ".type == Value::Type::LIST) {";
                    emitIndent(line);
                }
                indent++;
                {
                    string line = "long long __idx = (long long)valueToNumber(";
                    line += keyTmp;
                    line += ", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                }
                {
                    string line = "if (__idx < 0 || __idx >= (long long)";
                    line += tgt;
                    line += ".list->size()) oc_fail(\"list index out of range\", ";
                    line += to_string(s->line);
                    line += ");";
                    emitIndent(line);
                }
                {
                    string line = "(*";
                    line += tgt;
                    line += ".list)[__idx] = ";
                    line += valTmp;
                    line += ";";
                    emitIndent(line);
                }
                indent--;
                {
                    string line = "} else { oc_fail(\"can only mutate a list or dict at an index\", ";
                    line += to_string(s->line);
                    line += "); }";
                    emitIndent(line);
                }
                break;
            }
            case StmtKind::EXPR_STMT: {
                string line = "(void)(";
                line += emitExpr(s->expr);
                line += ");";
                emitIndent(line);
                break;
            }
            case StmtKind::TRY: {
                emitIndent("try {");
                indent++;
                emitBlock(s->body);
                indent--;
                if (!s->elseBody.empty()) {
                    emitIndent("} catch (OCodeError& __e) {");
                    indent++;
                    if (!s->iterVar.empty()) {
                        declareIfNeeded(s->iterVar);
                        string line = cxxName(s->iterVar);
                        line += " = Value::Str(__e.what());";
                        emitIndent(line);
                    }
                    emitBlock(s->elseBody);
                    indent--;
                    emitIndent("}");
                } else {
                    emitIndent("} catch (OCodeError&) {");
                    emitIndent("}");
                }
                break;
            }
            case StmtKind::BREAK: emitIndent("break;"); break;
            case StmtKind::SKIP:  emitIndent("continue;"); break;
            case StmtKind::USE: {
                string line = "/* compile mode: 'use ";
                line += s->name;
                line += "' is not supported - run via interpreter */";
                emitIndent(line);
                break;
            }
            case StmtKind::PYTHON_BLOCK:
                emitIndent("/* compile mode: pyoc blocks are not supported - run via interpreter */");
                break;
        }
    }

    void emitFunction(StmtPtr& fn) {
        set<string> saved = declaredVars;
        declaredVars.clear();
        for (auto& p : fn->params) declaredVars.insert(p);

        string sig = "Value ";
        sig += cxxName(fn->name);
        sig += "(";
        for (size_t i = 0; i < fn->params.size(); i++) {
            if (i) sig += ", ";
            sig += "Value ";
            sig += cxxName(fn->params[i]);
        }
        if (!fn->params.empty()) sig += ", int";
        else sig += "int";
        sig += " __line_unused) {";
        emit(sig);
        indent++;

        emitIndent("try {");
        indent++;
        emitBlock(fn->body);
        indent--;
        emitIndent("} catch (ReturnSignal& __ret) { return __ret.value; }");
        emitIndent("return Value::Number(0);");
        indent--;
        emit("}");
        emit("");

        declaredVars = saved;
    }

    string compileProgram(vector<StmtPtr>& program) {
        out.clear();
        out += OC_RUNTIME_CPP;
        out += "\n";
        for (auto& s : program) {
            if (s->kind == StmtKind::FUNC_DEF) {
                string sig = "Value ";
                sig += cxxName(s->name);
                sig += "(";
                for (size_t i = 0; i < s->params.size(); i++) {
                    if (i) sig += ", ";
                    sig += "Value";
                }
                if (s->params.empty()) sig += "int";
                else sig += ", int";
                sig += ");";
                emit(sig);
            }
        }
        emit("");
        for (auto& s : program) {
            if (s->kind == StmtKind::FUNC_DEF) emitFunction(s);
        }
        emit("int main(int argc, char** argv) {");
        indent = 1;
        emitIndent("for (int i = 0; i < argc; i++) g_args.push_back(argv[i]);");
        emitIndent("auto __args_vec = make_shared<vector<Value>>();");
        emitIndent("if (argc > 0) __args_vec->push_back(Value::Str(string(argv[0])));");
        emitIndent("for (int i = 1; i < argc; i++) __args_vec->push_back(Value::Str(string(argv[i])));");
        emitIndent("g_args_value = Value::List(__args_vec);");
        declaredVars.insert("args");

        emitIndent("try {");
        indent++;
        for (auto& s : program) {
            if (s->kind != StmtKind::FUNC_DEF) emitStmt(s);
        }
        indent--;
        emitIndent("} catch (OCodeError& __e) {");
        indent++;
        emitIndent("cerr << \"OCode error (line \" << __e.line << \"): \" << __e.what() << \"\\n\";");
        emitIndent("return 1;");
        indent--;
        emitIndent("} catch (ReturnSignal&) {");
        indent++;
        emitIndent("cerr << \"OCode error: 'return' used outside of a function\\n\";");
        emitIndent("return 1;");
        indent--;
        emitIndent("}");
        emitIndent("return 0;");
        emit("}");
        return out;
    }
};

static string compileOCodeToCpp(const string& source) {
    try {
        Lexer lexer(source);
        auto toks = lexer.tokenize();
        Parser parser(toks);
        auto program = parser.parseProgram();
        Compiler c;
        return c.compileProgram(program);
    } catch (OCodeError& e) {
        cerr << "OCode compile error (line " << e.line << "): " << e.what() << "\n";
        return "";
    }
}

static bool writeFile(const string& path, const string& content) {
    ofstream f(path);
    if (!f) return false;
    f << content;
    return (bool)f;
}

static string findCppCompiler() {
    if (system("g++ --version >/dev/null 2>&1") == 0) return "g++";
    if (system("clang++ --version >/dev/null 2>&1") == 0) return "clang++";
    return "";
}

static int compileAndLink(const string& cppPath, const string& outBinaryPath, string& errOut) {
    string cc = findCppCompiler();
    if (cc.empty()) { errOut = "no C++ compiler found (tried g++, clang++)"; return 1; }
    string cmd = cc + " -O2 -std=c++17 -pthread " + cppPath + " -o " + outBinaryPath + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { errOut = "popen failed"; return 1; }
    string all;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) all += buf;
    int rc = pclose(pipe);
    if (rc != 0) errOut = all;
    return rc;
}

static string getCachePath(const string& ocFile) {

    string absPath;
#ifdef _WIN32
    char abs[_MAX_PATH];
    if (_fullpath(abs, ocFile.c_str(), sizeof(abs))) absPath = abs;
    else absPath = ocFile;
#else
    char abs[4096];
    if (realpath(ocFile.c_str(), abs)) absPath = abs;
    else absPath = ocFile;
#endif

    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : absPath) { h ^= c; h *= 1099511628211ULL; }
    stringstream hs; hs << hex << h;

    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("LOCALAPPDATA");
#endif
    if (!home) home = "/tmp";
    string cacheDir = string(home) + "/.ocache";
#ifdef _WIN32
    _mkdir(cacheDir.c_str());
#else
    mkdir(cacheDir.c_str(), 0755);
#endif
    return cacheDir + "/bin-" + hs.str();
}

static bool isNewer(const string& a, const string& b) {
    struct stat sa, sb;
    if (stat(a.c_str(), &sa) != 0) return false;
    if (stat(b.c_str(), &sb) != 0) return true;
#ifdef _WIN32
    return difftime(sa.st_mtime, sb.st_mtime) >= 0;
#else
    return sa.st_mtime >= sb.st_mtime;
#endif
}

static string buildRunCmd(const string& binPath, int argc, char** argv, int userArgStart) {
    string cmd = binPath;
    for (int i = userArgStart; i < argc; i++) {
        cmd += " '";
        for (char c : string(argv[i])) {
            if (c == '\'') cmd += "'\\''";
            else cmd += c;
        }
        cmd += "'";
    }
    return cmd;
}

static int runInterpMode(const string& ocFile, int argc, char** argv, int userArgStart) {
    ifstream in(ocFile);
    if (!in) { cerr << "OCode error: could not open '" << ocFile << "'\n"; return 1; }
    stringstream ss; ss << in.rdbuf(); string source = ss.str();
    Interpreter interp;
    {
        auto argsVec = make_shared<vector<Value>>();
        if (argc > 0) argsVec->push_back(Value::Str(string(argv[0])));
        for (int i = userArgStart; i < argc; i++) argsVec->push_back(Value::Str(string(argv[i])));
        interp.globals.vars["args"] = Value::List(argsVec);
    }
    try {
        Lexer lexer(source);
        auto toks = lexer.tokenize();
        Parser parser(toks);
        auto program = parser.parseProgram();
        interp.run(program);
        interp.pySession.stop();
    } catch (OCodeError& e) {
        socketCloseAll();
        interp.pySession.stop();
        cerr << "OCode error (line " << e.line << "): " << e.what() << "\n";
        return 1;
    } catch (ReturnSignal&) {

    }
    socketCloseAll();
    interp.pySession.stop();
    return 0;
}

static int smartRunMode(const string& ocFile, int argc, char** argv, int userArgStart, bool forceRebuild) {
    string binPath = getCachePath(ocFile);
    string cppPath = binPath + ".cpp";

    if (!forceRebuild && isNewer(binPath, ocFile)) {
        string runCmd = buildRunCmd(binPath, argc, argv, userArgStart);
        int rc = system(runCmd.c_str());
        if (rc == -1) return 1;
        return WEXITSTATUS(rc);
    }

    ifstream in(ocFile);
    if (!in) { cerr << "OCode error: could not open '" << ocFile << "'\n"; return 1; }
    stringstream ss; ss << in.rdbuf();
    string source = ss.str();

    string cpp = compileOCodeToCpp(source);
    if (cpp.empty()) return 1;

    if (!writeFile(cppPath, cpp)) {
        cerr << "OCode error: could not write '" << cppPath << "'\n";
        return 1;
    }

    string errOut;
    int rc = compileAndLink(cppPath, binPath, errOut);
    if (rc != 0) {
        cerr << "OCode compile failed:\n" << errOut << "\n";
        remove(cppPath.c_str());
        return rc;
    }

    remove(cppPath.c_str());

    string runCmd = buildRunCmd(binPath, argc, argv, userArgStart);
    int runRc = system(runCmd.c_str());
    if (runRc == -1) return 1;
    return WEXITSTATUS(runRc);
}

int main(int argc, char** argv) {

    string exeName = argv[0];
    size_t slashPos = exeName.find_last_of("/\\");
    if (slashPos != string::npos) exeName = exeName.substr(slashPos + 1);

    bool isOcd = (exeName == "ocd");

    if (argc >= 2 && (string(argv[1]) == "install" || string(argv[1]) == "list" ||
                      string(argv[1]) == "remove"  || string(argv[1]) == "uninstall" ||
                      string(argv[1]) == "help")) isOcd = true;

    if (isOcd) {
        string subcmd = (argc >= 2) ? argv[1] : "help";

        if (subcmd == "install") {
            if (argc < 3) {
                cerr << "Usage: ocd install <file.oc>   (OCode package)\n";
                cerr << "       ocd install <package>    (Python/PyPI package)\n";
                return 1;
            }
            string arg = argv[2];

            if (arg.size() >= 3 && arg.substr(arg.size() - 3) == ".oc") {
                return ocdInstall(arg);
            }

            ifstream test(arg);
            if (test.good()) {
                test.close();
                return ocdInstall(arg);
            }

            return ocdPipInstall(arg);
        }

        if (subcmd == "list") {

            cout << "OCode packages:\n";
            int r1 = ocdList();
            cout << "\nPython packages:\n";
            int r2 = ocdPipList();
            return (r1 || r2) ? 1 : 0;
        }

        if (subcmd == "remove") {
            if (argc < 3) { cerr << "Usage: ocd remove <name>\n"; return 1; }
            return ocdRemove(argv[2]);
        }

        if (subcmd == "uninstall") {
            if (argc < 3) { cerr << "Usage: ocd uninstall <package>\n"; return 1; }
            return ocdPipUninstall(argv[2]);
        }

        if (subcmd == "help") { printOcdHelp(); return 0; }
    }

    bool explicitCompile = false;
    bool explicitInterp  = false;
    bool explicitRebuild = false;
    int fileArgIdx = 1;

    if (argc >= 2) {
        string flag = argv[1];
        if (flag == "--compile" || flag == "-c") { explicitCompile = true; fileArgIdx = 2; }
        else if (flag == "--interp"  || flag == "-i") { explicitInterp  = true; fileArgIdx = 2; }
        else if (flag == "--rebuild")                { explicitRebuild = true; fileArgIdx = 2; }
        else if (flag == "--run"     || flag == "-r") { fileArgIdx = 2; }
    }

    if (argc < fileArgIdx + 1) {
        cerr <<
            "Usage:\n"
            "  ocode <file.oc> [args...]        Run .oc file (default: compile & run, cached)\n"
            "  ocode --interp <file.oc> [args]  Run in interpreter mode (HTTP/network/file IO)\n"
            "  ocode --compile <file.oc>        Transpile .oc → .cpp and stop (no run)\n"
            "  ocode --rebuild <file.oc> [args] Force recompile (ignore cache) and run\n"
            "\n"
            "By default, OCode transpiles your .oc file to C++ and compiles it with\n"
            "g++ -O2, then runs the resulting native binary. The compiled binary is\n"
            "cached in $HOME/.ocache/ — so the second run is instant (no recompile).\n"
            "\n"
            "Use --interp for programs that use HTTP (fetch, http_get, ...), raw\n"
            "sockets, file I/O (read/write/ls/...), or pyoc blocks — those need the\n"
            "runtime and aren't yet supported in compiled mode.\n";
        return 1;
    }

    string ocFile = argv[fileArgIdx];

    if (explicitInterp) {
        return runInterpMode(ocFile, argc, argv, fileArgIdx + 1);
    }

    if (explicitCompile) {
        ifstream in(ocFile);
        if (!in) { cerr << "OCode error: could not open '" << ocFile << "'\n"; return 1; }
        stringstream ss; ss << in.rdbuf();
        string source = ss.str();
        string cpp = compileOCodeToCpp(source);
        if (cpp.empty()) return 1;
        string cppPath = ocFile;
        if (cppPath.size() > 3 && cppPath.substr(cppPath.size() - 3) == ".oc")
            cppPath = cppPath.substr(0, cppPath.size() - 3) + ".cpp";
        else
            cppPath = cppPath + ".cpp";
        if (!writeFile(cppPath, cpp)) {
            cerr << "OCode error: could not write '" << cppPath << "'\n";
            return 1;
        }
        cout << "Wrote " << cppPath << " (" << cpp.size() << " bytes)\n";
        string binHint = cppPath.substr(0, cppPath.size()-4);
        cout << "Compile with:  g++ -O2 -std=c++17 -pthread " << cppPath << " -o " << binHint << "\n";
        return 0;
    }

    return smartRunMode(ocFile, argc, argv, fileArgIdx + 1, explicitRebuild);
}
