# SPEC-channels.md - ZXBasic Channels, Streams & Data Files

Specification for channel redirection, stream-based data file I/O, directory management, and recursive erasure in ZXBasic, replicating Sinclair ZX Spectrum Interface 1 capabilities adapted to modern POSIX filesystems.

---

## 1. Overview & Architectural Principles

Sinclair ZX Spectrum BASIC introduced the stream and channel mechanism to unify input/output operations across screens, printers, microdrives, and serial lines via the `#` stream prefix. Interface 1 extended this with filesystem and peripheral support (`OPEN #`, `CLOSE #`, `MOVE`, `FORMAT`, `ERASE`).

`SPEC-channels` specifies a clean, robust POSIX implementation of channels and data files for ZXBasic:
* **Stream Abstraction:** Unified stream handles indexed by integer channel numbers `#n` (`0 .. 15`).
* **POSIX File Backing:** Channels map to standard C `FILE *` streams with dedicated lifecycle management.
* **Smart File Opening:** `OPEN #n, file` automatically decides between Read and Write mode based on file presence (no manual append flags).
* **Multi-Channel Control:** `CLOSE #n[, #m...]` allows closing multiple active streams in a single statement.
* **Stream I/O:** `PRINT #n` and `INPUT #n` redirect formatted output and structured data ingestion to open channels.
* **Persistent Stream Copying & File Moving:** `MOVE #n TO #m` performs efficient byte-level transfer without closing source or destination channels; `MOVE fromfile TO tofile` moves or renames non-open files by name using POSIX `rename(2)`.
* **Name-Based File Copying:** `COPY fromfile TO tofile` duplicates closed files on disk by name, with destination directory resolution.
* **Automatic Channel Teardown:** `CLEAR` closes all open user channels alongside symbol table and stack reset.
* **POSIX Directory Management:** `FORMAT dir` acts as directory navigation and creation (`mkdir -p` + `chdir`).
* **Recursive Filesystem Erasure:** `ERASE file` unlinks files and performs recursive directory deletion (`rm -rf`) when targeting directories.

---

## 2. Channel Architecture & Stream Table

### 2.1 Channel Number Allocation (`#0 .. #15`)
Channels are indexed by integer expressions evaluating to $0 \le n \le 15$:

| Channel Range | Classification | Default POSIX Binding | Behavior & Properties |
| --- | --- | --- | --- |
| `#0` | System Input | `stdin` (`rt->in`) | Lower screen / keyboard input stream. Read-capable. |
| `#1` | System Status / Input | `stdin` (`rt->in`) | Status area input stream. Read-capable. |
| `#2` | System Console Output | `stdout` (`rt->out`) | Main console display stream (default for unadorned `PRINT`). Write-capable. |
| `#3` | System Printer | `stderr` (or printer spool) | ZX Printer / secondary output stream. Write-capable. |
| `#4 .. #15` | User File Channels | Initially Closed (`NULL`) | Dynamically opened and closed by user programs via `OPEN #` and `CLOSE #`. |

> [!NOTE]
> System streams `#0 .. #3` are permanently initialized at runtime startup. Attempting to `CLOSE #0 .. #3` is prohibited and raises report `C Nonsense in BASIC`.
> User channels `#4 .. #15` are dynamically allocated for file I/O.

### 2.2 Channel Descriptor Model (`Channel` in `src/runtime.h`)
```c
#define MAX_CHANNELS 16
#define MIN_USER_CHANNEL 4
#define MAX_USER_CHANNEL 15

typedef enum {
    CHANNEL_MODE_NONE = 0,
    CHANNEL_MODE_READ,      // Opened for reading ("r")
    CHANNEL_MODE_WRITE,     // Opened for writing ("w")
    CHANNEL_MODE_SYSTEM     // Standard system stream (stdin/stdout/stderr)
} ChannelMode;

typedef struct {
    FILE *fp;               // Active file pointer (or NULL if closed)
    ChannelMode mode;       // Operating mode
    char *path;             // Resolved path string (heap allocated, NULL for system streams)
    bool is_open;           // Active status flag
} Channel;
```

---

## 3. Command Specifications

### 3.1 `OPEN #n, file_expr`

Opens a file stream and associates it with channel number $n$.

#### Syntax
```basic
OPEN #<channel_expr>, <file_expr>
```

#### Arguments
* `<channel_expr>`: An expression evaluating to an integer in the user channel range `4 .. 15`. Values outside `4 .. 15` or non-integers raise report `B Integer out of range`.
* `<file_expr>`: Any valid string expression evaluating to a non-empty POSIX file path.

#### Operational Rules
1. **Channel Collision:** If channel $n$ is already open, `OPEN` automatically flushes and closes the existing stream before opening the new target (safe re-opening).
2. **Existence Check & Mode Selection:**
   * The runtime tests the target path using `access(path, F_OK)`.
   * **If file exists (`access == 0`):** Opens the file in **Read Mode** (`"r"`).
     * If opening fails (e.g. permission denied `EACCES`, target is a directory `EISDIR`), raises report `C Nonsense in BASIC`.
   * **If file does not exist:** Opens the file in **Write Mode** (`"w"`), creating a new empty file.
     * If file creation fails (e.g. invalid directory path, read-only filesystem), raises report `C Nonsense in BASIC`.
3. **No Append Mode:** Existing files are always opened for reading from the beginning (offset 0); new files are opened for writing.

---

### 3.2 `CLOSE #n [, #m ...]`

Closes one or more currently open channels.

#### Syntax
```basic
CLOSE #<channel_expr> [, #<channel_expr> ...]
```

#### Operational Rules
1. Multiple channels may be specified as a comma-separated list:
   ```basic
   CLOSE #4
   CLOSE #4, #5, #6
   ```
2. For each channel $n$:
   * Must evaluate to a valid channel number `4 .. 15`.
   * If channel $n$ is currently open (`channels[n].is_open == true`):
     * Flushes buffers via `fflush(fp)`.
     * Closes descriptor via `fclose(fp)`.
     * Frees stored `path`.
     * Resets entry to `is_open = false`, `mode = CHANNEL_MODE_NONE`.
   * If channel $n$ is **not** open: harmless no-op (idempotent, conforms to modern error-resilient scripting).
   * Attempting to close system streams `#0 .. #3` raises report `C Nonsense in BASIC`.

#### 3.2.1 Channel Teardown via `CLEAR`
Executing `CLEAR` automatically closes all active user channels (`#4 .. #15`), flushing any buffered data to disk and resetting channel states, before resetting program variables and call stacks. This ensures that scripts and REPL sessions can reliably reset channel resources without having to enumerate every open channel.

---

### 3.3 `PRINT #n, [...]`

Redirects formatted character output to channel $n$.

#### Syntax
```basic
PRINT #<channel_expr> [, <print_list>]
PRINT #<channel_expr> ; <print_list>
PRINT #<channel_expr>
```

#### Operational Rules
1. `<channel_expr>` evaluates to integer $n$.
2. **Validation:**
   * Channel $n$ must be active (`is_open == true`) and writable (`mode == CHANNEL_MODE_WRITE` or system stream `#2` / `#3`).
   * If channel $n$ is closed or opened in `CHANNEL_MODE_READ`, execution aborts with report `C Nonsense in BASIC`.
3. **Output Formatting:**
   * Behaves identically to standard `PRINT`, but emits output to `channels[n].fp`:
     * Numeric expressions are formatted using standard ZX Spectrum number-to-string rules.
     * String expressions are output verbatim without enclosing quotes.
     * Semicolon (`;`) concatenates output without spacing.
     * Comma (`,`) advances to the next 16-character column tab stop.
     * Apostrophe (`'`) emits a newline (`\n`).
     * Trailing semicolon or comma suppresses the trailing newline.
     * Absence of trailing delimiter emits a newline (`\n`) and flushes buffers (`fflush`).

---

### 3.4 `INPUT #n, var [, var ...]`

Reads structured values from channel $n$ into variables.

#### Syntax
```basic
INPUT #<channel_expr>, <var_ident> [, <var_ident> ...]
```

#### Operational Rules
1. `<channel_expr>` evaluates to integer $n$.
2. **Validation:**
   * Channel $n$ must be active (`is_open == true`) and readable (`mode == CHANNEL_MODE_READ` or system stream `#0` / `#1`).
   * If channel $n$ is closed or opened in `CHANNEL_MODE_WRITE`, execution aborts with report `C Nonsense in BASIC`.
3. **Data Parsing & Ingestion:**
   * **Reading String Variables (`A$`):**
     * Reads characters from the file up to the next newline (`\n`) or EOF.
     * Strips trailing `\r` (CRLF handling).
     * If the line is enclosed in quotes (`"..."`), strips outer quotes (matching ZX Spectrum data serialization).
     * Assigns resulting string to variable in `rt->symtab`.
   * **Reading Numeric Variables (`A`):**
     * Skips leading whitespace and newlines.
     * Reads numeric token characters (digits, optional leading sign, optional decimal point, optional exponent `E...`).
     * If valid number parsed: converts to `double` and updates `rt->symtab`.
     * If non-numeric character encountered: raises report `C Nonsense in BASIC`.
   * **Comma-Separated Values:**
     * Successive variables in `INPUT #n, A, B$` consume comma-delimited or newline-delimited fields from the stream.
4. **EOF Handling:**
   * If EOF is reached when attempting to read a new value:
     * Raises report `E Out of DATA` (or report `C Nonsense in BASIC` / sets EOF status).
     * Reading an empty string at immediate EOF returns `""` with last error flag.

---

### 3.5 `MOVE #n TO #m` & `MOVE fromfile TO tofile`

`MOVE` serves two distinct operations disambiguated by syntax:
1. **Stream Copying (`MOVE #n TO #m`):** Copies data directly from source stream $n$ to destination stream $m$.
2. **File Moving / Renaming (`MOVE fromfile TO tofile`):** Renames or moves non-open files by path using POSIX `rename(2)`.

#### Syntax
```basic
MOVE #<src_channel_expr> TO #<dest_channel_expr>
MOVE <src_file_expr> TO <dest_file_expr>
```

#### Operational Rules for Stream Copying (`MOVE #n TO #m`)
1. Evaluates source channel $n$ and destination channel $m$.
2. **Validation:**
   * Source channel $n$ must be open for reading (`CHANNEL_MODE_READ` or system input `#0`).
   * Destination channel $m$ must be open for writing (`CHANNEL_MODE_WRITE` or system output `#2`).
   * If $n == m$, operation is a no-op.
   * If either stream is invalid or has wrong mode, raises report `C Nonsense in BASIC`.
3. **Block Transfer:**
   * Copies bytes from `channels[n].fp` to `channels[m].fp` using a 4096-byte chunk buffer until `feof(channels[n].fp)`.
   * Calls `fflush(channels[m].fp)` upon completion.
4. **Channel Persistence:**
   > [!IMPORTANT]
   > Contrary to original Sinclair ZX Spectrum Interface 1 behavior (which automatically closed both streams after `MOVE`), **both channels remain open** in ZXBasic.
   > The caller may continue reading, writing, or explicitly close them via `CLOSE #n, #m`.

#### Operational Rules for File Renaming (`MOVE fromfile TO tofile`)
1. `<src_file_expr>` and `<dest_file_expr>` must evaluate to string expressions denoting filesystem paths.
2. **Non-Open Files Only:** Neither source path nor destination path may be currently open in any user channel (`#4 .. #15`). Attempting to move an open file raises report `C Nonsense in BASIC`.
3. **POSIX Atomic Move/Rename:**
   * Delegates directly to POSIX `rename(src, dest)`.
   * If destination path specifies an existing directory, the file is moved into that directory using the source file's basename.
   * If source file does not exist, or filesystem permissions prohibit renaming, raises report `C Nonsense in BASIC`.
   * Replaces existing destination file atomically if destination already exists.

---

### 3.6 `COPY fromfile TO tofile`

Duplicates non-open files by name on the filesystem.

#### Syntax
```basic
COPY <src_file_expr> TO <dest_file_expr>
```

#### Operational Rules
1. `<src_file_expr>` and `<dest_file_expr>` must evaluate to string expressions denoting filesystem paths. Non-string raises report `C Nonsense in BASIC`.
2. **Non-Open Files Only:** Neither source path nor destination path may be currently open in any user channel (`#4 .. #15`). Attempting to copy an open file raises report `C Nonsense in BASIC`.
3. **Validation & Resolution:**
   * Source file must exist and must not be a directory.
   * If destination path specifies an existing directory, the target filename is formed by appending the source file's basename (`dest/basename(src)`).
   * If source and destination resolve to the identical file on disk, operation is a safe no-op.
4. **Binary Block Copy:**
   * Copies bytes in 4096-byte chunks from source to destination.
   * Creates or overwrites destination file, preserving source file permission bits (`st_mode & 0777`).
   * If read/write error occurs, removes incomplete destination file and raises report `C Nonsense in BASIC`.

---

### 3.7 `FORMAT dir_expr`

Changes the active working directory, creating intermediate directories if they do not exist.

#### Syntax
```basic
FORMAT <dir_expr>
```

#### Operational Rules
1. `<dir_expr>` is any valid string expression evaluating to a directory path. Non-string raises report `C Nonsense in BASIC`.
2. **Directory Creation (`mkdir -p` semantics):**
   * If the directory path does not exist, the runtime recursively creates all missing parent directories with permissions `0755` (`S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH`).
3. **Working Directory Navigation (`chdir`):**
   * Changes the current working directory of the process to the specified path via `chdir(2)`.
4. **Error Handling:**
   * If path resolution fails, permissions are insufficient, or the target exists but is a regular file: raises report `C Nonsense in BASIC`.

---

### 3.8 `ERASE file_expr` (Extended for Directories)

Deletes regular files or recursively deletes directories.

#### Syntax
```basic
ERASE <file_expr>
```

#### Operational Rules
1. Evaluates `<file_expr>` as a string expression.
2. **Inspection (`lstat`):**
   * If path does not exist: raises report `C Nonsense in BASIC`.
3. **Regular File or Symlink:**
   * Unlinks via `unlink(2)`.
4. **Directory (`S_ISDIR`):**
   * **Recursive Force Removal (`rm -rf` semantics):**
     * Recursively traverses all subdirectories and child entries using `nftw(3)` (or recursive `opendir`/`readdir`).
     * Unlinks all child files, symlinks, and nested directories.
     * Removes the target directory via `rmdir(2)`.
5. **Program Store Disambiguation:**
   * The disambiguation rule defined in `SPEC-files.md` Section 6.1 remains in effect:
     * If next token is a string expression $\rightarrow$ Filesystem erasure (`ERASE file`).
     * If next token is `TOKEN_TO` or numeric $\rightarrow$ Memory line range erasure (`ERASE 10 TO 50`).

---

## 4. Grammar, Lexer & Normalizer Specifications

### 4.1 New Tokens & Punctuation
```c
// Lexer token additions in src/lexer.h
TOKEN_HASH,      // '#'
TOKEN_OPEN,      // "OPEN"
TOKEN_CLOSE,     // "CLOSE"
TOKEN_MOVE,      // "MOVE"
TOKEN_FORMAT,    // "FORMAT"
```

### 4.2 Lexer Scanning Rules (`src/lexer.c`)
* Character `#` is scanned as `TOKEN_HASH`.
* Keywords `"OPEN"`, `"CLOSE"`, `"MOVE"`, `"FORMAT"` are recognized case-insensitively and emitted as corresponding tokens.

### 4.3 Auto-Capitalization & Normalization (`src/normalize.c`)
Add `"OPEN"`, `"CLOSE"`, `"MOVE"`, `"FORMAT"` to `is_zx_keyword()` in `src/normalize.c`:
* Input: `open #4, "data.txt": print #4, "hello": close #4`
* Stored: `OPEN #4, "data.txt": PRINT #4, "hello": CLOSE #4`
* Input: `move #4 to #5`
* Normalized: `MOVE #4 TO #5`
* Input: `format "sub/dir"`
* Normalized: `FORMAT "sub/dir"`

### 4.4 Ingestion Syntax Validation Rules
```
statement ::=
    ...
  | OPEN '#' expression ',' expression
  | CLOSE '#' expression ( ',' '#' expression )*
  | PRINT '#' expression ( ( ',' | ';' ) [ print_list ] )?
  | INPUT '#' expression ',' variable ( ',' variable )*
  | MOVE '#' expression TO '#' expression
  | FORMAT expression
  | ERASE expression
```

---

## 5. Architectural Runtime Changes

### 5.1 Runtime State Additions (`Runtime` in `src/runtime.h`)
```c
struct Runtime {
    Program program;
    SymTab symtab;
    Error last_error;
    FILE *in;
    FILE *out;
    ...
    Channel channels[MAX_CHANNELS]; // Channel table 0..15
};
```

### 5.2 Runtime Module Interface (`src/runtime.h`)
```c
// Initializes system channels 0..3 and zeros user channels 4..15
void runtime_channels_init(Runtime *rt);

// Flushes and closes all open user channels 4..15
void runtime_channels_close_all(Runtime *rt);

// Opens channel n in read or write mode based on file presence
bool runtime_channel_open(Runtime *rt, int channel_no, const char *path);

// Closes specified user channel n (4..15)
bool runtime_channel_close(Runtime *rt, int channel_no);

// Copies entire contents of stream src to dest without closing either
bool runtime_channel_move(Runtime *rt, int src_ch, int dest_ch);

// Changes working directory, creating intermediate directories if needed
bool runtime_format_dir(Runtime *rt, const char *path);

// Unlinks file or recursively deletes directory
bool runtime_erase_recursive(Runtime *rt, const char *path);
```

### 5.3 Parser Dispatch Logic (`src/parser.c`)
* **`TOKEN_OPEN`:** Parse `#`, evaluate channel integer, expect `,`, evaluate string path $\rightarrow$ call `runtime_channel_open`.
* **`TOKEN_CLOSE`:** Loop parsing `#`, evaluate channel integer, optional `,` $\rightarrow$ call `runtime_channel_close`.
* **`TOKEN_PRINT`:**
  * Peek for `TOKEN_HASH`. If present: advance, evaluate channel integer, parse optional comma/semicolon and print expressions $\rightarrow$ output to `channels[n].fp`.
  * If no `TOKEN_HASH`: standard console `PRINT` (channel `#2`).
* **`TOKEN_INPUT`:**
  * Peek for `TOKEN_HASH`. If present: advance, evaluate channel integer, expect `,`, parse comma-separated variables $\rightarrow$ read from `channels[n].fp`.
  * If no `TOKEN_HASH`: standard interactive `INPUT` (channel `#0`).
* **`TOKEN_MOVE`:** Parse `#`, evaluate source channel, expect `TOKEN_TO`, parse `#`, evaluate destination channel $\rightarrow$ call `runtime_channel_move`.
* **`TOKEN_FORMAT`:** Parse string expression $\rightarrow$ call `runtime_format_dir`.
* **`TOKEN_ERASE`:**
  * If lookahead is string: evaluate string $\rightarrow$ call `runtime_erase_recursive`.
  * If lookahead is numeric/`TO`: line range deletion $\rightarrow$ call `program_delete_range`.

---

## 6. Detailed Example Workflows

### 6.1 Creating, Writing, Reading & Closing Data Files
```basic
10 REM Create and write data file
20 OPEN #4, "scores.dat"
30 PRINT #4, "Alice", 9500
40 PRINT #4, "Bob", 8200
50 CLOSE #4
60 REM Re-open existing file (automatically opens in READ mode)
70 OPEN #4, "scores.dat"
80 INPUT #4, N1$, S1
90 INPUT #4, N2$, S2
100 CLOSE #4
110 PRINT "Score 1: "; N1$; " = "; S1
120 PRINT "Score 2: "; N2$; " = "; S2
```

### 6.2 Stream Copying with `MOVE`
```basic
10 OPEN #4, "source.txt"
20 OPEN #5, "target.txt"
30 MOVE #4 TO #5
40 REM Channels remain open! Write extra footer to target:
50 PRINT #5, "--- End of Backup ---"
60 CLOSE #4, #5
```

### 6.3 Directory Navigation and Recursive Cleanup
```basic
10 REM Create and enter nested directory
20 FORMAT "saves/user1"
30 OPEN #4, "game.sav"
40 PRINT #4, "LEVEL 5"
50 CLOSE #4
60 REM Return to parent and recursively wipe directory
70 FORMAT "../.."
80 ERASE "saves"
```

---

## 7. Verification & Test Plan (`tests/test_channels.c`)

### 7.1 Test Cases
1. **`OPEN` Mode Disambiguation:**
   * Call `OPEN #4, "new_file.txt"` when file does not exist $\rightarrow$ verify file created in write mode.
   * `PRINT #4, "Test Data"` followed by `CLOSE #4`.
   * Call `OPEN #4, "new_file.txt"` when file exists $\rightarrow$ verify opened in read mode.
   * `INPUT #4, A$` $\rightarrow$ verify `A$ == "Test Data"`.
   * Attempting to `PRINT #4, "Fail"` while in read mode $\rightarrow$ verify report `C Nonsense in BASIC`.
2. **`CLOSE` Multiple Channels:**
   * Open `#4`, `#5`, `#6`.
   * Execute `CLOSE #4, #5, #6`.
   * Verify all three channel descriptors are closed and set to inactive.
   * Verify repeated `CLOSE #4` is safe no-op.
3. **`MOVE #n TO #m` Persistence:**
   * Write 5 lines of text to `"file_a.txt"`.
   * Open `#4, "file_a.txt"` (read) and `#5, "file_b.txt"` (write).
   * Execute `MOVE #4 TO #5`.
   * Verify channels `#4` and `#5` are still open (`channels[4].is_open == true`).
   * Append footer line to `#5`: `PRINT #5, "FOOTER"`.
   * Close both channels.
   * Verify `"file_b.txt"` contains full content of `"file_a.txt"` plus `"FOOTER"`.
4. **`FORMAT` Directory Management:**
   * Execute `FORMAT "temp_test_dir/nested/deep"`.
   * Verify directories created on host filesystem.
   * Verify current working directory is changed to target directory.
   * Create file inside new directory, return to root, verify file exists at expected relative path.
5. **`ERASE` Recursive Directory Deletion:**
   * Create directory tree with multiple subdirectories and files.
   * Call `ERASE "temp_test_dir"`.
   * Verify entire tree is removed from host filesystem (`access(path, F_OK) != 0`).
   * Test regular file erasure continues to work as expected.
6. **Syntax Validation & Capitalization:**
   * Verify lowercase input `open #4, "f": move #4 to #5: format "d"` normalizes to uppercase keywords.
   * Verify syntax validation rejects malformed channel expressions (`OPEN 4`, `MOVE #4`, `CLOSE`).
