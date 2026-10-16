# SPECS-various.md - ZXBasic User Functions & Pretty Listing

Specification for user-defined functions (`DEF FN` and `FN`) and pretty-printed syntax-highlighted program listings (`LLIST`) in ZXBasic.

---

## 1. Overview & Architectural Principles

This specification defines the remaining advanced features extending ZXBasic beyond minimal Sinclair ZX Spectrum compatibility:
* **User-Defined Functions (`DEF FN`):** Enables definition of single-expression functions with zero or more parameters. Unlike Sinclair ZX Spectrum BASIC (which restricted function names to a single character `a .. z`), ZXBasic permits multi-character function names (`hypot`, `square`, `pad$`, `greet$`).
* **Function Invocation (`FN`):** Evaluates user-defined functions inside any expression context. Evaluates arguments, binds them to parameters with local shadowing semantics (without corrupting global variables of the same name), and evaluates the function's expression body.
* **Pretty-Printed Listing (`LLIST`):** Extends the classic printer listing keyword `LLIST` into an interactive terminal pretty-printer. Formats line numbers as right-aligned 4-digit integers, indents nested loops (`FOR` / `NEXT`), highlights `GOTO` / `GOSUB` target lines with visual markers, and applies ANSI syntax coloring across keywords, literals, identifiers, and comments. Standard `LIST` remains strictly unchanged.

---

## 2. User-Defined Functions (`DEF FN` and `FN`)

### 2.1 Statement: `DEF FN fname(...) = expression`

Defines a user function with an identifier, an optional parameter list, and a single body expression.

#### Syntax
```basic
DEF FN <fname> [ ( [ <param1> [, <param2> ...] ] ) ] = <expression>
```

#### Naming & Return Types
* `<fname>`: Any valid identifier name (e.g. `f`, `cube`, `hypot`, `pad$`, `format_name$`).
  * If `<fname>` ends with `$`, the function returns a string. The expression body must evaluate to a string expression.
  * If `<fname>` does not end with `$`, the function returns a number. The expression body must evaluate to a numeric expression.
  * Function names are case-insensitive (`FN Hypot` matches `DEF FN hypot`).

#### Parameter Declarations
* Zero or more formal parameters enclosed in parentheses, separated by commas.
* Parameter names may be numeric (e.g. `x`, `n`) or string (ending with `$`, e.g. `s$`, `name$`).
* Functions with zero parameters may omit parentheses: `DEF FN pi = 3.1415926535`.

#### Operational Semantics
1. **Definition-Time Execution:**
   * When executed in a running program or direct mode, `DEF FN` records the function's parameter names and expression body into the runtime's function table.
   * The expression body is not evaluated at definition time.
2. **Forward Reference Discovery:**
   * If `FN fname(...)` is called before the `DEF FN` line has been executed (or if `DEF FN` is located at the bottom of the program), the runtime automatically scans stored program lines for `DEF FN fname`, registers it, and proceeds with evaluation.
   * This matches classic Sinclair BASIC behavior where `DEF FN` lines could appear anywhere in the program store.

---

### 2.2 Expression: `FN fname(...)`

Invokes a defined user function from within an expression.

#### Syntax
```basic
FN <fname> [ ( [ <arg1> [, <arg2> ...] ] ) ]
```

#### Evaluation Rules
1. **Lookup:**
   * Looks up `<fname>` in registered functions. If not found, scans the Program Store for `DEF FN <fname>`. If still not found, raises report `C Nonsense in BASIC`.
2. **Argument Evaluation & Type Checking:**
   * Evaluates all argument expressions in the current environment from left to right.
   * The number of arguments must match the formal parameter count. Mismatches raise report `C Nonsense in BASIC`.
   * Argument types must match parameter types (string arguments for string parameters, numeric arguments for numeric parameters). Type mismatches raise report `C Nonsense in BASIC`.
3. **Local Parameter Binding & Shadowing:**
   * Parameter values are bound locally for the duration of the function body evaluation.
   * If a parameter shares a name with an existing global variable in `SymTab`, the global variable is shadowed (its value is preserved and restored upon function exit).
   * Variables referenced in the expression body that are not parameters refer to global variables or system variables.
4. **Recursion & Call Depth Protection:**
   * User functions can call other user functions or call themselves recursively.
   * Call stack recursion is bounded (limit: 256 frames) to prevent host stack overflow. Exceeding the depth limit raises report `4 Out of memory`.
5. **Return Value Validation:**
   * Evaluates the expression body with bound parameters.
   * If the function is numeric and the result is not a number, or if the function is string and the result is not a string, raises report `C Nonsense in BASIC`.

---

## 3. Pretty-Printed Program Listing (`LLIST`)

### 3.1 Statement: `LLIST`

Displays the entire program store formatted with syntax highlighting, right-aligned line numbers, loop indentation, and jump-target highlighting.

#### Syntax
```basic
LLIST
```

#### Operational Rules
1. **No Range Arguments:**
   * Unlike `LIST`, `LLIST` always lists the entire program from first to last line.
   * Supplying parameters to `LLIST` (e.g. `LLIST 10`) is a syntax error / raises report `C Nonsense in BASIC`.
2. **Jump Target Discovery:**
   * Prior to printing lines, `LLIST` scans the program store to detect all lines targeted by control-flow statements:
     * `GOTO <line>`
     * `GOSUB <line>`
     * `THEN <line>` (in `IF ... THEN <line>` shorthands)
     * `RUN <line>`
     * `RESTORE <line>`
   * Target line numbers are recorded in a target set.
3. **Line Number Formatting:**
   * Formats line numbers with a 4-digit right-aligned field (`%4u`).
   * **Target Lines:** Marked with a prominent indicator (e.g. `▶%4u ` in bold magenta).
   * **Standard Lines:** Formatted as ` %4u ` in cyan.
4. **Loop Indentation (`FOR` / `NEXT`):**
   * Tracks loop nesting depth starting at 0.
   * If a line starts with or is a `NEXT` statement, its indentation is decremented before printing the line.
   * Indentation adds 2 spaces per nested loop level.
   * `FOR` increments indentation level for subsequent lines.
   * Single-line loops (containing both `FOR` and `NEXT`) leave the net indentation unchanged.
5. **ANSI Syntax Coloring & Jump Indicators:**
   * **Jump Targets & Initiators:** Target lines are prefixed with `▶%4u ` in bold magenta (`\033[1;35m`). Jump initiating statements `GOTO` and `GOSUB` are highlighted with the matching triangle and bold magenta color (`\033[1;35m▶GOTO\033[0m`, `\033[1;35m▶GOSUB\033[0m`).
   * **Keywords:** Bold Yellow (`\033[1;33m`) for general statements, loops, built-in functions, `DEF`, `FN`.
   * **String Literals:** Bright Green (`\033[32m`), including enclosing quotes.
   * **Number Literals:** Magenta (`\033[35m`).
   * **Comments (`REM`):** Dim Gray (`\033[90m`).
   * **Identifiers & Operators:** Normal White / Reset (`\033[0m`).
   * Terminal attributes are reset at the end of every line (`\033[0m\n`).
6. **Independence:**
   * Standard `LIST` remains strictly unchanged (emitting plain unadorned `<line_no> <source>\n`).
