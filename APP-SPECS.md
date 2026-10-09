# SPECS.md - zxbasic (Modern C Sinclair BASIC Interpreter)

Specification and bootstrapping contract for building a lightweight, POSIX-compliant ZX Spectrum BASIC interpreter in C.

---

## 1. Project Overview & Scope

* **Target:** Linux (x86_64 / aarch64, Debian 13 "Trixie" on ChromeOS Crostini).
* **Executable Name:** `zxbasic`
* **Language standard:** C99 or C11 (`-std=c99 -Wall -Wextra -pedantic`).
* **Design principle:** Clean UNIX/POSIX terminal utility. No Sinclair hardware emulation (no ULA emulation, no display RAM attributes, no token-keyboard mapping).
* **Dependencies:** Standard POSIX libc only. Optional single-header library for REPL line-editing (`linenoise.h` or custom lightweight VT100 line editor). No heavy GUI/audio dependencies.

---

## 2. System Architecture

The executable operates as an interactive shell executing or storing lines based on the presence of a leading integer line number:


```
                           +---------------------------+
                           |     Terminal / Stdin      |
                           +---------------------------+
                                         |
                                  [Interactive REPL]
                                         |
                       +-----------------+-----------------+
                       |                                   |
              [Has Line Number?]                  [No Line Number]
                       |                                   |
             +---------v----------+              +---------v----------+
             | Program Store      |              | Immediate Executor |
             | (Sorted B-Tree/Map)|              | (Evaluate & Run)   |
             +--------------------+              +--------------------+
                       ^                                   |
                       |              [RUN]                |
                       +-----------------------------------+
                                         |
                                 +-------v-------+
                                 | Runtime State |
                                 | - Variables   |
                                 | - GOSUB Stack |
                                 | - FOR Loops   |
                                 | - DATA/RESTORE|
                                 +---------------+

```

### 2.1 Program Store
* **Line Range:** `1` to `9999`.
* **Data Structure:** Dynamically resized array of line entries kept in ascending order by line number:

```c
  typedef struct {
      uint16_t line_no;
      char *source; // Raw text stripped of the leading line number
  } ProgramLine;

  typedef struct {
      ProgramLine *lines;
      size_t count;
      size_t capacity;
  } Program;

```

* **Store Operations:**
* `<number> <text>`: Inserts or replaces `line_no`. Kept sorted via binary search + `memmove`.
* `<number>`: (No trailing text) Deletes the line matching `line_no`.



### 2.2 Execution Engine

* **Direct Commands:** Executed immediately without being saved (`LIST`, `RUN`, `NEW`, `CLEAR`, `SAVE`, `LOAD`).
* **Runtime Program Counter (PC):** An integer or pointer indexing into the current `ProgramLine`.
* **Variable Table:** Symbol tables for numbers (stored as `double`) and strings (`char*`).

---

## 3. Language Dialect Specification

### 3.1 Syntax & Formatting

* Case-insensitive keyword parsing (`PRINT`, `print`, `Print`).
* Multi-statement lines delimited by colon (`:`), e.g., `10 LET x=1 : PRINT x`.
* In-line comments initiated with `REM`.

### 3.2 Types & Expressions

* **Numbers:** Standard IEEE 754 64-bit float (`double`).
* **Strings:** Identifiers ending with `$` (e.g., `A$`, `NAME$`).
* **Operators:**
* Arithmetic: `+`, `-`, `*`, `/`, `^` (exponentiation).
* Relational: `=`, `<>`, `<`, `>`, `<=`, `>=`.
* Logical: `AND`, `OR`, `NOT`.


* **Sinclair String Slicing:** 1-based indexing syntax:
* `A$(start TO end)`, `A$(start TO)`, `A$(TO end)`.



### 3.3 Statement Support (Phase Breakdown)

* **Phase 1 (Bootstrap):** `LIST`, `RUN`, `NEW`, `CLEAR`, `CLS`, `PRINT` (strings and numeric literals), `LET` (assignment).
* **Phase 2 (Control Flow):** `GOTO`, `IF ... THEN`, `STOP`, `INPUT`.
* **Phase 3 (Loops & Subroutines):** `FOR ... TO ... STEP`, `NEXT`, `GOSUB`, `RETURN`, `DATA`, `READ`, `RESTORE`.
* **Phase 4 (Persistence & Functions):**
* File I/O: `SAVE "file.bas"`, `LOAD "file.bas"`.
* Math: `INT`, `ABS`, `SGN`, `SQR`, `RND`, `SIN`, `COS`, `TAN`.
* Strings: `LEN`, `STR$`, `VAL`, `CHR$`, `CODE`.



---

## 4. File I/O (SAVE / LOAD)

* Plain UTF-8 text files (`.bas`).
* Each line in the file matches standard ASCII text output:
```text
10 REM Simple Loop
20 FOR i=1 TO 5
30 PRINT "Index: "; i
40 NEXT i

```


* `SAVE "<path>"`: Serializes the current `Program` array sequentially into the designated file path.
* `LOAD "<path>"`: Invokes `NEW` (clears variables and existing program store), reads lines sequentially from the file, and runs them through the line-store insertion logic.

---

## 5. Sinclair Error Codes

Output Sinclair-style report format to standard error/output upon error:

```text
<Report Code> <Message>, <Line Number>:<Character Offset>

```

Primary codes to implement:

* `0 OK`
* `2 Variable not found`
* `3 Subscript out of range`
* `7 Return without GOSUB`
* `8 End of DATA`
* `9 Stop statement`
* `C Nonsense in BASIC` (General syntax error)

---

## 6. CLI Bootstrapping & File Structure

Target file layout for the project root:

```text
zxbasic/
├── Makefile
├── SPECS.md
├── src/
│   ├── main.c        # REPL entry point and input dispatcher
│   ├── program.c     # Line store (insert, delete, list, sort)
│   ├── program.h
│   ├── lexer.c       # Tokenizer and keyword recognizer
│   ├── lexer.h
│   ├── parser.c      # Expression parser & statement execution
│   └── parser.h
└── tests/
    └── test_store.c  # Unit tests for line management

```

### Initial Makefile Requirements

* Compiler flags: `CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 -g`
* Targets: `all` (builds `zxbasic`), `clean`, `test`.

