#include "interp/interp.h"

#include "check/type.h"
#include "support/literal.h"

#include <cstdint>
#include <cstdio>

namespace lucb {
namespace {

const int k_max_frames = 10'000;
const int64_t k_i64_min = static_cast<int64_t>(INT64_MIN);

struct Value {
    TypeKind kind = TypeKind::Unit;
    bool b = false;
    int64_t i = 0;
    string_view str;
    Type* type = nullptr;
    vector<Value> fields;
};

Value v_unit() {
    Value v;
    v.kind = TypeKind::Unit;
    return v;
}

Value v_bool(bool b) {
    Value v;
    v.kind = TypeKind::Bool;
    v.b = b;
    return v;
}

Value v_i64(int64_t i) {
    Value v;
    v.kind = TypeKind::I64;
    v.i = i;
    return v;
}

Value v_str(string_view s) {
    Value v;
    v.kind = TypeKind::Str;
    v.str = s;
    return v;
}

Value v_zero(Type* t) {
    Value v;
    if (t == nullptr) {
        return v;
    }
    v.kind = t->kind;
    v.type = t;
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                v.fields.push_back(v_zero(m->ty));
            }
        }
    }
    return v;
}

int field_index(Node* st, string_view name) {
    int i = 0;
    for (Node* m = st->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            if (m->text == name) {
                return i;
            }
            i++;
        }
    }
    return -1;
}

struct Slot {
    string_view name;
    Value value;
};

struct Frame {
    vector<Slot> slots;
};

struct Interp {
    Node* module = nullptr;
    vector<Frame> frames;
    string output;
    bool trapped = false;
    string trap;
    bool returning = false;
    Value ret;

    void fail(const string& message) {
        trapped = true;
        trap = message;
    }

    Slot* find_slot(string_view name) {
        if (frames.empty()) {
            return nullptr;
        }
        Frame& f = frames.back();
        for (int i = static_cast<int>(f.slots.size()) - 1; i >= 0; i--) {
            if (f.slots[static_cast<size_t>(i)].name == name) {
                return &f.slots[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

    Value* lvalue(Node* n) {
        if (n == nullptr || trapped) {
            return nullptr;
        }
        if (n->kind == NodeKind::Name || n->kind == NodeKind::Self) {
            string_view name = n->kind == NodeKind::Self ? string_view("self") : n->text;
            Slot* s = find_slot(name);
            if (s == nullptr) {
                fail("unknown name at runtime");
                return nullptr;
            }
            return &s->value;
        }
        if (n->kind == NodeKind::Member) {
            Value* obj = lvalue(n->left);
            if (obj == nullptr || obj->type == nullptr || obj->type->decl == nullptr) {
                fail("invalid field access");
                return nullptr;
            }
            int i = field_index(obj->type->decl, n->text);
            if (i < 0 || i >= static_cast<int>(obj->fields.size())) {
                fail("no such field");
                return nullptr;
            }
            return &obj->fields[static_cast<size_t>(i)];
        }
        fail("not an lvalue");
        return nullptr;
    }

    string decode_string(string_view tok) {
        // Strip quotes; keep simple escapes.
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
                } else if (e == '\\' || e == '"') {
                    out += e;
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

    string show(const Value& v) {
        if (v.kind == TypeKind::Bool) {
            return v.b ? "true" : "false";
        }
        if (v.kind == TypeKind::I64) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.i));
            return buf;
        }
        if (v.kind == TypeKind::Str) {
            return decode_string(v.str);
        }
        return "";
    }

    Value eval(Node* n) {
        if (n == nullptr || trapped) {
            return v_unit();
        }
        switch (n->kind) {
        case NodeKind::Literal:
            if (n->op == TokenKind::KwTrue) {
                return v_bool(true);
            }
            if (n->op == TokenKind::KwFalse) {
                return v_bool(false);
            }
            if (n->op == TokenKind::StringLit) {
                return v_str(n->text);
            }
            if (n->op == TokenKind::IntLit) {
                int64_t i = 0;
                parse_i64_literal(n->text, &i);
                return v_i64(i);
            }
            return v_unit();
        case NodeKind::Name:
        case NodeKind::Self: {
            Value* p = lvalue(n);
            if (p == nullptr) {
                return v_unit();
            }
            return *p;
        }
        case NodeKind::Group:
            return eval(n->left);
        case NodeKind::Unit:
            return v_unit();
        case NodeKind::Unary:
            return eval_unary(n);
        case NodeKind::Binary:
            return eval_binary(n);
        case NodeKind::Call:
            return eval_call(n);
        case NodeKind::Member: {
            Value* p = lvalue(n);
            if (p == nullptr) {
                return v_unit();
            }
            return *p;
        }
        case NodeKind::Conditional: {
            Value c = eval(n->type);
            if (c.b) {
                return eval(n->left);
            }
            return eval(n->right);
        }
        default:
            fail("unsupported expression at runtime");
            return v_unit();
        }
    }

    Value eval_unary(Node* n) {
        Value x = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        if (n->op == TokenKind::KwNot) {
            return v_bool(!x.b);
        }
        if (n->op == TokenKind::Plus) {
            return x;
        }
        if (n->op == TokenKind::Minus) {
            if (x.i == k_i64_min) {
                fail("integer overflow");
                return v_unit();
            }
            return v_i64(-x.i);
        }
        fail("unsupported unary operator");
        return v_unit();
    }

    Value eval_binary(Node* n) {
        if (n->op == TokenKind::KwAnd) {
            Value L = eval(n->left);
            if (trapped || !L.b) {
                return v_bool(false);
            }
            return eval(n->right);
        }
        if (n->op == TokenKind::KwOr) {
            Value L = eval(n->left);
            if (trapped || L.b) {
                return v_bool(true);
            }
            return eval(n->right);
        }
        Value L = eval(n->left);
        Value R = eval(n->right);
        if (trapped) {
            return v_unit();
        }
        TokenKind op = n->op;
        if (op == TokenKind::EqEq) {
            if (L.kind == TypeKind::Bool) {
                return v_bool(L.b == R.b);
            }
            return v_bool(L.i == R.i);
        }
        if (op == TokenKind::NotEq) {
            if (L.kind == TypeKind::Bool) {
                return v_bool(L.b != R.b);
            }
            return v_bool(L.i != R.i);
        }
        if (op == TokenKind::Lt) {
            return v_bool(L.i < R.i);
        }
        if (op == TokenKind::LtEq) {
            return v_bool(L.i <= R.i);
        }
        if (op == TokenKind::Gt) {
            return v_bool(L.i > R.i);
        }
        if (op == TokenKind::GtEq) {
            return v_bool(L.i >= R.i);
        }

        int64_t r = 0;
        if (op == TokenKind::Plus) {
            if (__builtin_add_overflow(L.i, R.i, &r)) {
                fail("integer overflow");
                return v_unit();
            }
            return v_i64(r);
        }
        if (op == TokenKind::Minus) {
            if (__builtin_sub_overflow(L.i, R.i, &r)) {
                fail("integer overflow");
                return v_unit();
            }
            return v_i64(r);
        }
        if (op == TokenKind::Star) {
            if (__builtin_mul_overflow(L.i, R.i, &r)) {
                fail("integer overflow");
                return v_unit();
            }
            return v_i64(r);
        }
        if (op == TokenKind::SlashSlash || op == TokenKind::Percent) {
            if (R.i == 0) {
                fail("division by zero");
                return v_unit();
            }
            if (L.i == k_i64_min && R.i == -1) {
                fail("integer overflow");
                return v_unit();
            }
            if (op == TokenKind::SlashSlash) {
                return v_i64(L.i / R.i);
            }
            return v_i64(L.i % R.i);
        }
        fail("unsupported binary operator");
        return v_unit();
    }

    Node* find_func(string_view name) {
        for (Node* d = module->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == name) {
                return d;
            }
        }
        return nullptr;
    }

    Value call_func(Node* fn, Value* self, Node* args) {
        if (static_cast<int>(frames.size()) >= k_max_frames) {
            fail("stack overflow");
            return v_unit();
        }
        Frame frame;
        if (self != nullptr) {
            Slot s;
            s.name = "self";
            s.value = *self;
            frame.slots.push_back(s);
        }
        Node* p = fn->right;
        Node* a = args;
        while (p != nullptr && a != nullptr) {
            Slot s;
            s.name = p->text;
            s.value = eval(a->left);
            if (trapped) {
                return v_unit();
            }
            frame.slots.push_back(s);
            p = p->next;
            a = a->next;
        }
        frames.push_back(frame);
        returning = false;
        exec(fn->body);
        if (self != nullptr && !frames.empty()) {
            Slot* ss = nullptr;
            Frame& top = frames.back();
            for (size_t i = 0; i < top.slots.size(); i++) {
                if (top.slots[i].name == "self") {
                    ss = &top.slots[i];
                    break;
                }
            }
            if (ss != nullptr) {
                *self = ss->value;
            }
        }
        Value result = returning ? ret : v_unit();
        returning = false;
        frames.pop_back();
        return result;
    }

    Value eval_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            Value a = eval(n->body != nullptr ? n->body->left : nullptr);
            if (!trapped) {
                output += show(a);
                output += '\n';
            }
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            Value a = eval(n->body != nullptr ? n->body->left : nullptr);
            fail(show(a));
            return v_unit();
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return eval_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Node* method = callee->resolved;
            if (method == nullptr || method->kind != NodeKind::Func) {
                fail("unknown method");
                return v_unit();
            }
            if ((method->flags & FlagStatic) != 0) {
                return call_func(method, nullptr, n->body);
            }
            Value* recv = lvalue(callee->left);
            if (recv == nullptr) {
                Value tmp = eval(callee->left);
                return call_func(method, &tmp, n->body);
            }
            return call_func(method, recv, n->body);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            return call_func(n->resolved, nullptr, n->body);
        }
        fail("unknown call");
        return v_unit();
    }

    Value eval_ctor(Node* n, Node* st) {
        Value v = v_zero(st->ty);
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->text.empty() || a->left == nullptr) {
                continue;
            }
            int i = field_index(st, a->text);
            if (i < 0) {
                continue;
            }
            v.fields[static_cast<size_t>(i)] = eval(a->left);
            if (trapped) {
                return v_unit();
            }
        }
        return v;
    }

    void exec(Node* n) {
        if (n == nullptr || trapped || returning) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            for (Node* s = n->body; s != nullptr; s = s->next) {
                exec(s);
                if (trapped || returning) {
                    return;
                }
            }
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Slot s;
            s.name = n->text;
            if (n->left != nullptr) {
                s.value = eval(n->left);
            } else {
                s.value = v_zero(n->ty);
            }
            if (!frames.empty()) {
                frames.back().slots.push_back(s);
            }
            break;
        }
        case NodeKind::Assign: {
            Value* dst = lvalue(n->left);
            Value src = eval(n->right);
            if (dst == nullptr || trapped) {
                return;
            }
            if (n->op == TokenKind::Eq) {
                *dst = src;
                break;
            }
            int64_t r = 0;
            if (n->op == TokenKind::PlusEq) {
                if (__builtin_add_overflow(dst->i, src.i, &r)) {
                    fail("integer overflow");
                    return;
                }
            } else if (n->op == TokenKind::MinusEq) {
                if (__builtin_sub_overflow(dst->i, src.i, &r)) {
                    fail("integer overflow");
                    return;
                }
            } else if (n->op == TokenKind::StarEq) {
                if (__builtin_mul_overflow(dst->i, src.i, &r)) {
                    fail("integer overflow");
                    return;
                }
            } else if (n->op == TokenKind::SlashSlashEq) {
                if (src.i == 0) {
                    fail("division by zero");
                    return;
                }
                if (dst->i == k_i64_min && src.i == -1) {
                    fail("integer overflow");
                    return;
                }
                r = dst->i / src.i;
            } else if (n->op == TokenKind::PercentEq) {
                if (src.i == 0) {
                    fail("division by zero");
                    return;
                }
                if (dst->i == k_i64_min && src.i == -1) {
                    fail("integer overflow");
                    return;
                }
                r = dst->i % src.i;
            } else {
                fail("unsupported assignment");
                return;
            }
            dst->i = r;
            break;
        }
        case NodeKind::If: {
            Value c = eval(n->left);
            if (trapped) {
                return;
            }
            if (c.b) {
                exec(n->body);
            } else {
                exec(n->right);
            }
            break;
        }
        case NodeKind::While:
            while (!trapped && !returning) {
                Value c = eval(n->left);
                if (trapped || !c.b) {
                    break;
                }
                exec(n->body);
            }
            break;
        case NodeKind::Return:
            ret = n->left != nullptr ? eval(n->left) : v_unit();
            returning = true;
            break;
        case NodeKind::ExprStmt:
            eval(n->left);
            break;
        default:
            fail("unsupported statement at runtime");
            break;
        }
    }
};

} // namespace

EvalResult eval_module(Node* module) {
    EvalResult result;
    if (module == nullptr) {
        return result;
    }
    Interp ip;
    ip.module = module;
    Node* answer = ip.find_func("answer");
    if (answer == nullptr) {
        ip.fail("no `answer` function");
        result.trapped = true;
        result.trap = ip.trap;
        result.output = ip.output;
        return result;
    }
    Value v = ip.call_func(answer, nullptr, nullptr);
    result.output = ip.output;
    if (ip.trapped) {
        result.trapped = true;
        result.trap = ip.trap;
        return result;
    }
    result.ok = true;
    if (v.kind == TypeKind::I64) {
        result.has_answer = true;
        result.answer = v.i;
    }
    return result;
}

} // namespace lucb
