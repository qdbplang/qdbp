let collect_methods ast =
  let rec collect_methods methods ast =
    match ast with
    | `App _ -> Error.internal_error "`App should be eliminated"
    | `Lambda _ -> Error.internal_error "`Lambda should be eliminated"
    | `Dispatch -> Error.internal_error "`Dispatch should be eliminated"
    | `Method _ -> Error.internal_error "`Method should be eliminated"
    | `Abort a -> (methods, `Abort a)
    | `VariableLookup v -> (methods, `VariableLookup v)
    | `IntProto i -> (methods, `IntProto i)
    | `EmptyPrototype e -> (methods, `EmptyPrototype e)
    | `StrProto s -> (methods, `StrProto s)
    | `Declaration (lhs, rhs, body, loc) ->
        let methods, rhs = collect_methods methods rhs in
        let methods, body = collect_methods methods body in
        (methods, `Declaration (lhs, rhs, body, loc))
    | `Drop (v, body, cnt) ->
        let methods, body = collect_methods methods body in
        (methods, `Drop (v, body, cnt))
    | `Dup (v, body, cnt) ->
        let methods, body = collect_methods methods body in
        (methods, `Dup (v, body, cnt))
    | `ExternalCall (fn, args, loc) ->
        let methods, args = List.fold_left_map collect_methods methods args in
        (methods, `ExternalCall (fn, args, loc))
    | `MethodInvocation (receiver, label, args, loc) ->
        let methods, receiver = collect_methods methods receiver in
        let methods, args =
          List.fold_left_map
            (fun methods (name, expr, loc) ->
              let methods, expr = collect_methods methods expr in
              (methods, (name, expr, loc)))
            methods args
        in
        (methods, `MethodInvocation (receiver, label, args, loc))
    | `PatternMatch (hasDefault, v, cases, loc) ->
        let methods, cases =
          List.fold_left_map
            (fun methods (tag, (arg, body, patternLoc), loc) ->
              let methods, body = collect_methods methods body in
              (methods, (tag, (arg, body, patternLoc), loc)))
            methods cases
        in
        (methods, `PatternMatch (hasDefault, v, cases, loc))
    | `TaggedObject (tag, value, loc) ->
        let methods, value = collect_methods methods value in
        (methods, `TaggedObject (tag, value, loc))
    | `PrototypeCopy (ext, (tag, meth, fieldLoc), size, loc, op) ->
        let methods, ext = collect_methods methods ext in
        let args, body, methLoc, meth_fvs = meth in
        let methods, body = collect_methods methods body in
        let method_id = Oo.id (object end) in
        let methods =
          AstTypes.IntMap.add method_id (args, body, methLoc, meth_fvs) methods
        in
        ( methods,
          `PrototypeCopy
            (ext, (tag, (method_id, meth_fvs), fieldLoc), size, loc, op) )
  in
  collect_methods AstTypes.IntMap.empty ast
