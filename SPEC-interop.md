# SPEC-interop.md - ZXBasic POSIX Interoperability & Scripting

Specification for POSIX environment integration, UNIX shebang execution, self-contained executable script serialization (`SAVE ... CODE`), and standard I/O stream channel mapping in ZXBasic.

---

## 1. Overview & Architectural Goals

ZXBasic bridges classic Sinclair ZX Spectrum BASIC programming with modern POSIX systems. To enable ZXBasic programs to behave as first-class UNIX utilities, CLI tools, and shell filters, this specification establishes three core interoperability features:

1. **Shebang Line Support (`#!`):** Seamless execution of BASIC scripts via UNIX direct execution (`./script.bas`), ignoring the leading interpreter directive during source ingestion.
2. **Executable Script Generation (`SAVE ... CODE`):** Exporting stored BASIC programs directly into executable POSIX scripts with an automated shebang header and executable permissions (`chmod u+x`).
3. **POSIX Standard I/O Channel Mapping (`#0`, `#1`, `#2`):** Direct alignment of Sinclair BASIC stream channels `#0`, `#1`, and `#2` to POSIX file descriptors `0` (`stdin`), `1` (`stdout`), and `2` (`stderr`), unifying standard stream I/O, error reporting, and UNIX pipeline redirection.
4. **Subprocess Execution & Stream Redirection (`RUN ... TO`):** Direct invocation of external host executables or BASIC programs with custom stream bindings (`RUN "file" [TO [#n], [#m], [#r]]`), redirecting `stdin`, `stdout`, and `stderr` to open channels or falling back to current system streams (`#0`, `#1`, `#2`).

---

## 2. Shebang Line Ingestion (`#!`)

### 2.1 Definition & Syntax
A UNIX shebang line begins with the two-character sequence `#!` at byte offset 0 of the file. It specifies the path to the interpreter used by the host operating system to execute the script:

```text
#!/usr/bin/env zxbasic
```

### 2.2 Ingestion Semantics
During program ingestion from a file—including batch execution from the command line (`zxbasic script.bas`), `LOAD "script.bas"`, `RUN "script.bas"`, and `MERGE "script.bas"`:

1. **First Line Check:** When reading the first physical line of a source file, the reader checks if the first two non-whitespace characters (or raw bytes 0 and 1) are `#!`.
2. **Ignored Directive:** If present, the entire first line (up to and including the terminating newline `\n` or `\r\n`) is discarded:
   * It **does not** generate a syntax error.
   * It **is not** stored in the `Program` store.
   * It **does not** occupy a line number.
   * It **is not** executed as an immediate command.
3. **Subsequent Lines:** Program line parsing begins immediately on the line following the shebang line.
4. **Scope & Edge Cases:**
   * A line containing `#!` that is *not* the first line in the file (e.g. appearing inside a `REM` statement, in a string literal, or after numbered lines) is treated as standard BASIC syntax.
   * Empty shebang lines (e.g. `#!` alone) or arbitrary interpreter paths (e.g. `#!/usr/local/bin/zxbasic -v`) are safely stripped.
   * Files without a shebang line continue to be ingested starting at line 1 with zero overhead or behavioral changes.

---

## 3. Executable Script Serialization: `SAVE "file" CODE`

In classic Sinclair BASIC, the `CODE` keyword was used with `SAVE` to persist raw binary memory dumps (`SAVE "name" CODE start, length`). In ZXBasic, `SAVE ... CODE` is adapted to produce modern, self-contained executable POSIX scripts.

### 3.1 Syntax
```basic
SAVE <file_expr> CODE
```
* `<file_expr>`: Any valid string expression evaluating to a target file path.
* `CODE`: Mandatory keyword indicating executable script serialization. Can follow the file expression with or without whitespace (e.g. `SAVE "script.bas" CODE` or `SAVE "script.bas"CODE`).
* **No `LINE` Auto-Run Required:** Unlike standard `SAVE <file_expr> [LINE [n]]`, `SAVE ... CODE` does not accept a `LINE` argument. Passing a file to `zxbasic` on the host command line (`zxbasic <file>`) already automatically begins execution from line 1 by default.

### 3.2 Operational Behavior
When `SAVE <file_expr> CODE` is executed:

1. **File Creation:** Opens the specified file path for writing (`"w"`).
2. **Shebang Header:** Writes the canonical portable shebang header as the very first line of the file:
   ```text
   #!/usr/bin/env zxbasic
   ```
3. **Program Export:** Exports all stored program lines in ascending order (`program_list`), formatted as `<line_no> <source>\n`.
4. **File Permissions (`chmod u+x`):**
   * Upon successful write and closure of the file stream, the runtime updates the file's POSIX access permissions using `chmod(path, mode)`.
   * Ensures the user executable permission bit (`S_IXUSR`) is set. Specifically, permissions are set to `0755` (`rwxr-xr-x`) or equivalent `mode | S_IXUSR | S_IXGRP | S_IXOTH`, masked by the active `umask`.
5. **Error Conditions:**
   * If file creation fails (e.g., directory does not exist, permission denied): raises report `C Nonsense in BASIC`.
   * If `chmod` fails: raises report `C Nonsense in BASIC`.
   * If `<file_expr>` is not a valid string: raises report `C Nonsense in BASIC`.

---

## 4. POSIX Standard Streams Channel Mapping (`#0`, `#1`, `#2`)

Sinclair BASIC utilizes a unified stream abstraction where I/O statements optionally address a channel number `#n`. ZXBasic maps channels `#0`, `#1`, and `#2` directly to POSIX standard streams.

### 4.1 System Channel Mapping Table

| Channel | POSIX Stream | File Descriptor | Operating Mode | Default Basic Commands | Description |
| :---: | :---: | :---: | :---: | :---: | :--- |
| `#0` | `stdin` (`rt->in`) | `0` | Read-only | `INPUT` | Standard input. Open by default. Ingests data from keyboard, pipes, or input redirection. |
| `#1` | `stdout` (`rt->out`) | `1` | Write-only | `PRINT` | Standard output. Open by default. Emits program display and piped output. |
| `#2` | `stderr` | `2` | Write-only | System Reports / Errors | Standard error. Open by default. Emits error reports and diagnostic messages; accessible to user programs for error logging. |
| `#3` | `aux` / printer | - | Write-only | - | Optional auxiliary stream / spooler (reserved). |
| `#4 .. #15` | User File Channels | - | Read or Write | `OPEN #`, `CLOSE #` | User-managed file I/O channels. Initially closed. |

### 4.2 Default Stream Behaviors

1. **Unadorned `PRINT` Statements:**
   * Any `PRINT` statement without an explicit `#n` channel selector implicitly targets channel `#1` (`stdout`).
   * Example: `PRINT "Hello"` is equivalent to `PRINT #1; "Hello"`.
2. **Unadorned `INPUT` Statements:**
   * Any `INPUT` statement without an explicit `#n` channel selector implicitly targets channel `#0` (`stdin`).
   * Example: `INPUT A$` is equivalent to `INPUT #0; A$`.
3. **Interpreter Diagnostics & Error Reports:**
   * System error reports (e.g. `0 OK, 0:1`, `B Integer out of range`), syntax errors, `STOP` reports, and debug messages are routed to channel `#2` (`stderr`).
   * This guarantees that in UNIX pipelines (`zxbasic filter.bas < in.txt > out.txt`), error messages and status codes do not corrupt piped standard output.

### 4.3 Explicit Channel Access (`#0`, `#1`, `#2`)

Channels `#0`, `#1`, and `#2` are open by default upon interpreter startup and require no preceding `OPEN #` call:

* **Direct Console/Piped Output (`PRINT #1`):**
  ```basic
  PRINT #1; "Data row: "; X; ", "; Y
  ```
* **Standard Error Logging (`PRINT #2`):**
  ```basic
  100 IF ERR_FLAG THEN PRINT #2; "ERROR: Invalid input configuration": STOP
  ```
  Writes the error string directly to POSIX `stderr` without polluting `stdout`.
* **Standard Input Reading (`INPUT #0`):**
  ```basic
  INPUT #0; LINE$
  ```
  Reads from standard input or an incoming shell pipe.

### 4.4 Stream Mode Constraints & Protections

1. **Mode Enforcement:**
   * Channel `#0` is **Read-Only**: executing `PRINT #0; ...` or `MOVE ... TO #0` raises report `C Nonsense in BASIC` (invalid stream direction).
   * Channels `#1` and `#2` are **Write-Only**: executing `INPUT #1; ...`, `INPUT #2; ...`, or `MOVE #1 TO ...` raises report `C Nonsense in BASIC`.
2. **Permanent System Allocation:**
   * Attempting to close `#0`, `#1`, or `#2` via `CLOSE #0`, `CLOSE #1`, or `CLOSE #2` is prohibited and raises report `C Nonsense in BASIC`.
   * Attempting to re-open `#0`, `#1`, or `#2` via `OPEN #0, "file"` is prohibited and raises report `B Integer out of range` (user channels must be `4 .. 15`).
3. **Interaction with `CLEAR`:**
   * The `CLEAR` command closes all open user file channels (`#4 .. #15`), but leaves system channels `#0`, `#1`, and `#2` intact and open.
4. **Stream Duplication & Piping (`MOVE`):**
   * Standard stream channels participate fully in stream transfers:
     * `MOVE #0 TO #4`: Streams all data from standard input into open file channel `#4`.
     * `MOVE #4 TO #1`: Dumps contents of open file channel `#4` to standard output.
     * `MOVE #4 TO #2`: Dumps contents of open file channel `#4` to standard error.

---

## 5. Subprocess Execution & Stream Redirection: `RUN "file" [TO [#n], [#m], [#r]]`

To integrate ZXBasic workflows directly with UNIX utilities and pipeline processing, `RUN` supports invoking external host executables and redirecting standard input, standard output, and standard error across open channel streams.

### 5.1 Syntax Variations
```basic
RUN <file_expr> [TO [[#]stdin_ch] [, [[#]stdout_ch]] [, [[#]stderr_ch]]]
```
* `<file_expr>`: Any valid string expression evaluating to a file path or executable binary.
* `TO`: Initiates positional stream redirection for the spawned process or loaded program.
* Channel Arguments: Up to three positional channel references corresponding to standard I/O:
  1. **First Argument (`stdin_ch`):** Redirects standard input (`stdin`, fd 0). Defaults to `#0` if omitted or left blank.
  2. **Second Argument (`stdout_ch`):** Redirects standard output (`stdout`, fd 1). Defaults to `#1` if omitted or left blank.
  3. **Third Argument (`stderr_ch`):** Redirects standard error (`stderr`, fd 2). Defaults to `#2` if omitted or left blank.
* Channel specifiers may optionally include a leading `#` prefix (e.g. `TO #4, #5, #2` or `TO 4, 5, 2`).
* Empty slots between commas retain defaults (e.g. `TO , #5` leaves `stdin` as `#0` and `stderr` as `#2`).

### 5.2 Target Dispatch: Host Executable vs. BASIC Program

When evaluating `<file_expr>`, the runtime inspects the file on the host filesystem:

1. **Host Executable Detection:**
   * The runtime tests the path for execute permissions (`access(path, X_OK) == 0`) and verifies that the path is not a directory (`!S_ISDIR`).
   * **If the file is executable:** The target is executed as an external POSIX process (subprocess mode). It is **not** loaded or parsed as a BASIC program.
   * **If the file is not executable:** The target is loaded and run as a BASIC program (chain-load mode).

### 5.3 Executable Process Spawning (Subprocess Mode)

When an executable file is detected:

1. **State Preservation:**
   * The existing BASIC program in memory, variable tables (`symtab`), call stacks, and loop stacks are **completely preserved**.
2. **Channel Resolution & File Descriptor Plumbing:**
   * The runtime resolves the underlying POSIX file descriptors for the three target channels (`stdin_ch`, `stdout_ch`, `stderr_ch`).
   * Channels backed by `FILE *` streams (such as user files opened via `OPEN #` or standard streams) are flushed (`fflush`).
3. **Execution (`fork` / `exec`):**
   * The child process is created via `fork(2)` and `execv(2)` / `posix_spawn(3)`.
   * Standard file descriptors `0`, `1`, and `2` in the child process are duplicated from the resolved channel descriptors using `dup2(2)`.
4. **Synchronous Wait & Status Capture:**
   * The parent ZXBasic process halts execution of subsequent BASIC statements and waits for the child process to complete via `waitpid(2)`.
   * Upon completion, the child process's exit code is extracted (`WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)`).
   * The exit code is saved into the read-only system variable `STATUS` (accessible in BASIC expressions).
5. **Execution Resumption:**
   * Execution in the parent BASIC program seamlessly resumes at the next statement or line following the `RUN` statement.

### 5.4 BASIC Program Chain-Loading with Redirection

If `<file_expr>` is not executable:

1. **Program Ingestion:**
   * Clears the current program store and runtime variables (`runtime_load`).
2. **Channel Rebinding:**
   * If the `TO` clause was provided, the loaded BASIC program executes with its standard I/O channels `#0`, `#1`, and/or `#2` bound to the designated channels for the duration of the run.
   * `INPUT` statements in the child program read from `stdin_ch`, `PRINT` statements emit to `stdout_ch`, and errors route to `stderr_ch`.

### 5.5 Channel Direction & Mode Validation

To prevent stream corruption or blocking, all channel references specified in the `TO` clause are strictly validated prior to execution:

* **Channel Range & State:** Each specified channel must evaluate to an integer $0 \le n \le 15$ and must be currently open. Invalid channel numbers raise report `B Integer out of range`; referencing a closed channel raises report `C Nonsense in BASIC`.
* **Direction Compatibility:**
  * `stdin_ch` must be readable (`CHANNEL_MODE_READ` or `#0`). Specifying a write-only channel raises report `C Nonsense in BASIC`.
  * `stdout_ch` must be writable (`CHANNEL_MODE_WRITE` or `#1`). Specifying a read-only channel raises report `C Nonsense in BASIC`.
  * `stderr_ch` must be writable (`CHANNEL_MODE_WRITE` or `#2`). Specifying a read-only channel raises report `C Nonsense in BASIC`.
* **File Errors:** If `<file_expr>` does not exist or cannot be accessed, raises report `C Nonsense in BASIC`.

---

## 6. Formal Syntax & EBNF

```ebnf
save_stmt        ::= "SAVE" ws+ string_expr (ws* "CODE" | [ws+ "LINE" [ws+ integer_expr]]) ;

run_stmt         ::= "RUN" [ws+ (integer_expr | string_expr [ws* run_suffix])] ;
run_suffix       ::= ("," | ws) ws* integer_expr | ws+ "TO" ws+ run_redirection ;
run_redirection  ::= [channel_ref] [ws* "," [ws* channel_ref] [ws* "," [ws* channel_ref]]] ;
channel_ref      ::= ["#"] channel_expr ;

print_stmt       ::= "PRINT" [ws* "#" channel_expr ws* (";" | ",")] print_items ;
input_stmt       ::= "INPUT" [ws* "#" channel_expr ws* (";" | ",")] input_items ;

channel_expr     ::= integer_expr ; (* 0..2 for system streams, 4..15 for user files *)
```

---

## 7. End-to-End Workflow Examples

### 7.1 Creating an Executable Filter Script
Within the interactive interpreter:
```basic
10 REM UNIX Stream Filter
20 INPUT LINE$
30 IF LINE$ = "EOF" THEN STOP
40 PRINT "PROCESSED: "; LINE$
50 GOTO 20
SAVE "filter" CODE
EXIT
```

On the host terminal:
```bash
$ ls -l filter
-rwxr-xr-x 1 user user 115 Oct 16 22:00 filter

$ head -n 1 filter
#!/usr/bin/env zxbasic

$ echo -e "apples\noranges\nEOF" | ./filter
PROCESSED: apples
PROCESSED: oranges
```

### 7.2 Separating Data and Error Streams
```basic
10 REM CLI Math Tool
20 INPUT "Enter divisor: "; D
30 IF D = 0 THEN PRINT #2; "Error: Division by zero is undefined": EXIT 1
40 PRINT "Result: "; 100 / D
```

Executing with redirection:
```bash
$ ./divtool > output.txt 2> error.log
```
* If divisor is 0, `error.log` receives `"Error: Division by zero is undefined"` and process exits with status code 1.
* If divisor is valid, `output.txt` receives `"Result: 50"`.

### 7.3 Spawning External Tools with Channel Redirection
```basic
10 REM Sort an input file using host /usr/bin/sort
20 OPEN #4, "unsorted.txt"
30 OPEN #5, "sorted.txt"
40 RUN "/usr/bin/sort" TO #4, #5
50 PRINT "Sort completed with exit code: "; STATUS
60 CLOSE #4, #5
```
* The host `/usr/bin/sort` utility is executed without disturbing memory.
* Its standard input is fed directly from channel `#4` (`unsorted.txt`).
* Its standard output is written directly to channel `#5` (`sorted.txt`).
* Standard error defaults to `#2` (`stderr`).
* Upon completion, control returns to line 50, where `STATUS` contains the process exit code (`0` on success).
