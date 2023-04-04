module TyVarMap = Map.Make(struct type t = int let compare = compare end)
module TyVarSet = Set.Make(struct type t = int let compare = compare end)
module TyVarPairSet = Set.Make(struct type t = (int * int) let compare = compare end)
exception UnifyError of string
module Env = struct
  module StringMap = Map.Make (String)
  let empty = StringMap.empty
  let extend env name ty = StringMap.add name ty env
  let lookup env name = StringMap.find_opt name env
end

let make_new_var_id tvars tvar =
  let (var_id, varmap) = tvars in
  let new_id = var_id + 1 in
  let tvars' = (new_id, TyVarMap.add var_id tvar varmap) in
  tvars', var_id
let get_tyvar id tyvars =
  let (_, varmap) = tyvars in
  TyVarMap.find id varmap
let make_new_unbound_id tvars level =
  let (var_id, varmap) = tvars in
  let unbound_id = var_id + 1 in
  let new_id = unbound_id + 1 in 
  let tvars' = (new_id, (TyVarMap.add var_id (`Unbound (unbound_id, level)) varmap)) in
  tvars', var_id
let make_new_unbound_var tvars level =
  let tvars, var_id = make_new_unbound_id tvars level in
  tvars, `TVar var_id
let update_tvars tvars var_id new_var =
  (fst tvars, TyVarMap.add var_id new_var (snd tvars))
let bool_type level tvars = 
  let tvars, var = make_new_unbound_var tvars level in
  let ty = `TVariant 
      (`TRowExtend 
         ("#True", (`TRecord `TRowEmpty),
          (`TRowExtend 
             ("#False", (`TRecord `TRowEmpty),
              (var)))))
  in
  tvars, ty
let generalize tvars level ty =
  let rec generalize state level ty =
    match ty with
    | `TVar id ->
      let tvars, already_generalized = state in
      begin
        match get_tyvar id tvars with
        | `Unbound (id', level') ->
          if level' > level then 
            let tvars, new_varid = make_new_var_id tvars (`Generic id') in
            (tvars, already_generalized), `TVar new_varid
          else
            (tvars, already_generalized), `TVar id
        | `Link ty' -> 
          begin
            match TyVarMap.find_opt id already_generalized with
            | None ->
              let tvars, newvarid = make_new_unbound_id tvars level in 
              let already_generalized =
                TyVarMap.add id (`TVar newvarid) already_generalized 
              in
              let (tvars, already_generalized), generalized_ty' =
                generalize (tvars, already_generalized) level ty'
              in
              let tvars = update_tvars tvars newvarid (`Link generalized_ty') in
              (tvars, already_generalized), `TVar newvarid
            | Some var -> (tvars, already_generalized), var
          end
        | `Generic _ -> (tvars, already_generalized), `TVar id
      end
    | `TArrow (param_tys, ret_ty) ->
      let state, param_tys = List.fold_left_map 
          (fun state param_ty -> generalize state level param_ty)
          state param_tys in
      let state, ret_ty = generalize state level ret_ty in
      state, `TArrow (List.rev param_tys, ret_ty)
    | `TRecord row ->
      let state, row = generalize state level row in
      state, `TRecord row
    | `TVariant row ->
      let state, row = generalize state level row in
      state, `TVariant row
    | `TRowExtend (label, ty, row) ->
      let state, ty = generalize state level ty in
      let state, row = generalize state level row in
      state, `TRowExtend (label, ty, row)
    | `TConst c -> state, `TConst c
    | `TRowEmpty -> state, `TRowEmpty
  in
  let ((tvars, _), ty) = generalize (tvars, TyVarMap.empty) level ty in
  tvars, ty
let adjust_levels tvars level ty =
  let rec adjust_levels state level ty =
    match ty with
    | `TConst _ -> state
    | `TRowEmpty -> state
    | `TRowExtend (_, ty, row) ->
      let state = adjust_levels state level ty in
      let state = adjust_levels state level row in
      state
    | `TRecord row ->
      let state = adjust_levels state level row in
      state
    | `TVariant row ->
      let state = adjust_levels state level row in
      state
    | `TArrow (param_tys, ret_ty) ->
      let state = List.fold_left
          (fun state param_ty -> adjust_levels state level param_ty)
          state param_tys in
      adjust_levels state level ret_ty
    | `TVar id ->
      let tvars, already_adjusted = state in
      begin
        match get_tyvar id tvars with
        | `Unbound (id', level') ->
          if level' > level then
            let tvars = update_tvars tvars id (`Unbound (id', level)) in
            (tvars, already_adjusted)
          else
            (tvars, already_adjusted)
        | `Link ty' ->
          begin
            if TyVarSet.mem id already_adjusted then
              (tvars, already_adjusted)
            else
              let already_adjusted = TyVarSet.add id already_adjusted in
              let (tvars, already_adjusted) =
                adjust_levels (tvars, already_adjusted) level ty' in
              (tvars, already_adjusted)
          end

        | `Generic _ ->
          (Error.internal_error "INTERNAL ERROR: should not be adjusting generic")
      end
  in
  let (tvars, _) = adjust_levels (tvars, TyVarSet.empty) level ty
  in tvars

let unify tvars already_unified ty1 ty2 =
  let rec unify state ty1 ty2 =
    match state with
    | `Error e -> `Error e
    | `Ok s ->
      if ty1 == ty2 then state
      else
        match ty1, ty2 with
        | `TConst ty1 , `TConst ty2 ->
          if ty1 = ty2 then state
          else `Error (`ConstTypeMismatch (ty1, ty2), s)
        | `TArrow (param_tys1, ret_ty1), `TArrow (param_tys2, ret_ty2) ->
          let state = List.fold_left2
              (fun state param_ty1 param_ty2 -> unify state param_ty1 param_ty2)
              state param_tys1 param_tys2 in
          unify state ret_ty1 ret_ty2
        | `TRecord row1, `TRecord row2 ->
          unify state row1 row2
        | `TVariant row1, `TVariant row2 ->
          unify state row1 row2
        | `TRowEmpty, `TRowEmpty -> state
        | `TRowExtend(label1, field_ty1, rest_row1), (`TRowExtend _ as row2) ->
          begin
            match rewrite_row state row2 label1 field_ty1 with
            |`Error e -> `Error e
            | `Ok (state, rest_row2) -> unify state rest_row1 rest_row2
          end
        | `TVar id, ty | ty, `TVar id ->
          unify_var state id ty
        | _ -> `Error (`CannotUnify (ty1, ty2), s)

  and unify_var state id ty =
    match state with 
    | `Error e -> `Error e
    | `Ok s ->
      let (tvars, already_unified) = s in
      match ty with
      | `TVar id' when TyVarPairSet.mem (id, id') already_unified ->
        `Ok (tvars, already_unified)
      | _ ->
        begin
          let already_unified = 
            begin
              match ty with
              | `TVar id' -> TyVarPairSet.add (id, id') already_unified
              | _ -> already_unified
            end
          in
          match get_tyvar id tvars with
          | `Unbound (id, level) ->
            let err = 
              begin
                match ty with
                | `TVar id' ->
                  begin
                    match get_tyvar id' tvars with
                    | `Unbound (id', _) -> 
                      if id = id' then Some (`Error (`UnifyingSameVariable, s))
                      else None
                    | _ -> None
                  end
                | _ -> None
              end
            in
            begin
              match err with
              | Some e -> e
              | None ->
                let tvars = adjust_levels tvars level ty in
                let tvars = update_tvars tvars id (`Link ty) in
                `Ok (tvars, already_unified)
            end
          | `Link ty' ->
            unify (`Ok (tvars, already_unified)) ty' ty
          | `Generic _ -> `Error (`CantUnifyGeneric (ty, `TVar id), s)
        end

  and rewrite_row state row2 label1 field_ty1 = 
    match state with 
    | `Error e -> `Error e
    | `Ok s -> 
      match row2 with
      | `TRowEmpty -> 
        `Error ((`MissingLabel label1), s)
      | `TRowExtend (label2, field_ty2, rest_row2) when label2 = label1 ->
        let state = unify state field_ty1 field_ty2 in
        `Ok (state, rest_row2)
      | `TRowExtend(label2, field_ty2, rest_row2) ->
        begin
          match rewrite_row state rest_row2 label1 field_ty1 with
          | `Error e -> `Error e
          | `Ok(state, rest_row2) ->
            `Ok (state, `TRowExtend(label2, field_ty2, rest_row2))
        end
      | `TVar id ->
        let (tvars, already_unified) = s in
        begin
          match get_tyvar id tvars with
          | `Unbound(_, level) ->
            let tvars, rest_row2_id = make_new_unbound_id tvars level in 
            let rest_row2 = `TVar rest_row2_id in
            let ty2 = `TRowExtend(label1, field_ty1, rest_row2) in
            let tvars = update_tvars tvars rest_row2_id (`Link ty2) in 
            `Ok ((`Ok (tvars, already_unified)), rest_row2)
          | `Link row2 ->
            rewrite_row (`Ok (tvars, already_unified)) row2 label1 field_ty1
          | `Generic _ -> 
            `Error ((`RewriteRowWithGeneric), s)
        end
      | _ -> 
        `Error ((`RewriteRowOnNonRow row2), s)
  in
  unify (`Ok (tvars, already_unified)) ty1 ty2


let instantiate tvars level ty =
  let rec instantiate state ty =
    match ty with
    | `TConst _ -> state, ty
    | `TRowEmpty -> state, ty
    | `TRowExtend (label, ty, row) ->
      let state, ty = instantiate state ty in
      let state, row = instantiate state row in
      state, `TRowExtend (label, ty, row)
    | `TRecord row ->
      let state, row = instantiate state row in
      state, `TRecord row
    | `TVariant row ->
      let state, row = instantiate state row in
      state, `TVariant row
    | `TArrow (param_tys, ret_ty) ->
      let state, param_tys = List.fold_left_map
          (fun state param_ty -> instantiate state param_ty)
          state param_tys in
      let state, ret_ty = instantiate state ret_ty in
      state, `TArrow (param_tys, ret_ty)
    | `TVar id ->
      let tvars, already_instantiated, id_var_map = state in
      begin
        match get_tyvar id tvars with
        | `Unbound _ ->
          state, ty
        | `Link ty' ->
          begin
            match TyVarMap.find_opt id already_instantiated with
            | None ->
              let tvars, var_id = make_new_unbound_id tvars level in
              let already_instantiated =
                TyVarMap.add id (`TVar var_id) already_instantiated in
              let (tvars, already_instantiated, id_var_map), var =
                instantiate (tvars, already_instantiated, id_var_map) ty' in
              let tvars = update_tvars tvars var_id (`Link var) in
              (tvars, already_instantiated, id_var_map), `TVar var_id
            | Some var -> (tvars, already_instantiated, id_var_map), var
          end
        | `Generic id ->
          match TyVarMap.find_opt id id_var_map with
          | None ->
            let tvars, var_id = make_new_unbound_id tvars level in
            let id_var_map = TyVarMap.add id (`TVar var_id) id_var_map in
            (tvars, already_instantiated, id_var_map), `TVar var_id
          | Some var -> (tvars, already_instantiated, id_var_map), var

      end
  in
  let (tvars, _, _), ty = instantiate (tvars, TyVarMap.empty, TyVarMap.empty) ty
  in tvars, ty

let rec row_is_complete tvars row =
  match row with 
  | `TRowEmpty -> true
  | `TRowExtend(_, _, rest_row) -> row_is_complete tvars rest_row
  | `TVar id ->
    begin
      match get_tyvar id tvars with
      | `Link ty ->
        row_is_complete tvars ty
      | _ -> false
    end
  | `TRecord row -> row_is_complete tvars row
  | `TVariant row -> row_is_complete tvars row
  | _ -> false

let rec get_row tvars row label =
  match row with 
  | `TRowExtend(l, field_ty, rest_row) ->
    if l = label then Some field_ty
    else get_row tvars rest_row label
  | `TVar id ->
    begin
      match get_tyvar id tvars with
      | `Link ty -> get_row tvars ty label
      | _ -> None
    end
  | `TRecord row -> get_row tvars row label
  | `TVariant row -> get_row tvars row label
  | _ -> None
let str_of_const_ty ty =
  match ty with
  | `Int -> "Integer Literal"
  | `Float -> "Float Literal"
  | `String -> "String Literal"
let rec kind_of tvars ty =
  match ty with
  | `TArrow _ -> "Method"
  | `TRecord _ -> "Nonempty Prototype"
  | `TVariant _ -> "Tagged Object"
  | `TRowEmpty -> "Empty Row"
  | `TConst c -> str_of_const_ty c
  | `TRowExtend _ -> "Row Extend"
  | `TVar id ->
    begin
      match get_tyvar id tvars with
      | `Unbound _ ->
        "Unbound "
      | `Link ty -> kind_of tvars ty
      | `Generic id -> "Generic " ^ (string_of_int id)
    end
let kind_of_non_tvar ty =
  match ty with
  | `TArrow _ -> "Method"
  | `TRecord _ -> "Nonempty Prototype"
  | `TVariant _ -> "Tagged Object"
  | `TRowEmpty -> "Empty Row"
  | `TConst c -> str_of_const_ty c
  | `TRowExtend _ -> "Row Extend"
  | `TVar _ -> Error.internal_error "INTERNAL ERROR: Should not get a TVar in `kind_of_non_tvar`"
let unify_error_msg (err, (tvars, _)) = 
  match err with
  | `CannotUnify (ty1, ty2) ->
    "tried to unify a "
    ^ kind_of tvars ty1
    ^ " with a "
    ^ kind_of tvars ty2
  | `ConstTypeMismatch (ty1, ty2) ->
    "tried to unify a "
    ^ str_of_const_ty ty1
    ^ "with a"
    ^ str_of_const_ty ty2
  | `MissingLabel label ->
    "expected expression to have " ^ label ^ " field"
  | `RewriteRowWithGeneric ->
    "INTERNAL ERROR: Tried calling rewrite_row on generic"
  | `RewriteRowOnNonRow row ->
    "INTERNAL ERROR: Tried calling rewrite_row on " ^ (kind_of tvars row)
  | `CantUnifyGeneric _ -> 
    "INTERNAL ERROR: All generics should be instantiated in unify"
  | `UnifyingSameVariable -> 
    "INTERNAL ERROR: Should not be unifying the same variable"
let loc expr =
  match expr with
  | `EmptyPrototype loc -> loc
  | `PrototypeExtension (_, _, loc) -> loc
  | `TaggedObject (_, _, loc) -> loc
  | `MethodInvocation (_, _, _, loc) -> loc
  | `PatternMatch (_, _, loc) -> loc
  | `Declaration (_, _, _, loc) -> loc
  | `VariableLookup (_, loc) -> loc
  | `Import (_, loc) -> loc
  | `ExternalCall (_, _, loc) -> loc
  | `IntLiteral (_, loc) -> loc
  | `FloatLiteral (_, loc) -> loc
  | `StringLiteral (_, loc) -> loc
  | `Abort loc -> loc
  | `Method (_, _, loc) -> loc
let infer imports files expr =
  let infer_error msg loc =
    Error.compile_error msg loc files in
  let rec match_fn_ty tvars loc num_params ty =
    match ty with 
    | `TArrow (param_tys, ret_ty) ->
      if List.length param_tys = num_params then
        tvars, param_tys, ret_ty
      else
        infer_error ("expected " ^ (string_of_int num_params)
                     ^ " arguments, but instead got " ^
                     (string_of_int (List.length param_tys))) loc
    | `TVar id ->
      begin
        match get_tyvar id tvars with
        | `Unbound (_, level) ->  
          let tvars, param_ty_list = List.fold_left_map
              (fun tvars _ -> make_new_unbound_var tvars level)
              tvars (List.init num_params (fun _ -> ())) in
          let tvars, ret_ty = make_new_unbound_var tvars level in
          let tvars = update_tvars tvars id (`Link (`TArrow (param_ty_list, ret_ty))) in
          tvars, param_ty_list, ret_ty
        | `Generic _ -> 
          infer_error
            ("expected a method but instead got a generic type variable")
            loc
        | `Link ty -> match_fn_ty tvars loc num_params ty
      end
    | ty -> 
      infer_error ("expected a method but instead got a " ^ (kind_of_non_tvar ty)) loc
  in
  let try_unify tvars already_unified locs ty1 ty2 =
    match (unify tvars already_unified ty1 ty2) with
    | `Ok state -> state
    | `Error e -> Error.compile_error (unify_error_msg e) locs files
  in
  let rec infer_method state env level meth =
    let (tvars, already_unififed) = state in
    let (args, body, _) = meth in
    let tvars, param_ty_list = List.fold_left_map
        (fun tvars _ -> make_new_unbound_var tvars level) tvars args in
    let fn_env = List.fold_left2
        (fun env param_name param_ty -> Env.extend env (fst param_name) param_ty)
        env args param_ty_list in
    let state, return_ty = infer (tvars, already_unififed) fn_env level body in
    state, `TArrow (param_ty_list, return_ty)
  and infer_record_select state env level expr =
    let (receiver, (name, _), _, _) = expr in
    let (tvars, already_unified) = state in
    let (tvars, rest_row_ty) = make_new_unbound_var tvars level in
    let (tvars, field_ty) = make_new_unbound_var tvars level in
    let receiver_ty = `TRecord (`TRowExtend(name, field_ty, rest_row_ty)) in
    let (tvars, already_unified), receiver_ty' =
      infer (tvars, already_unified) env level receiver in
    let (tvars, already_unified) =
      try_unify tvars already_unified (loc receiver) receiver_ty receiver_ty' in
    (tvars, already_unified), field_ty

  and infer state env level expr =
    match expr with
    | `EmptyPrototype _ -> state, `TRecord `TRowEmpty
    | `Abort _ -> 
      let tvars, already_unified = state in 
      let tvars, tvar = make_new_unbound_var tvars level in
      (tvars, already_unified), tvar
    | `IntLiteral _ -> state, (`TConst `Int)
    | `FloatLiteral _ -> state, (`TConst `Float)
    | `StringLiteral _ -> state, (`TConst `String)
    | `VariableLookup (name, loc) ->
      let tvars, already_unified = state in
      begin
        match Env.lookup env name with
        | Some ty ->
          let tvars, ty = instantiate tvars level ty in
          (tvars, already_unified), ty
        | None ->
          infer_error
            ("variable " ^ name ^ " was not previously defined")
            loc
      end
    | `Declaration ((name, _), rhs, post_decl_expr, _) ->
      let state, var_ty = infer state env (level + 1) rhs in
      let tvars, already_unified = state in
      let tvars, generalized_ty = generalize tvars level var_ty in
      let env' = Env.extend env name generalized_ty in
      let state', post_decl_ty = 
        infer (tvars, already_unified) env' level post_decl_expr in
      let tvars, already_unified = state' in
      (tvars, already_unified), post_decl_ty
    | `ExternalCall ((name, name_loc), args, _) ->
      let state = List.fold_left
          (fun state arg -> fst (infer state env level arg)) state args in
      let ty_str = String.sub 
          name
          (String.rindex name '_')
          (String.length name - String.rindex name '_')
      in
      let tvars, already_unified = state in
      let tvars, ty = match ty_str with
        | "_int" -> tvars, `TConst `Int
        | "_float" -> tvars, `TConst `Float
        | "_string" -> tvars, `TConst `String
        | "_bool" -> bool_type level tvars
        | _ -> 
          infer_error
            ("external call should end with '_int', '_float', '_string', or '_bool'"
             ^ " to signify its return type")
            name_loc
      in
      (tvars, already_unified), ty
    | `PrototypeExtension (extension, ((name, _), meth, _), _) ->
      let tvars, already_unified = state in
      let tvars, rest_row_ty = make_new_unbound_var tvars level in
      let tvars, field_ty = make_new_unbound_var tvars level in
      let extension_ty = `TRecord rest_row_ty in
      let state, extension_ty' = infer (tvars, already_unified) env level extension in
      let state, meth_ty = infer_method state env level meth in
      let tvars, already_unified = state in
      let tvars, already_unified =
        try_unify tvars already_unified (loc (`Method meth)) field_ty meth_ty in
      let tvars, already_unified =
        try_unify tvars already_unified (loc extension) extension_ty extension_ty' in
      if row_is_complete tvars rest_row_ty && get_row tvars rest_row_ty name = None then
        (tvars, already_unified), `TRecord (`TRowExtend(name, field_ty, rest_row_ty))
      else
        let tvars, field_ty' = make_new_unbound_var tvars level in 
        let tvars, rest_row_ty' = make_new_unbound_var tvars level in
        let (tvars, already_unified) =
          try_unify tvars already_unified (loc extension) extension_ty'
            (`TRecord (`TRowExtend(name, field_ty', rest_row_ty'))) in
        let (tvars, already_unified) =
          try_unify tvars already_unified (loc (`Method meth)) field_ty field_ty' in
        (tvars, already_unified), extension_ty

    | `TaggedObject ((tag, _), value, _) ->
      let tvars, already_unified = state in
      let tvars, rest_row_ty = make_new_unbound_var tvars level in
      let tvars, value_ty = make_new_unbound_var tvars level in
      let (tvars, already_unified), value_ty' =
        infer (tvars, already_unified) env level value in
      let tvars, already_unified =
        try_unify tvars already_unified (loc value) value_ty value_ty' in
      (tvars, already_unified), `TVariant (`TRowExtend(tag, value_ty, rest_row_ty))

    | `MethodInvocation invok ->
      let state, fn_ty = infer_record_select state env level invok in
      let (_, _, args, invok_loc) = invok in 
      let (tvars, already_unified) = state in 
      let tvars, param_tys, return_ty = 
        match_fn_ty tvars invok_loc (List.length args) fn_ty in 
      let state = List.fold_left2
          (fun state arg_ty arg -> 
             let (tvars, already_unified) = state in
             let (_, arg, _) = arg in
             let state, arg_ty' = infer (tvars, already_unified) env level arg in
             let (tvars, already_unified) = state in
             let (tvars, already_unified) =
               try_unify tvars already_unified (loc arg) arg_ty arg_ty' in
             (tvars, already_unified))
          (tvars, already_unified) param_tys args in
      let (tvars, already_unified) = state in
      (tvars, already_unified), return_ty
    | `PatternMatch (expr, cases, loc) ->
      let (tvars, already_unified) = state in
      let tvars, return_ty = make_new_unbound_var tvars level in
      let (tvars, already_unified), expr_ty = infer (tvars, already_unified) env level expr in
      let tvars, cases_row =
        infer_cases tvars already_unified env level return_ty `TRowEmpty cases in
      let tvars, already_unified =
        try_unify tvars already_unified loc expr_ty (`TVariant cases_row) in
      (tvars, already_unified), return_ty
    | `Import (filename, _) ->
      let expr = ResolveImports.ImportMap.find filename imports in
      infer state Env.empty level expr

  and infer_cases tvars already_unified env level return_ty rest_row_ty cases =
    match cases with
    | [] -> tvars, rest_row_ty
    | ((label, _), ((var_name, _), expr, _), loc) :: other_cases ->
      let tvars, variant_ty = make_new_unbound_var tvars level in
      let (tvars, already_unified), expr_ty =
        infer (tvars, already_unified) (Env.extend env var_name variant_ty) level expr
      in
      let tvars, already_unified =
        try_unify tvars already_unified loc return_ty expr_ty in
      let tvars, other_cases_row =
        infer_cases tvars already_unified env level return_ty rest_row_ty other_cases in
      let row_ty =`TRowExtend(label, variant_ty, other_cases_row) in
      tvars, row_ty
  in
  let (tvars, _), return_ty =
    infer ((0, TyVarMap.empty), TyVarPairSet.empty) Env.empty 0 expr
  in tvars, return_ty