# BASIC-INTERPRETER.md - Complete Specification for the ZXBasic Interpreter

A self-contained, implementation-independent specification for a modern Sinclair ZX Spectrum BASIC interpreter. This document serves as the sole reference specification for implementing a full, compatible interpreter in any programming language (Rust, Forth, Go, Python, C++, etc.) without requiring external references or target-specific tooling.

---

## 1. System Overview & Architecture

### 1.1 Architectural Model
The interpreter is an interactive environment and execution engine centered around three core components:
1. **Interactive REPL / Input Dispatcher:** Reads lines from the user or script stream.
   * If an input line starts with a numeric line number, it is treated as a **program line definition** and routed to the Program Store.
   * If an input line does not start with a line number, it is treated as a **direct command (Immediate Mode)** and executed immediately.
2. **Program Store:** An ordered collection of program lines indexed by line number (`1` to `9999`), maintained strictly in ascending numerical order.
3. **Runtime Engine:** Manages execution state, variable symbol tables, subroutine call stacks, loop stacks, data block cursors, terminal display attributes, and breakpoints.

```text
                      +-----------------------------+
                      |      Input / Stdin / REPL   |
                      +-----------------------------+
                                     |
                       [Check Leading Line Number]
                                     |
                +--------------------+--------------------+
                |                                         |
     [Has Line Number]                           [No Line Number]
                |                                         |
     +----------v-----------+                   +---------v-----------+
     | Line Ingestion Filter|                   | Immediate Executor  |
     | - Normalization      |                   | - Direct Commands   |
     | - Syntax Validation  |                   | - Interactive Exec  |
     +----------+-----------+                   +---------+-----------+
                |                                         |
     +----------v-----------+                             |
     |    Program Store     | <====== [RUN / GOTO] =======+
     | (Sorted Lines 1..9999|
     +----------+-----------+
                |
     +----------v-----------------------------------------+
     |                 Runtime State Engine               |
     | - Symbol Table (Scalars & Multi-dim Arrays)        |
     | - Call Stack (GOSUB/RETURN), Loop Stack (FOR/NEXT) |
     | - DATA Cursor, SysVars, Breakpoints, Resumption    |
     +----------------------------------------------------+
```

### 1.2 Execution Environments
* **Stored Program Mode:** The interpreter executes lines sequentially from the Program Store, advancing through statement sequences delimited by colons (`:`).
* **Immediate Mode (Direct Console Entry):** Statements execute in the current runtime context. Variables and system state persist between immediate commands and program execution unless explicitly reset.

---

## 2. Lexical Conventions & Syntax Grammar

### 2.1 Character Set & Source Encoding
* Source input is encoded in UTF-8 (compatible with 7-bit ASCII).
* Line terminators may be LF (`\n`) or CRLF (`\r\n`).

### 2.2 Line Numbers & Line Structure
* **Range:** Integers from `1` to `9999` inclusive. Line numbers outside this range raise report `B Integer out of range`.
* **Line Structure:** `[<line_number>] <statement> [ : <statement> ... ]`
* **Statement Separator:** The colon (`:`) separates multiple statements within a single line.
* **Line Replacement / Deletion:**
  * Entering `<number> <text>` inserts or replaces line `<number>` in the store.
  * Entering `<number>` with whitespace only (empty body) deletes line `<number>` from the store.

### 2.3 Comments (`REM`)
* Initiated by the keyword `REM`.
* All characters following `REM` up to the end of the physical line are treated as arbitrary comment text.
* Colons (`:`) occurring inside a `REM` do not terminate the comment; they are consumed as part of the comment text.

### 2.4 Case Sensitivity & Canonical Normalization
* **Keywords & Functions:** Case-insensitive upon input, but canonically converted to uppercase outside string literals and `REM` comments.
* **Variable Identifiers:** Strictly **case-sensitive** adhering to Sinclair convention (`score`, `Score`, and `SCORE` designate three distinct variables; `a$` and `A$` designate distinct string variables).
* **String Literals:** Exact character casing is preserved verbatim.

### 2.5 Ingestion-Time Syntax Verification
When a numbered line is entered, it must pass a full syntax validation pass before modifying the program store:
* If the line contains a syntax error, it is **rejected**: the existing program store remains unmodified, and the interpreter reports `C Nonsense in BASIC, <line_number>:<column>`.
* If the line is valid, it is canonicalized (keywords uppercased) and inserted into the store.

---

## 3. Type System & Values

The interpreter supports two primitive data types: Numbers and Strings.

### 3.1 Numbers
* **Format:** IEEE 754 double-precision 64-bit floating-point numbers.
* **Boolean Semantics:**
  * False is represented by `0`.
  * True is represented by `1`.
  * In conditional evaluation (`IF`), any non-zero value evaluates as True; `0` evaluates as False.
* **Integer Coercion:** Array subscripts, line numbers, terminal coordinates, and character codes coerce floats via floor/truncation semantics (`floor(x)`), and must satisfy their respective domain ranges.

### 3.2 Strings
* Dynamic-length byte sequences (0 to 65,535 bytes).
* Enclosed in double quotes (`"..."`).
* Escaped quotes inside strings are represented by two consecutive double quotes (`""`). Example: `"He said, ""Hello!"""` represents the string `He said, "Hello!"`.

### 3.3 Identifier Naming Rules
* **Numeric Scalar Variables:** Start with an alphabetic letter (`A-Z`, `a-z`), optionally followed by alphanumeric characters or underscores (`X`, `counter`, `total_1`).
* **String Scalar Variables:** Exactly one alphabetic letter followed immediately by a dollar sign (`A$`, `b$`, `z$`).
* **Numeric Arrays:** An identifier name followed by dimension bounds in parentheses (`M(10)`, `grid(3, 4)`).
* **String Arrays:** A single letter followed by `$` and dimension bounds in parentheses (`A$(10, 32)`). In Sinclair BASIC, string arrays have their last dimension representing the maximum string length of each element.

### 3.4 Arrays & Dimensioning
* Arrays are explicitly allocated via `DIM`.
* Indexing is **1-based** (`1 .. dimension_size`). Accessing index `0` or an index $> \text{size}$ raises report `3 Subscript out of range`.
* Re-dimensioning an existing array identifier without clearing memory raises report `C Nonsense in BASIC`.

---

## 4. Expressions, Operators & Precedence

Expressions are evaluated according to strict precedence rules (highest precedence 1 to lowest 9). Operators with equal precedence associate left-to-right, except exponentiation which associates right-to-left.

| Level | Operators | Description | Associativity |
| :---: | :--- | :--- | :---: |
| **1 (Highest)** | Functions, `( )`, Slicing | Function calls, grouping, array indexing, `A$(s TO e)` | Left-to-right |
| **2** | `^` | Exponentiation ($x^y$) | Right-to-left |
| **3** | Unary `+`, Unary `-` | Identity, Negation | Right-to-left |
| **4** | `*`, `/` | Multiplication, Division | Left-to-right |
| **5** | Binary `+`, Binary `-` | Addition, String Concatenation (`+`), Subtraction | Left-to-right |
| **6** | `=`, `<>`, `<`, `>`, `<=`, `>=` | Relational Comparisons (returns `1` or `0`) | Left-to-right |
| **7** | `NOT` | Logical NOT (`NOT 0 = 1`, `NOT non_zero = 0`) | Right-to-left |
| **8** | `AND` | Logical AND (Sinclair shortcut semantics) | Left-to-right |
| **9 (Lowest)** | `OR` | Logical OR (Sinclair shortcut semantics) | Left-to-right |

### 4.1 Sinclair Logical Operator Shortcuts
In Sinclair BASIC, `AND` and `OR` operate on numeric values with special selector semantics:
* **`A AND B`**: Evaluates `B`. If `B` is non-zero (True), the expression evaluates to `A`. If `B` is zero (False), it evaluates to `0`.
* **`A OR B`**: Evaluates `A`. If `A` is non-zero (True), the expression evaluates to `A`. If `A` is zero (False), it evaluates to `B`.

### 4.2 Sinclair String Slicing
Substrings use 1-based indexing using parentheses and the `TO` keyword:
* `A$(s TO e)`: Slice from character `s` through character `e` inclusive.
* `A$(s TO)`: Slice from character `s` through the end of `A$`.
* `A$(TO e)`: Slice from character `1` through character `e`.
* `A$(i)`: Shorthand for `A$(i TO i)`, extracting the single character at position `i`.
* **Rules & Out of Bounds:**
  * If $s > e$, the expression yields the empty string `""`.
  * If $s < 1$ or $e > \text{length}(A\$)$ (when $s \le e$), raises report `3 Subscript out of range`.

---

## 5. Built-in Functions

### 5.1 Mathematical Functions
| Function | Signature | Description | Domain / Error |
| :--- | :--- | :--- | :--- |
| `ABS(x)` | `Number -> Number` | Absolute value $\|x\|$ | All numbers |
| `ACS(x)` | `Number -> Number` | Arccosine in radians | $-1.0 \le x \le 1.0$; else `B Integer out of range` |
| `ASN(x)` | `Number -> Number` | Arcsine in radians | $-1.0 \le x \le 1.0$; else `B Integer out of range` |
| `ATN(x)` | `Number -> Number` | Arctangent in radians | All numbers |
| `COS(x)` | `Number -> Number` | Cosine of angle $x$ (radians) | All numbers |
| `EXP(x)` | `Number -> Number` | Exponential $e^x$ | All numbers |
| `INT(x)` | `Number -> Number` | Floor: largest integer $\le x$ (e.g. `INT(3.7)=3`, `INT(-3.7)=-4`) | All numbers |
| `LN(x)` | `Number -> Number` | Natural logarithm $\ln(x)$ | $x > 0$; else `B Integer out of range` |
| `RND` | `() -> Number` | Uniform pseudo-random float $0.0 \le r < 1.0$ | No argument required |
| `SGN(x)` | `Number -> Number` | Signum: `1` if $x > 0$, `0` if $x = 0$, `-1` if $x < 0$ | All numbers |
| `SIN(x)` | `Number -> Number` | Sine of angle $x$ (radians) | All numbers |
| `SQR(x)` | `Number -> Number` | Square root $\sqrt{x}$ | $x \ge 0$; else `B Integer out of range` |
| `TAN(x)` | `Number -> Number` | Tangent of angle $x$ (radians) | All numbers |

### 5.2 String & Conversion Functions
| Function | Signature | Description | Domain / Error |
| :--- | :--- | :--- | :--- |
| `CHR$(n)` | `Number -> String` | Single-character string with ASCII code `floor(n)` | $0 \le n \le 255$; else `B Integer out of range` |
| `CODE(s)` | `String -> Number` | ASCII code of the first character of $s$ (returns `0` if $s = ""$) | All strings |
| `LEN(s)` | `String -> Number` | Byte/character length of string $s$ | All strings |
| `STR$(n)` | `Number -> String` | Converts numeric value $n$ to its canonical decimal string | All numbers |
| `VAL(s)` | `String -> Number` | Evaluates string $s$ as a valid BASIC numeric expression | Syntax error in $s$ raises `C Nonsense in BASIC` |
| `INKEY$` | `() -> String` | Non-blocking single-key console read. Returns key or `""` | No argument required |

### 5.3 User-Defined Functions (`DEF FN` and `FN`)

* **Function Definition:** `DEF FN <fname>[(<param1> [, <param2> ...])] = <expression>`
  * Defines a custom user function.
  * Contrary to Sinclair BASIC (which restricted `fname` to a single letter), `fname` may be any valid identifier name (e.g. `hypot`, `square`, `pad$`).
  * If `fname` ends with `$`, it defines a string function and its expression must evaluate to a string. Otherwise, it defines a numeric function returning a number.
  * Parameter identifiers are formal parameters (numeric scalars or single-letter string scalars ending in `$`).
  * Zero or more parameters are supported. If zero parameters are required, parentheses may be omitted or empty (`DEF FN pi() = 3.14159265`).
* **Function Invocation:** `FN <fname>[(<arg1> [, <arg2> ...])]`
  * Evaluates arguments and invokes the function within any numeric or string expression.
  * **Scoping & Variable Shadowing:** Formal parameters dynamically shadow any existing global variables with identical names during function evaluation. When evaluation completes, existing outer variables are restored untouched. Non-parameter global variables remain readable within the function expression.
  * **Forward References & On-Demand Discovery:** If `FN <fname>` is invoked before the program line defining `DEF FN <fname>` has executed, the runtime automatically searches the stored program for a matching `DEF FN <fname>` definition. If no definition exists, raises report `2 Variable not found`.
  * **Recursion & Composition:** User functions can invoke other user functions or call themselves recursively (with recursion depth protection up to 256 calls).

---

## 6. Console Attributes, Terminal Model & System Variables

Display formatting is mapped to ANSI / VT100 terminal escape sequences while preserving Sinclair ZX Spectrum semantics.

### 6.1 Coordinate System & Screen Grid
* Sinclair coordinate system: `row 0..21`, `column 0..31` (0-indexed).
* Coordinates are clamped to the active terminal window dimensions (falling back to a default 32-column by 22-row viewport if unconstrained).
* ANSI cursor positioning emits: `\033[<row + 1>;<col + 1>H`.

### 6.2 Color Palette
Standard Sinclair 8-color indices, augmented by `-1` representing the terminal's native default color:

| Index | Color | ANSI Foreground | ANSI Background |
| :---: | :--- | :---: | :---: |
| `-1` | **Terminal Default / Reset** | `\033[39m` | `\033[49m` |
| `0` | Black | `\033[30m` | `\033[40m` |
| `1` | Blue | `\033[34m` | `\033[44m` |
| `2` | Red | `\033[31m` | `\033[41m` |
| `3` | Magenta | `\033[35m` | `\033[45m` |
| `4` | Green | `\033[32m` | `\033[42m` |
| `5` | Cyan | `\033[36m` | `\033[46m` |
| `6` | Yellow | `\033[33m` | `\033[43m` |
| `7` | White | `\033[37m` | `\033[47m` |

### 6.3 Attribute Scopes: Permanent vs. Temporary
1. **Permanent Attributes (`ATTR_P_*`):**
   * Set via standalone statements (`INK 2`, `PAPER 7`, `BRIGHT 1`, `INVERSE 0`).
   * Persist for all subsequent output across statements and program lines until explicitly modified.
2. **Temporary Attributes (`ATTR_T_*`):**
   * Set via inline stream modifier items inside `PRINT` or `INPUT` statements (`PRINT INK 2; "Red"; INK -1; "Normal"`).
   * Active only for items in the current statement; automatically revert to the permanent attributes at the end of that statement.

### 6.4 Style Attributes
* **`BRIGHT n`**: Controls intensity / bold styling.
  * `1`: High intensity / bold (`\033[1m`).
  * `0`: Normal intensity (`\033[22m`).
  * `-1`: Terminal default attribute.
* **`INVERSE n`**: Controls inverse video (swapping foreground/background).
  * `1`: Inverse video enabled (`\033[7m`).
  * `0`: Normal video (`\033[27m`).
  * `-1`: Terminal default attribute.

### 6.5 System Variables Subsystem (`sysvars`)
The interpreter maintains a table of system variables accessible to runtime routines and expression queries:

| Sysvar Name | Type | Default | Description |
| :--- | :---: | :---: | :--- |
| `ATTR_P_INK` | Integer | `-1` | Permanent foreground color index (`-1 .. 7`) |
| `ATTR_P_PAPER` | Integer | `-1` | Permanent background color index (`-1 .. 7`) |
| `ATTR_P_BRIGHT` | Integer | `-1` | Permanent intensity / bold index (`-1, 0, 1`) |
| `ATTR_P_INVERSE` | Integer | `-1` | Permanent inverse video index (`-1, 0, 1`) |
| `ATTR_T_INK` | Integer | `-2` (Inactive) | Temporary foreground color index |
| `ATTR_T_PAPER` | Integer | `-2` (Inactive) | Temporary background color index |
| `ATTR_T_BRIGHT` | Integer | `-2` (Inactive) | Temporary bold index |
| `ATTR_T_INVERSE` | Integer | `-2` (Inactive) | Temporary inverse index |
| `S_POSN_ROW` | Integer | `0` | Current virtual cursor row (`0 .. 21`) |
| `S_POSN_COL` | Integer | `0` | Current virtual cursor column (`0 .. 31`) |

---

## 7. Statement & Command Reference

### 7.1 Program Lifecycle & Execution Control

#### `RUN [<file_expr>] [<line_expr>]`
* **Syntax:** `RUN`, `RUN <line>`, `RUN <file>`, or `RUN <file> [,] <line>`
* **Semantics:**
  * Without arguments: Clears variables, call stacks, loop stacks; resets `DATA` pointer; sets `is_running = true`; starts execution at line 1.
  * With numeric argument `<line>`: Resets state as above, but starts execution at the specified line number.
  * With string argument `<file>`: Performs a clean `LOAD <file>` and begins execution from the target line (or line 1 / auto-run target).
  * Valid in Immediate Mode and inside programs (enabling program chain-loading).

#### `GOTO <line_expr>`
* **Syntax:** `GOTO <line_expr>`
* **Semantics:**
  * Evaluates `<line_expr>` as an integer line number. If line does not exist in store, raises `B Integer out of range`.
  * **In Program Mode:** Requests an immediate control transfer to statement 1 of `<line_expr>`.
  * **In Immediate Mode:** Launches execution starting at statement 1 of `<line_expr>`. **Unlike `RUN`, interactive `GOTO` preserves all existing variables and arrays in the symbol table**, resetting only the call stack, loop stack, and temporary attributes.

#### `STOP`
* **Syntax:** `STOP`
* **Semantics:** Halts program execution immediately, sets `is_running = false`, and emits report `9 STOP statement, <line>:<stmt>`. Preserves all variables, call frames, loop stacks, and data pointers.

#### `EXIT [<code_expr>]`
* **Syntax:** `EXIT [<code_expr>]`
* **Semantics:** Cleanly terminates the interpreter process and returns the evaluated integer exit status (defaults to `0`). Valid in Immediate Mode and stored lines.

#### `NEW`
* **Syntax:** `NEW`
* **Semantics:** Completely erases all lines in the Program Store, resets the runtime state, clears all variables, stacks, breakpoints, and returns to an empty environment.

#### `CLEAR`
* **Syntax:** `CLEAR`
* **Semantics:** Clears all variables and arrays in the symbol table, clears subroutine call and loop stacks, closes all open user channels, and clears all registered breakpoints without altering lines in the Program Store.

---

### 7.2 Memory & Assignment

#### `LET <var> = <expr>`
* **Syntax:** `LET <var> = <expr>`
* **Semantics:** Explicit assignment statement. Evaluates `<expr>` and assigns the resulting number or string into `<var>`. Explicit keyword `LET` is mandatory.

#### `DIM <ident>(<dim1> [, <dim2> ...])`
* **Syntax:** `DIM <ident>(<d1> [, <d2> ...])`
* **Semantics:** Allocates an array. Dimensions must be positive integers. Arrays are 1-indexed. Re-dimensioning an already dimensioned identifier raises report `C Nonsense in BASIC`.

#### `DEF FN <fname>[(<params>)] = <expr>`
* **Syntax:** `DEF FN <fname>[(<param1> [, <param2> ...])] = <expr>`
* **Semantics:** Registers a user-defined function in the symbol table. In stored program mode, execution skips past `DEF FN` without evaluating the expression body until invoked via `FN <fname>(...)`. See Section 5.3 for full specification.

---

### 7.3 Control Flow & Subroutines

#### `IF <expr> THEN <statement>`
* **Syntax:** `IF <cond_expr> THEN <statement>` or `IF <cond_expr> THEN <line_number>`
* **Semantics:**
  * If `<cond_expr>` evaluates to non-zero (True), execution proceeds with `<statement>`. If followed by a numeric literal `<line_number>`, acts as an implicit `GOTO <line_number>`.
  * If `<cond_expr>` evaluates to zero (False), the interpreter aborts execution of the remainder of the physical line, jumping to the next numbered line.

#### `FOR <var> = <start> TO <limit> [STEP <step>]`
* **Syntax:** `FOR <var> = <start> TO <limit> [STEP <step>]`
* **Semantics:**
  * Evaluates `<start>`, `<limit>`, and `<step>` (defaulting to `1` if omitted).
  * Assigns `<start>` to `<var>`.
  * Pushes loop state to the loop stack: loop variable name, target limit, step value, line index, and statement index following the `FOR`.

#### `NEXT <var>`
* **Syntax:** `NEXT <var>`
* **Semantics:**
  * Searches the loop stack for the matching loop variable `<var>`. If not found, raises `1 NEXT without FOR`.
  * Adds `step` to `<var>`.
  * Checks loop continuation:
    * If $\text{step} \ge 0$ and $\text{var} \le \text{limit}$: loops back to statement following `FOR`.
    * If $\text{step} < 0$ and $\text{var} \ge \text{limit}$: loops back to statement following `FOR`.
    * Otherwise: pops loop from stack and continues to next statement.

#### `GOSUB <line_expr>`
* **Syntax:** `GOSUB <line_expr>`
* **Semantics:** Evaluates `<line_expr>` as an integer line number. Pushes the return address (line index and statement index immediately following `GOSUB`) onto the subroutine call stack and jumps to statement 1 of target line. Call stack capacity limit raises `4 Out of memory`.

#### `RETURN`
* **Syntax:** `RETURN`
* **Semantics:** Pops the return address from the subroutine call stack and resumes execution there. If the call stack is empty, raises report `7 Return without GOSUB`.

---

### 7.4 Data Blocks

#### `DATA <item1> [, <item2> ...]`
* **Syntax:** `DATA <literal1> [, <literal2> ...]`
* **Semantics:** Declares inlined data literals (numbers or unquoted/quoted strings). Skipped during normal program flow.

#### `READ <var1> [, <var2> ...]`
* **Syntax:** `READ <var1> [, <var2> ...]`
* **Semantics:** Reads the next item from the global program `DATA` sequence into `<var>`. Type must match variable type. If reading past the end of available items, raises report `8 End of DATA`.

#### `RESTORE [<line_expr>]`
* **Syntax:** `RESTORE [<line_expr>]`
* **Semantics:** Resets the `DATA` reading cursor. If `<line_expr>` is omitted, resets to the first item in the program. If `<line_expr>` is provided, resets to the first `DATA` statement at or after that line number.

---

### 7.5 Console I/O & Terminal Formatting

#### `CLS`
* **Syntax:** `CLS`
* **Semantics:** Clears the terminal screen using permanent background/foreground attributes, repositions cursor to home position (`0, 0`), and resets output column counter to 0.

#### `PRINT [<item> [separator] ...]`
* **Syntax:** `PRINT [item [sep item ...]]`
* **Items:** Expressions (numbers, strings) or stream modifiers:
  * `AT row, col`: Virtual cursor positioning.
  * `INK n`, `PAPER n`, `BRIGHT n`, `INVERSE n`: Temporary visual attributes.
* **Separators:**
  * `;` (Semicolon): Concatenates with no spacing.
  * `,` (Comma): Advances cursor to next 16-character column tab stop (`0, 16, 32, ...`).
  * `'` (Apostrophe): Emits a newline (`\n`).
* **Trailing behavior:** If the statement does not end with `;` or `,`, a newline is emitted automatically. Reverts temporary attributes at statement end.

#### `INPUT [<prompt_str> ;] <var>`
* **Syntax:** `INPUT [<prompt_str> ;] <var>`
* **Semantics:** Displays optional prompt string and pauses execution to read user input from the console, parsing the input into `<var>`.

---

### 7.6 Storage & Filesystem Operations

#### `SAVE <file_expr> [LINE [<line_expr>]]`
* **Syntax:** `SAVE <file>` or `SAVE <file> LINE` or `SAVE <file> LINE <line>`
* **Semantics:**
  * Writes all program lines in ascending order to the UTF-8 text file designated by `<file_expr>`.
  * If `LINE` is specified without `<line_expr>`, appends a trailing direct command: `RUN\n`.
  * If `LINE <line_expr>` is specified, evaluates `<line_expr>` (must be integer `1..9999`) and appends: `RUN <line>\n`.
  * When this file is subsequently read by `LOAD`, the trailing `RUN` statement triggers automatic execution upon loading.

#### `LOAD <file_expr>`
* **Syntax:** `LOAD <file_expr>`
* **Semantics:** Implicitly executes `NEW` (clearing program and variables), reads lines sequentially from `<file_expr>`, and ingests numbered lines into the store. Any trailing direct command (e.g. `RUN` from auto-run save) is executed immediately upon ingestion.

#### `MERGE <file_expr>`
* **Syntax:** `MERGE <file_expr>`
* **Semantics:**
  * Ingests numbered program lines from `<file_expr>` into the store **without clearing existing program lines or variables**.
  * Colliding line numbers in the store are overwritten in place with incoming line text.
  * Non-colliding lines are inserted in sorted order.
  * Ignores unnumbered lines (e.g. trailing `RUN` directives) to prevent accidental execution during merging.

#### `CAT [<path_expr>]`
* **Syntax:** `CAT` or `CAT <path_expr>`
* **Semantics:**
  * Lists filesystem entries line-by-line relative to the current program directory (or optional sub-path).
  * Skips hidden files (names starting with `.`).
  * Sorts entries alphabetically, distinguishing subdirectories (`[DIR]`) and displaying file sizes in bytes.

#### `ERASE <file_expr>`
* **Syntax:** `ERASE <file_expr>`
* **Semantics:** Erases the specified file from disk via host filesystem unlinking. If `<file_expr>` points to a directory, it recursively removes the directory and all of its contents (`rm -rf` semantics). Valid in Immediate Mode and inside stored program lines. Failure to find file or permission error raises report `C Nonsense in BASIC`.

#### `FORMAT <dir_expr>`
* **Syntax:** `FORMAT <dir_expr>`
* **Semantics:** Changes the current working directory to `<dir_expr>`, creating intermediate parent directories (`mkdir -p` with permissions `0755`) if they do not already exist.

---

### 7.7 Channels & Data File Streams

ZXBasic implements Sinclair ZX Spectrum Interface 1 channel redirection adapted to POSIX filesystems. Streams `#0 .. #3` are permanently reserved for system input/output (`#0`/`#1`: input, `#2`: console output, `#3`: printer/stderr). User file streams are allocated across channels `#4 .. #15`.

#### `OPEN #n, <file_expr>`
* **Syntax:** `OPEN #<channel_expr>, <file_expr>`
* **Semantics:** Associates channel $n$ (`4 .. 15`) with a POSIX file:
  * **File exists:** Opens file in **Read Mode** (`"r"`).
  * **File does not exist:** Creates file and opens in **Write Mode** (`"w"`).
  * Re-opening an active channel automatically flushes and closes its previous file.

#### `CLOSE #n [, #m ...]`
* **Syntax:** `CLOSE #<channel_expr> [, #<channel_expr> ...]`
* **Semantics:** Flushes and closes one or more active user channels. Closing an already closed user channel is a harmless no-op. Attempting to close system streams `#0 .. #3` raises `C Nonsense in BASIC`.

#### `PRINT #n [, ...]`
* **Syntax:** `PRINT #<channel_expr> [, <print_list>]`
* **Semantics:** Emits formatted output to channel $n$. Channel $n$ must be open for writing (or console output stream `#2`). Delimiters `;` (compact) and `,` (tab stops) operate as expected.

#### `INPUT #n, <var> [, <var> ...]`
* **Syntax:** `INPUT #<channel_expr>, <var> [, <var> ...]`
* **Semantics:** Ingests structured comma-delimited, tab-delimited, or line-delimited data from channel $n$ into variables. Channel $n$ must be open for reading (or input stream `#0`/`#1`). Reaching end-of-file raises `8 End of DATA`.

#### `MOVE #n TO #m` / `MOVE <fromfile> TO <tofile>`
* **Syntax:** `MOVE #<src_channel> TO #<dest_channel>` or `MOVE <fromfile_expr> TO <tofile_expr>`
* **Semantics:**
  * **Channel Copy (`MOVE #n TO #m`):** Performs byte-level block streaming from readable channel $n$ to writable channel $m$ until EOF on $n$. **Both channels remain open** after transfer.
  * **File Move/Rename (`MOVE <fromfile> TO <tofile>`):** Renames or moves non-open files on disk by path using POSIX `rename(2)`. If destination is an existing directory, moves the file into that directory. Neither source nor destination may be open in a user channel. Fails with `C Nonsense in BASIC` if source does not exist, an operand is currently open, or filesystem permissions prohibit renaming.

#### `COPY <fromfile> TO <tofile>`
* **Syntax:** `COPY <fromfile_expr> TO <tofile_expr>` (or `COPY #<src_channel> TO #<dest_channel>`)
* **Semantics:**
  * **File Copy (`COPY <fromfile> TO <tofile>`):** Duplicates a non-open file by name on disk. If destination is an existing directory, copies the file into that directory using the source file's basename. Preserves source permissions. Neither source nor destination may be currently open in a user channel. Fails with `C Nonsense in BASIC` if source does not exist, source is a directory, an operand is open, or a write error occurs.
  * **Channel Copy (`COPY #n TO #m`):** Identical to `MOVE #n TO #m`, copying stream data without closing channels.

---

### 7.8 Program Store Manipulation

#### `LIST [[<from>] TO [<to>] | <start>]`
* **Syntax:** `LIST`, `LIST <start>`, `LIST <from> TO <to>`, `LIST TO <to>`, `LIST <from> TO`, `LIST TO`
* **Semantics:**
  * Outputs stored program lines formatted as `<line_no> <source_text>\n` in ascending order across the specified range.
  * `LIST`: Lists the entire program (`1 .. 9999`).
  * `LIST <start>`: Lists starting from line `<start>` through the end of the program.
  * `LIST <from> TO <to>`: Lists lines satisfying $from \le \text{line\_no} \le to$.
  * If $from > to$, no lines match and no output is produced.

#### `LLIST`
* **Syntax:** `LLIST`
* **Semantics:**
  * Pretty-prints the entire stored program to standard output with syntax highlighting, structured formatting, and control flow indicators.
  * **Line Numbers:** Formatted right-aligned to 4 digits (`%4u`).
  * **Jump Targets:** Lines targeted by `GOTO`, `GOSUB`, `RUN`, `RESTORE`, or `THEN <number>` are highlighted with a prominent marker (`▶%4u`) in bold magenta (`\033[1;35m`).
  * **Loop Indentation:** Indents nested code blocks inside `FOR` / `NEXT` loops by 2 spaces per nesting level. `NEXT` statements are dedented before printing.
  * **Syntax Highlighting:**
    * Keywords: Bold yellow (`\033[1;33m`)
    * String literals: Green (`\033[32m`)
    * Number literals: Magenta (`\033[35m`)
    * `REM` comments: Dim gray (`\033[90m`)
  * `LLIST` always lists the entire program and does not accept line range arguments (supplying any arguments raises report `C Nonsense in BASIC`). Standard `LIST` remains strictly unchanged.

#### `ERASE [<from>] TO [<to>]`
* **Syntax:** `ERASE <from> TO <to>`, `ERASE TO <to>`, `ERASE <from> TO`, `ERASE TO`, `ERASE <line>`
* **Semantics:**
  * Erases a range of stored program lines from memory:
    * `ERASE <from> TO <to>`: Removes lines where $from \le \text{line\_no} \le to$.
    * `ERASE TO <to>`: Removes lines from beginning through $to$.
    * `ERASE <from> TO`: Removes lines from $from$ through end of program.
    * `ERASE TO`: Removes all stored lines.
    * `ERASE <line>`: Single line deletion shorthand.
  * **Immediate Mode Only:** Modifying program line ranges during program execution is forbidden; if called inside a running program (`is_running = true`), raises report `C Nonsense in BASIC`.

#### `EDIT <line_number>`
* **Syntax:** `EDIT <line_number>`
* **Semantics:** Immediate mode only. Looks up `<line_number>` in the store (raising `B Integer out of range` if absent) and pre-populates the interactive REPL line input buffer formatted as `<line_number> <source_text>` with cursor at end.

#### `AUTO [<start> [, <step>]]`
* **Syntax:** `AUTO`, `AUTO <start>`, `AUTO <start>, <step>`
* **Semantics:** Immediate mode only. Enters automatic line numbering mode (defaults: `start = 10`, `step = 10`). Automatically displays prompt prefixed with line number. Exits on blank line submission or interrupt.

#### `RENUM [<new_start> [, <step>]]`
* **Syntax:** `RENUM`, `RENUM <new_start>`, `RENUM <new_start>, <step>`
* **Semantics:**
  * Immediate mode only. Renumbers all stored lines starting from `new_start` (default 10) by increment `step` (default 10).
  * If any renumbered line would exceed `9999`, the operation aborts without modifying memory and raises `B Integer out of range`.
  * Rewrites integer target literals following branch keywords `GOTO`, `GOSUB`, and `RESTORE`.

---

### 7.8 Interactive Debugging & Inspection

#### `VARS`
* **Syntax:** `VARS`
* **Semantics:** Dumps a structured table of all defined scalar variables and arrays from the symbol table to standard output, displaying:
  * Variable Name
  * Type (`NUMBER`, `STRING`, `ARRAY(N)`, `ARRAY(S)`)
  * Dimensions or byte size
  * Value preview (quoted string contents, numeric value, or array preview)

#### `BREAK [<line1> [, <line2> ...]]`
* **Syntax:** `BREAK` or `BREAK <line1> [, <line2> ...]`
* **Semantics:**
  * Standalone `BREAK` in a program line acts as an inline breakpoint, pausing execution before the next statement with report `9 Breakpoint reached, <line>:<stmt>`.
  * `BREAK <line1>, ...`: Registers line numbers in the runtime breakpoint table. When execution reaches any registered line, it suspends prior to executing statement 1.

#### `CONTINUE [<temp_stop_line>]`
* **Syntax:** `CONTINUE` or `CONTINUE <temp_stop_line>`
* **Semantics:**
  * Resumes execution from the suspended state following a `STOP`, `BREAK`, or Ctrl+C interrupt.
  * Resumes from the exact line and statement index where execution was suspended.
  * If `<temp_stop_line>` is supplied, registers a temporary one-shot breakpoint at that line before resuming.
  * If execution cannot be resumed (no prior stop state), raises report `C Nonsense in BASIC`.

---

## 8. Runtime Execution Model & State Machine

### 8.1 Execution State Components
A conforming interpreter maintains the following runtime state:
1. **Pointers & Status:**
   * `is_running`: Boolean flag indicating continuous execution.
   * `current_line_index`: Index into the sorted Program Store array.
   * `current_statement_index`: 1-based index of the statement within the line (`1, 2, ...`).
   * `current_char_offset`: Character offset within the line string.
   * `can_continue`: Flag indicating whether execution suspension state exists.
2. **Subroutine Call Stack (`call_stack`):** Pushes `(return_line_index, return_statement_index)` upon `GOSUB`; pops upon `RETURN`.
3. **Loop Stack (`for_stack`):** Records `(variable_name, limit_value, step_value, loop_line_index, loop_statement_index)` upon `FOR`; checked/popped upon `NEXT`.
4. **Symbol Table (`symtab`):** Hash table or map storing numbers (`f64`), strings, and multi-dimensional array descriptors.
5. **Data Pointer:** Records `(data_line_index, data_item_offset)` for `READ`/`RESTORE`.
6. **Breakpoints Table:** Collection of line numbers triggering execution suspension.
7. **Console Attributes & SysVars Table:** Permanent and temporary attribute states.

### 8.2 Execution Loop Algorithm
```text
function RUN_PROGRAM(start_line):
    reset_call_stack()
    reset_loop_stack()
    reset_data_pointer()
    is_running = true

    line_idx = find_line_index_ge(start_line)
    while is_running and line_idx < program_store.count:
        line = program_store.lines[line_idx]

        // Check registered breakpoints
        if line.line_no in breakpoints or line.line_no == temp_stop_line:
            temp_stop_line = 0
            suspend_execution(line_idx, 1, "Breakpoint reached")
            break

        stmt_idx = 1
        for statement in line.statements:
            current_line_idx = line_idx
            current_stmt_idx = stmt_idx

            execute_statement(statement)

            if jump_requested:
                line_idx = jump_target_line_idx
                stmt_idx = 1
                jump_requested = false
                break // break inner loop, continue outer

            if stop_requested or interrupted:
                suspend_execution(line_idx, stmt_idx + 1, "STOP statement")
                break

            stmt_idx += 1

        if not jump_requested:
            line_idx += 1

    is_running = false
```

### 8.3 Execution Interruption & Signal Handling
* Pressing `Ctrl+C` (POSIX `SIGINT`) must **not** terminate the interpreter process.
* If a program is running, sets an internal interrupt flag, pausing execution at the current statement and emitting:
  ```text
  9 BREAK into program, <line>:<statement>
  ```
* All runtime variables, stacks, and data cursors remain intact, allowing inspection via `VARS` and resumption via `CONTINUE`.
* In Immediate Mode, Ctrl+C clears the current input line buffer and returns to a fresh REPL prompt.

---

## 9. Diagnostic Report & Error Codes

When execution terminates, halts, or encounters a syntax or evaluation violation, the interpreter emits a standard Sinclair report:

```text
<Report Code> <Message> [, <Line Number>:<Statement Index>]
```
*(Line number and statement index are included if executing a stored line, and omitted in Immediate Mode).*

| Code | Message | Trigger Condition |
| :---: | :--- | :--- |
| **`0`** | `OK` | Normal termination / successful completion of program or direct command |
| **`1`** | `NEXT without FOR` | `NEXT` statement encountered without an active matching `FOR` loop |
| **`2`** | `Variable not found` | Accessing an unassigned numeric/string variable or undeclared array |
| **`3`** | `Subscript out of range` | Array index or string slice bounds exceeded |
| **`4`** | `Out of memory` | GOSUB stack overflow, loop nesting exhaustion, or allocation failure |
| **`7`** | `Return without GOSUB` | `RETURN` encountered with an empty subroutine call stack |
| **`8`** | `End of DATA` | `READ` attempted when all `DATA` pool elements have been consumed |
| **`9`** | `STOP statement` | `STOP` instruction executed (or Ctrl+C break / breakpoint reached) |
| **`B`** | `Integer out of range` | Line number or integer parameter out of range (`< 1` or `> 9999`) |
| **`C`** | `Nonsense in BASIC` | Syntax error during parsing, invalid keyword combination, or illegal command |
