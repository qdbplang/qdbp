let abort msg =
  assert (msg != "");
  prerr_endline msg;
  exit (-1)

let internal_error msg = abort ("INTERNAL ERROR: " ^ msg)

let str_of_loc (loc : Lexing.position) =
  let file = loc.pos_fname in
  let line = loc.pos_lnum in
  let col = loc.pos_cnum - loc.pos_bol in
  file ^ ":" ^ string_of_int line ^ ":" ^ string_of_int col

let surrounding_characters line_length start_col end_col =
  if line_length < 60 then (0, line_length)
  else if end_col - start_col > 60 then (start_col, end_col - start_col)
  else
    let start_col = max 0 (start_col - 30) in
    if start_col + 60 > line_length then (line_length - 60, 60)
    else (start_col, 60)

let prelude_length = List.length (String.split_on_char '\n' Prelude.prelude) - 1
let line_info line_num line = string_of_int (line_num - prelude_length) ^ "| " ^ line
let line_info_len line_num = String.length (string_of_int line_num ^ "| ")


let str_of_locs ((loc1 : Lexing.position), (loc2 : Lexing.position)) files =
  assert (loc1.pos_fname = loc2.pos_fname);
  let file = AstTypes.StringMap.find loc1.pos_fname files in
  let lines = String.split_on_char '\n' file in
  if loc1.pos_lnum = loc2.pos_lnum then
    let line = List.nth lines (loc1.pos_lnum - 1) in
    let col1 = loc1.pos_cnum - loc1.pos_bol in
    let col2 = loc2.pos_cnum - loc2.pos_bol in
    let start_col, length =
      surrounding_characters (String.length line) col1 col2
    in
    let line = String.sub line start_col length in
    let arrows =
      String.make (col1 - start_col + line_info_len loc1.pos_lnum) ' '
      ^ String.make (col2 - col1) '^'
    in
    line_info loc1.pos_lnum line ^ "\n" ^ arrows
  else
    let line_list =
      List.filteri
        (fun i _ -> i >= loc1.pos_lnum - 1 && i <= loc2.pos_lnum - 1)
        lines
    in
    let line_list =
      List.mapi
        (fun linenum line -> line_info (linenum + loc1.pos_lnum) line)
        line_list
    in
    String.concat "\n" line_list

let compile_error msg (locs : Lexing.position * Lexing.position) files =
  abort
    ("Compile Error:" ^ "\n" ^ msg ^ "\n\n" ^ (fst locs).pos_fname ^ ":\n"
   ^ str_of_locs locs files)

let unify_error files msg loc1 loc2 =
  abort
    ("Unify Error:" ^ "\n" ^ msg ^ "\n" ^ "while unifying:\n"
   ^ str_of_locs loc1 files ^ "\nwith:\n" ^ str_of_locs loc2 files)
