let method_name id = "method_" ^ string_of_int id
let rec indent level = if level = 0 then "" else indent (level - 1) ^ "  "
let newline level = "\n" ^ indent level
let varname id = "var_" ^ string_of_int id
let paren s = "(" ^ s ^ ")"
let brace s = "[" ^ s ^ "]"
let bracket s = "{" ^ s ^ "}"
let quoted s = "\"" ^ String.escaped s ^ "\""

let fn_sig id args =
  let args_strs =
    List.map (fun (id, _) -> "_qdbp_object_ptr " ^ varname id) args
  in
  "_qdbp_object_ptr " ^ method_name id
  ^ paren (String.concat ", " ("_qdbp_object_arr captures" :: args_strs))

let fn_declarations methods =
  let method_strs =
    List.map
      (fun (id, (args, _, _, _, _)) -> fn_sig id args)
      (AstTypes.IntMap.bindings methods)
  in
  let method_strs = List.map (fun s -> s ^ ";") method_strs in
  String.concat "\n" method_strs

let invoke_fn_name cnt = "_qdbp_invoke_" ^ string_of_int cnt

let invoke_fns methods =
  let arities =
    AstTypes.IntSet.of_list
      (List.map
         (fun (_, (args, _, _, _, _)) -> List.length args)
         (AstTypes.IntMap.bindings methods))
  in
  (* Arities 0 and 1 are hardcoded because they have special cases *)
  let arities = AstTypes.IntSet.remove 1 arities in
  let arities = AstTypes.IntSet.remove 2 arities in
  let invoke_fn arity =
    (* MUST keep up to date w/ invoke fns in proto.c *)
    let args =
      List.init arity (fun a -> "_qdbp_object_ptr arg" ^ string_of_int a)
    in
    let args = "_qdbp_object_ptr receiver" :: "_qdbp_label_t label" :: args in
    let fn_name = invoke_fn_name arity in
    let fn_args_types = List.init arity (fun _ -> "_qdbp_object_ptr") in
    let fn_args_types = "_qdbp_object_arr" :: fn_args_types in
    let fn_args_types_str = String.concat ", " fn_args_types in
    let fn_type = "_qdbp_object_ptr(*)" ^ paren fn_args_types_str in
    let fn_args = List.init arity (fun a -> "arg" ^ string_of_int a) in
    let fn_args = "captures" :: fn_args in
    let fn_args_str = String.concat ", " fn_args in
    "__attribute__((always_inline)) " ^
    "_qdbp_object_ptr " ^ fn_name
    ^ paren (String.concat ", " args)
    ^ " {" ^ newline 1 ^ "void* code;" ^ newline 1
    ^ "_qdbp_object_arr captures = _qdbp_get_method(receiver, label, &code);"
    ^ newline 1 ^ "return "
    ^ paren (paren fn_type ^ "code")
    ^ paren fn_args_str ^ ";\n}"
  in
  let arities_list = AstTypes.IntSet.elements arities in
  let fns = List.map invoke_fn arities_list in
  String.concat "\n" fns

let c_call fn args = fn ^ paren (String.concat ", " args)

let rec expr_to_c level expr =
  match expr with
  | `Abort _ -> "qdbp_abort()"
  | `Declaration (lhs, rhs, body, _, _) ->
      let body_c = expr_to_c (level + 1) body in
      let rhs_c = expr_to_c (level + 1) rhs in
      "_QDBP_LET"
      ^ paren
          (varname (fst lhs) ^ ", " ^ rhs_c ^ "," ^ newline (level + 1) ^ body_c)
  | `Drop (v, e, cnt) ->
      c_call "_QDBP_DROP"
        [ varname v; string_of_int cnt; expr_to_c (level + 1) e ]
  | `Dup (v, e, cnt) ->
      c_call "_QDBP_DUP"
        [ varname v; string_of_int cnt; expr_to_c (level + 1) e ]
  | `EmptyPrototype _ -> "_qdbp_empty_prototype()"
  | `IntProto (i, _) -> (
      (* Remove leading 0's from i *)
      (* Keep in mind the fact there might be a - sign in the beginning *)
      let rec remove_leading_zeros s =
        if String.starts_with ~prefix:"-" s then
          "-" ^ remove_leading_zeros (String.sub s 1 (String.length s - 1))
        else if
          String.starts_with ~prefix:"0x" s || String.starts_with ~prefix:"Ox" s
        then "0x" ^ remove_leading_zeros (String.sub s 1 (String.length s - 1))
        else if
          String.starts_with ~prefix:"0b" s || String.starts_with ~prefix:"0B" s
        then "0b" ^ remove_leading_zeros (String.sub s 1 (String.length s - 1))
        else if String.starts_with ~prefix:"0" s then
          remove_leading_zeros (String.sub s 1 (String.length s - 1))
        else s
      in
      let i = remove_leading_zeros i in
      let i = if i = "" then "0" else i in
      match int_of_string_opt i with
      | Some intval ->
          if intval > 4611686018427387903 || intval < -4611686018427387904 then
            c_call "_qdbp_make_boxed_int_from_cstr" [ "\"" ^ i ^ "\"" ]
          else c_call "_qdbp_make_unboxed_int" [ i ]
      | None ->
        prerr_string "INTERNAL ERROR: parsed an illegal int";
        exit 0)
  | `ExternalCall (fn, args, _, _) ->
      c_call (fst fn) (List.map (expr_to_c (level + 1)) args)
  | `StrProto (s, _) ->
      c_call "_qdbp_make_string" [ quoted s; string_of_int (String.length s) ]
  | `MethodInvocation (receiver, label, args, _, _) ->
      let args_c = List.map (fun (_, e, _) -> expr_to_c (level + 1) e) args in
      let label_c = Int64.to_string (fst label) in
      let receiver_c = expr_to_c (level + 1) receiver in
      c_call
        (invoke_fn_name (List.length args))
        (receiver_c :: label_c :: args_c)
  | `PatternMatch (hasDefault, v, cases, _, _) ->
      let rec cases_to_c level cases default =
        match cases with
        | [] -> (
            match default with
            | None -> "_qdbp_match_failed()"
            | Some e -> expr_to_c level e)
        | ((tag, _), ((arg, _), body, _), _) :: rest ->
            c_call "_QDBP_MATCH"
              [
                "tag";
                string_of_int tag;
                varname arg;
                expr_to_c (level + 1) body;
                newline level ^ cases_to_c (level + 1) rest default;
              ]
      in
      let casesC =
        if hasDefault then
          cases_to_c (level + 1) (List.tl cases)
            (Some
               (let (_, _), (_, body, _), _ = List.hd cases in
                body))
        else cases_to_c (level + 1) cases None
      in
      paren
        (c_call "_qdbp_decompose_variant" [ varname v; "&tag"; "&payload" ]
        ^ "," ^ newline level ^ casesC)
  | `PrototypeCopy (ext, ((label, _), (meth_id, meth_fvs), _), size, _, op, _)
    ->
      let ext_c = expr_to_c (level + 1) ext in
      let label_c = Int64.to_string label in
      let fn =
        match op with `Extend -> "_qdbp_extend" | `Replace -> "_qdbp_replace"
      in
      let code_ptr = "(void*)" ^ method_name meth_id in
      let fv_list = List.map varname (AstTypes.IntSet.elements meth_fvs) in
      let captures_c =
        if List.length fv_list > 0 then
          paren
            ("struct _qdbp_object*"
            ^ brace (string_of_int (List.length fv_list)))
          ^ bracket (String.concat ", " fv_list)
        else "NULL"
      in
      let args =
        [
          ext_c;
          label_c;
          code_ptr;
          captures_c;
          string_of_int (List.length fv_list);
        ]
      in
      let size_lg2 = int_of_float (log (float_of_int size) /. log 2.) in
      assert (size = 1 lsl size_lg2);
      let args =
        match op with
        | `Extend -> args @ [ string_of_int size; string_of_int size_lg2 ]
        | `Replace -> args
      in
      c_call fn args
  | `TaggedObject ((tag, _), value, _, _) ->
      c_call "_qdbp_variant_create"
        [ string_of_int tag; expr_to_c (level + 1) value ]
  | `VariableLookup (v, _) -> varname v

let fn_definitions methods =
  let fn_definition (id, (args, body, _, fvs, bvs)) =
    let declare id = indent 1 ^ "_qdbp_object_ptr " ^ varname id ^ ";" in
    let init_fv idx id =
      indent 1 ^ "_qdbp_object_ptr " ^ varname id ^ " = (captures"
      ^ brace (string_of_int idx)
      ^ ");"
    in
    (* Naively, we dup the first arg at callsite then drop it here *)
    (* Instead, omit both operations *)
    let _ =
      match args with
      | v :: _ ->
          newline 1 ^ c_call "_qdbp_drop" [ varname (fst v); string_of_int 1 ]
      | [] -> ""
    in
    let bvs_declarations = List.map declare (AstTypes.IntSet.elements bvs) in
    let fvs_initializations =
      List.mapi init_fv (AstTypes.IntSet.elements fvs)
    in
    fn_sig id args ^ " "
    ^ bracket
        ("\n"
        ^ String.concat "\n" (List.append bvs_declarations fvs_initializations)
        ^ "\n" ^ newline 1 ^ "_qdbp_tag_t tag; _qdbp_object_ptr payload;"
        ^ newline 1 ^ "return " ^ expr_to_c 1 body ^ ";\n")
  in
  let method_strs = List.map fn_definition (AstTypes.IntMap.bindings methods) in
  String.concat "\n" method_strs

let codegen_c methods main_method_id =
  let main_method = method_name main_method_id in
  {|
#define QDBP_DEBUG
#include "runtime.h"
|} ^ invoke_fns methods ^ "\n"
  ^ fn_declarations methods ^ "\n" ^ fn_definitions methods ^ "\n"
  ^ "\n  int main() {\n    _qdbp_init();\n    _qdbp_object_ptr result = "
  ^ main_method
  ^ "(NULL);\n    _qdbp_drop(result, 1); _qdbp_cleanup(); return _qdbp_get_return(result);\n  }\n  "
