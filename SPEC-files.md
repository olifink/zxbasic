# SPEC-files.md - ZXBasic Filesystem & Program Store Management

Specification for disk file operations, cataloging, program merging, and program line store range deletion in ZXBasic, adapted to modern POSIX environments while honoring Sinclair ZX Spectrum BASIC conventions.

---

## 1. Overview & Execution Ground Truth

In ZXBasic, program storage and persistence are managed across the `Program` and `Runtime` subsystems:
* **Program Store (`Program` in `src/program.h`, `src/program.c`):**
  * Maintains an ordered array of `ProgramLine` structs sorted strictly ascending by `line_no` (`1 .. 9999`).
  * `program_set_line(Program *p, uint16_t line_no, const char *source)`: Inserts a new line or replaces an existing line. If `source` is empty, deletes the line.
  * `program_clear(Program *p)`: Erases all lines from memory.
  * `program_list(const Program *p, uint16_t start_line, FILE *out)`: Outputs stored lines formatted as `<line_no> <source>\n`.
* **Runtime Execution State (`Runtime` in `src/runtime.h`, `src/runtime.c`):**
  * `symtab`: Variable symbol table storing scalar numbers, strings, and multi-dimensional arrays.
  * `is_running`, `cur_line_idx`, `cur_stmt_idx`, `cur_offset`: Control flow pointers.
  * `call_stack`, `for_stack`: Subroutine and loop frames.
  * `runtime_load(rt, path)`: Currently clears the entire program store (`program_clear`) and variables (`runtime_clear`), then parses lines from disk.
  * `runtime_save(rt, path)`: Writes stored lines sequentially to a text file.

### Summary of New Additions
| Command | Primary Scope | Description |
| --- | --- | --- |
| `MERGE <file_expr>` | Program / Disk | Loads program lines from file into memory without clearing the existing program or variables; colliding line numbers are overwritten. |
| `RUN [<file_expr>] [<line_expr>]` | Execution / Disk | Runs current program or loads `<file_expr>` and executes it from the specified line (or start). |
| `SAVE <file_expr> [LINE [<line_expr>]]` | Program / Disk | Saves program to disk. If optional `LINE` is specified, appends an auto-run directive (`RUN [n]`) to the end of the file. |
| `CAT [<path_expr>]` | Filesystem | Catalogs and displays directory entries line-by-line relative to the working/program directory. |
| `DELETE <file_expr>` | Filesystem | Removes the specified file from the host filesystem. |
| `DELETE [<from>] TO [<to>]` | Program Store | Deletes a range of stored program lines between `<from>` and `<to>` in memory. |
| `LIST [[<from>] TO [<to>] \| <start>]` | Program Store | Lists stored program lines across the specified range (or whole program). |

---

## 2. Program Merging: `MERGE "file"`

### 2.1 Syntax & Invocation
* **Syntax:** `MERGE <filepath_expr>`
* **Argument:** Any valid string expression (e.g. `MERGE "subroutines.bas"`, `MERGE F$`, `MERGE "lib/" + MODULE$ + ".bas"`).
* **Availability:** Valid both in immediate mode and as a program statement (enabling dynamic overlay loading).
* **Syntax Errors:** Missing argument or non-string expression raises report `C Nonsense in BASIC`.

### 2.2 Operational Semantics
1. **Preservation of Memory:**
   * Unlike `LOAD`, `MERGE` **does not** call `program_clear(&rt->program)` or `runtime_clear(rt)`.
   * All existing variables in `rt->symtab`, call frames, and loop stacks are preserved.
2. **Line Ingestion & Collision Overwriting:**
   * Reads the file line-by-line from disk.
   * For each line with a line number (`<line_no> <text>`):
     * If `<line_no>` already exists in `rt->program`, its text is replaced with the newly read line.
     * If `<line_no>` does not exist, it is inserted in sorted order into `rt->program`.
   * Lines undergo canonical keyword normalization (`normalize_line`).
3. **Handling of Trailing Direct Commands:**
   * If the file contains unnumbered lines (such as a trailing `RUN` statement from a `SAVE ... LINE` auto-run export), `MERGE` **ignores** them to prevent unintended program execution during a merge.
4. **File Errors:**
   * If the target file cannot be opened (non-existent, read permission denied), the operation aborts with report `C Nonsense in BASIC`, leaving the existing program memory untouched.

---

## 3. Program Execution & Chain-Loading: `RUN ["file"] [line]`

### 3.1 Syntax Variations
* `RUN` — Clears variables and executes the current in-memory program from line 1.
* `RUN <line_expr>` — Clears variables and executes the current in-memory program starting at `<line_expr>`.
* `RUN <file_expr>` — Loads `<file_expr>` from disk and runs it from line 1 (or its embedded auto-run target).
* `RUN <file_expr> <line_expr>` or `RUN <file_expr>, <line_expr>` — Loads `<file_expr>` from disk and begins execution at `<line_expr>`.

### 3.2 Parsing & Disambiguation
The parser checks the token immediately following `RUN`:
1. **No Tokens (or `:` / newline):** Defaults to `start_line = 0`, executes current program.
2. **Numeric Expression:** Evaluates `<line_expr>`. If valid line number (`1 .. 9999`), resets runtime and executes current program from that line.
3. **String Expression:** Evaluates `<file_expr>`.
   * Checks for an optional trailing delimiter: either a comma `,` or direct whitespace separation.
   * If followed by a numeric expression, evaluates `<line_expr>`.
   * Clears the current program and runtime (`runtime_load`), ingesting `<file_expr>`.
   * If `<line_expr>` was provided, begins execution at that line; otherwise starts from line 1 (or the loaded program's auto-run directive).

### 3.3 Programmatic Chain-Loading
* When executed inside a running program (`1000 RUN "level2.bas"`), this acts as a classic Sinclair chain-load:
  * The current program and variables are replaced by the new file.
  * Execution resumes seamlessly from the start of the new program.
* If the file cannot be loaded, execution halts with report `C Nonsense in BASIC` at the statement where `RUN` was invoked.

---

## 4. Program Serialization & Auto-Run: `SAVE "file" [LINE [n]]`

### 4.1 Syntax Variations
* `SAVE <file_expr>` — Standard save: exports all stored program lines to `<file_expr>`.
* `SAVE <file_expr> LINE` — Auto-run save: exports program lines and appends `RUN` at the end.
* `SAVE <file_expr> LINE <line_expr>` — Auto-run save with target: exports program lines and appends `RUN <n>` at the end.

### 4.2 File Format & Auto-Run Mechanism
1. **Source Export:**
   * Writes all program lines in ascending order:
     ```basic
     10 PRINT "WELCOME"
     20 GOTO 10
     ```
2. **Auto-Run Appending:**
   * If `LINE` is specified without `<line_expr>`:
     * Appends a trailing line:
       ```basic
       RUN
       ```
   * If `LINE <line_expr>` is specified:
     * Evaluates `<line_expr>` as an integer. Must be in range `1 .. 9999` (otherwise raises `B Integer out of range`).
     * Appends a trailing line:
       ```basic
       RUN <n>
       ```
3. **Ingestion Behavior upon `LOAD`:**
   * When `LOAD "file"` or host invocation reads the saved file, numbered lines are ingested into `rt->program`.
   * When the parser reaches the unnumbered trailing line `RUN [n]`, it executes it immediately as a direct command, launching the program automatically without requiring manual user input.

---

## 5. Directory Inspection: `CAT ["path"]`

### 5.1 Syntax Variations
* `CAT` — Displays contents of the current program / working directory (`.`).
* `CAT <path_expr>` — Displays contents of the specified sub-path or directory string.

### 5.2 Path Resolution
* If `<path_expr>` is omitted: catalogs the current working directory (`.`).
* If `<path_expr>` is provided:
  * If it begins with `/` (POSIX absolute path), resolves directly.
  * If it is relative (e.g., `"levels"` or `"./data"`), resolves relative to the current working directory.

### 5.3 Output Display Format
Entries are retrieved using POSIX `opendir` / `readdir` / `stat`, sorted alphabetically, and output line-by-line to `rt->out`:
* Hidden files (names beginning with `.`) are ignored by default.
* Subdirectories are displayed with a `[DIR]` prefix or trailing `/`.
* Regular files display their size in bytes.

```text
Directory of ./
  [DIR]  levels/
  [DIR]  sprites/
  [FILE] adventure.bas        1,420 bytes
  [FILE] intro.bas              384 bytes
  [FILE] readme.txt             112 bytes
```

### 5.4 Error Handling
* If the path does not exist or is not a readable directory, reports `C Nonsense in BASIC`.

---

## 6. Deletion Primitives: `DELETE`

The `DELETE` keyword serves a dual role in Sinclair-derived systems: filesystem file deletion and in-memory program line deletion.

### 6.1 Disambiguation Rule
The parser and syntax normalizer inspect the token immediately following `DELETE`:
* **File Deletion:** If the next token evaluates to a string expression (`TOKEN_STRING`, string variable identifier `A$`, or string expression), dispatch to **Filesystem Deletion** (`6.2`).
* **Program Line Deletion:** If the next token is `TOKEN_TO` or evaluates to a numeric expression, dispatch to **Program Line Deletion** (`6.3`).
* Any other token combination raises `C Nonsense in BASIC`.

---

### 6.2 Filesystem Deletion: `DELETE "file"`
* **Syntax:** `DELETE <file_expr>`
* **Semantics:** Removes the file designated by `<file_expr>` from disk via the POSIX `unlink(2)` system call.
* **Scope:** Valid both in Immediate Mode and inside stored program lines.
* **Error Handling:**
  * If the file does not exist (`ENOENT`), permission is denied (`EACCES`), or the target is a directory: raises report `C Nonsense in BASIC`.

---

### 6.3 Program Line Range Deletion: `DELETE [from] TO [to]`
* **Syntax Forms:**
  1. `DELETE <from> TO <to>` — Deletes all lines whose numbers satisfy $from \le \text{line\_no} \le to$.
  2. `DELETE TO <to>` — Omitted `from`: deletes all lines from the first stored line up to $to$ ($\text{line\_no} \le to$).
  3. `DELETE <from> TO` — Omitted `to`: deletes all lines from $from$ up to the last stored line ($\text{line\_no} \ge from$).
  4. `DELETE TO` — Both omitted: deletes all stored program lines from memory (equivalent to clearing the program store).
* **Optional Single-Line Convenience:**
  * `DELETE <line>` (without `TO` and not a string) can be supported as shorthand for `DELETE <line> TO <line>`.
* **Execution Environment:**
  * **Immediate Mode Only:** Like `EDIT`, `AUTO`, and `RENUM`, altering the program structure wholesale during active program execution is prohibited. If executed inside a running program (`rt->is_running == true`), raises report `C Nonsense in BASIC`.
* **Range Validation:**
  * Line numbers must be integers in range `1 .. 9999`. Non-integers or values outside this range raise `B Integer out of range`.
  * If $from > to$, no lines match; the operation is a no-op (or raises `B Integer out of range`).
* **Implementation Algorithm:**
  ```c
  void program_delete_range(Program *p, uint16_t from, uint16_t to) {
      size_t write_idx = 0;
      for (size_t read_idx = 0; read_idx < p->count; ++read_idx) {
          uint16_t line_no = p->lines[read_idx].line_no;
          if (line_no >= from && line_no <= to) {
              free(p->lines[read_idx].source);
          } else {
              if (write_idx != read_idx) {
                  p->lines[write_idx] = p->lines[read_idx];
              }
              write_idx++;
          }
      }
      p->count = write_idx;
  }
  ```

---

## 7. Program Listing: `LIST [<from>] [TO [<to>]]`

Like `DELETE`, the `LIST` statement supports program line ranges using `TO`, matching Sinclair BASIC conventions.

### 7.1 Syntax Variations
* `LIST` — Lists all program lines in memory (lines `1 .. 9999`).
* `LIST <start_expr>` — Lists from line `<start_expr>` to the end of the program (Sinclair standard single-parameter form).
* `LIST <from> TO <to>` — Lists all lines whose line numbers satisfy $from \le \text{line\_no} \le to$.
* `LIST TO <to>` — Omitted `from`: lists lines from the first stored line up to $to$ ($\text{line\_no} \le to$).
* `LIST <from> TO` — Omitted `to`: lists lines from $from$ up to the end of the program ($\text{line\_no} \ge from$).
* `LIST TO` — Both omitted: lists all stored program lines in memory (equivalent to `LIST`).

### 7.2 Semantics & Display
* Formats lines as `<line_no> <source_text>\n` emitted to `rt->out`.
* Lines are retrieved in ascending line number order.
* If $from > to$, no lines match the specified range and no output is produced.
* Valid both in Immediate Mode and inside stored program lines.

### 7.3 Error Conditions
* Line numbers must evaluate to integers in range `1 .. 9999`. Non-integers or values outside range raise `B Integer out of range`.
* Non-numeric expressions raise `C Nonsense in BASIC`.

---

## 8. Grammar, Lexer & Normalizer Specifications

### 8.1 New Keywords & Tokens
* `TOKEN_MERGE` — Keyword `"MERGE"`
* `TOKEN_CAT` — Keyword `"CAT"`
* `TOKEN_DELETE` — Keyword `"DELETE"`
* `TOKEN_LINE` — Keyword `"LINE"`

*(Existing tokens `TOKEN_LIST`, `TOKEN_RUN`, `TOKEN_SAVE`, `TOKEN_LOAD`, and `TOKEN_TO` are reused.)*

### 8.2 Keyword Auto-Capitalization (`src/normalize.c`)
Add `"MERGE"`, `"CAT"`, `"DELETE"`, `"LINE"` to `is_zx_keyword()`.
* Input: `10 merge "lib.bas": cat "data": delete 10 to 50`
* Stored: `10 MERGE "lib.bas": CAT "data": DELETE 10 TO 50`
* Input: `list 20 to 100`
* Normalized: `LIST 20 TO 100`
* Input: `save "backup.bas" line 20`
* Normalized: `SAVE "backup.bas" LINE 20`

### 8.3 Ingestion Syntax Gating
* `LIST [[<expr>] TO [<expr>] | <expr>]`: valid line numbers with optional `TO`.
* `MERGE <expr>`: `<expr>` must be valid syntax.
* `CAT [<expr>]`: optional `<expr>` must be valid syntax.
* `DELETE <expr>`: valid file string expression.
* `DELETE [from] TO [to]`: valid line numbers with `TO`.
* `SAVE <expr> [LINE [expr]]`: valid syntax for path and optional line target.
* `RUN [<expr>] [ [,] <expr>]`: valid syntax for optional file string and optional line number.

---

## 9. Architectural Runtime Changes

### 9.1 Module Interfaces

#### `src/program.h` & `src/program.c`
```c
// Deletes all lines with line_no between from and to (inclusive)
void program_delete_range(Program *p, uint16_t from, uint16_t to);

// Lists program lines between from and to (inclusive) to out stream
void program_list_range(const Program *p, uint16_t from, uint16_t to, FILE *out);
```

#### `src/runtime.h` & `src/runtime.c`
```c
// Merges program from file without clearing variables or non-colliding lines
bool runtime_merge(Runtime *rt, const char *path);

// Extended save supporting optional auto-run line (0 = none, or MIN_LINE_NO..MAX_LINE_NO)
// has_line=true with line_no=0 appends "RUN"; has_line=true with line_no>0 appends "RUN n"
bool runtime_save_ext(Runtime *rt, const char *path, bool has_line, uint16_t line_no);

// Lists directory contents line-by-line
bool runtime_cat(Runtime *rt, const char *subpath);

// Unlinks specified file from disk
bool runtime_delete_file(Runtime *rt, const char *path);
```

#### `src/parser.c`
* Dispatch `TOKEN_LIST`: parse optional range `[from] [TO [to]]` and call `program_list_range`.
* Dispatch `TOKEN_MERGE`: parse string expression $\rightarrow$ `runtime_merge`.
* Dispatch `TOKEN_CAT`: parse optional string expression $\rightarrow$ `runtime_cat`.
* Dispatch `TOKEN_DELETE`:
  * Lookahead: if string expression $\rightarrow$ `runtime_delete_file`.
  * If `TOKEN_TO` or numeric expression $\rightarrow$ parse `[from] TO [to]` range and call `program_delete_range`.
* Update `TOKEN_RUN`:
  * If next token is string: parse filename, optional comma/space, optional line number. Call `runtime_load` then run.
  * If next token is number: retain existing behavior (`runtime_run(rt, start_line)`).
* Update `TOKEN_SAVE`:
  * Parse path string. Check for optional `TOKEN_LINE`. If present, check for optional line number expression. Call `runtime_save_ext`.

---

## 10. Verification & Test Scenarios

### 10.1 Test Cases for Test Suite (`tests/test_files.c`)
1. **`MERGE` Integrity:**
   * Create Program A: `10 PRINT "A"`, `20 PRINT "A2"`, `30 PRINT "A3"`.
   * Create Program B on disk: `20 PRINT "B2_OVERWRITE"`, `40 PRINT "B4_NEW"`.
   * Execute `MERGE "B"`.
   * Verify lines: `10 PRINT "A"`, `20 PRINT "B2_OVERWRITE"`, `30 PRINT "A3"`, `40 PRINT "B4_NEW"`.
   * Verify existing variables in `symtab` are completely untouched.
2. **`RUN` with File and Line:**
   * Test `RUN "prog.bas"` executes loaded program from line 1.
   * Test `RUN "prog.bas", 20` executes starting at line 20.
   * Test programmatic chain-loading inside a running program.
3. **`SAVE ... LINE` Auto-Run:**
   * Save with `SAVE "auto.bas" LINE 30`.
   * Inspect saved file on disk; verify last line is `RUN 30`.
   * Call `LOAD "auto.bas"`; verify program automatically executes and reaches line 30.
   * Save with `SAVE "auto2.bas" LINE`; verify last line is `RUN`.
4. **`CAT` Directory Listing:**
   * Call `CAT` on current workspace; verify expected files appear line-by-line.
   * Call `CAT "tests"`; verify subdirectory files appear.
   * Verify error handling on non-existent directory.
5. **`DELETE "file"`:**
   * Create temporary file on disk. Call `DELETE "temp.bas"`. Verify file is unlinked.
   * Call `DELETE "nonexistent.bas"`; verify report `C Nonsense in BASIC`.
6. **`DELETE [from] TO [to]` Program Lines:**
   * Populate program with lines `10, 20, 30, 40, 50`.
   * Test `DELETE 20 TO 40` $\rightarrow$ lines `10, 50` remain.
   * Test `DELETE TO 20` $\rightarrow$ line `50` remains.
   * Test `DELETE 50 TO` $\rightarrow$ no lines remain.
   * Verify attempting inside a running program raises `C Nonsense in BASIC`.
7. **`LIST [<from>] [TO [<to>]]` Program Ranges:**
   * Populate program with lines `10, 20, 30, 40, 50`.
   * Test `LIST 20 TO 40` $\rightarrow$ outputs lines `20, 30, 40`.
   * Test `LIST TO 30` $\rightarrow$ outputs lines `10, 20, 30`.
   * Test `LIST 30 TO` $\rightarrow$ outputs lines `30, 40, 50`.
   * Test `LIST TO` $\rightarrow$ outputs all lines `10 .. 50`.
   * Test `LIST 20` $\rightarrow$ outputs lines `20 .. 50`.
   * Test `LIST 50 TO 20` $\rightarrow$ outputs nothing.
