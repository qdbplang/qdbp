module StringMap = Map.Make(String)

let add_variable varnames name =
  let id = Oo.id (object end) in
  StringMap.add name (id) varnames

let varname varnames name = 
  StringMap.find name varnames

let names_to_ints ast =
  (* FIXME: Make this purely functional *)
  let label_map = Hashtbl.create 10 in
  (* Need to hardcode so we can use from FFI *)
  Hashtbl.add label_map "False" 0;
  Hashtbl.add label_map "True" 1;
  let get_label name =
    match Hashtbl.find_opt label_map name with
    | Some label -> label
    | None ->
      let label = Oo.id (object end) in
      Hashtbl.add label_map name label;
      label
  in
  let rec names_to_ints varnames ast =
    match ast with
    | `EmptyPrototype loc ->
      `EmptyPrototype loc
    | `PrototypeCopy
        (ext, ((name, labelLoc), (args, body, methLoc), fieldLoc), loc, op) -> 
      let ext = names_to_ints varnames ext in
      let arg_names = List.map (fun (name, _) -> name) args in
      let varnames = List.fold_left add_variable varnames arg_names in
      let body = names_to_ints varnames body in
      let args = List.map (fun (name, loc) -> (varname varnames name, loc)) args in
      `PrototypeCopy
        (ext, (((get_label name), labelLoc), (args, body, methLoc), fieldLoc), loc, op)
    | `TaggedObject ((tag, tagLoc), value, loc) -> 
      let value = names_to_ints varnames value in
      `TaggedObject (((get_label tag), tagLoc), value, loc)
    | `MethodInvocation (receiver, (name, labelLoc), args, loc) ->
      let args = List.map (fun (name, arg, loc) -> name, names_to_ints varnames arg, loc) args in
      let receiver = names_to_ints varnames receiver in
      `MethodInvocation (receiver, ((get_label name), labelLoc), args, loc)
    | `PatternMatch (receiver, cases, loc) -> 
      let receiver = names_to_ints varnames receiver in
      let cases = List.map (
          fun ((name, nameLoc), ((arg, argLoc), body, patternLoc), loc) ->
            let varnames = add_variable varnames arg in
            let body = names_to_ints varnames body in
            (((get_label name), nameLoc), ((varname varnames arg, argLoc), body, patternLoc), loc)
        ) cases in
      let receiver_id = Oo.id (object end) in
      `Declaration ((receiver_id, loc), receiver, 
                    `PatternMatch (receiver_id, cases, loc), loc)
    | `Declaration ((name, nameLoc), rhs, body, loc) ->
      let rhs = names_to_ints varnames rhs in
      let varnames = add_variable varnames name in
      let body = names_to_ints varnames body in
      `Declaration (((varname varnames name), nameLoc), rhs, body, loc)
    | `VariableLookup (name, loc) ->
      let name = varname varnames name in
      `VariableLookup (name, loc)
    | `ExternalCall ((name, nameLoc), args, loc) ->
      let args = List.map (names_to_ints varnames) args in
      `ExternalCall ((name, nameLoc), args, loc)
    | `IntLiteral (i, loc) ->
      `IntLiteral (i, loc)
    | `FloatLiteral (f, loc) -> 
      `FloatLiteral (f, loc)
    | `StringLiteral (s, loc) ->
      `StringLiteral (s, loc)
    | `Abort loc ->
      `Abort loc
  in 
  names_to_ints StringMap.empty ast