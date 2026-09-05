//==============================================================================================
//
//   interp/eval - The expression walker of the oracle
//
//   DESCRIPTION:
//       Values, places, and the core of `eval`: literals, names, members, indexing, slicing,
//       array literals, `else`, `catch`, and `match`. The oracle models memory as typed
//       values rather than bytes; DESIGN.md lists what that leaves to the binary.
//
//==============================================================================================

#include "interp/interp_impl.h"

#include "support/literal.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

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

auto Interp::copy_value(const Value& v) -> Value {
    if (v.kind == TypeKind::Array || (v.type != nullptr && v.type->kind == TypeKind::Array)) {
        vector<Value> elems;
        size_t n = v.length != 0 ? v.length : v.fields.size();
        if (v.type != nullptr && v.type->length != 0) {
            n = static_cast<size_t>(v.type->length);
        }
        elems.reserve(n);
        if (v.ptr != nullptr) {
            for (size_t i = 0; i < n; i++) {
                elems.push_back(copy_value(v.ptr[i]));
            }
        } else {
            for (size_t i = 0; i < v.fields.size(); i++) {
                elems.push_back(copy_value(v.fields[i]));
            }
        }
        return make_array(v.type, std::move(elems));
    }
    Value c = v;
    if (v.kind == TypeKind::Struct || v.kind == TypeKind::Tuple) {
        c.fields.clear();
        for (size_t i = 0; i < v.fields.size(); i++) {
            c.fields.push_back(copy_value(v.fields[i]));
        }
    }
    return c;
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
            Type* raw = n->left != nullptr ? n->left->ty : nullptr;
            if (is_ptr(raw) && raw->elem != nullptr) {
                raw = raw->elem;
            }
            if (raw != nullptr && (raw->kind == TypeKind::Str || is_span(raw) || is_array(raw))) {
                return nullptr;
            }
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
    Value v = eval_uncast(n);
    if (n != nullptr && is_opt(n->ty) && v.kind != TypeKind::Optional) {
        v.present = true;
        v.kind = TypeKind::Optional;
        v.type = n->ty;
    }
    return v;
}

auto Interp::eval_uncast(Node* n) -> Value {
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
            ParsedInt p;
            if ((n->flags & FlagLiteralCached) != 0) {
                p.ok = true;
                p.value = n->cached;
            } else {
                p = parse_int_literal(n->text);
                n->cached = p.value;
                n->flags |= FlagLiteralCached;
            }
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
        if (n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Func || n->resolved->kind == NodeKind::ExternFunc)) {
            Value v;
            v.kind = TypeKind::Func;
            v.type = n->ty;
            v.fn = n->resolved;
            return v;
        }
        {
            Value* p = lvalue(n);
            if (p == nullptr) {
                return v_unit();
            }
            Value v = *p;
            if (n->ty != nullptr && n->ty->kind == TypeKind::Interface &&
                v.kind == TypeKind::Pointer) {
                v.kind = TypeKind::Interface;
                v.type = n->ty;
            }
            return v;
        }
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
    case NodeKind::Lambda: {
        Value v;
        v.kind = TypeKind::Func;
        v.type = n->ty;
        v.fn = n->resolved;
        return v;
    }
    case NodeKind::Tuple: {
        Value v;
        v.kind = TypeKind::Tuple;
        v.type = n->ty;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            v.fields.push_back(eval(e));
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
        return eval_conv(n->left,
                         n->type != nullptr && n->type->ty != nullptr ? n->type->ty : n->ty, false);
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
    case NodeKind::Return: {
        // `return try f()`: the try may already have set a failing result.
        Value value = n->left != nullptr ? eval(n->left) : v_unit();
        if (!(returning && ret.failed)) {
            ret = value;
        }
        returning = true;
        return ret;
    }
    case NodeKind::Break:
        breaking = true;
        jump_label = n->text;
        return v_unit();
    case NodeKind::Continue:
        continuing = true;
        jump_label = n->text;
        return v_unit();
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
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
        Value v;
        v.kind = TypeKind::Func;
        v.type = n->ty;
        v.fn = n->resolved;
        return v;
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
        if (lt->name == "luce") {
            string file = module != nullptr && !module->text.empty() ? string(module->text)
                                                                     : string("t.lucb");
            if (n->text == "file") {
                strings.push_back(file);
                return v_str(strings.back());
            }
            if (n->text == "line") {
                return v_int(n->ty != nullptr ? n->ty : nullptr, n->span.line);
            }
            if (n->text == "function") {
                string fn = current_fn != nullptr ? string(current_fn->text) : string("answer");
                strings.push_back(fn);
                return v_str(strings.back());
            }
            if (n->text == "location") {
                Value v;
                v.kind = TypeKind::Struct;
                v.type = n->ty;
                strings.push_back(file);
                v.fields.push_back(v_str(strings.back()));
                Value line;
                line.kind = TypeKind::U32;
                line.u = n->span.line;
                v.fields.push_back(line);
                string fn = current_fn != nullptr ? string(current_fn->text) : string("answer");
                strings.push_back(fn);
                v.fields.push_back(v_str(strings.back()));
                return v;
            }
        }
        if (lt->name == "files" && n->text == "missing") {
            return v_int(n->ty != nullptr ? n->ty : nullptr, 2);
        }
        // `module.constant`: another module's public top-level binding.
        if (n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Const || n->resolved->kind == NodeKind::Global)) {
            Slot* slot = find_slot(n->resolved->text, n->resolved);
            if (slot != nullptr) {
                return slot->value;
            }
        }
    }
    Value obj = eval(n->left);
    if (trapped) {
        return v_unit();
    }
    if (obj.kind == TypeKind::Pointer && obj.ptr != nullptr) {
        obj = *obj.ptr;
    }
    Type* raw = n->left != nullptr ? n->left->ty : obj.type;
    if (is_ptr(raw) && raw->elem != nullptr) {
        raw = raw->elem;
    }
    bool view = raw != nullptr && (raw->kind == TypeKind::Str || is_span(raw) || is_array(raw) ||
                                   obj.kind == TypeKind::Str || obj.kind == TypeKind::Span ||
                                   obj.kind == TypeKind::Array);
    if (view && n->text == "length") {
        if (obj.kind == TypeKind::Str) {
            return v_int(n->ty, decode_string(obj.str).size());
        }
        size_t len = obj.length != 0 ? obj.length : obj.fields.size();
        if (obj.kind == TypeKind::Array && obj.type != nullptr) {
            len = static_cast<size_t>(obj.type->length);
        }
        return v_int(n->ty != nullptr ? n->ty : nullptr, len);
    }
    if (view && n->text == "data") {
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
    if (obj.kind == TypeKind::ErrorVal || (raw != nullptr && raw->kind == TypeKind::ErrorVal)) {
        if (n->text == "code") {
            return v_int(n->ty, obj.u);
        }
        if (n->text == "message") {
            return v_str(obj.str);
        }
    }
    if (obj.kind == TypeKind::Struct ||
        (obj.type != nullptr && obj.type->kind == TypeKind::Struct && obj.type->decl != nullptr)) {
        Node* decl = obj.type != nullptr ? obj.type->decl : nullptr;
        if (decl != nullptr) {
            int i = field_index(decl, n->text);
            if (i >= 0 && i < static_cast<int>(obj.fields.size())) {
                return obj.fields[static_cast<size_t>(i)];
            }
        }
    }
    if (view && n->text == "bytes" &&
        (raw == nullptr || raw->kind == TypeKind::Str || obj.kind == TypeKind::Str)) {
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

auto Interp::eval_span_make(Node* n) -> Value {
    Value p = eval(n->body != nullptr ? n->body->left : nullptr);
    Value len =
        eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
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
    if (is_ptr(n->left != nullptr ? n->left->ty : nullptr) && n->left->ty->is_nullable) {
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
        uint64_t want = is_int_enum(st)
                            ? enum_case_int(st->decl, pat->resolved)
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
    if (pat->op == TokenKind::DotDotLt || pat->op == TokenKind::DotDotEq) {
        Value lo = eval(pat->left);
        Value hi = eval(pat->right);
        uint64_t s = as_u(scrut, st != nullptr ? st : scrut.type);
        uint64_t a = as_u(lo, lo.type);
        uint64_t b = as_u(hi, hi.type);
        if (pat->op == TokenKind::DotDotLt) {
            return s >= a && s < b;
        }
        return s >= a && s <= b;
    }
    if (pat->left != nullptr &&
        (pat->left->kind == NodeKind::Literal || pat->left->kind == NodeKind::Unary)) {
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

} // namespace lucb
