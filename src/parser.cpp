// ── src/parser.cpp ────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/parser.hpp>
#include <zconfig/expressions.hpp>
#include <zconfig/fs.hpp>
#include <zconfig/log.hpp>
#include <stack>
#include <set>
#include <format>

namespace zconfig {

namespace {

#define TRY(x) do { auto res = (x); if (!res) return std::unexpected(res.error()); } while(0)

static std::set<std::string> include_stack;

enum class TokenType {
    Identifier, String, Number, 
    Option, Menu, Computed, Include, When, Implies, Pattern, Range, Default, Label, Help, Tags, Danger, Collapsed, Choices, Choice, As, Validate,
    Macro, Generate, Title,
    TypeBool, TypeInt, TypeString, TypeEnum, TypeSet,
    LBrace, RBrace, LBracket, RBracket, LParen, RParen, Assign, Colon, Pipe, Dot, Comma,
    And, Or, Not, Eq, Neq, Lt, Gt, Lte, Gte, Question,
    Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr,
    Eof, Unknown
};

struct Token {
    TokenType type;
    std::string text;
    uint32_t line;
    uint32_t col;
    size_t start_pos;
};

class Lexer {
public:
    Lexer(std::string_view src) : source(src) {}

    Token next() {
        skip_whitespace();
        size_t token_start_offset = pos;
        if (pos >= source.size()) return {TokenType::Eof, "", line, col, token_start_offset};

        char c = source[pos];
        uint32_t start_col = col;

        if (c == '#') {
            while (pos < source.size() && source[pos] != '\n') {
                pos++; col++;
            }
            return next();
        }

        if (isalpha(c) || c == '_') {
            std::string text;
            while (pos < source.size()) {
                char ch = source[pos];
                // Allow hyphen inside identifiers only when immediately followed
                // by alphanumeric — e.g. mq-deadline, bitlocker-compat.
                // A hyphen followed by space/EOF is the SUB operator.
                if (isalnum(ch) || ch == '_' || ch == '.') {
                    text += ch; pos++; col++;
                } else if (ch == '-' && pos + 1 < source.size() && isalnum(source[pos + 1])) {
                    text += ch; pos++; col++;
                } else {
                    break;
                }
            }
            return {identify_keyword(text), text, line, start_col, token_start_offset};
        }

        if (isdigit(c)) {
            std::string text;
            if (c == '0' && pos + 1 < source.size() && (source[pos+1] == 'x' || source[pos+1] == 'X')) {
                text += source[pos++]; col++; // 0
                text += source[pos++]; col++; // x
                while (pos < source.size() && isxdigit(source[pos])) {
                    text += source[pos++]; col++;
                }
                return {TokenType::Number, text, line, start_col, token_start_offset};
            }
            while (pos < source.size() && (isalnum(source[pos]) || source[pos] == '_' || source[pos] == '.')) {
                text += source[pos++]; col++;
            }
            bool all_digits = true;
            for (char ch : text) if (!isdigit(ch)) all_digits = false;
            if (all_digits) return {TokenType::Number, text, line, start_col, token_start_offset};
            return {TokenType::Identifier, text, line, start_col, token_start_offset};
        }

        if (c == '"') {
            pos++; col++;
            std::string text;
            while (pos < source.size() && source[pos] != '"') {
                if (source[pos] == '\\' && pos + 1 < source.size()) {
                    pos++; col++; 
                    if (source[pos] == 'n') text += '\n';
                    else if (source[pos] == 't') text += '\t';
                    else if (source[pos] == 'r') text += '\r';
                    else if (source[pos] == '\\') text += '\\';
                    else if (source[pos] == '"') text += '"';
                    else text += source[pos];
                    pos++; col++;
                } else {
                    text += source[pos++]; col++;
                }
            }
            if (pos < source.size()) { pos++; col++; }
            return {TokenType::String, text, line, start_col, token_start_offset};
        }

        pos++; col++;
        switch (c) {
            case '(': return {TokenType::LParen, "(", line, start_col, token_start_offset};
            case ')': return {TokenType::RParen, ")", line, start_col, token_start_offset};
            case '{': return {TokenType::LBrace, "{", line, start_col, token_start_offset};
            case '}': return {TokenType::RBrace, "}", line, start_col, token_start_offset};
            case '[': return {TokenType::LBracket, "[", line, start_col, token_start_offset};
            case ']': return {TokenType::RBracket, "]", line, start_col, token_start_offset};
            case '=': 
                if (match('=')) return {TokenType::Eq, "==", line, start_col, token_start_offset};
                return {TokenType::Assign, "=", line, start_col, token_start_offset};
            case ':': return {TokenType::Colon, ":", line, start_col, token_start_offset};
            case '|': 
                if (match('|')) return {TokenType::Or, "||", line, start_col, token_start_offset};
                return {TokenType::BitOr, "|", line, start_col, token_start_offset};
            case '&':
                if (match('&')) return {TokenType::And, "&&", line, start_col, token_start_offset};
                return {TokenType::BitAnd, "&", line, start_col, token_start_offset};
            case '/': return {TokenType::Div, "/", line, start_col, token_start_offset};
            case '<':
                if (match('=')) return {TokenType::Lte, "<=", line, start_col, token_start_offset};
                if (match('<')) return {TokenType::Shl, "<<", line, start_col, token_start_offset};
                return {TokenType::Lt, "<", line, start_col, token_start_offset};
            case '>':
                if (match('=')) return {TokenType::Gte, ">=", line, start_col, token_start_offset};
                if (match('>')) return {TokenType::Shr, ">>", line, start_col, token_start_offset};
                return {TokenType::Gt, ">", line, start_col, token_start_offset};
            case '.': 
                if (match('.')) return {TokenType::Range, "..", line, start_col, token_start_offset};
                return {TokenType::Dot, ".", line, start_col, token_start_offset};
            case ',': return {TokenType::Comma, ",", line, start_col, token_start_offset};
            case '!': 
                if (match('=')) return {TokenType::Neq, "!=", line, start_col, token_start_offset};
                return {TokenType::Not, "!", line, start_col, token_start_offset};
            case '+': return {TokenType::Add, "+", line, start_col, token_start_offset};
            case '-': return {TokenType::Sub, "-", line, start_col, token_start_offset};
            case '*': return {TokenType::Mul, "*", line, start_col, token_start_offset};
            case '%': return {TokenType::Mod, "%", line, start_col, token_start_offset};
            case '?': return {TokenType::Question, "?", line, start_col, token_start_offset};
            case '^': return {TokenType::BitXor, "^", line, start_col, token_start_offset};
            case '@': return {TokenType::Unknown, "@", line, start_col, token_start_offset};
        }

        return {TokenType::Unknown, std::string(1, c), line, start_col, token_start_offset};
    }

    size_t get_pos() const { return pos; }
    std::string_view get_source() const { return source; }

private:
    std::string_view source;
    size_t pos = 0;
    uint32_t line = 1;
    uint32_t col = 1;

    void skip_whitespace() {
        while (pos < source.size()) {
            if (source[pos] == '\n') {
                line++; col = 1; pos++;
            } else if (isspace(source[pos])) {
                col++; pos++;
            } else {
                break;
            }
        }
    }

    bool match(char c) {
        if (pos < source.size() && source[pos] == c) {
            pos++; col++; return true;
        }
        return false;
    }

    TokenType identify_keyword(const std::string& s) {
        if (s == "option") return TokenType::Option;
        if (s == "menu") return TokenType::Menu;
        if (s == "computed") return TokenType::Computed;
        if (s == "include") return TokenType::Include;
        if (s == "when") return TokenType::When;
        if (s == "validate") return TokenType::Validate;
        if (s == "implies") return TokenType::Implies;
        if (s == "macro") return TokenType::Macro;
        if (s == "generate") return TokenType::Generate;
        if (s == "pattern") return TokenType::Pattern;
        if (s == "range") return TokenType::Range;
        if (s == "default") return TokenType::Default;
        if (s == "label") return TokenType::Label;
        if (s == "help") return TokenType::Help;
        if (s == "tags") return TokenType::Tags;
        if (s == "danger") return TokenType::Danger;
        if (s == "collapsed") return TokenType::Collapsed;
        if (s == "choices") return TokenType::Choices;
        if (s == "choice") return TokenType::Choice;
        if (s == "as") return TokenType::As;
        if (s == "title") return TokenType::Title;
        if (s == "bool") return TokenType::TypeBool;
        if (s == "int") return TokenType::TypeInt;
        if (s == "string") return TokenType::TypeString;
        if (s == "enum") return TokenType::TypeEnum;
        if (s == "set") return TokenType::TypeSet;
        if (s == "and") return TokenType::And;
        if (s == "or") return TokenType::Or;
        if (s == "not") return TokenType::Not;
        return TokenType::Identifier;
    }
};

class ParserImpl {
public:
    ParserImpl(std::string_view src, Registry& r, std::string fname, std::string pref = "", uint32_t d = 0) 
        : lexer(src), reg(r), filename(std::move(fname)), prefix(std::move(pref)), depth(d) {
        current = lexer.next();
    }

    Result<void> parse() {
        while (current.type != TokenType::Eof) {
            if (current.type == TokenType::Option) TRY(parse_option());
            else if (current.type == TokenType::Menu) TRY(parse_menu());
            else if (current.type == TokenType::Computed) TRY(parse_computed());
            else if (current.type == TokenType::Validate) TRY(parse_validation());
            else if (current.type == TokenType::Macro) TRY(parse_macro_definition());
            else if (current.type == TokenType::Generate) TRY(parse_generate());
            else if (current.type == TokenType::Include) TRY(parse_include());
            else if (current.type == TokenType::Title) TRY(parse_title());
            else if (current.type == TokenType::Identifier) {
                if (reg.find_macro(current.text)) TRY(parse_macro_invocation());
                else return err("Unexpected identifier: " + current.text);
            }
            else return err("Unexpected token at top level: " + current.text);
        }
        return {};
    }

private:
    Lexer lexer;
    Registry& reg;
    std::string filename;
    std::string prefix;
    uint32_t depth;
    Token current;
    std::stack<Menu*> menu_stack;

    void advance() { 
        current = lexer.next(); 
        if (zconfig::log::debug_enabled) {
            ZLOG_DEBUG("token parsed: '{}' ({}:{}:{})", current.text, filename, current.line, current.col);
        }
    }

    std::unexpected<Diagnostic> err(const std::string& msg) {
        return std::unexpected(Diagnostic::build(ErrorCode::ParseError, msg, filename, current.line, current.col));
    }

    Result<void> expect(TokenType t, const std::string& msg = "") {
        if (current.type == t) {
            advance();
            return {};
        }
        return err(msg.empty() ? "Unexpected token" : msg);
    }

    Result<std::string> parse_identifier() {
        if (current.type != TokenType::Identifier) return err("Expected identifier");
        std::string s = current.text;
        advance();
        return s;
    }

    Result<std::string> parse_string() {
        if (current.type != TokenType::String) return err("Expected string");
        std::string s = current.text;
        advance();
        return s;
    }

    Result<std::unique_ptr<Expression>> parse_expression() {
        return parse_ternary();
    }

    Result<std::unique_ptr<Expression>> parse_ternary() {
        auto cond = parse_implies();
        if (!cond) return cond;
        if (current.type != TokenType::Question) return cond;
        advance(); // consume '?'
        auto then_expr = parse_ternary();
        if (!then_expr) return then_expr;
        // expect ':' — but Colon is already used for type declarations;
        // in expression context after '?' it must be the ternary separator.
        TRY(expect(TokenType::Colon, "Expected ':' in ternary expression"));
        auto else_expr = parse_ternary();
        if (!else_expr) return else_expr;
        return std::make_unique<TernaryExpr>(std::move(*cond), std::move(*then_expr), std::move(*else_expr));
    }

    Result<std::unique_ptr<Expression>> parse_implies() {
        auto left = parse_or();
        if (!left) return left;
        while (current.type == TokenType::Implies) {
            advance();
            auto right = parse_or();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), BinOp::Implies, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_or() {
        auto left = parse_and(); 
        if (!left) return left;
        while (current.type == TokenType::Or || current.type == TokenType::BitOr) {
            BinOp op = (current.type == TokenType::Or) ? BinOp::Or : BinOp::BitOr;
            advance();
            auto right = parse_and();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_and() {
        auto left = parse_xor();
        if (!left) return left;
        while (current.type == TokenType::And || current.type == TokenType::BitAnd) {
            BinOp op = (current.type == TokenType::And) ? BinOp::And : BinOp::BitAnd;
            advance();
            auto right = parse_xor();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_xor() {
        auto left = parse_equality();
        if (!left) return left;
        while (current.type == TokenType::BitXor) {
            advance();
            auto right = parse_equality();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), BinOp::BitXor, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_equality() {
        auto left = parse_compare();
        if (!left) return left;
        while (current.type == TokenType::Eq || current.type == TokenType::Neq) {
            BinOp op = (current.type == TokenType::Eq) ? BinOp::Eq : BinOp::Neq;
            advance();
            auto right = parse_compare();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_compare() {
        auto left = parse_shift();
        if (!left) return left;
        while (current.type == TokenType::Lt || current.type == TokenType::Gt || current.type == TokenType::Lte || current.type == TokenType::Gte) {
            BinOp op;
            if (current.type == TokenType::Lt) op = BinOp::Lt;
            else if (current.type == TokenType::Gt) op = BinOp::Gt;
            else if (current.type == TokenType::Lte) op = BinOp::Lte;
            else op = BinOp::Gte;
            advance();
            auto right = parse_shift();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_shift() {
        auto left = parse_term();
        if (!left) return left;
        while (current.type == TokenType::Shl || current.type == TokenType::Shr) {
            BinOp op = (current.type == TokenType::Shl) ? BinOp::Shl : BinOp::Shr;
            advance();
            auto right = parse_term();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_term() {
        auto left = parse_factor();
        if (!left) return left;
        while (current.type == TokenType::Add || current.type == TokenType::Sub) {
            BinOp op = (current.type == TokenType::Add) ? BinOp::Add : BinOp::Sub;
            advance();
            auto right = parse_factor();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_factor() {
        auto left = parse_unary();
        if (!left) return left;
        while (current.type == TokenType::Mul || current.type == TokenType::Div || current.type == TokenType::Mod) {
            BinOp op;
            if (current.type == TokenType::Mul) op = BinOp::Mul;
            else if (current.type == TokenType::Div) op = BinOp::Div;
            else op = BinOp::Mod;
            advance();
            auto right = parse_unary();
            if (!right) return right;
            left = std::make_unique<BinaryExpr>(std::move(*left), op, std::move(*right));
        }
        return left;
    }

    Result<std::unique_ptr<Expression>> parse_unary() {
        if (current.type == TokenType::Not) {
            advance();
            auto expr = parse_unary();
            if (!expr) return expr;
            return std::make_unique<UnaryExpr>(std::move(*expr), UnaryExpr::Op::Not);
        }
        if (current.type == TokenType::Sub) {
            advance();
            auto expr = parse_unary();
            if (!expr) return expr;
            return std::make_unique<UnaryExpr>(std::move(*expr), UnaryExpr::Op::Neg);
        }
        return parse_primary();
    }

    Result<std::unique_ptr<Expression>> parse_primary() {
        if (current.type == TokenType::LParen) {
            advance();
            auto expr = parse_expression();
            if (!expr) return expr;
            TRY(expect(TokenType::RParen));
            return expr;
        }
        if (current.type == TokenType::Number) {
            int64_t val = 0;
            if (current.text.size() > 2 && (current.text[1] == 'x' || current.text[1] == 'X')) {
                val = std::stoll(current.text, nullptr, 16);
            } else {
                val = std::stoll(current.text, nullptr, 10);
            }
            advance();
            return std::make_unique<LiteralExpr>(val);
        }
        if (current.type == TokenType::String) {
            auto s = current.text;
            advance();
            if (s.find("${") != std::string::npos) {
                auto expr = std::make_unique<InterpolatedStringExpr>();
                size_t pos = 0;
                while (pos < s.size()) {
                    size_t start = s.find("${", pos);
                    if (start == std::string::npos) {
                        expr->parts.push_back({s.substr(pos), nullptr});
                        break;
                    }
                    if (start > pos) expr->parts.push_back({s.substr(pos, start - pos), nullptr});
                    size_t end = s.find("}", start);
                    if (end == std::string::npos) {
                        expr->parts.push_back({s.substr(start), nullptr});
                        break;
                    }
                    std::string sym_name = s.substr(start + 2, end - start - 2);
                    expr->parts.push_back({"", std::make_unique<SymbolRefExpr>(sym_name, &reg)});
                    pos = end + 1;
                }
                return expr;
            }
            return std::make_unique<LiteralExpr>(s);
        }
        if (current.type == TokenType::LBrace) {
            advance();
            auto set_expr = std::make_unique<SetExpr>();
            while (current.type != TokenType::RBrace && current.type != TokenType::Eof) {
                if (current.type == TokenType::Unknown && current.text == "$") {
                    advance();
                    TRY(expect(TokenType::LBrace));
                    auto sym = parse_identifier();
                    if (!sym) return std::unexpected(sym.error());
                    TRY(expect(TokenType::RBrace));
                    set_expr->elements.push_back(std::make_unique<SymbolRefExpr>(*sym, &reg));
                } else if (current.type == TokenType::Identifier || current.type == TokenType::Number || current.type == TokenType::String) {
                    set_expr->elements.push_back(std::make_unique<LiteralExpr>(current.text));
                    advance();
                } else {
                    return err("Unexpected token in set literal: " + current.text);
                }
                if (current.type == TokenType::Comma) advance();
            }
            TRY(expect(TokenType::RBrace));
            return set_expr;
        }
        if (current.type == TokenType::Identifier) {
            std::string name = current.text;
            advance();
            if (name == "true") return std::make_unique<LiteralExpr>(true);
            if (name == "false") return std::make_unique<LiteralExpr>(false);
            
            if (name == "env" && current.type == TokenType::LParen) {
                advance();
                auto val = parse_string();
                if (!val) return std::unexpected(val.error());
                TRY(expect(TokenType::RParen));
                return std::make_unique<EnvExpr>(*val);
            }
            if (name == "shell" && current.type == TokenType::LParen) {
                advance();
                auto val = parse_string();
                if (!val) return std::unexpected(val.error());
                TRY(expect(TokenType::RParen));
                return std::make_unique<ShellExpr>(*val);
            }
            if (name == "is_defined" && current.type == TokenType::LParen) {
                advance();
                auto sym_name = parse_identifier();
                if (!sym_name) return std::unexpected(sym_name.error());
                TRY(expect(TokenType::RParen));
                return std::make_unique<IsDefinedExpr>(*sym_name, &reg);
            }
            if (name == "abs" && current.type == TokenType::LParen) {
                advance();
                auto inner = parse_expression();
                if (!inner) return std::unexpected(inner.error());
                TRY(expect(TokenType::RParen));
                return std::make_unique<AbsExpr>(std::move(*inner));
            }

            if (!prefix.empty() && name.find('.') == std::string::npos) {
                return std::make_unique<SymbolRefExpr>(prefix + "." + name, &reg);
            }
            return std::make_unique<SymbolRefExpr>(name, &reg);
        }
        return err("Expected expression, found: " + current.text);
    }

    Result<Type> parse_type() {
        Type t;
        if (current.type == TokenType::TypeBool) t = Type::Bool;
        else if (current.type == TokenType::TypeInt) t = Type::Int;
        else if (current.type == TokenType::TypeString) t = Type::String;
        else if (current.type == TokenType::TypeEnum) t = Type::Enum;
        else if (current.type == TokenType::TypeSet) t = Type::Set;
        else return err("Expected type (bool|int|string|enum|set)");
        advance();
        return t;
    }

    Result<void> parse_macro_definition() {
        advance();
        auto name = parse_identifier();
        if (!name) return std::unexpected(name.error());
        
        MacroDefinition macro;
        macro.name = *name;
        
        TRY(expect(TokenType::LParen));
        while (current.type != TokenType::RParen && current.type != TokenType::Eof) {
            auto arg = parse_identifier();
            if (!arg) return std::unexpected(arg.error());
            macro.arg_names.push_back(*arg);
            if (current.type == TokenType::Comma) advance();
        }
        TRY(expect(TokenType::RParen));
        
        TRY(expect(TokenType::LBrace));
        int brace_depth = 1;
        size_t start = current.start_pos;
        
        while (brace_depth > 0 && current.type != TokenType::Eof) {
            if (current.type == TokenType::LBrace) brace_depth++;
            else if (current.type == TokenType::RBrace) brace_depth--;
            if (brace_depth > 0) advance();
        }
        size_t end = current.start_pos;
        
        macro.body = std::string(lexer.get_source().substr(start, end - start));
        TRY(expect(TokenType::RBrace));
        
        reg.register_macro(std::move(macro));
        return {};
    }

    Result<void> parse_macro_invocation() {
        if (depth > 100) return err("Maximum macro recursion depth exceeded");
        auto* macro = reg.find_macro(current.text);
        advance();
        
        std::vector<std::string> args;
        TRY(expect(TokenType::LParen));
        while (current.type != TokenType::RParen && current.type != TokenType::Eof) {
            if (current.type == TokenType::String || current.type == TokenType::Identifier || current.type == TokenType::Number) {
                args.push_back(current.text);
                advance();
            } else return err("Expected macro argument");
            if (current.type == TokenType::Comma) advance();
        }
        TRY(expect(TokenType::RParen));
        
        if (args.size() != macro->arg_names.size()) {
            return err(std::format("Macro {} expected {} args, got {}", macro->name, macro->arg_names.size(), args.size()));
        }
        
        std::string expanded = macro->body;
        for (size_t i = 0; i < args.size(); ++i) {
            std::string placeholder = "${" + macro->arg_names[i] + "}";
            size_t pos = 0;
            while ((pos = expanded.find(placeholder, pos)) != std::string::npos) {
                expanded.replace(pos, placeholder.length(), args[i]);
                pos += args[i].length();
            }
        }
        
        return Parser::parse(expanded, reg, filename + "::" + macro->name, prefix, depth + 1);
    }

    Result<void> parse_generate() {
        advance();
        GeneratorBackend backend;
        if (current.text == "header") backend = GeneratorBackend::Header;
        else if (current.text == "makefile") backend = GeneratorBackend::Makefile;
        else if (current.text == "json") backend = GeneratorBackend::JSON;
        else return err("Expected generator backend (header|makefile|json)");

        for (const auto& existing : reg.get_generators()) {
            if (existing->backend == backend) {
                return err(std::format("Generator for backend '{}' is already defined", current.text));
            }
        }
        advance();
        auto path = parse_string();
        if (!path) return std::unexpected(path.error());
        
        auto gen = std::make_unique<GenerateNode>();
        gen->backend = backend;
        gen->output_path = *path;
        reg.add_generator(std::move(gen));
        return {};
    }

    Result<void> parse_title() {
        if (reg.get_title() != "Zconfig Project") return err("Title can only be set once");
        advance();
        auto t = parse_string();
        if (!t) return std::unexpected(t.error());
        reg.set_title(*t);
        return {};
    }

    Result<void> parse_option() {
        advance();
        auto name = parse_identifier();
        if (!name) return std::unexpected(name.error());
        TRY(expect(TokenType::Colon));
        auto type_res = parse_type();
        if (!type_res) return std::unexpected(type_res.error());
        Type type = *type_res;

        std::unique_ptr<Expression> default_expr;
        if (current.type == TokenType::Assign) {
            advance();
            auto expr = parse_expression();
            if (!expr) return std::unexpected(expr.error());
            default_expr = std::move(*expr);
        }

        auto sym = std::make_unique<Symbol>();
        sym->name = *name;
        sym->type = type;
        if (default_expr) {
            sym->defaults.push_back({std::move(default_expr), nullptr});
        }

        if (current.type == TokenType::LBrace) {
            advance();
            while (current.type != TokenType::RBrace && current.type != TokenType::Eof) {
                if (current.type == TokenType::Label) {
                    advance();
                    auto s = parse_string();
                    if (!s) return std::unexpected(s.error());
                    sym->meta.label = *s;
                } else if (current.type == TokenType::Help) {
                    advance();
                    auto s = parse_string();
                    if (!s) return std::unexpected(s.error());
                    sym->meta.help = *s;
                } else if (current.type == TokenType::When) {
                    advance();
                    auto expr = parse_expression();
                    if (!expr) return std::unexpected(expr.error());
                    sym->when_condition = std::move(*expr);
                } else if (current.type == TokenType::Choices) {
                     advance();
                     while (current.type == TokenType::Identifier || current.type == TokenType::Number
                            || current.type == TokenType::String) {
                         sym->meta.choices.push_back(current.text);
                         advance();
                         // lexer emits BitOr for '|'; Pipe is never emitted
                         if (current.type == TokenType::Pipe || current.type == TokenType::BitOr) advance();
                     }
                } else if (current.type == TokenType::Choice) {
                    advance();
                    std::string id;
                    if (current.type == TokenType::Identifier || current.type == TokenType::Number) {
                        id = current.text; advance();
                    } else return err("Expected choice identifier");
                    
                    ChoiceInfo info;
                    TRY(expect(TokenType::LBrace));
                    while (current.type != TokenType::RBrace && current.type != TokenType::Eof) {
                        if (current.type == TokenType::Label) {
                            advance();
                            auto s = parse_string(); if (!s) return std::unexpected(s.error());
                            info.label = *s;
                        } else if (current.type == TokenType::Help) {
                            advance();
                            auto s = parse_string(); if (!s) return std::unexpected(s.error());
                            info.help = *s;
                        } else if (current.type == TokenType::When) {
                            advance();
                            auto expr = parse_expression(); if (!expr) return std::unexpected(expr.error());
                            info.when_condition = std::move(*expr);
                        } else advance();
                    }
                    TRY(expect(TokenType::RBrace));
                    sym->meta.choices_meta[id] = std::move(info);
                } else if (current.type == TokenType::Range) {
                    advance();
                    auto min_val = std::stoll(current.text, nullptr, 0); advance();
                    TRY(expect(TokenType::Range));
                    auto max_val = std::stoll(current.text, nullptr, 0); advance();
                    sym->meta.range = Range{min_val, max_val};
                } else if (current.type == TokenType::Pattern) {
                    advance();
                    auto p = parse_string(); if (!p) return std::unexpected(p.error());
                    sym->meta.pattern = *p;
                } else if (current.type == TokenType::Default) {
                    advance();
                    if (current.type == TokenType::LBrace) {
                        advance();
                        while (current.type != TokenType::RBrace && current.type != TokenType::Eof) {
                            auto val_expr = parse_expression();
                            if (!val_expr) return std::unexpected(val_expr.error());
                            std::unique_ptr<Expression> cond_expr;
                            if (current.type == TokenType::When) {
                                advance();
                                auto ce = parse_expression();
                                if (!ce) return std::unexpected(ce.error());
                                cond_expr = std::move(*ce);
                            }
                            sym->defaults.push_back({std::move(*val_expr), std::move(cond_expr)});
                        }
                        TRY(expect(TokenType::RBrace));
                    } else {
                        auto val_expr = parse_expression();
                        if (!val_expr) return std::unexpected(val_expr.error());
                        sym->defaults.push_back({std::move(*val_expr), nullptr});
                    }
                } else if (current.type == TokenType::Implies) {
                    advance();
                    auto expr = parse_expression();
                    if (!expr) return std::unexpected(expr.error());
                    sym->implications.push_back({nullptr, std::move(*expr)});
                } else if (current.type == TokenType::Tags) {
                    advance();
                    TRY(expect(TokenType::LBracket));
                    while (current.type == TokenType::String) {
                        sym->meta.tags.push_back(current.text);
                        advance();
                        if (current.type == TokenType::Comma) advance();
                    }
                    TRY(expect(TokenType::RBracket));
                } else if (current.type == TokenType::Danger) {
                    advance();
                    if (current.text == "none") sym->meta.danger = DangerLevel::None;
                    else if (current.text == "warning") sym->meta.danger = DangerLevel::Warning;
                    else if (current.text == "critical") sym->meta.danger = DangerLevel::Critical;
                    advance();
                } else if (current.type == TokenType::Collapsed) {
                    sym->meta.collapsed = true;
                    advance();
                } else advance(); 
            }
            TRY(expect(TokenType::RBrace));
        }

        Symbol* s_ptr = sym.get();
        sym->source_file = filename;
        if (!menu_stack.empty()) {
             sym->parent = menu_stack.top();
             menu_stack.top()->children.push_back(s_ptr);
        }
        return reg.register_symbol(std::move(sym), prefix);
    }

    Result<void> parse_menu() {
        advance();
        auto label = parse_string();
        if (!label) return std::unexpected(label.error());
        auto menu = std::make_unique<Menu>();
        menu->label = *label;
        if (current.type == TokenType::When) {
            advance();
            auto expr = parse_expression();
            if (!expr) return std::unexpected(expr.error());
            menu->when_condition = std::move(*expr);
        }
        TRY(expect(TokenType::LBrace));
        Menu* m_ptr = menu.get();
        menu->source_file = filename;
        if (!menu_stack.empty()) {
            menu->parent = menu_stack.top();
            menu_stack.top()->children.push_back(m_ptr);
        } else {
            reg.add_root_menu(m_ptr);
        }
        reg.register_node(std::move(menu));
        menu_stack.push(m_ptr);
        while (current.type != TokenType::RBrace && current.type != TokenType::Eof) {
             if (current.type == TokenType::Option) TRY(parse_option());
             else if (current.type == TokenType::Menu) TRY(parse_menu());
             else if (current.type == TokenType::Computed) TRY(parse_computed());
             else if (current.type == TokenType::Include) TRY(parse_include());
             else if (current.type == TokenType::Validate) TRY(parse_validation());
             else if (current.type == TokenType::Macro) TRY(parse_macro_definition());
             else if (current.type == TokenType::Identifier && reg.find_macro(current.text)) TRY(parse_macro_invocation());
             else advance();
        }
        menu_stack.pop();
        TRY(expect(TokenType::RBrace));
        return {};
    }
    
    Result<void> parse_computed() {
        advance();
        auto name = parse_identifier();
        if (!name) return std::unexpected(name.error());
        TRY(expect(TokenType::Colon));
        auto type_res = parse_type();
        if (!type_res) return std::unexpected(type_res.error());
        TRY(expect(TokenType::Assign));
        auto expr = parse_expression();
        if (!expr) return std::unexpected(expr.error());
        auto sym = std::make_unique<ComputedSymbol>();
        sym->name = *name;
        sym->type = *type_res;
        sym->expr = std::move(*expr);
        if (!menu_stack.empty()) {
             sym->parent = menu_stack.top();
             menu_stack.top()->children.push_back(sym.get());
        }
        sym->source_file = filename;
        return reg.register_computed(std::move(sym), prefix);
    }

    Result<void> parse_validation() {
        advance();
        auto msg = parse_string();
        if (!msg) return std::unexpected(msg.error());
        TRY(expect(TokenType::LBrace));
        auto expr = parse_expression();
        if (!expr) return std::unexpected(expr.error());
        TRY(expect(TokenType::RBrace));
        auto node = std::make_unique<ValidationNode>();
        node->message = *msg;
        node->condition = std::move(*expr);
        reg.register_validation(node.get());
        reg.register_node(std::move(node));
        return {};
    }
    
    Result<void> parse_include() {
        advance();
        auto path = parse_string();
        if (!path) return std::unexpected(path.error());
        
        std::string as_prefix = prefix;
        if (current.type == TokenType::As) {
            advance();
            auto p = parse_identifier();
            if (!p) return std::unexpected(p.error());
            if (as_prefix.empty()) as_prefix = *p;
            else as_prefix += "." + *p;
        }

        bool   has_when = false;
        bool   cond_active = true;
        if (current.type == TokenType::When) {
            has_when = true;
            advance();
            auto expr = parse_expression();
            if (!expr) return std::unexpected(expr.error());
            auto val = (*expr)->evaluate();
            bool cond = false;
            if (std::holds_alternative<bool>(val)) cond = std::get<bool>(val);
            cond_active = cond;
        }

        std::string resolved = fs::resolve_path(*path, filename);
        reg.record_include(filename, resolved, *path, as_prefix, has_when, cond_active);

        if (!cond_active) return {};
        if (include_stack.contains(resolved)) return err("Circular include: " + resolved);
        
        auto content = fs::read_file(resolved);
        if (!content) return std::unexpected(content.error());
        
        include_stack.insert(resolved);
        auto res = Parser::parse(*content, reg, resolved, as_prefix, depth);
        include_stack.erase(resolved);
        return res;
    }
};

} // anon ns

Result<void> Parser::parse(std::string_view source, Registry& reg, const std::string& filename, const std::string& prefix, uint32_t depth) {
    if (depth == 0) reg.set_include_root(filename, filename);
    ParserImpl impl(source, reg, filename, prefix, depth);
    return impl.parse();
}

}
