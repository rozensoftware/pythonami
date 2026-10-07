#!/bin/sh

set -eu

pythonami=$1
tmpdir=${TMPDIR:-/tmp}/pythonami-debug-stats-$$
trap 'rm -rf "$tmpdir"' EXIT
mkdir "$tmpdir"
printf '14\n' >"$tmpdir/expected14"
printf 'Python68K 0.8.1\n' >"$tmpdir/expected_version"
tr -d '\r' <"$tmpdir/expected14" >"$tmpdir/expected14.normalized"
tr -d '\r' <"$tmpdir/expected_version" >"$tmpdir/expected_version.normalized"

"$pythonami" -c 'print(2 + 3 * 4)' >"$tmpdir/plain.out" 2>"$tmpdir/plain.err"
tr -d '\r' <"$tmpdir/plain.out" >"$tmpdir/plain.out.normalized"
diff -u "$tmpdir/expected14.normalized" "$tmpdir/plain.out.normalized"
test ! -s "$tmpdir/plain.err"

"$pythonami" --debug -c 'print(2 + 3 * 4)' >"$tmpdir/debug.out" 2>"$tmpdir/debug.err"
tr -d '\r' <"$tmpdir/debug.out" >"$tmpdir/debug.out.normalized"
diff -u "$tmpdir/expected14.normalized" "$tmpdir/debug.out.normalized"
test "$(grep -c '^--- Python68K debug statistics (top-level source) ---$' "$tmpdir/debug.err")" -eq 1
test "$(grep -c '^--- end Python68K debug statistics ---$' "$tmpdir/debug.err")" -eq 1
for field in \
    memory_current_bytes memory_peak_bytes allocation_count free_count \
    failed_count global_count builtin_count source_file_count source_bytes \
    source_lines token_count bytecode_bytes maximum_stack frame_count \
    value_stack_count live_object_count
do
    grep "^$field=" "$tmpdir/debug.err" >/dev/null
done

"$pythonami" --debug -c 'print(2 + 3 * 4)' >"$tmpdir/debug2.out" 2>"$tmpdir/debug2.err"
diff -u "$tmpdir/debug.out" "$tmpdir/debug2.out"
diff -u "$tmpdir/debug.err" "$tmpdir/debug2.err"

set +e
"$pythonami" --debug -c 'print(1 // 0)' >"$tmpdir/failure.out" 2>"$tmpdir/failure.err"
status=$?
set -e
test "$status" -eq 11
grep '^--- Python68K debug statistics (top-level source) ---$' "$tmpdir/failure.err" >/dev/null
grep '^ZeroDivisionError:' "$tmpdir/failure.err" >/dev/null

"$pythonami" --debug -V >"$tmpdir/version.out" 2>"$tmpdir/version.err"
tr -d '\r' <"$tmpdir/version.out" >"$tmpdir/version.out.normalized"
diff -u "$tmpdir/expected_version.normalized" "$tmpdir/version.out.normalized"
test ! -s "$tmpdir/version.err"

echo "PASS: debug statistics CLI"