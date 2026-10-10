# SPEC-colors.md - ZX-Basic Console Attributes & Screen Control

Specification for cursor positioning and visual attribute styling in standard VT100 / ANSI terminal environments, matching Sinclair ZX Spectrum BASIC semantics.

---

## 1. Overview & Terminal Model

In original Sinclair BASIC, display attributes applied to 8×8 character cells in a fixed 32×24 screen grid. On modern POSIX terminals, ZXBasic maps these concepts directly to standard ANSI/VT100 escape sequences emitted to standard output.

* **Coordinate System:** Sinclair BASIC uses 0-indexed screen coordinates: `row 0..21`, `column 0..31` (rows 22 and 23 were traditionally reserved for command inputs and system prompts). In ZXBasic, rows and columns are 0-based and clamped to the current terminal window dimensions (falling back to a default 32×24 viewport if unconstrained).
* **Scope of Attributes:**
  * **Permanent:** Set via standalone statements (`INK 2`). Affects all subsequent output until changed.
  * **Temporary:** Embedded as inline stream modifiers inside `PRINT` or `INPUT` statements (`PRINT INK 2; "Red"; INK 7; "White"`). Only affects the immediate statement/item; reverts to the permanent attributes at the end of the statement.

---

## 2. Generic System Variables Architecture (`sysvars`)

To decouple state management and enable future extensions (e.g., sound timers, custom stream handles, error traps), system variables are unified under a generic dictionary/table runtime subsystem.

### 2.1 Sysvar Representation
```c
typedef enum {
    SVAR_TYPE_INT,
    SVAR_TYPE_DOUBLE,
    SVAR_TYPE_STR
} SvarType;

typedef struct {
    const char *name;
    SvarType type;
    union {
        int64_t i_val;
        double  d_val;
        char   *s_val;
    } val;
    uint8_t flags; // e.g., READ_ONLY, PERSISTENT
} SysVar;

```

### 2.2 Core Console System Variables

The runtime initializes and tracks the following system variables:

| Sysvar Name | Type | Sinclair Equivalent | Default | Description |
| --- | --- | --- | --- | --- |
| `ATTR_P_INK` | INT | `ATTR_P` (bits 0-2) | `-1` | Permanent foreground color index (`0..7`, or `-1` = Terminal default) |
| `ATTR_P_PAPER` | INT | `ATTR_P` (bits 3-5) | `-1` | Permanent background color index (`0..7`, or `-1` = Terminal default) |
| `ATTR_P_BRIGHT` | INT | `ATTR_P` (bit 6) | `0` | Permanent brightness flag (`0` or `1`) |
| `ATTR_P_INVERSE` | INT | `ATTR_P` (bit 7) | `0` | Permanent reverse video flag (`0` or `1`) |
| `ATTR_T_INK` | INT | `ATTR_T` | `-2` | Temporary foreground override (`-2` = inactive, `-1` = Terminal default) |
| `ATTR_T_PAPER` | INT | `ATTR_T` | `-2` | Temporary background override (`-2` = inactive, `-1` = Terminal default) |
| `ATTR_T_BRIGHT` | INT | `ATTR_T` | `-2` | Temporary brightness override (`-2` = inactive, `-1` = reset to `0`) |
| `ATTR_T_INVERSE` | INT | `ATTR_T` | `-2` | Temporary inverse override (`-2` = inactive, `-1` = reset to `0`) |
| `S_POSN_ROW` | INT | `S_POSN` (high byte) | `0` | Current 0-based cursor line |
| `S_POSN_COL` | INT | `S_POSN` (low byte) | `0` | Current 0-based cursor column |
| `SCR_ROWS` | INT | - | `24` | Virtual/detected terminal rows |
| `SCR_COLS` | INT | - | `32` | Virtual/detected terminal columns |

---

## 3. Color & Attribute Mappings

### 3.1 Spectrum Color Indices to ANSI SGR Codes

The Sinclair 3-bit color palette maps directly to the standard ANSI 8-color matrix (indices $0 \dots 7$), with `-1` representing terminal defaults:

| Code | Sinclair Color | ANSI Color Name | Normal (`BRIGHT 0`) | High Intensity (`BRIGHT 1`) |
| --- | --- | --- | --- | --- |
| `-1` | Terminal Default | Terminal Default | `\033[39m` / `\033[49m` | `\033[39m` / `\033[49m` |
| `0` | Black | Black | `\033[30m` / `\033[40m` | `\033[90m` / `\033[100m` |
| `1` | Blue | Blue | `\033[34m` / `\033[44m` | `\033[94m` / `\033[104m` |
| `2` | Red | Red | `\033[31m` / `\033[41m` | `\033[91m` / `\033[101m` |
| `3` | Magenta | Magenta | `\033[35m` / `\033[45m` | `\033[95m` / `\033[105m` |
| `4` | Green | Green | `\033[32m` / `\033[42m` | `\033[92m` / `\033[102m` |
| `5` | Cyan | Cyan | `\033[36m` / `\033[46m` | `\033[96m` / `\033[106m` |
| `6` | Yellow | Yellow | `\033[33m` / `\033[43m` | `\033[93m` / `\033[103m` |
| `7` | White | White | `\033[37m` / `\033[47m` | `\033[97m` / `\033[107m` |
| `8` | Transparent / Keep | - | No change | No change |

### 3.2 ANSI Escape Sequence Dispatch

Whenever active attributes change, the runtime calculates the delta and emits the corresponding ANSI Select Graphic Rendition (SGR) sequence:

* **Reset Sequence:** `\033[0m`
* **Default Terminal Colors:**
  * Foreground: `\033[39m`
  * Background: `\033[49m`
* **Inverse (Reverse Video):**
  * `INVERSE 1`: `\033[7m`
  * `INVERSE 0`: `\033[27m`

* **Color Selection Formula:**
  * For Foreground (`INK c`, where $c \in [-1..8]$):
    * If $c == -1$: emit `\033[39m` (terminal default foreground)
    * If $c \in [0..7]$ and `BRIGHT == 1`: code is $90 + c$
    * If $c \in [0..7]$ and `BRIGHT == 0`: code is $30 + c$
    * If $c == 8$: no change

  * For Background (`PAPER c`, where $c \in [-1..8]$):
    * If $c == -1$: emit `\033[49m` (terminal default background)
    * If $c \in [0..7]$ and `BRIGHT == 1`: code is $100 + c$
    * If $c \in [0..7]$ and `BRIGHT == 0`: code is $40 + c$
    * If $c == 8$: no change

---

## 4. Statements & Syntactical Grammar

### 4.1 Keyword Semantics

#### `AT row, col`

* **Usage:** `AT <row_expr>, <col_expr>` (Allowed only within `PRINT` and `INPUT`).
* **Behavior:**
  * Moves the cursor to the 0-indexed position: `row` ($0 \dots \text{SCR\_ROWS}-1$), `col` ($0 \dots \text{SCR\_COLS}-1$).
  * Emits ANSI cursor position: `\033[<row+1>;<col+1>H`.
  * Updates `S_POSN_ROW` and `S_POSN_COL`.
  * Out-of-bounds parameters raise `B Integer out of range`.

#### `INK color`

* **Usage:** Standalone statement or inline within `PRINT`/`INPUT`.
* **Argument:** Integer expression evaluated to $-1 \dots 8$.
  * `-1`: Reset to terminal default foreground color (`\033[39m`).
  * $0 \dots 7$: Sets the foreground color index.
  * $8$: Transparent/keep current foreground.
* Values outside $[-1 \dots 8]$ raise `B Integer out of range`.

#### `PAPER color`

* **Usage:** Standalone statement or inline within `PRINT`/`INPUT`.
* **Argument:** Integer expression evaluated to $-1 \dots 8$.
  * `-1`: Reset to terminal default background color (`\033[49m`).
  * $0 \dots 7$: Sets the background color index.
  * $8$: Transparent/keep current background.
* Values outside $[-1 \dots 8]$ raise `B Integer out of range`.

#### `BRIGHT flag`

* **Usage:** Standalone statement or inline within `PRINT`/`INPUT`.
* **Argument:** Integer expression evaluated to `-1` (reset to normal / `0`), `0` (normal), `1` (high intensity), or `8` (no change).
* Other values raise `B Integer out of range`.

#### `INVERSE flag`

* **Usage:** Standalone statement or inline within `PRINT`/`INPUT`.
* **Argument:** Integer expression evaluated to `-1` (reset to normal / `0`), `0` (normal), `1` (reverse foreground and background), or `8` (no change).
* Other values raise `B Integer out of range`.

### 4.2 Temporary vs. Permanent Behavior

* **Standalone statement:**
```text
10 INK 2: PAPER 7: BRIGHT 1
20 INK -1: PAPER -1
```
Modifies permanent `ATTR_P_*` variables. Emits the corresponding escape codes immediately.

* **Inline stream modifier:**
```text
20 PRINT AT 10, 5; INK 4; "Green Text"; INK -1; "Default Text"
```
* `INK 4` updates `ATTR_T_INK = 4`. The runtime pushes the updated escape codes before writing `"Green Text"`.
* `INK -1` temporarily switches back to terminal default colors (`\033[39m`).
* At the conclusion of the `PRINT` or `INPUT` statement, the runtime checks if any `ATTR_T_*` variables were modified (i.e., not equal to `-2` / `ATTR_INACTIVE`). If so, they are reset to `-2`, and escape sequences are re-emitted restoring the permanent `ATTR_P_*` state.



---

## 5. Screen Clearing: `CLS`

* **Statement:** `CLS`
* **Behavior:**
1. Emits terminal clear screen: `\033[2J`.
2. Moves cursor to top-left origin: `\033[H`.
3. Re-applies current permanent attributes (`ATTR_P_PAPER`, `ATTR_P_INK`, etc.).
4. Resets `S_POSN_ROW = 0` and `S_POSN_COL = 0`.

