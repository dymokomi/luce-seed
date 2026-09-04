#include "emit/emit.h"

#include "check/type.h"
#include "support/literal.h"

#include <cinttypes>
#include <cstdio>

namespace lucb {
namespace {

string ident(string_view prefix, string_view name) {
    string s;
    s.append(prefix.data(), prefix.size());
    s.append(name.data(), name.size());
    return s;
}

string struct_ident(Node* st) {
    return ident("lb_", st->text);
}

string func_ident(Node* fn, Node* owner) {
    if (owner != nullptr) {
        return ident("lb_", owner->text) + "_" + string(fn->text);
    }
    return ident("lb_", fn->text);
}

string c_type(Type* t) {
    if (t == nullptr) {
        return "void";
    }
    switch (t->kind) {
    case TypeKind::Bool:
        return "bool";
    case TypeKind::I64:
        return "int64_t";
    case TypeKind::Str:
        return "const char*";
    case TypeKind::Struct:
        if (t->decl != nullptr) {
            return struct_ident(t->decl);
        }
        return ident("lb_", t->name);
    case TypeKind::Unit:
    case TypeKind::Never:
    case TypeKind::Error:
        return "void";
    }
    return "void";
}

string c_escape(string_view s) {
    string out = "\"";
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

string decode_lit(string_view tok) {
    if (tok.size() >= 2 && tok[0] == '"') {
        tok = tok.substr(1, tok.size() - 2);
    }
    string out;
    for (size_t i = 0; i < tok.size(); i++) {
        if (tok[i] == '\\' && i + 1 < tok.size()) {
            char e = tok[i + 1];
            if (e == 'n') {
                out += '\n';
            } else if (e == 't') {
                out += '\t';
            } else {
                out += e;
            }
            i++;
        } else {
            out += tok[i];
        }
    }
    return out;
}

struct Emitter {
    string out;
    int indent = 0;

    void pad() {
        for (int i = 0; i < indent; i++) {
            out += "    ";
        }
    }

    void line(const string& s) {
        pad();
        out += s;
        out += '\n';
    }

    string emit_expr(Node* n) {
        if (n == nullptr) {
            return "0";
        }
        switch (n->kind) {
        case NodeKind::Literal:
            return emit_literal(n);
        case NodeKind::Name:
            return ident("lb_", n->text);
        case NodeKind::Self:
            return "(*self)";
        case NodeKind::Group:
            return "(" + emit_expr(n->left) + ")";
        case NodeKind::Unit:
            return "((void)0)";
        case NodeKind::Unary:
            return emit_unary(n);
        case NodeKind::Binary:
            return emit_binary(n);
        case NodeKind::Call:
            return emit_call(n);
        case NodeKind::Member:
            return emit_member(n);
        case NodeKind::Conditional:
            return "(" + emit_expr(n->type) + " ? " + emit_expr(n->left) + " : " +
                   emit_expr(n->right) + ")";
        default:
            return "/* unsupported expr */ 0";
        }
    }

    string emit_literal(Node* n) {
        if (n->op == TokenKind::KwTrue) {
            return "true";
        }
        if (n->op == TokenKind::KwFalse) {
            return "false";
        }
        if (n->op == TokenKind::StringLit) {
            return c_escape(decode_lit(n->text));
        }
        int64_t value = 0;
        parse_i64_literal(n->text, &value);
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64 "LL", value);
        return buf;
    }

    string emit_unary(Node* n) {
        if (n->op == TokenKind::KwNot) {
            return "(!" + emit_expr(n->left) + ")";
        }
        if (n->op == TokenKind::Plus) {
            return emit_expr(n->left);
        }
        if (n->op == TokenKind::Minus) {
            return "lb_neg_i64(" + emit_expr(n->left) + ")";
        }
        return "0";
    }

    string emit_binary(Node* n) {
        string L = emit_expr(n->left);
        string R = emit_expr(n->right);
        TokenKind op = n->op;
        if (op == TokenKind::KwAnd) {
            return "(" + L + " && " + R + ")";
        }
        if (op == TokenKind::KwOr) {
            return "(" + L + " || " + R + ")";
        }
        if (op == TokenKind::EqEq) {
            return "(" + L + " == " + R + ")";
        }
        if (op == TokenKind::NotEq) {
            return "(" + L + " != " + R + ")";
        }
        if (op == TokenKind::Lt) {
            return "(" + L + " < " + R + ")";
        }
        if (op == TokenKind::LtEq) {
            return "(" + L + " <= " + R + ")";
        }
        if (op == TokenKind::Gt) {
            return "(" + L + " > " + R + ")";
        }
        if (op == TokenKind::GtEq) {
            return "(" + L + " >= " + R + ")";
        }
        const char* helper = nullptr;
        if (op == TokenKind::Plus) {
            helper = "lb_add_i64";
        } else if (op == TokenKind::Minus) {
            helper = "lb_sub_i64";
        } else if (op == TokenKind::Star) {
            helper = "lb_mul_i64";
        } else if (op == TokenKind::SlashSlash) {
            helper = "lb_div_i64";
        } else if (op == TokenKind::Percent) {
            helper = "lb_mod_i64";
        }
        if (helper != nullptr) {
            return string(helper) + "(" + L + ", " + R + ")";
        }
        return "0";
    }

    string emit_member(Node* n) {
        if (n->left != nullptr && n->left->kind == NodeKind::Self) {
            return "self->" + string(n->text);
        }
        return emit_expr(n->left) + "." + string(n->text);
    }

    string emit_addr(Node* n) {
        if (n == nullptr) {
            return "NULL";
        }
        if (n->kind == NodeKind::Self) {
            return "self";
        }
        if (n->kind == NodeKind::Name) {
            return "&" + ident("lb_", n->text);
        }
        if (n->kind == NodeKind::Member) {
            return "&(" + emit_member(n) + ")";
        }
        return "&(" + emit_expr(n) + ")";
    }

    string emit_args(Node* args) {
        string s;
        bool first = true;
        for (Node* a = args; a != nullptr; a = a->next) {
            if (!first) {
                s += ", ";
            }
            first = false;
            s += emit_expr(a->left);
        }
        return s;
    }

    string emit_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            Type* t = arg != nullptr ? arg->ty : nullptr;
            string e = emit_expr(arg);
            if (t != nullptr && t->kind == TypeKind::Bool) {
                return "lb_print_bool(" + e + ")";
            }
            if (t != nullptr && t->kind == TypeKind::Str) {
                return "lb_print_str(" + e + ")";
            }
            return "lb_print_i64(" + e + ")";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            return "lb_trap(" + emit_expr(arg) + ")";
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return emit_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Node* method = callee->resolved;
            Node* obj = callee->left;
            Type* ot = obj != nullptr ? obj->ty : nullptr;
            Node* owner = ot != nullptr ? ot->decl : nullptr;
            string name = func_ident(method, owner);
            string args = emit_args(n->body);
            if (method != nullptr && (method->flags & FlagStatic) != 0) {
                return name + "(" + args + ")";
            }
            string recv = emit_addr(obj);
            if (args.empty()) {
                return name + "(" + recv + ")";
            }
            return name + "(" + recv + ", " + args + ")";
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            string name = func_ident(n->resolved, nullptr);
            string args = emit_args(n->body);
            return name + "(" + args + ")";
        }
        return "0";
    }

    string emit_ctor(Node* n, Node* st) {
        string s = "(" + struct_ident(st) + "){";
        bool first = true;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->text.empty()) {
                continue;
            }
            if (!first) {
                s += ", ";
            }
            first = false;
            s += "." + string(a->text) + " = " + emit_expr(a->left);
        }
        s += "}";
        return s;
    }

    void emit_block(Node* n) {
        line("{");
        indent++;
        if (n != nullptr) {
            for (Node* s = n->kind == NodeKind::Block ? n->body : n; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        }
        indent--;
        line("}");
    }

    void emit_stmt(Node* n) {
        if (n == nullptr) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            emit_block(n);
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            string ty = c_type(n->ty);
            string name = ident("lb_", n->text);
            string init = "0";
            if (n->left != nullptr) {
                init = emit_expr(n->left);
            } else if (n->ty != nullptr && n->ty->kind == TypeKind::Struct) {
                init = "{0}";
            } else if (n->ty != nullptr && n->ty->kind == TypeKind::Bool) {
                init = "false";
            }
            line(ty + " " + name + " = " + init + ";");
            break;
        }
        case NodeKind::Assign: {
            string dst = emit_expr(n->left);
            // self.x emits self->x; Name emits lb_name.
            if (n->left != nullptr && n->left->kind == NodeKind::Self) {
                dst = "(*self)";
            }
            string src = emit_expr(n->right);
            if (n->op == TokenKind::Eq) {
                line(dst + " = " + src + ";");
                break;
            }
            const char* helper = "lb_add_i64";
            if (n->op == TokenKind::MinusEq) {
                helper = "lb_sub_i64";
            } else if (n->op == TokenKind::StarEq) {
                helper = "lb_mul_i64";
            } else if (n->op == TokenKind::SlashSlashEq) {
                helper = "lb_div_i64";
            } else if (n->op == TokenKind::PercentEq) {
                helper = "lb_mod_i64";
            }
            line(dst + " = " + string(helper) + "(" + dst + ", " + src + ");");
            break;
        }
        case NodeKind::If:
            emit_if(n);
            break;
        case NodeKind::While:
            pad();
            out += "while (" + emit_expr(n->left) + ") ";
            if (n->body != nullptr && n->body->kind == NodeKind::Block) {
                out += '\n';
                emit_block(n->body);
            } else {
                out += "{\n";
                indent++;
                emit_stmt(n->body);
                indent--;
                line("}");
            }
            break;
        case NodeKind::Return:
            if (n->left == nullptr) {
                line("return;");
            } else if (n->left->kind == NodeKind::Call && n->left->left != nullptr &&
                       n->left->left->kind == NodeKind::Name && n->left->left->text == "trap") {
                line(emit_expr(n->left) + ";");
            } else {
                line("return " + emit_expr(n->left) + ";");
            }
            break;
        case NodeKind::ExprStmt:
            line(emit_expr(n->left) + ";");
            break;
        default:
            line("/* unsupported stmt */");
            break;
        }
    }

    void emit_if(Node* n) {
        pad();
        out += "if (" + emit_expr(n->left) + ") ";
        if (n->body != nullptr && n->body->kind == NodeKind::Block) {
            out += '\n';
            emit_block(n->body);
        } else {
            out += "{\n";
            indent++;
            emit_stmt(n->body);
            indent--;
            line("}");
        }
        if (n->right == nullptr) {
            return;
        }
        if (n->right->kind == NodeKind::If) {
            pad();
            out += "else ";
            // continue on same conceptual chain; emit_if writes "if"
            // so we need "else if". Rewrite first line: call emit_if after else.
            // emit_if always starts with pad+if. So write "else " without newline
            // then emit_if which pads again — that puts else and if on two lines
            // which is valid C: `else\n if`.
            out += '\n';
            emit_if(n->right);
            return;
        }
        pad();
        out += "else ";
        if (n->right->kind == NodeKind::Block) {
            out += '\n';
            emit_block(n->right);
        } else {
            out += "{\n";
            indent++;
            emit_stmt(n->right);
            indent--;
            line("}");
        }
    }

    void emit_sig(Node* fn, Node* owner, bool define) {
        string ret = c_type(fn->ty);
        if (fn->ty != nullptr && fn->ty->kind == TypeKind::Unit) {
            ret = "void";
        }
        string name = func_ident(fn, owner);
        string sig = ret + " " + name + "(";
        bool first = true;
        if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
            if ((fn->flags & FlagMutating) != 0) {
                sig += struct_ident(owner) + "* self";
            } else {
                sig += "const " + struct_ident(owner) + "* self";
            }
            first = false;
        }
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (!first) {
                sig += ", ";
            }
            first = false;
            sig += c_type(p->ty) + " " + ident("lb_", p->text);
        }
        if (first) {
            sig += "void";
        }
        sig += ")";
        if (!define) {
            line(sig + ";");
            return;
        }
        line(sig + " {");
        indent++;
        if (fn->body != nullptr && fn->body->kind == NodeKind::Block) {
            for (Node* s = fn->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(fn->body);
        }
        indent--;
        line("}");
        out += '\n';
    }

    void emit_struct(Node* st) {
        line("typedef struct " + struct_ident(st) + " {");
        indent++;
        bool any = false;
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                line(c_type(m->ty) + " " + string(m->text) + ";");
                any = true;
            }
        }
        if (!any) {
            line("int unused;");
        }
        indent--;
        line("} " + struct_ident(st) + ";");
        out += '\n';
    }

    void emit_module(Node* mod) {
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n\n";
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                emit_struct(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, false);
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, false);
                    }
                }
            }
        }
        out += '\n';
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, true);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, true);
            }
        }
    }
};

} // namespace

string emit_c(Node* module) {
    Emitter e;
    if (module != nullptr) {
        e.emit_module(module);
    }
    return e.out;
}

} // namespace lucb
