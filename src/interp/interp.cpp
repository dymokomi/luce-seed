#include "interp/interp.h"

#include "check/type.h"
#include "support/literal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>

namespace lucb {
namespace {

const int k_max_frames = 10'000;

struct Value {
    TypeKind kind = TypeKind::Unit;
    Type* type = nullptr;
    bool b = false;
    uint64_t u = 0;
    double f = 0;
    string_view str;
    vector<Value> fields;
    Value* ptr = nullptr;
    size_t length = 0;
    bool present = true;
    bool failed = false;
    int32_t err_code = 0;
    string_view err_msg;
};

Value v_unit() {
    Value v;
    return v;
}

Value v_bool(bool b) {
    Value v;
    v.kind = TypeKind::Bool;
    v.b = b;
    return v;
}

Value v_int(Type* t, uint64_t u) {
    Value v;
    v.type = t;
    v.kind = t != nullptr ? t->kind : TypeKind::I64;
    if (t != nullptr && is_int(t)) {
        v.u = u & int_mask(int_bits(t));
    } else {
        v.u = u;
    }
    return v;
}

Value v_i64(int64_t i) {
    Value v;
    v.kind = TypeKind::I64;
    v.u = static_cast<uint64_t>(i);
    return v;
}

Value v_float(Type* t, double f) {
    Value v;
    v.type = t;
    v.kind = t != nullptr ? t->kind : TypeKind::F64;
    v.f = t != nullptr && t->kind == TypeKind::F32 ? static_cast<double>(static_cast<float>(f)) : f;
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
    v.type = t;
    v.kind = t->kind;
    if ((t->kind == TypeKind::Struct || t->kind == TypeKind::Union) && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                v.fields.push_back(v_zero(m->ty));
            }
        }
    }
    if (t->kind == TypeKind::Enum) {
        v.u = 0;
        v.present = true;
    }
    if (t->kind == TypeKind::Span) {
        v.length = 0;
        v.ptr = nullptr;
    }
    if (t->kind == TypeKind::Str) {
        v.str = {};
        v.length = 0;
    }
    return v;
}

int bits_of(const Value& v, Type* t) {
    int bits = int_bits(t != nullptr ? t : v.type);
    if (bits == 0) {
        return 64;
    }
    return bits;
}

int64_t as_s(const Value& v, Type* t) {
    int bits = bits_of(v, t);
    uint64_t u = v.u & int_mask(bits);
    if (bits < 64 && (u & (uint64_t{1} << (bits - 1))) != 0) {
        return static_cast<int64_t>(u | ~int_mask(bits));
    }
    return static_cast<int64_t>(u);
}

uint64_t as_u(const Value& v, Type* t) {
    return v.u & int_mask(bits_of(v, t));
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
    Node* decl = nullptr;
    Value value;
};

struct Frame {
    std::deque<Slot> slots;
};

struct Interp {
    Node* module = nullptr;
    vector<Node*> all_modules;
    vector<Frame> frames;
    std::deque<vector<Value>> storage;
    string output;
    bool trapped = false;
    string trap;
    bool returning = false;
    bool breaking = false;
    bool continuing = false;
    string_view jump_label;
    bool in_catch = false;
    bool recovered = false;
    Value recover_val;
    Node* current_fn = nullptr;
    Value ret;
    string err_storage;
    Frame globals;
    Value current_alloc;
    std::deque<string> strings;
    struct Deferred {
        Node* n = nullptr;
        bool err_only = false;
    };
    vector<vector<Deferred>> defers;

    void init_memory() {
        current_alloc.kind = TypeKind::Allocator;
        current_alloc.ptr = nullptr;
        current_alloc.u = 0;
    }

    void fail(const string& message) {
        trapped = true;
        trap = message;
    }

    Value make_array(Type* t, vector<Value> elems) {
        storage.push_back(std::move(elems));
        Value v;
        v.kind = TypeKind::Array;
        v.type = t;
        v.ptr = storage.back().data();
        v.length = storage.back().size();
        return v;
    }

    Value zero_of(Type* t) {
        Value v = v_zero(t);
        if (t != nullptr && t->kind == TypeKind::Array) {
            vector<Value> elems;
            elems.resize(static_cast<size_t>(t->length));
            for (size_t i = 0; i < elems.size(); i++) {
                elems[i] = zero_of(t->elem);
            }
            return make_array(t, std::move(elems));
        }
        if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr) {
            v.fields.clear();
            for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Field) {
                    v.fields.push_back(zero_of(m->ty));
                }
            }
        }
        return v;
    }

    Slot* find_slot(string_view name, Node* decl = nullptr) {
        if (!frames.empty()) {
            Frame& f = frames.back();
            for (int i = static_cast<int>(f.slots.size()) - 1; i >= 0; i--) {
                if (f.slots[static_cast<size_t>(i)].name == name) {
                    return &f.slots[static_cast<size_t>(i)];
                }
            }
        }
        if (decl != nullptr) {
            for (size_t i = 0; i < globals.slots.size(); i++) {
                if (globals.slots[i].decl == decl) {
                    return &globals.slots[i];
                }
            }
        }
        for (int i = static_cast<int>(globals.slots.size()) - 1; i >= 0; i--) {
            if (globals.slots[static_cast<size_t>(i)].name == name) {
                return &globals.slots[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

    uint64_t enum_case_int(Node* en, Node* cse) {
        uint64_t next = 0;
        if (en == nullptr) {
            return 0;
        }
        for (Node* m = en->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::EnumCase) {
                continue;
            }
            uint64_t v = next;
            if (m->left != nullptr && m->left->kind == NodeKind::Literal &&
                m->left->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(m->left->text);
                if (p.ok) {
                    v = p.value;
                }
            }
            if (m == cse) {
                return v;
            }
            next = v + 1;
        }
        return 0;
    }

    int enum_tag_of(Node* en, Node* cse) {
        int i = 0;
        if (en == nullptr) {
            return 0;
        }
        for (Node* m = en->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::EnumCase) {
                continue;
            }
            if (m == cse) {
                return i;
            }
            i++;
        }
        return 0;
    }

    Value v_enum_case(Node* cse, Type* t) {
        Value v;
        v.kind = TypeKind::Enum;
        v.type = t;
        if (is_int_enum(t)) {
            v.u = enum_case_int(t != nullptr ? t->decl : nullptr, cse);
        } else {
            v.u = static_cast<uint64_t>(enum_tag_of(t != nullptr ? t->decl : nullptr, cse));
        }
        return v;
    }

    Value* lvalue(Node* n) {
        if (n == nullptr || trapped) {
            return nullptr;
        }
        if (n->kind == NodeKind::Name || n->kind == NodeKind::Self) {
            string_view name = n->kind == NodeKind::Self ? string_view("self") : n->text;
            Slot* s = find_slot(name, n->resolved);
            if (s == nullptr) {
                fail("unknown name at runtime");
                return nullptr;
            }
            return &s->value;
        }
        if (n->kind == NodeKind::Member) {
            Type* lt = n->left != nullptr ? n->left->ty : nullptr;
            if (lt != nullptr && lt->kind == TypeKind::Module) {
                if (n->text == "allocator") {
                    return &current_alloc;
                }
                fail("not an lvalue");
                return nullptr;
            }
            Value* obj = lvalue(n->left);
            if (obj != nullptr && obj->kind == TypeKind::Pointer && obj->ptr != nullptr) {
                obj = obj->ptr;
            }
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
        if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
            Value p = eval(n->left);
            if (trapped) {
                return nullptr;
            }
            if (p.ptr == nullptr) {
                fail("null pointer");
                return nullptr;
            }
            return p.ptr;
        }
        if (n->kind == NodeKind::Index) {
            Value* base = lvalue(n->left);
            Value idxv = eval(n->body);
            if (trapped || base == nullptr) {
                return nullptr;
            }
            size_t i = static_cast<size_t>(as_u(idxv, n->body != nullptr ? n->body->ty : nullptr));
            Type* bt = n->left != nullptr ? n->left->ty : base->type;
            if (is_ptr(bt) && base->kind == TypeKind::Pointer) {
                if (base->ptr == nullptr) {
                    fail("null pointer");
                    return nullptr;
                }
                return base->ptr + static_cast<ptrdiff_t>(i);
            }
            if (base->kind == TypeKind::Pointer && base->ptr != nullptr) {
                if (base->ptr->kind == TypeKind::Array || !base->ptr->fields.empty()) {
                    base = base->ptr;
                }
            }
            size_t nlen = base->length != 0 ? base->length : base->fields.size();
            if (is_array(bt) || is_span(bt) || base->kind == TypeKind::Array ||
                base->kind == TypeKind::Span) {
                if (i >= nlen) {
                    fail("index out of bounds");
                    return nullptr;
                }
                if (base->ptr != nullptr) {
                    return base->ptr + static_cast<ptrdiff_t>(i);
                }
                return &base->fields[i];
            }
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
        if (v.kind == TypeKind::Str) {
            return decode_string(v.str);
        }
        if (is_float(v.type) || v.kind == TypeKind::F32 || v.kind == TypeKind::F64) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", v.f);
            return buf;
        }
        if (is_unsigned_int(v.type) || v.kind == TypeKind::U8 || v.kind == TypeKind::U16 ||
            v.kind == TypeKind::U32 || v.kind == TypeKind::U64 || v.kind == TypeKind::Usize ||
            v.kind == TypeKind::Char) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(as_u(v, v.type)));
            return buf;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(as_s(v, v.type)));
        return buf;
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
            if (n->op == TokenKind::KwNone) {
                Value v;
                v.type = n->ty;
                v.present = false;
                if (n->ty != nullptr && is_ptr(n->ty)) {
                    v.kind = TypeKind::Pointer;
                    v.ptr = nullptr;
                } else {
                    v.kind = TypeKind::Optional;
                }
                return v;
            }
            if (n->op == TokenKind::StringLit) {
                Value v = v_str(n->text);
                v.length = decode_string(n->text).size();
                v.type = n->ty;
                if (n->ty != nullptr && n->ty->kind == TypeKind::CStr) {
                    v.kind = TypeKind::CStr;
                }
                return v;
            }
            if (n->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(n->text);
                Type* t = n->ty;
                if (t != nullptr && is_opt(t) && is_int(t->elem)) {
                    Value v = v_int(t->elem, p.value);
                    v.kind = TypeKind::Optional;
                    v.type = t;
                    v.present = true;
                    return v;
                }
                if (t == nullptr || !is_int(t)) {
                    return v_i64(static_cast<int64_t>(p.value));
                }
                return v_int(t, p.value);
            }
            if (n->op == TokenKind::FloatLit) {
                ParsedFloat p = parse_float_literal(n->text);
                return v_float(n->ty, p.value);
            }
            if (n->op == TokenKind::CharLit) {
                uint32_t cp = 0;
                parse_char_literal(n->text, &cp);
                if (n->ty != nullptr && n->ty->kind == TypeKind::U8) {
                    return v_int(n->ty, cp);
                }
                Value v;
                v.kind = TypeKind::Char;
                v.type = n->ty;
                v.u = cp;
                return v;
            }
            return v_unit();
        case NodeKind::Name:
        case NodeKind::Self: {
            Value* p = lvalue(n);
            if (p == nullptr) {
                return v_unit();
            }
            Value v = *p;
            if (n->ty != nullptr && n->ty->kind == TypeKind::Interface && v.kind == TypeKind::Pointer) {
                v.kind = TypeKind::Interface;
                v.type = n->ty;
            }
            return v;
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
        case NodeKind::Member:
            return eval_member(n);
        case NodeKind::Index:
            return eval_index(n);
        case NodeKind::Slice:
            return eval_slice(n);
        case NodeKind::ArrayLit:
            return eval_array_lit(n);
        case NodeKind::SpanMake:
            return eval_span_make(n);
        case NodeKind::New:
            return eval_new(n);
        case NodeKind::Alloc:
            return eval_alloc(n);
        case NodeKind::Formatted:
            return eval_formatted(n);
        case NodeKind::Cast:
            return eval_conv(n->left, n->ty, false);
        case NodeKind::Else:
            return eval_else(n);
        case NodeKind::Catch:
            return eval_catch(n);
        case NodeKind::CaseValue:
            return eval_case_value(n);
        case NodeKind::Match:
        case NodeKind::MatchExpr:
            return eval_match(n);
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

    Value eval_case_value(Node* n) {
        Value v = v_enum_case(n->resolved, n->ty);
        if (n->body != nullptr && n->resolved != nullptr) {
            for (Node* a = n->body; a != nullptr; a = a->next) {
                v.fields.push_back(eval(a->left != nullptr ? a->left : a));
            }
        }
        return v;
    }

    Value eval_member(Node* n) {
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
            return v_enum_case(n->resolved, n->ty);
        }
        Type* lt = n->left != nullptr ? n->left->ty : nullptr;
        if (lt != nullptr && lt->kind == TypeKind::Module) {
            if (n->text == "allocator") {
                return current_alloc;
            }
            if (n->text == "heap") {
                return heap_alloc_value();
            }
            if (n->text == "exhausted") {
                Value v;
                v.kind = TypeKind::I32;
                v.u = 1;
                return v;
            }
        }
        Value obj = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        if (obj.kind == TypeKind::Pointer && obj.ptr != nullptr) {
            obj = *obj.ptr;
        }
        if (n->text == "length") {
            if (obj.kind == TypeKind::Str) {
                return v_int(n->ty, decode_string(obj.str).size());
            }
            size_t len = obj.length != 0 ? obj.length : obj.fields.size();
            if (obj.kind == TypeKind::Array && obj.type != nullptr) {
                len = static_cast<size_t>(obj.type->length);
            }
            return v_int(n->ty != nullptr ? n->ty : nullptr, len);
        }
        if (n->text == "data") {
            Value v;
            v.kind = TypeKind::Pointer;
            v.type = n->ty;
            if (obj.kind == TypeKind::Span) {
                v.ptr = obj.ptr;
            } else if (obj.kind == TypeKind::Array && !obj.fields.empty()) {
                v.ptr = &obj.fields[0];
            }
            return v;
        }
        if (n->text == "bytes") {
            Value v;
            v.kind = TypeKind::Span;
            v.type = n->ty;
            v.str = obj.str;
            v.length = decode_string(obj.str).size();
            return v;
        }
        Value* p = lvalue(n);
        if (p == nullptr) {
            return v_unit();
        }
        return *p;
    }

    Value eval_index(Node* n) {
        Value* slot = nullptr;
        if (n->left != nullptr &&
            (n->left->kind == NodeKind::Name || n->left->kind == NodeKind::Self ||
             n->left->kind == NodeKind::Member || n->left->kind == NodeKind::Index)) {
            slot = lvalue(n);
            if (slot != nullptr && !trapped) {
                return *slot;
            }
            if (trapped) {
                return v_unit();
            }
        }
        Value base = eval(n->left);
        Value idxv = eval(n->body);
        if (trapped) {
            return v_unit();
        }
        size_t i = static_cast<size_t>(as_u(idxv, n->body != nullptr ? n->body->ty : nullptr));
        if (base.kind == TypeKind::Str || !base.str.empty()) {
            string d = decode_string(base.str);
            if (i >= d.size()) {
                fail("index out of bounds");
                return v_unit();
            }
            return v_int(n->ty, static_cast<unsigned char>(d[i]));
        }
        if (base.kind == TypeKind::Span) {
            if (i >= base.length) {
                fail("index out of bounds");
                return v_unit();
            }
            if (base.ptr != nullptr) {
                return base.ptr[i];
            }
            if (!base.str.empty()) {
                string d = decode_string(base.str);
                if (i >= d.size()) {
                    fail("index out of bounds");
                    return v_unit();
                }
                return v_int(n->ty, static_cast<unsigned char>(d[i]));
            }
        }
        if (base.kind == TypeKind::Array) {
            if (i >= base.length) {
                fail("index out of bounds");
                return v_unit();
            }
            if (base.ptr != nullptr) {
                return base.ptr[i];
            }
            return base.fields[i];
        }
        if (base.kind == TypeKind::Pointer && base.ptr != nullptr) {
            return base.ptr[i];
        }
        fail("cannot index");
        return v_unit();
    }

    Value eval_slice(Node* n) {
        Value base = eval(n->left);
        size_t len = base.length != 0 ? base.length : base.fields.size();
        if (base.kind == TypeKind::Array && base.type != nullptr) {
            len = static_cast<size_t>(base.type->length);
        }
        if (base.kind == TypeKind::Str) {
            len = decode_string(base.str).size();
        }
        size_t start = 0;
        size_t end = len;
        if (n->body != nullptr) {
            start = static_cast<size_t>(as_u(eval(n->body), n->body->ty));
        }
        if (n->right != nullptr) {
            end = static_cast<size_t>(as_u(eval(n->right), n->right->ty));
        }
        if (trapped) {
            return v_unit();
        }
        if (start > end || end > len) {
            fail("index out of bounds");
            return v_unit();
        }
        Value v;
        v.kind = TypeKind::Span;
        v.type = n->ty;
        v.length = end - start;
        if (base.ptr != nullptr) {
            v.ptr = base.ptr + static_cast<ptrdiff_t>(start);
        } else if (base.kind == TypeKind::Array && !base.fields.empty()) {
            v.ptr = &base.fields[start];
        } else {
            v.str = base.str;
        }
        return v;
    }

    Value eval_array_lit(Node* n) {
        vector<Value> elems;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            elems.push_back(eval(e));
            if (trapped) {
                return v_unit();
            }
        }
        return make_array(n->ty, std::move(elems));
    }

    Value eval_formatted(Node* n) {
        string s;
        for (Node* p = n != nullptr ? n->body : nullptr; p != nullptr; p = p->next) {
            if (p->kind == NodeKind::FormatText) {
                s += decode_string(p->text);
            } else if (p->kind == NodeKind::FormatField) {
                Value f = eval(p->left);
                if (trapped) {
                    return v_unit();
                }
                s += show(f);
            }
        }
        strings.push_back(s);
        return v_str(strings.back());
    }

    Value eval_format(Node* n) {
        Node* bufn = n->body != nullptr ? n->body->left : nullptr;
        Node* msgn = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        Value buf = eval(bufn);
        Value msg;
        if (msgn != nullptr && msgn->kind == NodeKind::Formatted) {
            msg = eval_formatted(msgn);
        } else {
            msg = eval(msgn);
        }
        if (trapped) {
            return v_unit();
        }
        string text = msg.kind == TypeKind::Str ? decode_string(msg.str) : show(msg);
        if (text.size() > buf.length) {
            Value r;
            r.kind = TypeKind::Fallible;
            r.failed = true;
            r.err_code = 1;
            r.err_msg = "memory.exhausted";
            r.type = n->ty;
            return r;
        }
        strings.push_back(text);
        Value r;
        r.kind = TypeKind::Fallible;
        r.failed = false;
        r.type = n->ty;
        r.str = strings.back();
        r.length = strings.back().size();
        return r;
    }

    Value eval_span_make(Node* n) {
        Value p = eval(n->body != nullptr ? n->body->left : nullptr);
        Value len = eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
        if (trapped) {
            return v_unit();
        }
        Value v;
        v.kind = TypeKind::Span;
        v.type = n->ty;
        v.ptr = p.ptr;
        v.length = static_cast<size_t>(as_u(len, n->ty));
        return v;
    }

    Value heap_alloc_value() {
        Value a;
        a.kind = TypeKind::Allocator;
        a.ptr = nullptr;
        a.u = 0;
        return a;
    }

    Value fail_exhausted(Type* fail_ty) {
        Value v;
        v.kind = TypeKind::Fallible;
        v.type = fail_ty;
        v.failed = true;
        v.err_code = 1;
        v.err_msg = "memory.exhausted";
        return v;
    }

    Value ok_payload(Value payload, Type* fail_ty) {
        payload.failed = false;
        payload.kind = TypeKind::Fallible;
        payload.type = fail_ty;
        return payload;
    }

    Value as_alloc(Node* n) {
        if (n == nullptr) {
            return current_alloc;
        }
        Type* t = n->ty;
        if (t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer") {
            Value* p = lvalue(n);
            Value a;
            a.kind = TypeKind::Allocator;
            a.ptr = p;
            a.u = 1;
            return a;
        }
        return eval(n);
    }

    bool bump_fixed(Value* fb, size_t size, size_t align) {
        if (fb == nullptr || fb->fields.size() < 3) {
            return false;
        }
        size_t used = static_cast<size_t>(fb->fields[2].u);
        size_t cap = static_cast<size_t>(fb->fields[1].u);
        if (align < 1) {
            align = 1;
        }
        size_t start = used;
        size_t rem = start % align;
        if (rem != 0) {
            start += align - rem;
        }
        if (start + size < start || start + size > cap) {
            return false;
        }
        fb->fields[2].u = start + size;
        return true;
    }

    bool take_bytes(const Value& a, size_t size, size_t align) {
        if (size == 0) {
            return true;
        }
        if (a.kind == TypeKind::Allocator && a.ptr != nullptr) {
            return bump_fixed(a.ptr, size, align);
        }
        return true;
    }

    Value eval_new(Node* n) {
        Value a = as_alloc(n->right);
        if (trapped) {
            return v_unit();
        }
        Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
        if (is_span(payload)) {
            Node* count_n = n->type != nullptr ? n->type->right : nullptr;
            Value cv = eval(count_n);
            if (trapped) {
                return v_unit();
            }
            size_t count = static_cast<size_t>(as_u(cv, count_n != nullptr ? count_n->ty : nullptr));
            Type* elem = payload->elem;
            int esz = type_size(elem);
            int eal = type_align(elem);
            if (count != 0 && esz > 0 && count > static_cast<size_t>(-1) / static_cast<size_t>(esz)) {
                return fail_exhausted(n->ty);
            }
            size_t bytes = static_cast<size_t>(esz) * count;
            if (!take_bytes(a, bytes, static_cast<size_t>(eal < 1 ? 1 : eal))) {
                return fail_exhausted(n->ty);
            }
            vector<Value> elems;
            elems.resize(count);
            for (size_t i = 0; i < count; i++) {
                elems[i] = zero_of(elem);
            }
            Value sp = make_array(payload, std::move(elems));
            sp.kind = TypeKind::Span;
            sp.type = payload;
            return ok_payload(sp, n->ty);
        }
        Type* elem = is_ptr(payload) ? payload->elem : payload;
        int esz = type_size(elem);
        int eal = type_align(elem);
        if (!take_bytes(a, static_cast<size_t>(esz < 0 ? 0 : esz), static_cast<size_t>(eal < 1 ? 1 : eal))) {
            return fail_exhausted(n->ty);
        }
        Value init;
        if (n->body != nullptr && n->body->kind == NodeKind::CaseValue) {
            init = eval(n->body);
        } else if (n->body != nullptr && n->resolved != nullptr &&
                   n->resolved->kind == NodeKind::Struct) {
            init = eval_ctor(n, n->resolved);
        } else {
            init = zero_of(elem);
        }
        if (trapped) {
            return v_unit();
        }
        if (elem != nullptr && elem->kind == TypeKind::Array) {
            Value arr = init.kind == TypeKind::Array ? init : zero_of(elem);
            storage.push_back(vector<Value>{arr});
            Value p;
            p.kind = TypeKind::Pointer;
            p.type = payload;
            p.ptr = storage.back().data();
            return ok_payload(p, n->ty);
        }
        storage.push_back(vector<Value>{init});
        Value p;
        p.kind = TypeKind::Pointer;
        p.type = payload;
        p.ptr = storage.back().data();
        return ok_payload(p, n->ty);
    }

    Value eval_alloc(Node* n) {
        Value a = as_alloc(n->right);
        if (trapped) {
            return v_unit();
        }
        Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
        size_t count = 0;
        Type* elem = payload != nullptr ? payload->elem : nullptr;
        size_t align = static_cast<size_t>(type_align(elem) < 1 ? 1 : type_align(elem));
        if (n->type == nullptr) {
            Value sv = eval(n->body != nullptr ? n->body->left : nullptr);
            Value av = eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                          : nullptr);
            if (trapped) {
                return v_unit();
            }
            count = static_cast<size_t>(as_u(sv, n->body != nullptr && n->body->left != nullptr
                                                     ? n->body->left->ty
                                                     : nullptr));
            align = static_cast<size_t>(as_u(av, nullptr));
            if (align < 1) {
                align = 1;
            }
            elem = payload != nullptr ? payload->elem : nullptr;
        } else {
            Node* count_n = n->type->right;
            Value cv = eval(count_n);
            if (trapped) {
                return v_unit();
            }
            count = static_cast<size_t>(as_u(cv, count_n != nullptr ? count_n->ty : nullptr));
        }
        int esz = type_size(elem);
        if (n->type == nullptr) {
            esz = 1;
        }
        if (count != 0 && esz > 0 && count > static_cast<size_t>(-1) / static_cast<size_t>(esz)) {
            return fail_exhausted(n->ty);
        }
        size_t bytes = n->type == nullptr ? count : static_cast<size_t>(esz) * count;
        size_t nlen = n->type == nullptr ? count : count;
        if (!take_bytes(a, bytes, align)) {
            return fail_exhausted(n->ty);
        }
        vector<Value> elems;
        elems.resize(nlen);
        for (size_t i = 0; i < nlen; i++) {
            elems[i] = zero_of(elem);
        }
        Value sp = make_array(payload, std::move(elems));
        sp.kind = TypeKind::Span;
        sp.type = payload;
        return ok_payload(sp, n->ty);
    }

    Value eval_unary(Node* n) {
        Value x = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        if (n->op == TokenKind::KwTry) {
            if (x.failed) {
                ret = x;
                returning = true;
                return v_unit();
            }
            x.kind = n->ty != nullptr ? n->ty->kind : x.kind;
            x.type = n->ty;
            return x;
        }
        if (n->op == TokenKind::KwNot) {
            return v_bool(!x.b);
        }
        if (n->op == TokenKind::Amp) {
            Value* p = lvalue(n->left);
            Value v;
            v.kind = TypeKind::Pointer;
            v.type = n->ty;
            v.ptr = p;
            if (n->ty != nullptr && n->ty->kind == TypeKind::Interface) {
                v.kind = TypeKind::Interface;
            }
            return v;
        }
        if (n->op == TokenKind::Star) {
            if (x.ptr == nullptr) {
                fail("null pointer");
                return v_unit();
            }
            return *x.ptr;
        }
        if (n->op == TokenKind::Plus) {
            return x;
        }
        Type* t = n->ty != nullptr ? n->ty : x.type;
        if (n->op == TokenKind::Tilde) {
            Type* bits_t = is_int_enum(t) ? t->elem : t;
            uint64_t u = ~as_u(x, bits_t);
            if (is_int_enum(t)) {
                Value v;
                v.kind = TypeKind::Enum;
                v.type = t;
                v.u = u & int_mask(int_bits(bits_t));
                return v;
            }
            return v_int(t, u);
        }
        if (is_float(t)) {
            if (n->op == TokenKind::Minus) {
                return v_float(t, -x.f);
            }
        }
        int bits = int_bits(t);
        if (n->op == TokenKind::MinusPercent) {
            if (is_signed_int(t)) {
                uint64_t r = 0u - static_cast<uint64_t>(as_s(x, t));
                return v_int(t, r);
            }
            return v_int(t, 0u - as_u(x, t));
        }
        if (n->op == TokenKind::Minus) {
            if (n->left != nullptr && n->left->kind == NodeKind::Literal &&
                n->left->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(n->left->text);
                if (p.ok &&
                    p.value == static_cast<uint64_t>(int_max_signed(int_bits(t))) + 1) {
                    return v_int(t, static_cast<uint64_t>(int_min(t)));
                }
            }
            int64_t a = as_s(x, t);
            if (a == int_min(t)) {
                fail("integer overflow");
                return v_unit();
            }
            return v_int(t, static_cast<uint64_t>(-a));
        }
        (void)bits;
        fail("unsupported unary operator");
        return v_unit();
    }

    bool cmp_num(const Value& L, const Value& R, Type* t, TokenKind op) {
        if (is_float(t) || L.kind == TypeKind::F32 || L.kind == TypeKind::F64) {
            if (op == TokenKind::Lt) {
                return L.f < R.f;
            }
            if (op == TokenKind::LtEq) {
                return L.f <= R.f;
            }
            if (op == TokenKind::Gt) {
                return L.f > R.f;
            }
            return L.f >= R.f;
        }
        if (is_unsigned_int(t) || is_unsigned_int(L.type)) {
            uint64_t a = as_u(L, t != nullptr ? t : L.type);
            uint64_t b = as_u(R, t != nullptr ? t : R.type);
            if (op == TokenKind::Lt) {
                return a < b;
            }
            if (op == TokenKind::LtEq) {
                return a <= b;
            }
            if (op == TokenKind::Gt) {
                return a > b;
            }
            return a >= b;
        }
        int64_t a = as_s(L, t != nullptr ? t : L.type);
        int64_t b = as_s(R, t != nullptr ? t : R.type);
        if (op == TokenKind::Lt) {
            return a < b;
        }
        if (op == TokenKind::LtEq) {
            return a <= b;
        }
        if (op == TokenKind::Gt) {
            return a > b;
        }
        return a >= b;
    }

    Value arith(Type* t, const Value& L, const Value& R, TokenKind op) {
        if (is_float(t)) {
            double a = L.f;
            double b = R.f;
            if (op == TokenKind::Plus) {
                return v_float(t, a + b);
            }
            if (op == TokenKind::Minus) {
                return v_float(t, a - b);
            }
            if (op == TokenKind::Star) {
                return v_float(t, a * b);
            }
            if (op == TokenKind::Slash) {
                return v_float(t, a / b);
            }
            fail("unsupported float operator");
            return v_unit();
        }
        int bits = int_bits(t);
        bool sig = is_signed_int(t);
        if (op == TokenKind::Amp) {
            return v_int(t, as_u(L, t) & as_u(R, t));
        }
        if (op == TokenKind::Pipe) {
            return v_int(t, as_u(L, t) | as_u(R, t));
        }
        if (op == TokenKind::Caret) {
            return v_int(t, as_u(L, t) ^ as_u(R, t));
        }
        if (op == TokenKind::LtLt || op == TokenKind::GtGt) {
            uint64_t n = as_u(R, R.type != nullptr ? R.type : t);
            if (n >= static_cast<uint64_t>(bits)) {
                fail("shift count out of range");
                return v_unit();
            }
            if (op == TokenKind::LtLt) {
                return v_int(t, as_u(L, t) << n);
            }
            if (sig) {
                return v_int(t, static_cast<uint64_t>(as_s(L, t) >> n));
            }
            return v_int(t, as_u(L, t) >> n);
        }
        if (sig) {
            int64_t a = as_s(L, t);
            int64_t b = as_s(R, t);
            int64_t r = 0;
            if (op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star) {
                bool ov = false;
                if (op == TokenKind::Plus) {
                    ov = __builtin_add_overflow(a, b, &r);
                } else if (op == TokenKind::Minus) {
                    ov = __builtin_sub_overflow(a, b, &r);
                } else {
                    ov = __builtin_mul_overflow(a, b, &r);
                }
                if (ov || r < int_min(t) || r > int_max_signed(bits)) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, static_cast<uint64_t>(r));
            }
            if (op == TokenKind::PlusPercent) {
                return v_int(t, static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
            }
            if (op == TokenKind::MinusPercent) {
                return v_int(t, static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
            }
            if (op == TokenKind::StarPercent) {
                return v_int(t, static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
            }
            if (op == TokenKind::PlusPipe) {
                if (__builtin_add_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                    r = a < 0 ? int_min(t) : int_max_signed(bits);
                }
                return v_int(t, static_cast<uint64_t>(r));
            }
            if (op == TokenKind::MinusPipe) {
                if (__builtin_sub_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                    r = a < 0 ? int_min(t) : int_max_signed(bits);
                }
                return v_int(t, static_cast<uint64_t>(r));
            }
            if (op == TokenKind::StarPipe) {
                if (__builtin_mul_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                    r = ((a < 0) != (b < 0)) ? int_min(t) : int_max_signed(bits);
                }
                return v_int(t, static_cast<uint64_t>(r));
            }
            if (op == TokenKind::SlashSlash || op == TokenKind::Percent) {
                if (b == 0) {
                    fail("division by zero");
                    return v_unit();
                }
                if (a == int_min(t) && b == -1) {
                    fail("integer overflow");
                    return v_unit();
                }
                r = op == TokenKind::SlashSlash ? a / b : a % b;
                return v_int(t, static_cast<uint64_t>(r));
            }
        } else {
            uint64_t a = as_u(L, t);
            uint64_t b = as_u(R, t);
            uint64_t maxv = int_max_unsigned(bits);
            if (op == TokenKind::Plus) {
                if (bits >= 64 ? a > UINT64_MAX - b : a + b > maxv) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, a + b);
            }
            if (op == TokenKind::Minus) {
                if (a < b) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, a - b);
            }
            if (op == TokenKind::Star) {
                if (b != 0 && a > maxv / b) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, a * b);
            }
            if (op == TokenKind::PlusPercent) {
                return v_int(t, a + b);
            }
            if (op == TokenKind::MinusPercent) {
                return v_int(t, a - b);
            }
            if (op == TokenKind::StarPercent) {
                return v_int(t, a * b);
            }
            if (op == TokenKind::PlusPipe) {
                if (bits >= 64 ? a > UINT64_MAX - b : a + b > maxv) {
                    return v_int(t, maxv);
                }
                return v_int(t, a + b);
            }
            if (op == TokenKind::MinusPipe) {
                return v_int(t, a < b ? 0 : a - b);
            }
            if (op == TokenKind::StarPipe) {
                if (b != 0 && a > maxv / b) {
                    return v_int(t, maxv);
                }
                return v_int(t, a * b);
            }
            if (op == TokenKind::SlashSlash || op == TokenKind::Percent) {
                if (b == 0) {
                    fail("division by zero");
                    return v_unit();
                }
                return v_int(t, op == TokenKind::SlashSlash ? a / b : a % b);
            }
        }
        fail("unsupported binary operator");
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
        Type* t = n->ty;
        if (L.kind == TypeKind::Pointer || R.kind == TypeKind::Pointer) {
            if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
                bool eq = L.ptr == R.ptr;
                return v_bool(op == TokenKind::EqEq ? eq : !eq);
            }
            if (op == TokenKind::Plus && L.kind == TypeKind::Pointer && L.ptr != nullptr) {
                L.ptr += static_cast<ptrdiff_t>(as_s(R, R.type));
                L.type = n->ty;
                return L;
            }
            if (op == TokenKind::Minus && L.kind == TypeKind::Pointer && R.kind == TypeKind::Pointer) {
                return v_int(n->ty, static_cast<uint64_t>(L.ptr - R.ptr));
            }
            if (op == TokenKind::Minus && L.kind == TypeKind::Pointer && L.ptr != nullptr) {
                L.ptr -= static_cast<ptrdiff_t>(as_s(R, R.type));
                L.type = n->ty;
                return L;
            }
            if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
                op == TokenKind::GtEq) {
                if (op == TokenKind::Lt) {
                    return v_bool(L.ptr < R.ptr);
                }
                if (op == TokenKind::LtEq) {
                    return v_bool(L.ptr <= R.ptr);
                }
                if (op == TokenKind::Gt) {
                    return v_bool(L.ptr > R.ptr);
                }
                return v_bool(L.ptr >= R.ptr);
            }
        }
        if (L.kind == TypeKind::Enum || R.kind == TypeKind::Enum || is_int_enum(t)) {
            Type* bits_t = is_int_enum(t) ? t->elem : t;
            uint64_t a = as_u(L, bits_t != nullptr ? bits_t : L.type);
            uint64_t b = as_u(R, bits_t != nullptr ? bits_t : R.type);
            if (op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret) {
                Value v;
                v.kind = TypeKind::Enum;
                v.type = t;
                if (op == TokenKind::Amp) {
                    v.u = a & b;
                } else if (op == TokenKind::Pipe) {
                    v.u = a | b;
                } else {
                    v.u = a ^ b;
                }
                if (bits_t != nullptr) {
                    v.u &= int_mask(int_bits(bits_t));
                }
                return v;
            }
        }
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            bool eq = false;
            if (L.kind == TypeKind::Bool) {
                eq = L.b == R.b;
            } else if (L.kind == TypeKind::Str) {
                eq = show(L) == show(R);
            } else if (is_float(L.type)) {
                eq = L.f == R.f;
            } else if (L.kind == TypeKind::Enum || R.kind == TypeKind::Enum) {
                eq = L.u == R.u;
            } else {
                eq = as_u(L, L.type) == as_u(R, R.type);
            }
            return v_bool(op == TokenKind::EqEq ? eq : !eq);
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            Type* ct = L.type != nullptr ? L.type : R.type;
            return v_bool(cmp_num(L, R, ct, op));
        }
        Type* at = t != nullptr ? t : L.type;
        if (is_opt(at)) {
            at = at->elem;
        }
        Value r = arith(at, L, R, op);
        if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
            op == TokenKind::StarQuestion) {
            if (trapped) {
                trapped = false;
                trap = {};
                Value none;
                none.kind = TypeKind::Optional;
                none.type = t;
                none.present = false;
                return none;
            }
            r.present = true;
            r.kind = TypeKind::Optional;
            r.type = t;
        }
        return r;
    }

    Value eval_else(Node* n) {
        Value v = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        bool some = v.present && !(v.kind == TypeKind::Pointer && v.ptr == nullptr) &&
                    !(v.kind == TypeKind::Optional && !v.present);
        if (v.kind == TypeKind::Optional) {
            some = v.present;
        }
        if (is_ptr(n->left != nullptr ? n->left->ty : nullptr) &&
            n->left->ty->is_nullable) {
            some = v.ptr != nullptr;
        }
        if (some) {
            if (v.kind == TypeKind::Optional && n->ty != nullptr) {
                v.kind = n->ty->kind;
                v.type = n->ty;
            }
            return v;
        }
        return eval(n->right);
    }

    void run_catch_handler(Node* n, const Value& errv) {
        bool saved = in_catch;
        in_catch = true;
        Slot err;
        err.name = n->text;
        err.value.kind = TypeKind::ErrorVal;
        err.value.u = static_cast<uint64_t>(errv.err_code);
        err.value.str = errv.err_msg;
        if (!frames.empty()) {
            frames.back().slots.push_back(err);
        }
        recover_val = v_unit();
        recovered = false;
        exec(n->body);
        if (!frames.empty()) {
            frames.back().slots.pop_back();
        }
        in_catch = saved;
        if (recovered) {
            returning = false;
        }
    }

    Value eval_catch(Node* n) {
        Value v = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        if (!v.failed) {
            if (n->ty != nullptr) {
                v.kind = n->ty->kind;
                v.type = n->ty;
            }
            return v;
        }
        run_catch_handler(n, v);
        return recover_val;
    }

    bool match_pat(Node* pat, const Value& scrut, Type* st) {
        if (pat == nullptr) {
            return false;
        }
        if (pat->text == "_") {
            return true;
        }
        if (pat->text == "none") {
            if (scrut.kind == TypeKind::Optional) {
                return !scrut.present;
            }
            return scrut.ptr == nullptr;
        }
        if (st != nullptr && st->kind == TypeKind::Enum && pat->resolved != nullptr &&
            pat->resolved->kind == NodeKind::EnumCase) {
            uint64_t want = is_int_enum(st) ? enum_case_int(st->decl, pat->resolved)
                                            : static_cast<uint64_t>(enum_tag_of(st->decl, pat->resolved));
            if (scrut.u != want) {
                return false;
            }
            Node* p = pat->resolved->body;
            Node* b = pat->body;
            size_t i = 0;
            while (p != nullptr && b != nullptr) {
                if (b->text != "_") {
                    Slot s;
                    s.name = b->text;
                    if (i < scrut.fields.size()) {
                        s.value = scrut.fields[i];
                    }
                    frames.back().slots.push_back(s);
                }
                p = p->next;
                b = b->next;
                i++;
            }
            return true;
        }
        if (pat->text == "some") {
            if (scrut.kind == TypeKind::Optional && scrut.present) {
                if (pat->body != nullptr && !pat->body->text.empty()) {
                    Slot s;
                    s.name = pat->body->text;
                    s.value = scrut;
                    s.value.kind = st != nullptr && st->elem != nullptr ? st->elem->kind : scrut.kind;
                    s.value.type = st != nullptr ? st->elem : scrut.type;
                    frames.back().slots.push_back(s);
                }
                return true;
            }
            return false;
        }
        if (pat->left != nullptr && pat->left->kind == NodeKind::Literal) {
            Value lit = eval(pat->left);
            if (lit.kind == TypeKind::Bool) {
                return lit.b == scrut.b;
            }
            return as_u(lit, lit.type) == as_u(scrut, scrut.type);
        }
        return false;
    }

    Value eval_match(Node* n) {
        Value scrut = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
            size_t mark = frames.empty() ? 0 : frames.back().slots.size();
            bool hit = false;
            for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                if (match_pat(pat, scrut, n->left != nullptr ? n->left->ty : nullptr)) {
                    hit = true;
                    break;
                }
            }
            if (hit && arm->type != nullptr) {
                Value g = eval(arm->type);
                hit = g.b;
            }
            if (hit) {
                Value r = v_unit();
                if (n->kind == NodeKind::MatchExpr) {
                    r = eval(arm->body);
                } else {
                    exec(arm->body);
                }
                while (!frames.empty() && frames.back().slots.size() > mark) {
                    frames.back().slots.pop_back();
                }
                return r;
            }
            while (!frames.empty() && frames.back().slots.size() > mark) {
                frames.back().slots.pop_back();
            }
        }
        fail("non-exhaustive match");
        return v_unit();
    }

    Value eval_conv(Node* srcn, Type* dest, bool checked) {
        Value x = eval(srcn);
        if (trapped || dest == nullptr) {
            return x;
        }
        Type* src = srcn != nullptr ? srcn->ty : x.type;
        if (is_float(src) && is_int(dest)) {
            double a = x.f;
            int bits = int_bits(dest);
            if (checked) {
                if (a != a || a < static_cast<double>(int_min(dest)) ||
                    (is_signed_int(dest)
                         ? a > static_cast<double>(int_max_signed(bits))
                         : a < 0 || a > static_cast<double>(int_max_unsigned(bits)))) {
                    fail("integer conversion out of range");
                    return v_unit();
                }
                if (is_signed_int(dest)) {
                    return v_int(dest, static_cast<uint64_t>(static_cast<int64_t>(a)));
                }
                return v_int(dest, static_cast<uint64_t>(a));
            }
            if (a != a) {
                return v_int(dest, 0);
            }
            if (is_signed_int(dest)) {
                if (a <= static_cast<double>(int_min(dest))) {
                    return v_int(dest, static_cast<uint64_t>(int_min(dest)));
                }
                if (a >= static_cast<double>(int_max_signed(bits))) {
                    return v_int(dest, static_cast<uint64_t>(int_max_signed(bits)));
                }
                return v_int(dest, static_cast<uint64_t>(static_cast<int64_t>(a)));
            }
            if (a < 0) {
                return v_int(dest, 0);
            }
            if (a >= static_cast<double>(int_max_unsigned(bits))) {
                return v_int(dest, int_max_unsigned(bits));
            }
            return v_int(dest, static_cast<uint64_t>(a));
        }
        if (is_int(src) && is_float(dest)) {
            double a = is_signed_int(src) ? static_cast<double>(as_s(x, src))
                                          : static_cast<double>(as_u(x, src));
            return v_float(dest, a);
        }
        if (is_float(src) && is_float(dest)) {
            return v_float(dest, x.f);
        }
        if (is_int_enum(src) && is_int(dest)) {
            return v_int(dest, as_u(x, src->elem != nullptr ? src->elem : dest));
        }
        if (is_int(src) && is_int_enum(dest)) {
            uint64_t u = as_u(x, src);
            if (checked && dest->decl != nullptr) {
                bool ok = false;
                uint64_t next = 0;
                for (Node* c = dest->decl->body; c != nullptr; c = c->next) {
                    if (c->kind != NodeKind::EnumCase) {
                        continue;
                    }
                    uint64_t v = next;
                    if (c->left != nullptr && c->left->kind == NodeKind::Literal) {
                        ParsedInt p = parse_int_literal(c->left->text);
                        if (p.ok) {
                            v = p.value;
                        }
                    }
                    if (v == u) {
                        ok = true;
                        break;
                    }
                    next = v + 1;
                }
                if (!ok) {
                    fail("invalid enum value");
                    return v_unit();
                }
            }
            Value v;
            v.kind = TypeKind::Enum;
            v.type = dest;
            v.u = u;
            return v;
        }
        if (is_int(src) && is_int(dest)) {
            int64_t s = is_signed_int(src) ? as_s(x, src) : static_cast<int64_t>(as_u(x, src));
            int to_bits = int_bits(dest);
            if (checked) {
                if (is_signed_int(dest)) {
                    if (s < int_min(dest) || s > int_max_signed(to_bits)) {
                        fail("integer conversion out of range");
                        return v_unit();
                    }
                    return v_int(dest, static_cast<uint64_t>(s));
                }
                if (s < 0 || static_cast<uint64_t>(s) > int_max_unsigned(to_bits)) {
                    fail("integer conversion out of range");
                    return v_unit();
                }
                return v_int(dest, static_cast<uint64_t>(s));
            }
            return v_int(dest, static_cast<uint64_t>(s));
        }
        if ((src != nullptr && src->kind == TypeKind::Char) && is_int(dest)) {
            if (checked && is_signed_int(dest) &&
                x.u > static_cast<uint64_t>(int_max_signed(int_bits(dest)))) {
                fail("integer conversion out of range");
                return v_unit();
            }
            if (checked && is_unsigned_int(dest) && x.u > int_max_unsigned(int_bits(dest))) {
                fail("integer conversion out of range");
                return v_unit();
            }
            return v_int(dest, x.u);
        }
        if (is_ptr(dest)) {
            x.type = dest;
            x.kind = TypeKind::Pointer;
            return x;
        }
        x.type = dest;
        x.kind = dest->kind;
        return x;
    }

    Node* find_func(string_view name) {
        vector<Node*> mods = all_modules;
        if (mods.empty() && module != nullptr) {
            mods.push_back(module);
        }
        for (size_t i = 0; i < mods.size(); i++) {
            if (mods[i] == nullptr) {
                continue;
            }
            for (Node* d = mods[i]->body; d != nullptr; d = d->next) {
                if (d->kind == NodeKind::Func && d->text == name) {
                    return d;
                }
            }
        }
        return nullptr;
    }

    void load_globals() {
        vector<Node*> mods = all_modules;
        if (mods.empty() && module != nullptr) {
            mods.push_back(module);
        }
        for (size_t i = 0; i < mods.size(); i++) {
            if (mods[i] == nullptr) {
                continue;
            }
            for (Node* d = mods[i]->body; d != nullptr; d = d->next) {
                if (d->kind != NodeKind::Global && d->kind != NodeKind::Const) {
                    continue;
                }
                Slot s;
                s.name = d->text;
                s.decl = d;
                if (d->left != nullptr) {
                    s.value = eval(d->left);
                } else {
                    s.value = zero_of(d->ty);
                }
                if (d->ty != nullptr) {
                    s.value.type = d->ty;
                    s.value.kind = d->ty->kind;
                }
                globals.slots.push_back(s);
            }
        }
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
            if (p->ty != nullptr) {
                s.value.type = p->ty;
                s.value.kind = p->ty->kind;
            }
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
        if ((fn->flags & FlagFallible) != 0 && !result.failed) {
            result.failed = false;
            result.kind = TypeKind::Fallible;
        }
        return result;
    }

    Value eval_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
            return heap_alloc_value();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "assert") {
            Value c = eval(n->body != nullptr ? n->body->left : nullptr);
            if (trapped) {
                return v_unit();
            }
            if (!c.b) {
                string msg = "assert failed";
                if (n->body != nullptr && n->body->next != nullptr) {
                    Value m = eval(n->body->next->left);
                    if (m.kind == TypeKind::Str) {
                        msg = decode_string(m.str);
                    }
                }
                fail(msg);
            }
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            if (arg != nullptr && arg->kind == NodeKind::Formatted) {
                Value a = eval_formatted(arg);
                if (!trapped) {
                    output += show(a);
                    output += '\n';
                }
                return v_unit();
            }
            Value a = eval(arg);
            if (!trapped) {
                output += show(a);
                output += '\n';
            }
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "location") {
            Value v;
            v.kind = TypeKind::Struct;
            v.type = n->ty;
            v.fields.push_back(v_str(n->ty != nullptr ? string_view("t.lucb") : string_view("t.lucb")));
            Value line;
            line.kind = TypeKind::U32;
            line.u = n->span.line;
            v.fields.push_back(line);
            string fn = current_fn != nullptr ? string(current_fn->text) : string("answer");
            strings.push_back(fn);
            v.fields.push_back(v_str(strings.back()));
            return v;
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "format") {
            return eval_format(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            Value a = eval(n->body != nullptr ? n->body->left : nullptr);
            fail(show(a));
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "error") {
            Value code = eval(n->body != nullptr ? n->body->left : nullptr);
            Value msg = eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                           : nullptr);
            ret.failed = true;
            ret.kind = TypeKind::Fallible;
            ret.err_code = static_cast<int32_t>(as_s(code, code.type));
            err_storage = msg.kind == TypeKind::Str ? decode_string(msg.str) : show(msg);
            ret.err_msg = err_storage;
            returning = true;
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name &&
            (callee->text == "sizeof" || callee->text == "alignof")) {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            Type* t = arg != nullptr ? arg->ty : nullptr;
            uint64_t v = callee->text == "sizeof" ? static_cast<uint64_t>(type_size(t))
                                                  : static_cast<uint64_t>(type_align(t));
            return v_int(n->ty, v);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
            Node* tyarg = n->body != nullptr ? n->body->left : nullptr;
            Node* field = n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                        : nullptr;
            Type* t = tyarg != nullptr ? tyarg->ty : nullptr;
            string_view fname = field != nullptr ? field->text : string_view{};
            return v_int(n->ty, static_cast<uint64_t>(type_offset(t, fname)));
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
            Value v = v_enum_case(n->resolved, n->ty);
            for (Node* a = n->body; a != nullptr; a = a->next) {
                v.fields.push_back(eval(a->left));
            }
            return v;
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && n->ty != nullptr &&
            n->body != nullptr &&
            (n->resolved == nullptr ||
             (n->resolved->kind == NodeKind::Enum && is_int_enum(n->ty)))) {
            Type* dest = n->ty;
            if (is_int(dest) || is_float(dest) || dest->kind == TypeKind::Char ||
                is_int_enum(dest)) {
                return eval_conv(n->body->left, dest, true);
            }
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return eval_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Type* lt = callee->left != nullptr ? callee->left->ty : nullptr;
            if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
                n->resolved->kind == NodeKind::Func) {
                return call_func(n->resolved, nullptr, n->body);
            }
            Node* method = callee->resolved;
            Type* ot = callee->left != nullptr ? callee->left->ty : nullptr;
            if (is_ptr(ot) && ot->elem != nullptr) {
                ot = ot->elem;
            }
            if (ot != nullptr && ot->kind == TypeKind::Interface) {
                Value view = eval(callee->left);
                if (trapped || view.ptr == nullptr) {
                    fail("null interface");
                    return v_unit();
                }
                Value* obj = view.ptr;
                Type* ct = obj->type;
                Node* impl = nullptr;
                if (ct != nullptr && ct->decl != nullptr) {
                    impl = nullptr;
                    for (Node* m = ct->decl->body; m != nullptr; m = m->next) {
                        if (m->kind == NodeKind::Func && m->text == callee->text) {
                            impl = m;
                            break;
                        }
                    }
                }
                if (impl == nullptr) {
                    fail("unknown method");
                    return v_unit();
                }
                return call_func(impl, obj, n->body);
            }
            if (callee->text == "compare" && (method == nullptr || method->kind != NodeKind::Func)) {
                Value L = eval(callee->left);
                Value R = eval(n->body != nullptr ? n->body->left : nullptr);
                if (trapped) {
                    return v_unit();
                }
                int64_t cmp = 0;
                Type* lt = callee->left != nullptr ? callee->left->ty : L.type;
                if (is_float(lt) || L.kind == TypeKind::F32 || L.kind == TypeKind::F64) {
                    cmp = L.f < R.f ? -1 : L.f > R.f ? 1 : 0;
                } else if (L.kind == TypeKind::Str || (lt != nullptr && lt->kind == TypeKind::Str)) {
                    string a = decode_string(L.str);
                    string b = decode_string(R.str);
                    cmp = a < b ? -1 : a > b ? 1 : 0;
                } else {
                    int64_t lv = as_s(L, lt);
                    int64_t rv = as_s(R, n->body != nullptr && n->body->left != nullptr
                                             ? n->body->left->ty
                                             : R.type);
                    cmp = lv < rv ? -1 : lv > rv ? 1 : 0;
                }
                return v_i64(cmp);
            }
            if (method == nullptr || method->kind != NodeKind::Func) {
                fail("unknown method");
                return v_unit();
            }
            if ((method->flags & FlagStatic) != 0) {
                Node* owner = lt != nullptr ? lt->decl : nullptr;
                if (method->text == "over" && owner != nullptr && owner->text == "FixedBuffer") {
                    Value buf = eval(n->body != nullptr ? n->body->left : nullptr);
                    if (trapped) {
                        return v_unit();
                    }
                    Value fb = zero_of(n->ty);
                    fb.kind = TypeKind::Struct;
                    fb.type = n->ty;
                    fb.fields.clear();
                    Value data;
                    data.kind = TypeKind::Pointer;
                    data.ptr = buf.ptr != nullptr ? buf.ptr : buf.fields.data();
                    fb.fields.push_back(data);
                    Value cap;
                    cap.kind = TypeKind::Usize;
                    cap.u = buf.length != 0 ? buf.length : buf.fields.size();
                    if (buf.kind == TypeKind::Array && buf.type != nullptr) {
                        cap.u = buf.type->length;
                    }
                    fb.fields.push_back(cap);
                    Value used;
                    used.kind = TypeKind::Usize;
                    used.u = 0;
                    fb.fields.push_back(used);
                    return fb;
                }
                return call_func(method, nullptr, n->body);
            }
            Value* recv = lvalue(callee->left);
            if (recv == nullptr) {
                Value tmp = eval(callee->left);
                return call_func(method, &tmp, n->body);
            }
            return call_func(method, recv, n->body);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::ExternFunc) {
            return eval_extern(n, n->resolved);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            return call_func(n->resolved, nullptr, n->body);
        }
        fail("unknown call");
        return v_unit();
    }

    string extern_symbol(Node* fn) {
        if (fn == nullptr) {
            return {};
        }
        if (fn->left != nullptr && fn->left->kind == NodeKind::Literal) {
            string s = string(fn->left->text);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                return s.substr(1, s.size() - 2);
            }
            return s;
        }
        return string(fn->text);
    }

    string cstr_text(const Value& v) {
        if (v.kind == TypeKind::Str || v.kind == TypeKind::CStr) {
            return decode_string(v.str);
        }
        return {};
    }

    string interp_printf(const string& fmt, const vector<Value>& args) {
        string out;
        size_t ai = 0;
        for (size_t i = 0; i < fmt.size(); i++) {
            if (fmt[i] != '%' || i + 1 >= fmt.size()) {
                out += fmt[i];
                continue;
            }
            i++;
            if (fmt[i] == '%') {
                out += '%';
                continue;
            }
            while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'z' || fmt[i] == 'h')) {
                i++;
            }
            if (i >= fmt.size() || ai >= args.size()) {
                break;
            }
            const Value& a = args[ai++];
            char spec = fmt[i];
            char buf[64];
            if (spec == 'd' || spec == 'i') {
                snprintf(buf, sizeof(buf), "%d", static_cast<int>(as_s(a, a.type)));
                out += buf;
            } else if (spec == 'u') {
                snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(as_u(a, a.type)));
                out += buf;
            } else if (spec == 's') {
                out += cstr_text(a);
            } else if (spec == 'c') {
                out += static_cast<char>(as_u(a, a.type));
            } else if (spec == 'f' || spec == 'g') {
                snprintf(buf, sizeof(buf), "%g", a.f);
                out += buf;
            }
        }
        return out;
    }

    Value eval_extern(Node* n, Node* fn) {
        string name = extern_symbol(fn);
        vector<Value> args;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            args.push_back(eval(a->left));
            if (trapped) {
                return v_unit();
            }
        }
        Type* rt = n->ty != nullptr ? n->ty : fn->ty;
        Value r = v_unit();
        r.type = rt;
        if (name == "abs") {
            int64_t v = args.empty() ? 0 : as_s(args[0], args[0].type);
            if (v < 0) {
                v = -v;
            }
            return v_int(rt, static_cast<uint64_t>(v));
        }
        if (name == "strlen") {
            string s = args.empty() ? string() : cstr_text(args[0]);
            return v_int(rt, s.size());
        }
        if (name == "printf") {
            string fmt = args.empty() ? string() : cstr_text(args[0]);
            vector<Value> rest;
            for (size_t i = 1; i < args.size(); i++) {
                rest.push_back(args[i]);
            }
            string printed = interp_printf(fmt, rest);
            output += printed;
            return v_int(rt, printed.size());
        }
        if (name == "lb_null_probe") {
            r.kind = TypeKind::Pointer;
            r.ptr = nullptr;
        } else {
            fail("unknown extern `" + name + "`");
            return v_unit();
        }
        if (needs_null_foreign(rt)) {
            bool is_null = r.ptr == nullptr;
            if (rt->kind == TypeKind::CStr) {
                is_null = r.str.data() == nullptr;
            }
            if (is_null) {
                fail("null_foreign");
                return v_unit();
            }
        }
        return r;
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
            defers.emplace_back();
            for (Node* s = n->body; s != nullptr; s = s->next) {
                exec(s);
                if (trapped || returning || breaking || continuing) {
                    break;
                }
            }
            if (!defers.empty()) {
                vector<Deferred> d = defers.back();
                defers.pop_back();
                if (!trapped) {
                    bool failing = returning && ret.failed;
                    for (int i = static_cast<int>(d.size()) - 1; i >= 0; i--) {
                        if (d[static_cast<size_t>(i)].err_only && !failing) {
                            continue;
                        }
                        Node* dn = d[static_cast<size_t>(i)].n;
                        Value dv = eval(dn->left);
                        if (dv.failed && dn->body != nullptr) {
                            run_catch_handler(dn, dv);
                        }
                    }
                }
            }
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Slot s;
            s.name = n->text;
            if (n->left != nullptr) {
                if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                    n->left->kind == NodeKind::Name) {
                    Value* src = lvalue(n->left);
                    s.value.kind = TypeKind::Span;
                    s.value.type = n->ty;
                    if (src != nullptr) {
                        s.value.ptr = src->ptr != nullptr ? src->ptr : src->fields.data();
                        s.value.length = src->length != 0 ? src->length : src->fields.size();
                        s.value.str = src->str;
                    }
                } else {
                    s.value = eval(n->left);
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                        s.value.kind == TypeKind::Array) {
                        s.value.ptr = s.value.ptr;
                        s.value.kind = TypeKind::Span;
                        s.value.type = n->ty;
                    }
                    if (n->ty != nullptr) {
                        s.value.type = n->ty;
                        s.value.kind = n->ty->kind;
                    }
                }
            } else {
                s.value = zero_of(n->ty);
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
                Type* dt = n->left != nullptr ? n->left->ty : dst->type;
                src.kind = dst->kind;
                src.type = dt != nullptr ? dt : dst->type;
                if (dt != nullptr) {
                    src.kind = dt->kind;
                }
                *dst = src;
                break;
            }
            TokenKind op = TokenKind::Plus;
            if (n->op == TokenKind::PlusEq) {
                op = TokenKind::Plus;
            } else if (n->op == TokenKind::MinusEq) {
                op = TokenKind::Minus;
            } else if (n->op == TokenKind::StarEq) {
                op = TokenKind::Star;
            } else if (n->op == TokenKind::SlashSlashEq) {
                op = TokenKind::SlashSlash;
            } else if (n->op == TokenKind::PercentEq) {
                op = TokenKind::Percent;
            } else if (n->op == TokenKind::PlusPercentEq) {
                op = TokenKind::PlusPercent;
            } else if (n->op == TokenKind::MinusPercentEq) {
                op = TokenKind::MinusPercent;
            } else if (n->op == TokenKind::StarPercentEq) {
                op = TokenKind::StarPercent;
            } else if (n->op == TokenKind::PlusPipeEq) {
                op = TokenKind::PlusPipe;
            } else if (n->op == TokenKind::MinusPipeEq) {
                op = TokenKind::MinusPipe;
            } else if (n->op == TokenKind::StarPipeEq) {
                op = TokenKind::StarPipe;
            } else if (n->op == TokenKind::AmpEq) {
                op = TokenKind::Amp;
            } else if (n->op == TokenKind::PipeEq) {
                op = TokenKind::Pipe;
            } else if (n->op == TokenKind::CaretEq) {
                op = TokenKind::Caret;
            } else if (n->op == TokenKind::LtLtEq) {
                op = TokenKind::LtLt;
            } else if (n->op == TokenKind::GtGtEq) {
                op = TokenKind::GtGt;
            } else {
                fail("unsupported assignment");
                return;
            }
            Value r = arith(n->left->ty, *dst, src, op);
            if (!trapped) {
                *dst = r;
            }
            break;
        }
        case NodeKind::If: {
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Value v = eval(let != nullptr ? let->left : nullptr);
                if (trapped) {
                    return;
                }
                bool some = v.kind == TypeKind::Optional ? v.present : v.ptr != nullptr;
                if (some) {
                    Slot s;
                    s.name = let != nullptr ? let->text : string_view{};
                    s.value = v;
                    if (v.kind == TypeKind::Optional && v.type != nullptr && v.type->elem != nullptr) {
                        s.value.kind = v.type->elem->kind;
                        s.value.type = v.type->elem;
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                } else {
                    exec(n->right);
                }
            } else {
                Value c = eval(n->left);
                if (trapped) {
                    return;
                }
                if (c.b) {
                    exec(n->body);
                } else {
                    exec(n->right);
                }
            }
            break;
        }
        case NodeKind::While:
            while (!trapped && !returning && !breaking) {
                continuing = false;
                if (n->flags & FlagIfLet) {
                    Node* let = n->left;
                    Value v = eval(let != nullptr ? let->left : nullptr);
                    if (trapped) {
                        return;
                    }
                    bool some = v.kind == TypeKind::Optional ? v.present : v.ptr != nullptr;
                    if (!some) {
                        break;
                    }
                    Slot s;
                    s.name = let != nullptr ? let->text : string_view{};
                    s.value = v;
                    if (v.kind == TypeKind::Optional && v.type != nullptr && v.type->elem != nullptr) {
                        s.value.kind = v.type->elem->kind;
                        s.value.type = v.type->elem;
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                } else {
                    Value c = eval(n->left);
                    if (trapped || !c.b) {
                        break;
                    }
                    exec(n->body);
                }
                if (breaking) {
                    if (jump_label.empty() || jump_label == n->text) {
                        breaking = false;
                    }
                    break;
                }
                if (continuing) {
                    if (jump_label.empty() || jump_label == n->text) {
                        continuing = false;
                        continue;
                    }
                    break;
                }
            }
            break;
        case NodeKind::Return:
            ret = n->left != nullptr ? eval(n->left) : v_unit();
            returning = true;
            break;
        case NodeKind::Break:
            breaking = true;
            jump_label = n->text;
            break;
        case NodeKind::Continue:
            continuing = true;
            jump_label = n->text;
            break;
        case NodeKind::Defer:
        case NodeKind::Errdefer: {
            Deferred d;
            d.n = n;
            d.err_only = n->kind == NodeKind::Errdefer;
            if (!defers.empty()) {
                defers.back().push_back(d);
            } else {
                eval(n->left);
            }
            break;
        }
        case NodeKind::Recover:
            recover_val = n->left != nullptr ? eval(n->left) : v_unit();
            recovered = true;
            returning = true;
            break;
        case NodeKind::Match:
            eval_match(n);
            break;
        case NodeKind::For: {
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                Value a = eval(n->right->left);
                Value b = eval(n->right->right);
                if (trapped) {
                    return;
                }
                int64_t start = as_s(a, a.type);
                int64_t end = as_s(b, b.type);
                bool closed = n->right->op == TokenKind::DotDotEq;
                for (int64_t i = start; !trapped && !returning && !breaking &&
                                        (closed ? i <= end : i < end);
                     i++) {
                    continuing = false;
                    Slot s;
                    s.name = n->text;
                    s.value = v_int(n->ty, static_cast<uint64_t>(i));
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                    if (breaking) {
                        if (jump_label.empty() || jump_label == n->text) {
                            breaking = false;
                        }
                        break;
                    }
                    if (continuing) {
                        if (jump_label.empty() || jump_label == n->text) {
                            continuing = false;
                            continue;
                        }
                        break;
                    }
                }
                break;
            }
            Value it = eval(n->right);
            if (trapped) {
                return;
            }
            size_t len = it.length != 0 ? it.length : it.fields.size();
            if (it.kind == TypeKind::Array && it.type != nullptr) {
                len = static_cast<size_t>(it.type->length);
            }
            if (it.kind == TypeKind::Str) {
                string d = decode_string(it.str);
                len = d.size();
                for (size_t i = 0; i < len && !trapped && !returning; i++) {
                    Slot s;
                    s.name = n->text;
                    s.value = v_int(n->ty, static_cast<unsigned char>(d[static_cast<size_t>(i)]));
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Char) {
                        s.value.kind = TypeKind::Char;
                        s.value.u = static_cast<unsigned char>(d[i]);
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                }
                break;
            }
            for (size_t i = 0; i < len && !trapped && !returning; i++) {
                Slot s;
                s.name = n->text;
                Value* elems = it.ptr;
                Value elem;
                if (elems != nullptr) {
                    elem = elems[i];
                } else if (i < it.fields.size()) {
                    elem = it.fields[i];
                }
                if (n->flags & FlagByPtr) {
                    Value p;
                    p.kind = TypeKind::Pointer;
                    p.type = n->ty;
                    if (elems != nullptr) {
                        p.ptr = elems + static_cast<ptrdiff_t>(i);
                    } else if (i < it.fields.size()) {
                        p.ptr = &it.fields[i];
                    }
                    s.value = p;
                } else {
                    s.value = elem;
                }
                frames.back().slots.push_back(s);
                exec(n->body);
                frames.back().slots.pop_back();
            }
            break;
        }
        case NodeKind::ExprStmt:
            eval(n->left);
            break;
        case NodeKind::Free:
            eval(n->left);
            if (n->right != nullptr) {
                as_alloc(n->right);
            }
            break;
        case NodeKind::With: {
            Value saved = current_alloc;
            current_alloc = as_alloc(n->left);
            exec(n->body);
            current_alloc = saved;
            break;
        }
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
    ip.all_modules.push_back(module);
    ip.init_memory();
    ip.load_globals();
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
    if (v.failed) {
        result.trapped = true;
        result.trap = string(v.err_msg);
        return result;
    }
    result.ok = true;
    if (v.kind == TypeKind::I64 || v.kind == TypeKind::Fallible ||
        (v.type != nullptr && (v.type->kind == TypeKind::I64 || is_fail(v.type)))) {
        result.has_answer = true;
        result.answer = as_s(v, v.type);
    }
    return result;
}

TestRun eval_tests(const vector<Node*>& modules) {
    TestRun run;
    for (size_t mi = 0; mi < modules.size(); mi++) {
        Node* mod = modules[mi];
        if (mod == nullptr) {
            continue;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind != NodeKind::Test) {
                continue;
            }
            Interp ip;
            ip.module = modules.empty() ? mod : modules[0];
            ip.all_modules = modules;
            ip.init_memory();
            ip.load_globals();
            ip.exec(d->body);
            string title = string(d->text);
            if (title.size() >= 2 && title.front() == '"') {
                title = title.substr(1, title.size() - 2);
            }
            if (ip.trapped) {
                run.failed++;
                run.output += "FAIL  " + title + "\n      trap: " + ip.trap + "\n";
            } else if (ip.returning && ip.ret.failed) {
                run.failed++;
                run.output += "FAIL  " + title + "\n      error: " + string(ip.ret.err_msg) + "\n";
            } else {
                run.passed++;
                run.output += "ok    " + title + "\n";
            }
            run.output += ip.output;
        }
    }
    return run;
}

int32_t eval_main(const vector<Node*>& modules, Node* entry, const vector<string>& args,
                  EvalResult* result) {
    Interp ip;
    ip.module = entry != nullptr ? entry : (modules.empty() ? nullptr : modules[0]);
    ip.all_modules = modules;
    if (ip.module == nullptr) {
        if (result != nullptr) {
            result->trapped = true;
            result->trap = "no module";
        }
        return 1;
    }
    ip.init_memory();
    ip.load_globals();
    Node* main_fn = nullptr;
    for (Node* d = ip.module->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func && d->text == "main") {
            main_fn = d;
            break;
        }
    }
    if (main_fn == nullptr) {
        if (result != nullptr) {
            result->trapped = true;
            result->trap = "no `main` function";
        }
        return 1;
    }
    vector<Value> argv;
    for (size_t i = 0; i < args.size(); i++) {
        argv.push_back(v_str(args[i]));
        argv.back().length = args[i].size();
    }
    ip.storage.push_back(std::move(argv));
    Value span;
    span.kind = TypeKind::Span;
    span.type = main_fn->right != nullptr ? main_fn->right->ty : nullptr;
    span.ptr = ip.storage.back().data();
    span.length = ip.storage.back().size();
    Frame frame;
    Slot s;
    s.name = main_fn->right != nullptr ? main_fn->right->text : string_view("arguments");
    s.value = span;
    frame.slots.push_back(s);
    ip.frames.push_back(frame);
    ip.returning = false;
    ip.exec(main_fn->body);
    ip.frames.pop_back();
    if (result != nullptr) {
        result->output = ip.output;
        if (ip.trapped) {
            result->trapped = true;
            result->trap = ip.trap;
            return 1;
        }
        result->ok = true;
    }
    if (ip.trapped) {
        return 1;
    }
    if (ip.returning && ip.ret.failed) {
        if (result != nullptr) {
            result->trapped = true;
            result->trap = string(ip.ret.err_msg);
        }
        return 1;
    }
    if (ip.returning) {
        return static_cast<int32_t>(as_s(ip.ret, ip.ret.type));
    }
    return 0;
}

} // namespace lucb
