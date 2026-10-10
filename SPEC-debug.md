# SPEC-debug.md - ZXBasic Interactive Debugging & Inspection

Specification for interactive inspection, breakpoint management, and execution resumption in ZXBasic, referencing the current execution engine as ground truth.

---

## 1. Overview & Execution Ground Truth

In ZXBasic, program execution is managed by the `Runtime` subsystem (`src/runtime.h`, `src/runtime.c`):

* **Execution Pointers:**
  * `cur_line_idx`: 0-based index into `program.lines` (`0 .. program.count - 1`).
  * `cur_stmt_idx`: 1-based index of the statement currently executing within the line (`1, 2, ...`).
  * `cur_offset`: character offset into the source string for statement parsing.
* **Stop / Interruption States:**
  * `STOP` statement: Halts continuous execution, sets `stop_requested = true`, `last_error.code = ERR_STOP`, and emits report `9 STOP statement, <line>:<stmt>`.
  * Break / SIGINT (Ctrl+C): Sets `g_interrupted`, causing `stop_requested = true`, `last_error.code = ERR_STOP`, `last_error.custom_msg = "BREAK into program"`, emitting report `9 BREAK into program, <line>:<stmt>`.
  * Breakpoint / Stop Target: Halts execution before executing the targeted line, emitting report `9 Breakpoint reached, <line>:1`.
* **State Preservation:**
  * Halting execution sets `is_running = false` but leaves all variables (`symtab`), system variables (`sysvars`), call frames (`call_stack`), loop frames (`for_stack`), and data pointers intact.
  * Direct commands in immediate mode (e.g. `PRINT X`, `LET Y = 10`, `VARS`) execute within this suspended runtime environment without resetting program counters or stacks.

---

## 2. Variable Inspection: `VARS`

### 2.1 Syntax & Availability
* **Syntax:** `VARS`
* **Availability:** Valid both in interactive Immediate Mode and as a program instruction inside lines (e.g., `100 VARS`).
* **Arguments:** Takes no arguments. Trailing characters before a statement delimiter (`:` or newline) raise `C Nonsense in BASIC`.

### 2.2 Behavior
Dumps all user-allocated scalar variables and arrays from `rt->symtab` to standard output as a structured table.

### 2.3 Display Format
```text
Variable   Type       Dimensions / Size   Value Preview
─────────────────────────────────────────────────────────────────
A          NUMBER     scalar              42.125
SCORE      NUMBER     scalar              1500
NAME$      STRING     14 bytes            "Sinclair User"
M          ARRAY(N)   (10, 10)            [[0, 0, ...], ...]
C$         ARRAY(S)   (5, 32)             ["Row 1", ...]
```

* **Columns:**
  1. `Variable`: Variable identifier name (e.g. `A`, `NAME$`).
  2. `Type`: Data classification (`NUMBER`, `STRING`, `ARRAY(N)`, `ARRAY(S)`).
  3. `Dimensions / Size`: `scalar`, byte length for strings, or dimensional bounds `(d1, d2, ...)` for arrays.
  4. `Value Preview`: Formatted numerical value, quoted string contents, or initial array element summary. Strings and array previews longer than 32 characters are truncated with trailing `...`.
* **Empty Table:** If no variables are currently defined, prints `(no variables defined)`.

---

## 3. Breakpoints & Screen / State Clearing

### 3.1 Breakpoint Management
* **`BREAK [line1 [, line2, ...]]`:**
  * Standalone `BREAK` in a program line acts as an inline breakpoint, suspending execution before the next statement with `9 Breakpoint reached, <line>:<stmt>`.
  * `BREAK line1 [, line2, ...]`: Registers line numbers into the runtime's breakpoint table (`breakpoints[]`).
  * If a specified line number is outside `1..9999` or not found in `program`, raises `B Integer out of range`.
* **Triggering a Breakpoint:**
  * When `runtime_run` reaches a line registered in `breakpoints[]` (before executing statement 1 of that line), execution suspends with report `9 Breakpoint reached, <line>:1`, recording continuation resumption at `<line>:1`.

### 3.2 Clearing Breakpoints: `CLEAR`
* **Syntax:** `CLEAR`
* **Behavior:**
  * Wipes all variables (`symtab_clear(&rt->symtab)`).
  * Resets call stack (`call_sp = 0`) and loop stack (`for_sp = 0`).
  * Resets DATA read pointers and temporary console attributes.
  * **Clears all registered breakpoints** in `rt->breakpoints` and any active temporary stop targets.
  * Resets continuation capability (`can_continue = false`).

---

## 4. Execution Resumption: `CONTINUE`

### 4.1 Syntax & Availability
* **Syntax:** `CONTINUE [line_expr]`.
* **Availability:** Available **both as a program instruction** (e.g., `100 CONTINUE`, `100 CONTINUE 500`) and **interactively** in immediate mode.

### 4.2 Parameter Behavior
* **No Parameter (`CONTINUE`):**
  * Resumes execution from the recorded continuation point until normal termination (`0 OK`), an error, a `STOP`, or a breakpoint.
* **With Parameter (`CONTINUE line_expr`):**
  * Evaluates `line_expr` as a target line number.
  * If `line_expr < 1` or `line_expr > 9999`, or if the target line number does not exist in `rt->program`, raises `B Integer out of range`.
  * Sets a temporary one-shot stop target at that line number (`rt->temp_stop_line`).
  * Resumes execution. When the engine arrives at the target line (before executing statement 1 of that line), execution suspends with report `9 Breakpoint reached, <target_line>:1` (or `9 STOP statement, <target_line>:1`), and the temporary stop target is cleared.

### 4.3 Continuation Pointer Resolution
When program execution halts, the runtime updates continuation tracking (`rt->can_continue = true` and `rt->cont_line_idx`, `rt->cont_stmt_idx`):

1. **Stopped via `STOP` statement:**
   * Resumes at the statement *immediately after* the `STOP`.
   * If further statements exist on the same line (separated by `:`), resumes at `cur_stmt_idx + 1`.
   * If `STOP` was the last statement on the line, resumes at statement 1 of the next line (`cur_line_idx + 1`).
2. **Stopped via Break / SIGINT (Ctrl+C):**
   * Resumes at the statement or line where the interruption occurred.
3. **Stopped at a Line Breakpoint / Temporary Stop Target:**
   * Because the line was suspended *before* execution began, resumes at statement 1 of that targeted line.
4. **Invalid Continuation:**
   * If no program was run, or the program previously ended normally with report `0 OK`, calling `CONTINUE` raises `C Nonsense in BASIC`.

---

## 5. Architectural Runtime Changes

### 5.1 Runtime Struct Additions (`src/runtime.h`)
```c
#define MAX_BREAKPOINTS 64

typedef struct Runtime {
    ...
    // Continuation & Debugging state
    bool can_continue;
    size_t cont_line_idx;
    int cont_stmt_idx;
    uint16_t temp_stop_line;       // 0 if none active

    // Breakpoint registry
    uint16_t breakpoints[MAX_BREAKPOINTS];
    size_t breakpoint_count;
    ...
} Runtime;
```

### 5.2 Lexer & Normalizer Updates
* Tokens: `TOKEN_CONTINUE`, `TOKEN_VARS`, `TOKEN_BREAK`.
* Keywords added to `is_zx_keyword`: `"CONTINUE"`, `"VARS"`, `"BREAK"`.
* Syntax validation rules for `CONTINUE [expr]`, `VARS`, and `BREAK [line [, line...]]`.
