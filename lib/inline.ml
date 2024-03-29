let rename_args arg_ids fvs body =
  let argmap =
    List.fold_left
      (fun argmap (id, _) ->
        AstTypes.StringMap.add id
          ("V" ^ string_of_int (Oo.id (object end)))
          argmap)
      AstTypes.StringMap.empty arg_ids
  in
  let argmap =
    AstTypes.StringSet.fold
      (fun v argmap -> AstTypes.StringMap.add v v argmap)
      fvs argmap
  in
  let varname varmap name = AstTypes.StringMap.find name varmap in
  let rec rename varmap body =
    match body with
    | `PatternMatch (hasDefault, receiver, cases, loc) ->
        let receiver = rename varmap receiver in
        let cases =
          List.map
            (fun ((tag, tagLoc), ((arg, argLoc), body, patternLoc), caseLoc) ->
              let varmap = AstTypes.StringMap.add arg arg varmap in
              let body = rename varmap body in
              ((tag, tagLoc), ((arg, argLoc), body, patternLoc), caseLoc))
            cases
        in
        `PatternMatch (hasDefault, receiver, cases, loc)
    | `MethodInvocation (receiver, (name, labelLoc), args, loc) ->
        let receiver = rename varmap receiver in
        let args =
          List.map
            (fun (name, a, loc) ->
              let a = rename varmap a in
              (name, a, loc))
            args
        in
        `MethodInvocation (receiver, (name, labelLoc), args, loc)
    | `PrototypeCopy
        (ext, ((name, labelLoc), (args, body, methLoc), fieldLoc), size, loc, op)
      ->
        let ext = rename varmap ext in
        let varmap =
          List.fold_left
            (fun varmap arg ->
              AstTypes.StringMap.add (fst arg) (fst arg) varmap)
            varmap args
        in
        let body = rename varmap body in
        `PrototypeCopy
          ( ext,
            ((name, labelLoc), (args, body, methLoc), fieldLoc),
            size,
            loc,
            op )
    | `TaggedObject (tag, value, loc) ->
        `TaggedObject (tag, rename varmap value, loc)
    | `Declaration ((name, nameLoc), rhs, body, loc) ->
        let rhs = rename varmap rhs in
        let varmap = AstTypes.StringMap.add name name varmap in
        let body = rename varmap body in
        `Declaration ((name, nameLoc), rhs, body, loc)
    | `VariableLookup (v, loc) -> `VariableLookup (varname varmap v, loc)
    | `ExternalCall (name, args, loc) ->
        let args = List.map (rename varmap) args in
        `ExternalCall (name, args, loc)
    | `Abort _ as a -> a
    | `StrProto _ as s -> s
    | `EmptyPrototype _ as e -> e
    | `IntProto _ as e -> e
  in
  let arg_ids = List.map (fun (id, loc) -> (varname argmap id, loc)) arg_ids in
  (arg_ids, rename argmap body)

let inline depth expr =
  let can_inline proto label env =
    match AstTypes.StringMap.find_opt label proto with
    | Some (_, _, _, fvs) ->
        let res =
          AstTypes.StringSet.for_all
            (fun fv -> AstTypes.StringMap.mem fv env)
            fvs
        in
        res
    | None -> false
  in
  let rec inline depth env expr =
    match expr with
    | `PatternMatch (hasDefault, receiver, cases, loc) -> (
        let peval_receiver, receiver = inline depth env receiver in
        match peval_receiver with
        | `Variant (label, payload)
          when List.length
                 (List.find_all (fun ((tag, _), _, _) -> label = tag) cases)
               = 1 ->
            let case = List.find (fun ((tag, _), _, _) -> label = tag) cases in
            let tag, ((argname, argloc), body, patternLoc), caseLoc = case in
            let env = AstTypes.StringMap.add argname payload env in
            let pevalbody, body = inline depth env body in
            ( pevalbody,
              `PatternMatch
                ( hasDefault,
                  receiver,
                  [ (tag, ((argname, argloc), body, patternLoc), caseLoc) ],
                  loc ) )
        | `Unit | `Variant _ ->
            if List.length cases != 1 then
              let cases =
                List.map
                  (fun ( (tag, tagLoc),
                         ((arg, argLoc), body, patternLoc),
                         caseLoc ) ->
                    let env = AstTypes.StringMap.add arg `Unit env in
                    let _, body = inline depth env body in
                    ((tag, tagLoc), ((arg, argLoc), body, patternLoc), caseLoc))
                  cases
              in
              (`Unit, `PatternMatch (hasDefault, receiver, cases, loc))
            else
              let case = List.hd cases in
              let (tag, tagLoc), ((arg, argLoc), body, patternLoc), caseLoc =
                case
              in
              let env = AstTypes.StringMap.add arg `Unit env in
              let peval, body = inline depth env body in
              let case =
                ((tag, tagLoc), ((arg, argLoc), body, patternLoc), caseLoc)
              in
              (peval, `PatternMatch (hasDefault, receiver, [ case ], loc))
        | `Proto _ -> Error.internal_error "Shouldn't have proto here")
    | `MethodInvocation (receiver, (name, labelLoc), args, loc) -> (
        let peval_receiver, receiver = inline depth env receiver in
        match peval_receiver with
        | `Variant _ -> Error.internal_error "Expected proto or unit"
        | `Proto p when can_inline p name env ->
            let arg_ids, body, _, methFvs = AstTypes.StringMap.find name p in
            let arg_ids, body = rename_args arg_ids methFvs body in
            let arg_exprs =
              List.map
                (fun (_, e, _) ->
                  let _, e = inline depth env e in
                  e)
                args
            in
            let args = List.combine arg_exprs arg_ids in
            let inlined =
              List.fold_right
                (fun (arg, arg_id) e -> `Declaration (arg_id, arg, e, loc))
                args body
            in
            if depth > 0 then
              let _, inlined = FreeVariablesAndDCE.free_variables inlined in
              (`Unit, inlined)
            else (`Unit, inlined)
        | `Unit | `Proto _ ->
            let args =
              List.map
                (fun (name, arg, loc) ->
                  let _, arg = inline depth env arg in
                  (name, arg, loc))
                args
            in
            (`Unit, `MethodInvocation (receiver, (name, labelLoc), args, loc)))
    | `PrototypeCopy
        (ext, ((name, labelLoc), (args, body, methLoc), fieldLoc), size, loc, op)
      ->
        let peval_ext, ext = inline depth env ext in
        (* Add each arg to env *)
        let env =
          List.fold_left
            (fun env (name, _) -> AstTypes.StringMap.add name `Unit env)
            env args
        in
        let _, body = inline depth env body in
        let meth_fvs, body = FreeVariablesAndDCE.free_variables body in
        let meth_fvs =
          List.fold_left
            (fun meth_fvs (id, _) -> AstTypes.StringSet.remove id meth_fvs)
            meth_fvs args
        in
        let peval =
          match peval_ext with
          | `Proto proto ->
              `Proto
                (AstTypes.StringMap.add name
                   (args, body, methLoc, meth_fvs)
                   proto)
          | `Unit -> `Unit
          | `Variant _ -> Error.internal_error "Expected proto or unit"
        in
        ( peval,
          `PrototypeCopy
            ( ext,
              ((name, labelLoc), (args, body, methLoc), fieldLoc),
              size,
              loc,
              op ) )
    | `TaggedObject ((tag, tagLoc), value, loc) ->
        let peval_value, value = inline depth env value in
        (`Variant (tag, peval_value), `TaggedObject ((tag, tagLoc), value, loc))
    | `Declaration ((name, nameLoc), rhs, body, loc) ->
        let peval_rhs, rhs = inline depth env rhs in
        let env = AstTypes.StringMap.add name peval_rhs env in
        let peval_body, body = inline depth env body in
        (peval_body, `Declaration ((name, nameLoc), rhs, body, loc))
    | `VariableLookup (v, loc) ->
        (AstTypes.StringMap.find v env, `VariableLookup (v, loc))
    | `ExternalCall (name, args, loc) ->
        let args =
          List.map
            (fun arg ->
              let _, e = inline depth env arg in
              e)
            args
        in
        (`Unit, `ExternalCall (name, args, loc))
    | `EmptyPrototype _ as e -> (`Proto AstTypes.StringMap.empty, e)
    | `IntProto _ as e -> (`Unit, e)
    | `StrProto _ as s -> (`Unit, s)
    | `Abort _ as a -> (`Unit, a)
  in
  let _, expr = inline depth AstTypes.StringMap.empty expr in
  expr
