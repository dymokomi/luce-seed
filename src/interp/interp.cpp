#include "interp/interp.h"
#include "interp/interp_impl.h"

#include <cstdio>

namespace lucb {

auto Interp::find_slot(string_view name, Node* decl) -> Slot* {
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

auto Interp::find_func(string_view name) -> Node* {
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

auto Interp::load_globals() -> void {
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

auto Interp::call_func(Node* fn, Value* self, Node* args) -> Value {
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
        bool saved_ret = returning;
        Value saved_retv = ret;
        returning = false;
        Node* saved_fn = current_fn;
        current_fn = fn;
        exec(fn->body);
        current_fn = saved_fn;
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
        returning = saved_ret;
        ret = saved_ret ? saved_retv : ret;
        frames.pop_back();
        if ((fn->flags & FlagFallible) != 0 && !result.failed) {
            result.failed = false;
            result.kind = TypeKind::Fallible;
        }
        return result;
    }

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
    result.err = ip.err;
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
        result->err = ip.err;
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
