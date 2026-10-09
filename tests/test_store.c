#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/program.h"

static void test_program_basic(void) {
    Program p;
    program_init(&p);
    assert(p.count == 0);

    // Insert out of order
    assert(program_insert_or_replace(&p, 30, "PRINT 30"));
    assert(program_insert_or_replace(&p, 10, "PRINT 10"));
    assert(program_insert_or_replace(&p, 50, "PRINT 50"));
    assert(program_insert_or_replace(&p, 20, "PRINT 20"));
    assert(program_insert_or_replace(&p, 40, "PRINT 40"));

    assert(p.count == 5);
    // Verify sorted
    assert(p.lines[0].line_no == 10 && strcmp(p.lines[0].source, "PRINT 10") == 0);
    assert(p.lines[1].line_no == 20 && strcmp(p.lines[1].source, "PRINT 20") == 0);
    assert(p.lines[2].line_no == 30 && strcmp(p.lines[2].source, "PRINT 30") == 0);
    assert(p.lines[3].line_no == 40 && strcmp(p.lines[3].source, "PRINT 40") == 0);
    assert(p.lines[4].line_no == 50 && strcmp(p.lines[4].source, "PRINT 50") == 0);

    // Replace line 30
    assert(program_insert_or_replace(&p, 30, "PRINT 30 REPLACED"));
    assert(p.count == 5);
    assert(p.lines[2].line_no == 30 && strcmp(p.lines[2].source, "PRINT 30 REPLACED") == 0);

    // Find exact index
    assert(program_find_index(&p, 10) == 0);
    assert(program_find_index(&p, 30) == 2);
    assert(program_find_index(&p, 50) == 4);
    assert(program_find_index(&p, 25) == -1);

    // Find >= index
    assert(program_find_ge_index(&p, 5) == 0);   // >= 5 is line 10 (idx 0)
    assert(program_find_ge_index(&p, 20) == 1);  // >= 20 is line 20 (idx 1)
    assert(program_find_ge_index(&p, 25) == 2);  // >= 25 is line 30 (idx 2)
    assert(program_find_ge_index(&p, 50) == 4);  // >= 50 is line 50 (idx 4)
    assert(program_find_ge_index(&p, 51) == -1); // >= 51 doesn't exist

    // Delete lines
    assert(program_delete(&p, 30));
    assert(p.count == 4);
    assert(program_find_index(&p, 30) == -1);
    assert(p.lines[2].line_no == 40);

    // Delete non-existent
    assert(!program_delete(&p, 99));

    // Bounds checking
    assert(!program_insert_or_replace(&p, 0, "INVALID"));
    assert(!program_insert_or_replace(&p, 10000, "INVALID"));
    assert(program_insert_or_replace(&p, 1, "REM MIN"));
    assert(program_insert_or_replace(&p, 9999, "REM MAX"));

    // Clear
    program_clear(&p);
    assert(p.count == 0);

    program_free(&p);
    printf("test_store: PASS\n");
}

int main(void) {
    test_program_basic();
    return 0;
}
