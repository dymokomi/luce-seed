#include "emit/emitter.h"

#include "support/literal.h"
#include <cstdio>

namespace lucb {

auto Emitter::run_defers(const vector<Node*>& d) -> void {
        for (int i = static_cast<int>(d.size()) - 1; i >= 0; i--) {
            Node* dn = d[static_cast<size_t>(i)];
            if (dn->kind == NodeKind::Errdefer) {
                continue;
            }
            line(emit_expr(dn->left) + ";");
        }
    }

auto Emitter::unwind_scope(const Scope& sc) -> void {
        run_defers(sc.defers);
        if (sc.restore_alloc) {
            line("lb_set_alloc(" + sc.alloc_save + ");");
        }
    }

auto Emitter::run_defers_from(int from) -> void {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= from; i--) {
            unwind_scope(scopes[static_cast<size_t>(i)]);
        }
    }

auto Emitter::snapshot_defers() -> string {
        string saved = out;
        int saved_indent = indent;
        out = {};
        indent = 0;
        run_defers_from(0);
        string s = out;
        out = saved;
        indent = saved_indent;
        return s;
    }

auto Emitter::emit_free(Node* n) -> void {
        Type* t = n->left != nullptr ? n->left->ty : nullptr;
        string a = emit_allocator(n->right);
        string e = emit_expr(n->left);
        if (is_ptr(t)) {
            string et = c_type(t->elem);
            line("lb_release_bytes(" + a + ", (lb_span){ (void*)(" + e + "), sizeof(" + et +
                 ") });");
            return;
        }
        if (is_span(t)) {
            string et = c_type(t->elem);
            int id = tmp();
            string sn = "_lb_s" + std::to_string(id);
            line("{ lb_span " + sn + " = " + e + "; lb_release_bytes(" + a + ", (lb_span){ " + sn +
                 ".data, " + sn + ".length * sizeof(" + et + ") }); }");
            return;
        }
        line("(void)(" + e + ");");
    }

auto Emitter::emit_with(Node* n) -> void {
        int id = tmp();
        string save = "_lb_as" + std::to_string(id);
        line("{");
        indent++;
        line("lb_alloc " + save + " = lb_get_alloc();");
        line("lb_set_alloc(" + emit_allocator(n->left) + ");");
        Scope sc;
        sc.restore_alloc = true;
        sc.alloc_save = save;
        scopes.push_back(sc);
        emit_stmt(n->body);
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        indent--;
        line("}");
    }

auto Emitter::emit_block(Node* n) -> void {
        line("{");
        indent++;
        scopes.push_back(Scope{});
        if (n != nullptr) {
            for (Node* s = n->kind == NodeKind::Block ? n->body : n; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        indent--;
        line("}");
    }

auto Emitter::emit_stmt(Node* n) -> void {
        if (n == nullptr) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            emit_block(n);
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            if (n->body != nullptr && n->text.empty() && is_tup(n->ty)) {
                int id = tmp();
                string tn = "_lb_tu" + std::to_string(id);
                line(c_type(n->ty) + " " + tn + " = " + emit_expr(n->left) + ";");
                int i = 0;
                for (Node* nm = n->body; nm != nullptr; nm = nm->next) {
                    line(c_type(nm->ty) + " " + ident("lb_", nm->text) + " = " + tn + ".a" +
                         std::to_string(i) + ";");
                    i++;
                }
                break;
            }
            string ty = c_type(n->ty);
            string name = ident("lb_", n->text);
            if (n->flags & FlagUninit) {
                line(ty + " " + name + ";");
                break;
            }
            string init = "0";
            if (n->left != nullptr) {
                init = emit_expr(n->left);
                Type* st = n->left->ty;
                if (is_span(n->ty) && is_array(st)) {
                    char nbuf[32];
                    snprintf(nbuf, sizeof(nbuf), "%lluULL",
                             static_cast<unsigned long long>(st->length));
                    init = "((lb_span){" + init + ".d, " + nbuf + "})";
                }
            } else if (n->ty != nullptr &&
                       (n->ty->kind == TypeKind::Struct || n->ty->kind == TypeKind::Union ||
                        n->ty->kind == TypeKind::Enum || is_array(n->ty) || is_span(n->ty) ||
                        n->ty->kind == TypeKind::Str || is_opt(n->ty) ||
                        n->ty->kind == TypeKind::Allocator)) {
                init = "{0}";
            } else if (n->ty != nullptr && n->ty->kind == TypeKind::Bool) {
                init = "false";
            }
            line(ty + " " + name + " = " + init + ";");
            break;
        }
        case NodeKind::Assign: {
            if (n->left != nullptr && n->left->kind == NodeKind::Member &&
                n->left->text == "allocator") {
                Type* lt = n->left->left != nullptr ? n->left->left->ty : nullptr;
                if (lt != nullptr && lt->kind == TypeKind::Module) {
                    line("lb_set_alloc(" + emit_allocator(n->right) + ");");
                    break;
                }
            }
            string dst = emit_expr(n->left);
            // self.x emits self->x; Name emits lb_name.
            if (n->left != nullptr && n->left->kind == NodeKind::Self) {
                dst = "(*self)";
            }
            string src = emit_expr(n->right);
            Type* lt = n->left != nullptr ? n->left->ty : nullptr;
            if (is_atomic(lt)) {
                if (n->op == TokenKind::Eq) {
                    line(dst + " = " + src + ";");
                } else if (n->op == TokenKind::PlusEq) {
                    line(dst + " += " + src + ";");
                } else if (n->op == TokenKind::MinusEq) {
                    line(dst + " -= " + src + ";");
                } else if (n->op == TokenKind::AmpEq) {
                    line(dst + " &= " + src + ";");
                } else if (n->op == TokenKind::PipeEq) {
                    line(dst + " |= " + src + ";");
                } else if (n->op == TokenKind::CaretEq) {
                    line(dst + " ^= " + src + ";");
                }
                break;
            }
            if (n->op == TokenKind::Eq) {
                line(dst + " = " + src + ";");
                break;
            }
            TokenKind op = TokenKind::Plus;
            if (n->op == TokenKind::MinusEq) {
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
            }
            Type* t = n->left != nullptr ? n->left->ty : nullptr;
            const char* helper = "add";
            if (op == TokenKind::Minus) {
                helper = "sub";
            } else if (op == TokenKind::Star) {
                helper = "mul";
            } else if (op == TokenKind::SlashSlash) {
                helper = "div";
            } else if (op == TokenKind::Percent) {
                helper = "mod";
            } else if (op == TokenKind::PlusPercent) {
                helper = "addw";
            } else if (op == TokenKind::MinusPercent) {
                helper = "subw";
            } else if (op == TokenKind::StarPercent) {
                helper = "mulw";
            } else if (op == TokenKind::PlusPipe) {
                helper = "adds";
            } else if (op == TokenKind::MinusPipe) {
                helper = "subs";
            } else if (op == TokenKind::StarPipe) {
                helper = "muls";
            }
            line(dst + " = " + emit_helper(helper, t, dst, src) + ";");
            break;
        }
        case NodeKind::If:
            emit_if(n);
            break;
        case NodeKind::While:
            emit_while(n);
            break;
        case NodeKind::Return:
            emit_return(n);
            break;
        case NodeKind::Break:
        case NodeKind::Continue:
            emit_jump(n);
            break;
        case NodeKind::Defer:
        case NodeKind::Errdefer:
            if (!scopes.empty()) {
                scopes.back().defers.push_back(n);
            } else {
                line(emit_expr(n->left) + ";");
            }
            break;
        case NodeKind::Recover:
            if (!catch_var.empty()) {
                line(catch_var + " = " + emit_expr(n->left) + ";");
            } else {
                string e = emit_expr(n->left);
                if (fn_fallible()) {
                    e = wrap_ok(e);
                }
                run_defers_from(0);
                line("return " + e + ";");
            }
            break;
        case NodeKind::Match:
            emit_match(n);
            break;
        case NodeKind::For: {
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                emit_for_range(n);
                break;
            }
            Type* it = n->right != nullptr ? n->right->ty : nullptr;
            string seq = emit_expr(n->right);
            string idx = ident("lb_i_", n->text);
            string len;
            string elem_e;
            if (is_array(it)) {
                char nbuf[32];
                snprintf(nbuf, sizeof(nbuf), "%lluULL",
                         static_cast<unsigned long long>(it->length));
                len = nbuf;
                elem_e = seq + ".d[" + idx + "]";
            } else if (it != nullptr && it->kind == TypeKind::Str) {
                len = seq + ".length";
                elem_e = "((const unsigned char*)" + seq + ".data)[" + idx + "]";
            } else {
                len = seq + ".length";
                string et = n->ty != nullptr
                                ? c_type((n->flags & FlagByPtr) != 0 && is_ptr(n->ty) ? n->ty->elem
                                                                                     : n->ty)
                                : "uint8_t";
                if (n->ty != nullptr && is_ptr(n->ty)) {
                    et = c_type(n->ty->elem);
                }
                elem_e = "((" + et + "*)" + seq + ".data)[" + idx + "]";
            }
            Scope sc;
            sc.loop = true;
            scopes.push_back(sc);
            line("for (size_t " + idx + " = 0; " + idx + " < " + len + "; " + idx + "++) {");
            indent++;
            if (n->flags & FlagByPtr) {
                line(c_type(n->ty) + " " + ident("lb_", n->text) + " = &(" + elem_e + ");");
            } else {
                line(c_type(n->ty) + " " + ident("lb_", n->text) + " = " + elem_e + ";");
            }
            emit_stmt(n->body);
            indent--;
            line("}");
            if (!scopes.empty()) {
                unwind_scope(scopes.back());
                scopes.pop_back();
            }
            break;
        }
        case NodeKind::Free:
            emit_free(n);
            break;
        case NodeKind::With:
            emit_with(n);
            break;
        case NodeKind::ExprStmt:
            if (is_error_call(n->left)) {
                run_defers_from(0);
                line("return " + emit_expr(n->left) + ";");
            } else {
                line(emit_expr(n->left) + ";");
            }
            break;
        case NodeKind::Asm: {
            string body;
            for (Node* ln = n->body; ln != nullptr; ln = ln->next) {
                if (ln->kind == NodeKind::Literal) {
                    string t = string(ln->text);
                    body += t;
                    if (t.empty() || t.back() != '\n') {
                        body += '\n';
                    }
                }
            }
            string arch = string(n->text);
            if (arch == "arm64") {
                line("#if defined(__aarch64__)");
            } else if (arch == "x86_64") {
                line("#if defined(__x86_64__)");
            } else {
                line("#if 0");
            }
            line("{");
            indent++;
            string outs;
            string ins;
            string clobs = "\"memory\"";
            bool first_out = true;
            bool first_in = true;
            vector<string> out_lhs;
            vector<string> out_rhs;
            for (Node* op = n->left; op != nullptr; op = op->next) {
                if (op->text == "options") {
                    continue;
                }
                string place;
                if (op->type != nullptr) {
                    place = string(op->type->text);
                    if (place.size() >= 2 && place.front() == '"' && place.back() == '"') {
                        place = place.substr(1, place.size() - 2);
                    }
                }
                bool is_out = op->text == "out" || op->text == "inout";
                bool is_in = op->text == "in" || op->text == "inout";
                bool clobber_only = is_out && op->left != nullptr &&
                                    op->left->kind == NodeKind::Name && op->left->text == "_";
                if (clobber_only) {
                    if (!place.empty()) {
                        clobs += ", \"" + place + "\"";
                    }
                    continue;
                }
                string rn = "_lb_as" + std::to_string(tmp());
                Type* ot = op->left != nullptr ? op->left->ty : nullptr;
                string ty = c_type(ot);
                if (ty == "void" || ty.empty()) {
                    ty = "int64_t";
                }
                if (place == "reg" || place.empty()) {
                    if (is_in) {
                        line(ty + " " + rn + " = " + emit_expr(op->left) + ";");
                    } else {
                        line(ty + " " + rn + ";");
                    }
                } else if (is_in) {
                    line("register " + ty + " " + rn + " asm(\"" + place + "\") = " +
                         emit_expr(op->left) + ";");
                } else {
                    line("register " + ty + " " + rn + " asm(\"" + place + "\");");
                }
                if (is_out) {
                    if (!first_out) {
                        outs += ", ";
                    }
                    first_out = false;
                    outs += string(is_in ? "\"+r\"(" : "\"=r\"(") + rn + ")";
                    if (op->left != nullptr) {
                        out_lhs.push_back(emit_expr(op->left));
                        out_rhs.push_back(rn);
                    }
                } else if (is_in) {
                    if (!first_in) {
                        ins += ", ";
                    }
                    first_in = false;
                    ins += "\"r\"(" + rn + ")";
                }
            }
            string tmpl = body;
            string escaped;
            for (size_t i = 0; i < tmpl.size(); i++) {
                if (tmpl[i] == '%') {
                    escaped += "%%";
                } else {
                    escaped += tmpl[i];
                }
            }
            line("asm volatile(" + c_escape(escaped) + " : " + outs + " : " + ins + " : " + clobs +
                 ");");
            for (size_t i = 0; i < out_lhs.size(); i++) {
                line(out_lhs[i] + " = " + out_rhs[i] + ";");
            }
            indent--;
            line("}");
            line("#endif");
            break;
        }
        default:
            line("/* unsupported stmt */");
            break;
        }
    }

auto Emitter::any_defers() -> bool {
        for (size_t i = 0; i < scopes.size(); i++) {
            if (!scopes[i].defers.empty() || scopes[i].restore_alloc) {
                return true;
            }
        }
        return false;
    }

auto Emitter::loop_scope(string_view label) -> int {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            if (scopes[static_cast<size_t>(i)].loop &&
                (label.empty() || scopes[static_cast<size_t>(i)].label == label)) {
                return i;
            }
        }
        return 0;
    }

auto Emitter::emit_return(Node* n) -> void {
        if (n->left != nullptr && n->left->kind == NodeKind::Call && n->left->left != nullptr &&
            n->left->left->kind == NodeKind::Name && n->left->left->text == "trap") {
            line(emit_expr(n->left) + ";");
            return;
        }
        if (is_error_call(n->left)) {
            string e = emit_expr(n->left);
            if (any_defers()) {
                int id = tmp();
                string t = "_lb_ret" + std::to_string(id);
                line(fn_c_ret(current_fn) + " " + t + " = " + e + ";");
                run_defers_from(0);
                line("return " + t + ";");
            } else {
                line("return " + e + ";");
            }
            return;
        }
        if (n->left == nullptr) {
            run_defers_from(0);
            if (fn_fallible()) {
                line("return " + wrap_ok("0") + ";");
            } else {
                line("return;");
            }
            return;
        }
        string e = emit_expr(n->left);
        if (fn_fallible()) {
            e = wrap_ok(e);
        }
        if (any_defers()) {
            int id = tmp();
            string t = "_lb_ret" + std::to_string(id);
            line(fn_c_ret(current_fn) + " " + t + " = " + e + ";");
            run_defers_from(0);
            line("return " + t + ";");
        } else {
            line("return " + e + ";");
        }
    }

auto Emitter::emit_jump(Node* n) -> void {
        int from = loop_scope(n->text);
        run_defers_from(from);
        if (n->text.empty()) {
            line(n->kind == NodeKind::Break ? "break;" : "continue;");
            return;
        }
        string lab = n->kind == NodeKind::Break ? "lb_brk_" : "lb_cont_";
        lab += string(n->text);
        line("goto " + lab + ";");
    }

auto Emitter::emit_while(Node* n) -> void {
        Scope sc;
        sc.loop = true;
        sc.label = n->text;
        scopes.push_back(sc);
        if (n->flags & FlagIfLet) {
            Node* let = n->left;
            line("while (1) {");
            indent++;
            int id = tmp();
            string on = "_lb_o" + std::to_string(id);
            Type* ot = let != nullptr && let->left != nullptr ? let->left->ty : nullptr;
            line(c_type(ot) + " " + on + " = " + emit_expr(let != nullptr ? let->left : nullptr) +
                 ";");
            string cond = is_opt(ot) ? on + ".present" : on + " != ((void*)0)";
            line("if (!(" + cond + ")) break;");
            if (let != nullptr && !let->text.empty()) {
                if (is_opt(ot) && ot->elem != nullptr) {
                    line(c_type(ot->elem) + " " + ident("lb_", let->text) + " = " + on + ".value;");
                } else {
                    line(c_type(ot) + " " + ident("lb_", let->text) + " = " + on + ";");
                }
            }
            emit_stmt(n->body);
            if (!n->text.empty()) {
                line("lb_cont_" + string(n->text) + ": ;");
            }
            indent--;
            line("}");
        } else {
            pad();
            out += "while (" + emit_expr(n->left) + ") {\n";
            indent++;
            emit_stmt(n->body);
            if (!n->text.empty()) {
                line("lb_cont_" + string(n->text) + ": ;");
            }
            indent--;
            line("}");
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        if (!n->text.empty()) {
            line("lb_brk_" + string(n->text) + ": ;");
        }
    }

auto Emitter::emit_for_range(Node* n) -> void {
        Scope sc;
        sc.loop = true;
        scopes.push_back(sc);
        string ty = c_type(n->ty);
        string name = ident("lb_", n->text);
        string a = emit_expr(n->right->left);
        string b = emit_expr(n->right->right);
        string cmp = n->right->op == TokenKind::DotDotEq ? " <= " : " < ";
        line("for (" + ty + " " + name + " = (" + ty + ")(" + a + "); " + name + cmp + "(" + ty +
             ")(" + b + "); " + name + "++) {");
        indent++;
        emit_stmt(n->body);
        indent--;
        line("}");
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
    }

auto Emitter::emit_match(Node* n) -> void {
        Type* st = n->left != nullptr ? n->left->ty : nullptr;
        int id = tmp();
        string sv = "_lb_m" + std::to_string(id);
        line(c_type(st) + " " + sv + " = " + emit_expr(n->left) + ";");
        if (is_int(st) || is_int_enum(st) || (st != nullptr && st->kind == TypeKind::Bool)) {
            line("switch ((int64_t)(" + sv + ")) {");
            indent++;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        line("default:");
                    } else if (pat->resolved != nullptr &&
                               pat->resolved->kind == NodeKind::EnumCase) {
                        line("case " + std::to_string(emit_case_int(st->decl, pat->resolved)) +
                             "LL:");
                    } else if (pat->left != nullptr) {
                        line("case " + emit_expr(pat->left) + ":");
                    }
                }
                line("{");
                indent++;
                emit_stmt(arm->body);
                line("break;");
                indent--;
                line("}");
            }
            indent--;
            line("}");
            return;
        }
        if (is_enum(st)) {
            line("switch ((int)(" + sv + ".tag)) {");
            indent++;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        line("default:");
                    } else if (pat->resolved != nullptr) {
                        int tag = emit_case_tag(st->decl, pat->resolved);
                        line("case " + std::to_string(tag) + ":");
                    }
                }
                line("{");
                indent++;
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    Node* cse = pat->resolved;
                    if (cse == nullptr || cse->body == nullptr) {
                        continue;
                    }
                    Node* p = cse->body;
                    Node* b = pat->body;
                    while (p != nullptr && b != nullptr) {
                        if (b->text != "_") {
                            line(c_type(p->ty) + " " + ident("lb_", b->text) + " = " + sv + ".u." +
                                 string(cse->text) + "." + string(p->text) + ";");
                        }
                        p = p->next;
                        b = b->next;
                    }
                }
                emit_stmt(arm->body);
                line("break;");
                indent--;
                line("}");
            }
            indent--;
            line("}");
            return;
        }
        if (is_opt(st)) {
            bool first = true;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                string cond = "0";
                string bind;
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        cond = "1";
                    } else if (pat->text == "none") {
                        cond = "(!" + sv + ".present)";
                    } else if (pat->text == "some") {
                        cond = sv + ".present";
                        if (pat->body != nullptr && !pat->body->text.empty() && st->elem != nullptr) {
                            bind = c_type(st->elem) + " " + ident("lb_", pat->body->text) + " = " +
                                   sv + ".value;";
                        }
                    }
                }
                pad();
                out += first ? "if (" : "else if (";
                out += cond + ") {\n";
                first = false;
                indent++;
                if (!bind.empty()) {
                    line(bind);
                }
                emit_stmt(arm->body);
                indent--;
                line("}");
            }
        }
    }

auto Emitter::emit_if(Node* n) -> void {
        if (n->flags & FlagIfLet) {
            Node* let = n->left;
            int id = tmp();
            string on = "_lb_o" + std::to_string(id);
            Type* ot = let != nullptr && let->left != nullptr ? let->left->ty : nullptr;
            line(c_type(ot) + " " + on + " = " + emit_expr(let != nullptr ? let->left : nullptr) +
                 ";");
            string cond = is_opt(ot) ? on + ".present" : on + " != ((void*)0)";
            pad();
            out += "if (" + cond + ") {\n";
            indent++;
            if (let != nullptr && !let->text.empty()) {
                if (is_opt(ot) && ot->elem != nullptr) {
                    line(c_type(ot->elem) + " " + ident("lb_", let->text) + " = " + on + ".value;");
                } else {
                    line(c_type(ot) + " " + ident("lb_", let->text) + " = " + on + ";");
                }
            }
            emit_stmt(n->body);
            indent--;
            line("}");
            if (n->right != nullptr) {
                pad();
                out += "else ";
                if (n->right->kind == NodeKind::If) {
                    out += '\n';
                    emit_if(n->right);
                } else if (n->right->kind == NodeKind::Block) {
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
            return;
        }
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

} // namespace lucb
