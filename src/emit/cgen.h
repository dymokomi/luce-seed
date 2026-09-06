//==============================================================================================
//
//   emit/cgen - C spelling helpers
//
//   DESCRIPTION:
//       Declarations of the naming functions the emitter units share. Implementation header.
//
//==============================================================================================

#pragma once

#include "check/type.h"
#include "parse/ast.h"

namespace lucb {

string ident(string_view prefix, string_view name);
string struct_ident(Node* st, string_view prefix = {});
string c_symbol(Node* fn);
string func_ident(Node* fn, Node* owner, string_view prefix = {});
// The manifest's `symbol_prefix`, put before every exported symbol (§17.6); set once per build.
void set_export_prefix(string_view prefix);
string_view export_prefix();
string sanitize_type_name(const string& s);
string array_c_name(Type* t);
string opt_c_name(Type* t);
string fail_c_name(Type* t);
string tup_c_name(Type* t);
string fn_c_name(Type* t);
string c_type(Type* t);
uint64_t emit_case_int(Node* en, Node* cse);
int emit_case_tag(Node* en, Node* cse);
string fn_c_ret(Node* fn);
string word_cast(Type* t, const string& e);
string bits_lit(Type* t);
string down_cast(Type* t, const string& e);
string c_escape(string_view s);
string decode_lit(string_view tok);

} // namespace lucb
