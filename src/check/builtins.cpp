//==============================================================================================
//
//   check/builtins - The standard modules the seed supplies in place of Base source
//
//   DESCRIPTION:
//       base.md §16.6 names the standard modules; `luce-base` will write them in Base. The
//       seed synthesizes their declarations here instead: `memory` and the `Allocator`
//       interface with `FixedBuffer`, `io` and `Writer`, `Location`, `files`, `process`,
//       `thread`, `sync`, `atomic`, `c`, and the compile-time `luce` facts. Every node made
//       here carries `FlagBuiltin`, which is how the emitter knows the runtime header already
//       defines the record.
//
//==============================================================================================

#include "check/checker.h"

#include "support/literal.h"
#include "lex/lexer.h"
#include "parse/parser.h"

namespace lucb {

auto Checker::syn_node(NodeKind k, const char* name) -> Node* {
    Node* n = arena->make<Node>();
    n->kind = k;
    n->text = keep(name);
    n->flags = FlagPub | FlagBuiltin;
    return n;
}

auto Checker::syn_method(const char* name, Type* result, bool mutating) -> Node* {
    Node* m = syn_node(NodeKind::Func, name);
    m->ty = result;
    if (mutating) {
        m->flags |= FlagMutating;
    }
    return m;
}

// The standard modules are synthesized once per program, so a `Writer` in one
// module is the same type as a `Writer` in another; later modules replay the
// recorded bindings.
auto Checker::bind_builtin(const char* name, Type* type, Node* decl) -> void {
    builtin_bindings.push_back(BuiltinBinding{name, type, decl, false});
    bind(name, type, false, decl);
}

// A standard module this seed keeps as a builtin. Its name binds only where a module imports
// it (§3.5), so a local named `io` is ordinary elsewhere; `bind_imports` looks it up here.
auto Checker::bind_builtin_module(const char* name, Type* type, Node* decl) -> void {
    builtin_bindings.push_back(BuiltinBinding{name, type, decl, true});
}

auto Checker::builtin_module(string_view name) const -> const BuiltinBinding* {
    for (const BuiltinBinding& b : builtin_bindings) {
        if (b.is_module && name == b.name) {
            return &b;
        }
    }
    return nullptr;
}

// Every text a parsed tree views into `source` copied into the arena, so the tree stays
// readable after the source is gone.
auto Checker::keep_texts(Node* n) -> void {
    for (; n != nullptr; n = n->next) {
        n->text = keep(string(n->text));
        n->label = keep(string(n->label));
        keep_texts(n->left);
        keep_texts(n->right);
        keep_texts(n->body);
        keep_texts(n->type);
        keep_texts(n->attrs);
    }
}

// Declarations a builtin module carries as Base text rather than as nodes built by hand:
// parsed, given their types, and checked in a scope of their own, then appended to the module.
auto Checker::append_builtin_text(Node* module, const char* text) -> void {
    DiagnosticBag quiet;
    Source source = Source::from_bytes(string(module->text) + ".lucb", text, quiet);
    vector<Token> tokens = tokenize(source, quiet);
    ParseResult parsed = parse(source, tokens, *arena, quiet);
    if (parsed.module == nullptr) {
        return;
    }
    keep_texts(parsed.module); // the source dies with this call; the tree outlives the checker
    push_scope();
    Node* last = module->body;
    while (last != nullptr && last->next != nullptr) {
        last = last->next;
    }
    for (Node* d = parsed.module->body; d != nullptr; d = d->next) {
        d->flags |= FlagBuiltin | FlagPub;
        if (d->kind == NodeKind::Interface) {
            Type* t = make_type(TypeKind::Interface, d->text);
            t->decl = d;
            d->ty = t;
            bind(d->text, t, false, d);
        }
    }
    for (Node* d = parsed.module->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Interface) {
            check_interface(d);
        }
    }
    pop_scope();
    if (last != nullptr) {
        last->next = parsed.module->body;
    } else {
        module->body = parsed.module->body;
    }
}

auto Checker::bind_memory() -> void {
    if (!builtin_bindings.empty()) {
        for (size_t i = 0; i < builtin_bindings.size(); i++) {
            if (!builtin_bindings[i].is_module) {
                bind(builtin_bindings[i].name, builtin_bindings[i].type, false,
                     builtin_bindings[i].decl);
            }
        }
        return;
    }
    Node* al_alloc = syn_node(NodeKind::Func, "allocate");
    al_alloc->flags |= FlagMutating;
    Node* al_sz = syn_node(NodeKind::Param, "size");
    al_sz->ty = ty_usize;
    Node* al_al = syn_node(NodeKind::Param, "alignment");
    al_al->ty = ty_usize;
    al_sz->next = al_al;
    al_alloc->right = al_sz;
    al_alloc->ty = intern_opt(intern_sp(ty_u8, false));
    Node* al_resize = syn_node(NodeKind::Func, "resize");
    al_resize->flags |= FlagMutating;
    Node* rs_block = syn_node(NodeKind::Param, "block");
    rs_block->ty = intern_sp(ty_u8, false);
    Node* rs_size = syn_node(NodeKind::Param, "size");
    rs_size->ty = ty_usize;
    rs_block->next = rs_size;
    al_resize->right = rs_block;
    al_resize->ty = ty_bool;
    Node* al_rel = syn_node(NodeKind::Func, "release");
    al_rel->flags |= FlagMutating;
    Node* rl_block = syn_node(NodeKind::Param, "block");
    rl_block->ty = intern_sp(ty_u8, false);
    al_rel->right = rl_block;
    al_rel->ty = t_unit();
    al_alloc->next = al_resize;
    al_resize->next = al_rel;
    Node* alloc_iface = syn_node(NodeKind::Interface, "Allocator");
    alloc_iface->body = al_alloc;
    ty_alloc = intern_iface(alloc_iface, false);
    alloc_iface->ty = ty_alloc;
    Node* data = syn_node(NodeKind::Field, "data");
    data->ty = intern_ptr(ty_u8, false, false, false);
    Node* cap = syn_node(NodeKind::Field, "cap");
    cap->ty = ty_usize;
    Node* used = syn_node(NodeKind::Field, "used");
    used->ty = ty_usize;
    data->next = cap;
    cap->next = used;

    Node* over = syn_node(NodeKind::Func, "over");
    over->flags |= FlagStatic;
    Node* bufp = syn_node(NodeKind::Param, "buffer");
    bufp->ty = intern_sp(ty_u8, false);
    over->right = bufp;

    Node* fb = syn_node(NodeKind::Struct, "FixedBuffer");
    fb->body = data;
    used->next = over;
    ty_fixed = make_type(TypeKind::Struct, "FixedBuffer");
    ty_fixed->decl = fb;
    fb->ty = ty_fixed;
    over->ty = ty_fixed;
    fixed_decl = fb;

    Node* alloc_g = syn_node(NodeKind::Global, "allocator");
    alloc_g->flags |= FlagThreadLocal;
    alloc_g->ty = ty_alloc;
    Node* heap_g = syn_node(NodeKind::Const, "heap");
    heap_g->ty = ty_alloc;
    Node* exh = syn_node(NodeKind::Const, "exhausted");
    exh->ty = ty_errcode != nullptr ? ty_errcode : ty_i32;
    Node* m_copy = syn_node(NodeKind::Func, "copy");
    Node* cp_to = syn_node(NodeKind::Param, "to");
    cp_to->ty = intern_sp(ty_u8, false);
    Node* cp_from = syn_node(NodeKind::Param, "from");
    cp_from->ty = intern_sp(ty_u8, true);
    Node* cp_n = syn_node(NodeKind::Param, "count");
    cp_n->ty = ty_usize;
    cp_to->next = cp_from;
    cp_from->next = cp_n;
    m_copy->right = cp_to;
    m_copy->ty = t_unit();
    Node* m_move = syn_node(NodeKind::Func, "move");
    Node* mv_to = syn_node(NodeKind::Param, "to");
    mv_to->ty = intern_sp(ty_u8, false);
    Node* mv_from = syn_node(NodeKind::Param, "from");
    mv_from->ty = intern_sp(ty_u8, true);
    Node* mv_n = syn_node(NodeKind::Param, "count");
    mv_n->ty = ty_usize;
    mv_to->next = mv_from;
    mv_from->next = mv_n;
    m_move->right = mv_to;
    m_move->ty = t_unit();
    Node* m_set = syn_node(NodeKind::Func, "set");
    Node* st_span = syn_node(NodeKind::Param, "span");
    st_span->ty = intern_sp(ty_u8, false);
    Node* st_byte = syn_node(NodeKind::Param, "byte");
    st_byte->ty = ty_u8;
    st_span->next = st_byte;
    m_set->right = st_span;
    m_set->ty = t_unit();
    Node* m_grow = syn_node(NodeKind::Func, "grow");
    m_grow->flags |= FlagFallible;
    Node* gr_block = syn_node(NodeKind::Param, "block");
    gr_block->ty = intern_sp(ty_u8, false);
    Node* gr_size = syn_node(NodeKind::Param, "size");
    gr_size->ty = ty_usize;
    gr_block->next = gr_size;
    m_grow->right = gr_block;
    m_grow->ty = intern_sp(ty_u8, false);
    Node* m_read = syn_node(NodeKind::Func, "read");
    Node* rd_addr = syn_node(NodeKind::Param, "address");
    rd_addr->ty = intern_ptr(ty_void, false, false, false);
    m_read->right = rd_addr;
    m_read->ty = t_unit();
    Node* m_write = syn_node(NodeKind::Func, "write");
    Node* wr_addr = syn_node(NodeKind::Param, "address");
    wr_addr->ty = intern_ptr(ty_void, false, false, false);
    Node* wr_val = syn_node(NodeKind::Param, "value");
    wr_val->ty = ty_unit;
    wr_addr->next = wr_val;
    m_write->right = wr_addr;
    m_write->ty = t_unit();
    alloc_g->next = heap_g;
    heap_g->next = exh;
    exh->next = m_copy;
    m_copy->next = m_move;
    m_move->next = m_set;
    m_set->next = m_grow;
    m_grow->next = m_read;
    m_read->next = m_write;

    Node* mem = syn_node(NodeKind::Module, "memory");
    mem->body = alloc_g;
    Type* mt = make_type(TypeKind::Module, "memory");
    mt->decl = mem;
    memory_mod = mem;

    bind_builtin("Allocator", ty_alloc, nullptr);
    bind_builtin("CAllocator", ty_alloc, nullptr);
    bind_builtin("FixedBuffer", ty_fixed, fb);
    bind_builtin_module("memory", mt, mem);

    if (ty_fmt == nullptr) {
        ty_fmt = make_type(TypeKind::Fmt, "fmt");
    }
    Node* wr = syn_node(NodeKind::Interface, "Writer");
    Node* wfn = syn_node(NodeKind::Func, "write");
    wfn->flags |= FlagMutating | FlagFallible;
    Node* wpar = syn_node(NodeKind::Param, "bytes");
    wpar->ty = intern_sp(ty_u8, true);
    wfn->right = wpar;
    wfn->ty = ty_usize;
    wr->body = wfn;
    ty_writer = intern_iface(wr, false);
    wr->ty = ty_writer;
    bind_builtin("Writer", ty_writer, wr);

    Node* loc = syn_node(NodeKind::Struct, "Location");
    Node* ffile = syn_node(NodeKind::Field, "file");
    ffile->ty = ty_str;
    Node* fline = syn_node(NodeKind::Field, "line");
    fline->ty = ty_u32;
    Node* ffun = syn_node(NodeKind::Field, "function");
    ffun->ty = ty_str;
    ffile->next = fline;
    fline->next = ffun;
    loc->body = ffile;
    ty_location = make_type(TypeKind::Struct, "Location");
    ty_location->decl = loc;
    loc->ty = ty_location;
    bind_builtin("Location", ty_location, loc);

    Node* l_loc = syn_node(NodeKind::Const, "location");
    l_loc->ty = ty_location;
    Node* l_file = syn_node(NodeKind::Const, "file");
    l_file->ty = ty_str;
    Node* l_line = syn_node(NodeKind::Const, "line");
    l_line->ty = ty_u32;
    Node* l_fn = syn_node(NodeKind::Const, "function");
    l_fn->ty = ty_str;
    l_loc->next = l_file;
    l_file->next = l_line;
    l_line->next = l_fn;
    Node* luce = syn_node(NodeKind::Module, "luce");
    luce->body = l_loc;
    Type* luce_t = make_type(TypeKind::Module, "luce");
    luce_t->decl = luce;
    bind_builtin("luce", luce_t, luce);

    Node* io_out = syn_node(NodeKind::Func, "stdout");
    io_out->ty = ty_writer;
    Node* io_err = syn_node(NodeKind::Func, "stderr");
    io_err->ty = ty_writer;
    io_out->next = io_err;
    Node* io = syn_node(NodeKind::Module, "io");
    io->body = io_out;
    Type* io_t = make_type(TypeKind::Module, "io");
    io_t->decl = io;
    bind_builtin_module("io", io_t, io);

    Node* f_read = syn_node(NodeKind::Func, "read");
    f_read->flags |= FlagFallible;
    Node* rp = syn_node(NodeKind::Param, "path");
    rp->ty = ty_cstr;
    f_read->right = rp;
    f_read->ty = intern_sp(ty_u8, false);
    Node* f_write = syn_node(NodeKind::Func, "write");
    f_write->flags |= FlagFallible;
    Node* wp = syn_node(NodeKind::Param, "path");
    wp->ty = ty_cstr;
    Node* wb = syn_node(NodeKind::Param, "bytes");
    wb->ty = intern_sp(ty_u8, true);
    wp->next = wb;
    f_write->right = wp;
    f_write->ty = t_unit();
    Node* f_miss = syn_node(NodeKind::Const, "missing");
    f_miss->ty = ty_errcode != nullptr ? ty_errcode : ty_i32;
    Node* f_list = syn_node(NodeKind::Func, "list");
    f_list->flags |= FlagFallible;
    Node* lp = syn_node(NodeKind::Param, "path");
    lp->ty = ty_cstr;
    f_list->right = lp;
    f_list->ty = intern_sp(ty_str, false);
    f_read->next = f_write;
    f_write->next = f_miss;
    f_miss->next = f_list;
    Node* files = syn_node(NodeKind::Module, "files");
    files->body = f_read;
    Type* files_t = make_type(TypeKind::Module, "files");
    files_t->decl = files;
    bind_builtin_module("files", files_t, files);

    Node* p_run = syn_node(NodeKind::Func, "run");
    p_run->flags |= FlagFallible;
    Node* pr_prog = syn_node(NodeKind::Param, "program");
    pr_prog->ty = ty_cstr;
    Node* pr_args = syn_node(NodeKind::Param, "arguments");
    pr_args->ty = intern_sp(ty_cstr, true);
    pr_prog->next = pr_args;
    p_run->right = pr_prog;
    Type* pr_ret[3] = {ty_i32, ty_str, ty_str};
    p_run->ty = intern_tup(pr_ret, 3);
    Node* process = syn_node(NodeKind::Module, "process");
    process->body = p_run;
    Type* process_t = make_type(TypeKind::Module, "process");
    process_t->decl = process;
    bind_builtin_module("process", process_t, process);

    Node* c_in = syn_node(NodeKind::Func, "stdin");
    c_in->ty = intern_ptr(ty_void, false, false, false);
    Node* c_out = syn_node(NodeKind::Func, "stdout");
    c_out->ty = intern_ptr(ty_void, false, false, false);
    Node* c_err = syn_node(NodeKind::Func, "stderr");
    c_err->ty = intern_ptr(ty_void, false, false, false);
    c_in->next = c_out;
    c_out->next = c_err;
    Node* cmod = syn_node(NodeKind::Module, "c");
    cmod->body = c_in;
    ty_c_mod = make_type(TypeKind::Module, "c");
    ty_c_mod->decl = cmod;

    Node* ord = syn_node(NodeKind::Enum, "Ordering");
    Type* ot = make_type(TypeKind::Enum, "Ordering");
    ot->decl = ord;
    ot->elem = ty_i32;
    ord->ty = ot;
    const char* onames[] = {"relaxed", "acquire", "release", "acq_rel", "seq_cst", "signal"};
    const int ovals[] = {0, 2, 3, 4, 5, 6};
    Node* oprev = nullptr;
    for (int i = 0; i < 6; i++) {
        Node* cse = syn_node(NodeKind::EnumCase, onames[i]);
        cse->ty = ot;
        Node* lit = arena->make<Node>();
        lit->kind = NodeKind::Literal;
        lit->op = TokenKind::IntLit;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", ovals[i]);
        lit->text = keep(buf);
        cse->left = lit;
        if (oprev != nullptr) {
            oprev->next = cse;
        } else {
            ord->body = cse;
        }
        oprev = cse;
    }
    bind_builtin("Ordering", ot, ord);

    Node* fence = syn_node(NodeKind::Func, "fence");
    Node* oarg = syn_node(NodeKind::Param, "order");
    oarg->ty = ot;
    fence->right = oarg;
    fence->ty = t_unit();
    Node* amod = syn_node(NodeKind::Module, "atomic");
    amod->body = fence;
    Type* amt = make_type(TypeKind::Module, "atomic");
    amt->decl = amod;
    bind_builtin_module("atomic", amt, amod);

    Node* hid = syn_node(NodeKind::Field, "id");
    hid->ty = ty_usize;
    Node* join = syn_node(NodeKind::Func, "join");
    join->flags |= FlagFallible | FlagPub;
    join->ty = t_unit();
    Node* det = syn_node(NodeKind::Func, "detach");
    det->flags |= FlagPub;
    det->ty = t_unit();
    hid->next = join;
    join->next = det;
    Node* hs = syn_node(NodeKind::Struct, "Handle");
    hs->body = hid;
    Type* ht = make_type(TypeKind::Struct, "Handle");
    ht->decl = hs;
    hs->ty = ht;

    Node* spawn = syn_node(NodeKind::Func, "spawn");
    spawn->flags |= FlagFallible | FlagPub;
    Node* sp_entry = syn_node(NodeKind::Param, "entry");
    sp_entry->ty = intern_ptr(ty_void, false, false, false);
    Node* sp_ctx = syn_node(NodeKind::Param, "context");
    sp_ctx->ty = intern_ptr(ty_void, false, false, true);
    sp_entry->next = sp_ctx;
    spawn->right = sp_entry;
    spawn->ty = ht;

    Node* cur = syn_node(NodeKind::Func, "current");
    cur->flags |= FlagPub;
    cur->ty = ht;
    Node* pause = syn_node(NodeKind::Func, "pause");
    pause->flags |= FlagPub;
    pause->ty = t_unit();
    Node* yield = syn_node(NodeKind::Func, "yield");
    yield->flags |= FlagPub;
    yield->ty = t_unit();
    Node* slp = syn_node(NodeKind::Func, "sleep");
    slp->flags |= FlagPub;
    Node* ms = syn_node(NodeKind::Param, "milliseconds");
    ms->ty = ty_usize;
    slp->right = ms;
    slp->ty = t_unit();
    spawn->next = cur;
    cur->next = pause;
    pause->next = yield;
    yield->next = slp;
    Node* hpub = syn_node(NodeKind::Struct, "Handle");
    hpub->ty = ht;
    hpub->body = hs->body;
    hpub->flags |= FlagPub;
    slp->next = hpub;
    Node* tmod = syn_node(NodeKind::Module, "thread");
    tmod->body = spawn;
    Type* tt = make_type(TypeKind::Module, "thread");
    tt->decl = tmod;
    bind_builtin_module("thread", tt, tmod);

    Node* m_lock = syn_method("lock", t_unit(), true);
    Node* m_unlock = syn_method("unlock", t_unit(), true);
    Node* m_try = syn_method("try", ty_bool, true);
    m_lock->next = m_unlock;
    m_unlock->next = m_try;
    Node* mu = syn_node(NodeKind::Struct, "Mutex");
    mu->body = m_lock;
    Type* tmu = make_type(TypeKind::Struct, "Mutex");
    tmu->decl = mu;
    mu->ty = tmu;

    Node* c_wait = syn_method("wait", t_unit(), true);
    Node* c_mp = syn_node(NodeKind::Param, "mutex");
    c_mp->ty = intern_ptr(tmu, false, false, false);
    c_wait->right = c_mp;
    Node* c_sig = syn_method("signal", t_unit(), true);
    Node* c_bc = syn_method("broadcast", t_unit(), true);
    c_wait->next = c_sig;
    c_sig->next = c_bc;
    Node* cv = syn_node(NodeKind::Struct, "Condition");
    cv->body = c_wait;
    Type* tcv = make_type(TypeKind::Struct, "Condition");
    tcv->decl = cv;
    cv->ty = tcv;

    Node* o_run = syn_method("run", t_unit(), true);
    Node* on = syn_node(NodeKind::Struct, "Once");
    on->body = o_run;
    Type* ton = make_type(TypeKind::Struct, "Once");
    ton->decl = on;
    on->ty = ton;

    Node* s_acq = syn_method("acquire", t_unit(), true);
    Node* s_rel = syn_method("release", t_unit(), true);
    s_acq->next = s_rel;
    Node* sm = syn_node(NodeKind::Struct, "Semaphore");
    sm->body = s_acq;
    Type* tsm = make_type(TypeKind::Struct, "Semaphore");
    tsm->decl = sm;
    sm->ty = tsm;

    mu->next = cv;
    cv->next = on;
    on->next = sm;
    Node* smod = syn_node(NodeKind::Module, "sync");
    smod->body = mu;
    Type* st = make_type(TypeKind::Module, "sync");
    st->decl = smod;
    bind_builtin_module("sync", st, smod);
    // the protocols `for` consumes and formatting calls (§8.3, §14.4), as Base text, once
    // every name they mention is bound
    Binding* lb = lookup("luce");
    if (lb != nullptr && lb->decl != nullptr) {
        append_builtin_text(lb->decl,
                            "pub interface Iterator[T]:\n"
                            "    mutating func next() -> T?\n"
                            "pub interface Iterable[T, I: Iterator[T]]:\n"
                            "    func iterator() -> I\n"
                            "pub interface Display:\n"
                            "    func display(sink: Writer) -> !\n");
    }
}

} // namespace lucb
