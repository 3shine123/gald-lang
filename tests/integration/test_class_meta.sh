#!/bin/bash
# Test that class metadata (+class getter, vtable, class var) is correctly generated
set -euo pipefail

OVICC="${OVICC:-./builddir/ovicc}"
FIXTURE="${1:-tests/fixtures/hello.ov}"
OUTPUT=$(mktemp /tmp/ovic_test_XXXX.c)
trap "rm -f $OUTPUT" EXIT

total=0
passed=0

test_name() {
    printf "  %-55s " "$1"
    total=$((total + 1))
}

pass() {
    passed=$((passed + 1))
    echo "PASS"
}

fail() {
    echo "FAIL: $1"
}

# Compile the fixture
"$OVICC" "$FIXTURE" -o "$OUTPUT" 2>/dev/null || { fail "ovicc failed"; exit 1; }

# Check +class function declarations
test_name "+class declaration exists"
if grep -q "NPClass \* NPObject_getClass" "$OUTPUT"; then pass; else fail "missing NPObject_getClass declaration"; fi

test_name "+class definition has braces"
if grep -qE "NPObject_getClass.*\{" "$OUTPUT"; then pass; else fail "NPObject_getClass body missing braces"; fi

test_name "+class returns &class var"
if grep -q "return &ovic_NPObject_class" "$OUTPUT"; then pass; else fail "NPObject_getClass missing &ovic_NPObject_class"; fi

# Check class metadata variable
test_name "class metadata variable declared"
if grep -q "NPClass ovic_NPObject_class;" "$OUTPUT"; then pass; else fail "missing NPClass ovic_NPObject_class"; fi

# Check vtable excludes class methods
test_name "vtable excludes class methods"
INST_VTABLE=$(awk '/ovic_NPObject_vtable_inst = \{/{flag=1; next} /^\};/{flag=0} flag' "$OUTPUT")
if echo "$INST_VTABLE" | grep -q "\.init"; then
    if echo "$INST_VTABLE" | grep -q "\.alloc"; then fail "alloc found in instance vtable"; else pass; fi
else
    fail "no vtable_inst found"
fi

# Check vtable contains instance methods
test_name "vtable contains init and dealloc"
VTABLE_CONTENT=$(awk '/ovic_NPObject_vtable_inst = \{/{flag=1; next} /^\};/{flag=0} flag' "$OUTPUT")
if echo "$VTABLE_CONTENT" | grep -q "\.init"; then
    pass
else
    fail "init or dealloc missing from vtable"
fi

# Check vtable index macros exclude class methods
test_name "vtable indices exclude +alloc"
if grep -q "vtable_index_alloc" "$OUTPUT"; then fail "alloc has vtable index"; else pass; fi

# Check vtable indices exist for instance methods
test_name "vtable indices exist for instance methods"
if grep -q "vtable_index_init" "$OUTPUT" && grep -q "vtable_index_dealloc" "$OUTPUT"; then pass;
else fail "missing vtable_index for instance methods"; fi

# Check _cmd parameter
test_name "+class takes self and _cmd"
if grep -q "NPObject_getClass(NPObject \* self, SEL _cmd)" "$OUTPUT"; then pass; else fail "getClass signature missing _cmd"; fi

# Student class also gets metadata
test_name "Student class gets getClass"
if grep -q "Student_getClass" "$OUTPUT"; then pass; else fail "missing Student_getClass"; fi

# Meta vtable tests
test_name "meta vtable exists for NPObject"
if grep -q "ovic_NPObject_meta_vtable_inst" "$OUTPUT"; then pass; else fail "missing NPObject meta vtable"; fi

test_name "meta vtable contains alloc and class"
META_VTABLE=$(awk '/ovic_NPObject_meta_vtable_inst = \{/{flag=1; next} /^\};/{flag=0} flag' "$OUTPUT")
if echo "$META_VTABLE" | grep -q "\.alloc" && echo "$META_VTABLE" | grep -q "\.class"; then pass; else fail "alloc or class missing from meta vtable"; fi

test_name "meta vtable uses getClass for +class"
if grep -q "\.class = NPObject_getClass" "$OUTPUT"; then pass; else fail "meta vtable class field wrong"; fi

test_name "NPClass has class_vtable pointer"
if grep -q "\.class_vtable = &ovic_NPObject_meta_vtable_inst" "$OUTPUT"; then pass; else fail "missing class_vtable init"; fi

# Class metadata init section
test_name "class metadata init for NPObject"
if grep -q "ovic_NPObject_class = (NPClass)" "$OUTPUT"; then pass; else fail "missing NPObject_class init"; fi

test_name "class metadata init for Student"
if grep -q "ovic_Student_class = (NPClass)" "$OUTPUT"; then pass; else fail "missing Student_class init"; fi

# Compile the generated C with clang
test_name "generated C compiles with clang"
if clang -fsyntax-only -I./include "$OUTPUT" 2>/dev/null; then pass; else fail "clang compilation failed"; fi

echo
echo "$passed/$total passed"
exit $([ "$passed" -eq "$total" ])
