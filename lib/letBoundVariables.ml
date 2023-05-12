module IntMap = Map.Make(struct type t = int let compare = compare end)
module VarSet = Set.Make(struct type t = int let compare = compare end)
let rec let_bound_variables ast =
  match ast with
  | `Abort a -> VarSet.empty, `Abort a
  | `Declaration (lhs, rhs, body, loc, fvs) ->
    let bvs, rhs = let_bound_variables rhs in
    let bvs', body = let_bound_variables body in
    let bvs'' = VarSet.union bvs bvs' in
    let bvs'' = VarSet.add (fst lhs) bvs'' in
    bvs'', `Declaration (lhs, rhs, body, loc, fvs, bvs'')
  | `VariableLookup v ->
    VarSet.empty, `VariableLookup v
  | `EmptyPrototype e ->
    VarSet.empty, `EmptyPrototype e
  | `StringLiteral s ->
    VarSet.empty, `StringLiteral s
  | `IntLiteral i ->
    VarSet.empty, `IntLiteral i
  | `FloatLiteral f ->
    VarSet.empty, `FloatLiteral f
  | `MethodInvocation (receiver, label, args, loc, fvs) ->
    let bvs, receiver = let_bound_variables receiver in
    let bvs', args = List.fold_left_map (
      fun bvs (name, expr, loc) ->
        let bvs', expr = let_bound_variables expr in
        VarSet.union bvs bvs', (name, expr, loc)
    ) bvs args in
    let bvs'' = VarSet.union bvs bvs' in
    bvs'', `MethodInvocation (receiver, label, args, loc, fvs, bvs'')
  | `Drop (v, value) ->
    let bvs, value = let_bound_variables value in
    bvs, `Drop (v, value)
  | `Dup (v, value) ->
    let bvs, value = let_bound_variables value in
    bvs, `Dup (v, value)
  | `PatternMatch (v, cases, loc, fvs) ->
    let bvs, cases = List.fold_left_map (
      fun bvs (tag, (arg, body, patternLoc), loc) ->
        let bvs', body = let_bound_variables body in
        let bvs'' = VarSet.union bvs bvs' in
        let bvs'' = VarSet.add (fst arg) bvs'' in
        bvs'', (tag, (arg, body, patternLoc), loc)
    ) VarSet.empty cases in
    bvs, `PatternMatch (v, cases, loc, fvs, bvs)
  | `PrototypeCopy (ext, (tag, meth, fieldLoc), loc, op, fvs) ->
    let bvs, ext = let_bound_variables ext in
    bvs, `PrototypeCopy (ext, (tag, meth, fieldLoc), loc, op, fvs, bvs)
  | `TaggedObject (tag, value, loc, fvs) ->
    let bvs, value = let_bound_variables value in
    bvs, `TaggedObject (tag, value, loc, fvs, bvs)
  | `ExternalCall (fn, args, loc, fvs) ->
    let bvs, args = List.fold_left_map (
      fun bvs arg ->
        let bvs', arg = let_bound_variables arg in
        VarSet.union bvs bvs', arg
    ) VarSet.empty args in
    bvs, `ExternalCall (fn, args, loc, fvs, bvs)

