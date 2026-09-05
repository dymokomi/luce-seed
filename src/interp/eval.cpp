#include "interp/interp_impl.h"

#include "support/literal.h"
#include <cstdio>

namespace lucb {

auto Interp::make_array(Type* t, vector<Value> elems) -> Value {
        storage.push_back(std::move(elems));
        Value v;
        v.kind = TypeKind::Array;
        v.type = t;
        v.ptr = storage.back().data();
        v.length = storage.back().size();
        return v;
    }

auto Interp::zero_of(Type* t) -> Value {
        if (t != nullptr && t->kind == TypeKind::Tuple) {
            Value v;
            v.kind = TypeKind::Tuple;
            v.type = t;
            for (int i = 0; i < t->ntargs; i++) {
                v.fields.push_back(zero_of(t->args[i]));
            }
            return v;
        }
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

auto Interp::enum_case_int(Node* en, Node* cse) -> uint64_t {
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

auto Interp::enum_tag_of(Node* en, Node* cse) -> int {
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

auto Interp::v_enum_case(Node* cse, Type* t) -> Value {
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

auto Interp::lvalue(Node* n) -> Value* {
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
            if (n->text == "length" || n->text == "data" || n->text == "bytes") {
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
                if (i < base->fields.size()) {
                    return &base->fields[i];
                }
                return nullptr;
            }
        }
        fail("not an lvalue");
        return nullptr;
    }

auto Interp::decode_string(string_view tok) -> string {
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

auto Interp::show(const Value& v) -> string {
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

auto Interp::eval(Node* n) -> Value {
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

auto Interp::eval_case_value(Node* n) -> Value {
        Value v = v_enum_case(n->resolved, n->ty);
        if (n->body != nullptr && n->resolved != nullptr) {
            for (Node* a = n->body; a != nullptr; a = a->next) {
                v.fields.push_back(eval(a->left != nullptr ? a->left : a));
            }
        }
        return v;
    }

auto Interp::eval_member(Node* n) -> Value {
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
            string d = decode_string(obj.str);
            vector<Value> elems;
            elems.resize(d.size());
            Type* et = n->ty != nullptr ? n->ty->elem : nullptr;
            for (size_t i = 0; i < d.size(); i++) {
                elems[i] = v_int(et, static_cast<unsigned char>(d[i]));
            }
            storage.push_back(std::move(elems));
            Value v;
            v.kind = TypeKind::Span;
            v.type = n->ty;
            v.ptr = storage.back().empty() ? nullptr : storage.back().data();
            v.length = storage.back().size();
            return v;
        }
        Value* p = lvalue(n);
        if (p == nullptr) {
            return v_unit();
        }
        return *p;
    }

auto Interp::eval_index(Node* n) -> Value {
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

auto Interp::eval_slice(Node* n) -> Value {
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

auto Interp::eval_array_lit(Node* n) -> Value {
        vector<Value> elems;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            elems.push_back(eval(e));
            if (trapped) {
                return v_unit();
            }
        }
        return make_array(n->ty, std::move(elems));
    }

auto Interp::eval_formatted(Node* n) -> Value {
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

auto Interp::eval_format(Node* n) -> Value {
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

auto Interp::eval_span_make(Node* n) -> Value {
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

auto Interp::heap_alloc_value() -> Value {
        Value a;
        a.kind = TypeKind::Allocator;
        a.ptr = nullptr;
        a.u = 0;
        return a;
    }

auto Interp::fail_exhausted(Type* fail_ty) -> Value {
        Value v;
        v.kind = TypeKind::Fallible;
        v.type = fail_ty;
        v.failed = true;
        v.err_code = 1;
        v.err_msg = "memory.exhausted";
        return v;
    }

auto Interp::ok_payload(Value payload, Type* fail_ty) -> Value {
        payload.failed = false;
        payload.kind = TypeKind::Fallible;
        payload.type = fail_ty;
        return payload;
    }

auto Interp::as_alloc(Node* n) -> Value {
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

auto Interp::bump_fixed(Value* fb, size_t size, size_t align) -> bool {
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

auto Interp::take_bytes(const Value& a, size_t size, size_t align) -> bool {
        if (size == 0) {
            return true;
        }
        if (a.kind == TypeKind::Allocator && a.ptr != nullptr) {
            return bump_fixed(a.ptr, size, align);
        }
        return true;
    }

auto Interp::eval_new(Node* n) -> Value {
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

auto Interp::eval_alloc(Node* n) -> Value {
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

auto Interp::eval_unary(Node* n) -> Value {
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

auto Interp::cmp_num(const Value& L, const Value& R, Type* t, TokenKind op) -> bool {
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

auto Interp::arith(Type* t, const Value& L, const Value& R, TokenKind op) -> Value {
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

auto Interp::eval_binary(Node* n) -> Value {
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

auto Interp::eval_else(Node* n) -> Value {
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

auto Interp::run_catch_handler(Node* n, const Value& errv) -> void {
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

auto Interp::eval_catch(Node* n) -> Value {
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

auto Interp::match_pat(Node* pat, const Value& scrut, Type* st) -> bool {
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

auto Interp::eval_match(Node* n) -> Value {
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

auto Interp::eval_conv(Node* srcn, Type* dest, bool checked) -> Value {
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

auto Interp::eval_call(Node* n) -> Value {
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
                if (callee->text == "fence" || callee->text == "pause" ||
                    callee->text == "yield" || callee->text == "sleep") {
                    return v_unit();
                }
                if (callee->text == "current") {
                    Value h;
                    h.kind = TypeKind::Struct;
                    h.type = n->ty;
                    Value id;
                    id.kind = TypeKind::Usize;
                    id.u = 1;
                    h.fields.push_back(id);
                    return h;
                }
                if (callee->text == "spawn") {
                    Node* entry = n->body != nullptr ? n->body->left : nullptr;
                    Node* fn = entry != nullptr ? entry->resolved : nullptr;
                    Node* ctxn = n->body != nullptr && n->body->next != nullptr
                                     ? n->body->next
                                     : nullptr;
                    if (fn != nullptr) {
                        call_func(fn, nullptr, ctxn);
                    }
                    Value h;
                    h.kind = TypeKind::Struct;
                    h.type = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                    Value id;
                    id.kind = TypeKind::Usize;
                    id.u = 1;
                    h.fields.push_back(id);
                    return ok_payload(h, n->ty);
                }
                return call_func(n->resolved, nullptr, n->body);
            }
            Node* method = callee->resolved;
            Type* ot = callee->left != nullptr ? callee->left->ty : nullptr;
            if (is_ptr(ot) && ot->elem != nullptr) {
                ot = ot->elem;
            }
            if (is_atomic(callee->left != nullptr ? callee->left->ty : nullptr) || is_atomic(ot)) {
                Type* at = is_atomic(callee->left->ty) ? callee->left->ty : ot;
                Type* elem = at->elem;
                Value* slot = lvalue(callee->left);
                if (slot == nullptr) {
                    fail("atomic needs an lvalue");
                    return v_unit();
                }
                Value arg = n->body != nullptr && callee->text != "load" ? eval(n->body->left)
                                                                        : v_unit();
                if (callee->text == "load") {
                    Value v = *slot;
                    v.type = elem;
                    v.kind = elem != nullptr ? elem->kind : v.kind;
                    return v;
                }
                if (callee->text == "store") {
                    arg.type = elem;
                    if (elem != nullptr) {
                        arg.kind = elem->kind;
                    }
                    *slot = arg;
                    return v_unit();
                }
                if (callee->text == "wait") {
                    return v_unit();
                }
                if (callee->text == "wake") {
                    return v_unit();
                }
                if (callee->text == "cas") {
                    Value exp = n->body != nullptr ? eval(n->body->left) : v_unit();
                    Value des = n->body != nullptr && n->body->next != nullptr
                                    ? eval(n->body->next->left)
                                    : v_unit();
                    bool ok = as_u(*slot, elem) == as_u(exp, elem);
                    Value obs = *slot;
                    if (ok) {
                        des.type = elem;
                        if (elem != nullptr) {
                            des.kind = elem->kind;
                        }
                        *slot = des;
                    }
                    Value tup;
                    tup.kind = TypeKind::Tuple;
                    tup.type = n->ty;
                    Value b;
                    b.kind = TypeKind::Bool;
                    b.b = ok;
                    tup.fields.push_back(b);
                    obs.type = elem;
                    if (elem != nullptr) {
                        obs.kind = elem->kind;
                    }
                    tup.fields.push_back(obs);
                    return tup;
                }
                Value prev = *slot;
                prev.type = elem;
                if (elem != nullptr) {
                    prev.kind = elem->kind;
                }
                TokenKind op = TokenKind::PlusPercent;
                if (callee->text == "sub") {
                    op = TokenKind::MinusPercent;
                } else if (callee->text == "set") {
                    op = TokenKind::Pipe;
                } else if (callee->text == "clear") {
                    op = TokenKind::Amp;
                    arg.u = ~arg.u;
                } else if (callee->text == "flip") {
                    op = TokenKind::Caret;
                } else if (callee->text == "swap") {
                    *slot = arg;
                    return prev;
                } else if (callee->text == "max" || callee->text == "min") {
                    uint64_t a = as_u(*slot, elem);
                    uint64_t b = as_u(arg, elem);
                    bool take = callee->text == "max" ? a >= b : a <= b;
                    if (!take) {
                        arg.type = elem;
                        if (elem != nullptr) {
                            arg.kind = elem->kind;
                        }
                        *slot = arg;
                    }
                    return prev;
                }
                Value r = arith(elem, *slot, arg, op);
                if (!trapped) {
                    *slot = r;
                }
                return prev;
            }
            if (ot != nullptr && ot->kind == TypeKind::Struct && ot->name == "Handle" &&
                callee->text == "join") {
                Value ok;
                ok.kind = TypeKind::Fallible;
                ok.failed = false;
                ok.type = n->ty;
                return ok;
            }
            if (ot != nullptr && ot->kind == TypeKind::Struct && ot->name == "Handle" &&
                callee->text == "detach") {
                return v_unit();
            }
            if (ot != nullptr && ot->kind == TypeKind::Struct &&
                (ot->name == "Mutex" || ot->name == "Condition" || ot->name == "Once" ||
                 ot->name == "Semaphore")) {
                Value* slot = lvalue(callee->left);
                if (slot != nullptr && slot->kind == TypeKind::Pointer) {
                    slot = slot->ptr;
                }
                if (slot == nullptr) {
                    fail("sync needs an lvalue");
                    return v_unit();
                }
                if (ot->name == "Mutex" && callee->text == "lock") {
                    if (slot->u != 0) {
                        fail("mutex locked");
                        return v_unit();
                    }
                    slot->u = 1;
                    return v_unit();
                }
                if (ot->name == "Mutex" && callee->text == "unlock") {
                    slot->u = 0;
                    return v_unit();
                }
                if (ot->name == "Mutex" && callee->text == "try") {
                    if (slot->u != 0) {
                        return v_bool(false);
                    }
                    slot->u = 1;
                    return v_bool(true);
                }
                if (ot->name == "Condition" && callee->text == "wait") {
                    Value mu = n->body != nullptr ? eval(n->body->left) : v_unit();
                    if (trapped) {
                        return v_unit();
                    }
                    if (mu.ptr == nullptr) {
                        fail("null pointer");
                        return v_unit();
                    }
                    mu.ptr->u = 0;
                    mu.ptr->u = 1;
                    return v_unit();
                }
                if (ot->name == "Condition" &&
                    (callee->text == "signal" || callee->text == "broadcast")) {
                    slot->u += 1;
                    return v_unit();
                }
                if (ot->name == "Once" && callee->text == "run") {
                    if (slot->u == 2) {
                        return v_unit();
                    }
                    if (slot->u != 0) {
                        fail("once running");
                        return v_unit();
                    }
                    slot->u = 1;
                    Node* entry = n->body != nullptr ? n->body->left : nullptr;
                    Node* fn = entry != nullptr ? entry->resolved : nullptr;
                    if (fn != nullptr) {
                        call_func(fn, nullptr, nullptr);
                    }
                    slot->u = 2;
                    return v_unit();
                }
                if (ot->name == "Semaphore" && callee->text == "acquire") {
                    if (slot->u == 0) {
                        fail("semaphore empty");
                        return v_unit();
                    }
                    slot->u -= 1;
                    return v_unit();
                }
                if (ot->name == "Semaphore" && callee->text == "release") {
                    slot->u += 1;
                    return v_unit();
                }
                fail("unknown method");
                return v_unit();
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

auto Interp::extern_symbol(Node* fn) -> string {
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

auto Interp::cstr_text(const Value& v) -> string {
        if (v.kind == TypeKind::Str || v.kind == TypeKind::CStr) {
            return decode_string(v.str);
        }
        return {};
    }

auto Interp::interp_printf(const string& fmt, const vector<Value>& args) -> string {
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

auto Interp::eval_extern(Node* n, Node* fn) -> Value {
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

auto Interp::eval_ctor(Node* n, Node* st) -> Value {
        Value v = v_zero(st->ty);
        if (st == nullptr) {
            return v;
        }
        int i = 0;
        for (Node* f = st->body; f != nullptr; f = f->next) {
            if (f->kind != NodeKind::Field) {
                continue;
            }
            Node* provided = nullptr;
            for (Node* a = n != nullptr ? n->body : nullptr; a != nullptr; a = a->next) {
                if (a->text == f->text) {
                    provided = a;
                    break;
                }
            }
            if (provided != nullptr && provided->left != nullptr) {
                if (i < static_cast<int>(v.fields.size())) {
                    v.fields[static_cast<size_t>(i)] = eval(provided->left);
                }
            } else if (f->left != nullptr) {
                if (i < static_cast<int>(v.fields.size())) {
                    v.fields[static_cast<size_t>(i)] = eval(f->left);
                }
            }
            if (trapped) {
                return v_unit();
            }
            i++;
        }
        return v;
    }

} // namespace lucb
