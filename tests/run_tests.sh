#!/bin/sh
#
# tests/run_tests.sh -- test suite for cbundle.
#
# Each test builds a small fixture project under tests/tmp/, bundles it, and
# checks the result. Where node is available the bundle is executed and its
# stdout compared against an expected value; without node the suite still
# checks that bundling succeeds or fails as expected.
#
# Usage:
#     make test                     # normal path
#     CBUNDLE=/path/to/cbundle sh tests/run_tests.sh
#
set -u

CBUNDLE=${CBUNDLE:-$(pwd)/build/cbundle}
ROOT=$(cd "$(dirname "$0")" && pwd)
TMP="$ROOT/tmp"

pass=0
fail=0

if [ ! -x "$CBUNDLE" ]; then
    echo "run_tests: '$CBUNDLE' is not executable; run 'make' first" >&2
    exit 1
fi

if command -v node >/dev/null 2>&1; then
    HAVE_NODE=1
else
    HAVE_NODE=0
    echo "run_tests: node not found -- execution checks will be skipped"
fi

rm -rf "$TMP"
mkdir -p "$TMP"

ok()   { pass=$((pass + 1)); printf '  ok   %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf '  FAIL %s\n' "$1"; shift; [ $# -gt 0 ] && printf '       %s\n' "$*"; }

# fixture <name> -- create and enter a clean fixture directory
fixture() {
    CASE_DIR="$TMP/$1"
    rm -rf "$CASE_DIR"
    mkdir -p "$CASE_DIR"
}

# expect_output <name> <expected> -- bundle index.js and compare node's stdout
expect_output() {
    name=$1
    expected=$2
    if ! out=$("$CBUNDLE" "$CASE_DIR/index.js" -o "$CASE_DIR/bundle.js" 2>&1); then
        bad "$name" "bundling failed: $out"
        return
    fi
    if [ "$HAVE_NODE" = 0 ]; then
        ok "$name (bundled; not executed)"
        return
    fi
    if ! actual=$(node "$CASE_DIR/bundle.js" 2>&1); then
        bad "$name" "node failed: $actual"
        return
    fi
    if [ "$actual" = "$expected" ]; then
        ok "$name"
    else
        bad "$name" "expected [$expected] got [$actual]"
    fi
}

# expect_failure <name> <substring> -- bundling must fail, mentioning substring
expect_failure() {
    name=$1
    needle=$2
    if out=$("$CBUNDLE" "$CASE_DIR/index.js" -o /dev/null 2>&1); then
        bad "$name" "expected failure, but bundling succeeded"
        return
    fi
    case "$out" in
        *"$needle"*) ok "$name" ;;
        *)           bad "$name" "error did not mention '$needle': $out" ;;
    esac
}

echo "cbundle test suite ($CBUNDLE)"
echo

# ---------------------------------------------------------------------------
# 1. A single module with no dependencies
# ---------------------------------------------------------------------------
fixture single
cat > "$CASE_DIR/index.js" <<'EOF'
console.log('alone');
EOF
expect_output "single module" "alone"

# ---------------------------------------------------------------------------
# 2. Relative requires, exports and module.exports
# ---------------------------------------------------------------------------
fixture relative
mkdir -p "$CASE_DIR/lib"
cat > "$CASE_DIR/index.js" <<'EOF'
const add = require('./lib/add');
const { name } = require('./lib/meta.js');
console.log(name, add(20, 22));
EOF
cat > "$CASE_DIR/lib/add.js" <<'EOF'
module.exports = (a, b) => a + b;
EOF
cat > "$CASE_DIR/lib/meta.js" <<'EOF'
exports.name = 'answer';
EOF
expect_output "relative requires" "answer 42"

# ---------------------------------------------------------------------------
# 3. Directory resolution: ./dir -> ./dir/index.js, and package.json "main"
# ---------------------------------------------------------------------------
fixture dirs
mkdir -p "$CASE_DIR/shapes" "$CASE_DIR/node_modules/pkg/src"
cat > "$CASE_DIR/index.js" <<'EOF'
console.log(require('./shapes').kind, require('pkg')());
EOF
cat > "$CASE_DIR/shapes/index.js" <<'EOF'
exports.kind = 'square';
EOF
cat > "$CASE_DIR/node_modules/pkg/package.json" <<'EOF'
{ "name": "pkg", "main": "src/entry.js", "scripts": { "main": "nope" } }
EOF
cat > "$CASE_DIR/node_modules/pkg/src/entry.js" <<'EOF'
module.exports = () => 'from-pkg';
EOF
expect_output "directory and node_modules resolution" "square from-pkg"

# ---------------------------------------------------------------------------
# 4. JSON modules
# ---------------------------------------------------------------------------
fixture json
cat > "$CASE_DIR/index.js" <<'EOF'
const data = require('./data.json');
console.log(data.items.join('-'), data.n);
EOF
cat > "$CASE_DIR/data.json" <<'EOF'
{ "items": ["a", "b"], "n": 3 }
EOF
expect_output "json module" "a-b 3"

# ---------------------------------------------------------------------------
# 5. Circular dependencies behave like Node: the partial exports are visible
# ---------------------------------------------------------------------------
fixture circular
cat > "$CASE_DIR/index.js" <<'EOF'
const a = require('./a');
console.log(a.value, a.fromB());
EOF
cat > "$CASE_DIR/a.js" <<'EOF'
exports.value = 'a';
const b = require('./b');
exports.fromB = () => b.label();
EOF
cat > "$CASE_DIR/b.js" <<'EOF'
const a = require('./a');
exports.label = () => 'b-sees-' + a.value;
EOF
expect_output "circular dependency" "a b-sees-a"

# ---------------------------------------------------------------------------
# 6. The scanner must ignore require-like text in comments, strings and regexes
# ---------------------------------------------------------------------------
fixture lexer
cat > "$CASE_DIR/index.js" <<'EOF'
// require('./nope-comment')
/* require('./nope-block') */
const s1 = "require('./nope-dq')";
const s2 = 'require(\'./nope-sq\')';
const re = /require\('\.\/nope-re'\)/;
const division = 10 / 2 / 1;
const obj = { require: function () { return 'method'; } };
const viaMember = obj.require('./nope-member');
const real = require('./real');
const tpl = `x ${require('./tpl')} y`;
console.log(real, tpl, viaMember, division, s1.length > 0 && s2.length > 0 && re.source.length > 0);
EOF
cat > "$CASE_DIR/real.js" <<'EOF'
module.exports = 'REAL';
EOF
cat > "$CASE_DIR/tpl.js" <<'EOF'
module.exports = 'TPL';
EOF
expect_output "lexer ignores strings, comments and regexes" "REAL x TPL y method 5 true"

# ---------------------------------------------------------------------------
# 7. A module is included once even when required from several places
# ---------------------------------------------------------------------------
fixture dedupe
cat > "$CASE_DIR/index.js" <<'EOF'
require('./one');
require('./two');
console.log(require('./counter').count);
EOF
cat > "$CASE_DIR/counter.js" <<'EOF'
exports.count = 0;
exports.bump = () => (exports.count += 1);
EOF
cat > "$CASE_DIR/one.js" <<'EOF'
require('./counter').bump();
EOF
cat > "$CASE_DIR/two.js" <<'EOF'
require('././counter').bump();
EOF
expect_output "shared module is instantiated once" "2"
if [ "$(grep -c 'counter.js ---' "$CASE_DIR/bundle.js" 2>/dev/null)" = "1" ]; then
    ok "shared module appears once in the output"
else
    bad "shared module appears once in the output" "found $(grep -c 'counter.js ---' "$CASE_DIR/bundle.js") copies"
fi

# ---------------------------------------------------------------------------
# 8. __filename, __dirname and module.exports identity
# ---------------------------------------------------------------------------
fixture globals
cat > "$CASE_DIR/index.js" <<'EOF'
const info = require('./info');
console.log(info.file, info.isSelf);
EOF
cat > "$CASE_DIR/info.js" <<'EOF'
exports.file = __filename;
exports.isSelf = module.exports === exports;
EOF
expect_output "__filename and module identity" "info.js true"

# ---------------------------------------------------------------------------
# 9. A shebang line in the entry must not break the wrapper
# ---------------------------------------------------------------------------
fixture shebang
printf '#!/usr/bin/env node\nconsole.log("shebang ok");\n' > "$CASE_DIR/index.js"
expect_output "shebang is stripped" "shebang ok"

# ---------------------------------------------------------------------------
# 10. A trailing line comment must not swallow the closing brace
# ---------------------------------------------------------------------------
fixture trailing
printf 'console.log("trailing ok"); // no newline at end of file' > "$CASE_DIR/index.js"
expect_output "file ending in a line comment" "trailing ok"

# ---------------------------------------------------------------------------
# 11. Unresolvable dependency is a build error naming the file and line
# ---------------------------------------------------------------------------
fixture missing
cat > "$CASE_DIR/index.js" <<'EOF'
const ok = 1;
const gone = require('./does-not-exist');
EOF
expect_failure "missing dependency is reported" "cannot resolve './does-not-exist'"

# ---------------------------------------------------------------------------
# 12. --global exposes the entry exports
# ---------------------------------------------------------------------------
fixture global
cat > "$CASE_DIR/index.js" <<'EOF'
module.exports = { hi: () => 'hi' };
EOF
if "$CBUNDLE" "$CASE_DIR/index.js" --global MyLib -o "$CASE_DIR/bundle.js" 2>/dev/null; then
    if [ "$HAVE_NODE" = 1 ]; then
        actual=$(node -e "require('$CASE_DIR/bundle.js'); console.log(typeof MyLib.hi === 'function');" 2>&1)
        [ "$actual" = "true" ] && ok "--global exposes exports" \
                               || bad "--global exposes exports" "$actual"
    else
        ok "--global exposes exports (bundled; not executed)"
    fi
else
    bad "--global exposes exports" "bundling failed"
fi

# ---------------------------------------------------------------------------
# 13. --graph lists every module without emitting a bundle
# ---------------------------------------------------------------------------
fixture graphout
cat > "$CASE_DIR/index.js" <<'EOF'
require('./dep');
EOF
cat > "$CASE_DIR/dep.js" <<'EOF'
module.exports = 1;
EOF
out=$("$CBUNDLE" "$CASE_DIR/index.js" --graph 2>&1)
case "$out" in
    *"[0] index.js"*"[1] dep.js"*"2 modules"*) ok "--graph output" ;;
    *) bad "--graph output" "$out" ;;
esac

# ---------------------------------------------------------------------------
# 14. Bundling is deterministic: same input, byte-identical output
# ---------------------------------------------------------------------------
fixture determinism
cat > "$CASE_DIR/index.js" <<'EOF'
require('./a');
EOF
cat > "$CASE_DIR/a.js" <<'EOF'
module.exports = require('./b');
EOF
cat > "$CASE_DIR/b.js" <<'EOF'
module.exports = 'b';
EOF
"$CBUNDLE" "$CASE_DIR/index.js" -o "$CASE_DIR/one.js" 2>/dev/null
"$CBUNDLE" "$CASE_DIR/index.js" -o "$CASE_DIR/two.js" 2>/dev/null
if cmp -s "$CASE_DIR/one.js" "$CASE_DIR/two.js"; then
    ok "output is deterministic"
else
    bad "output is deterministic" "two runs differed"
fi

# ---------------------------------------------------------------------------
# 15. The bundled example project still runs
# ---------------------------------------------------------------------------
EXAMPLE="$ROOT/../examples/app/index.js"
if [ -f "$EXAMPLE" ]; then
    if "$CBUNDLE" "$EXAMPLE" -o "$TMP/example.js" 2>/dev/null; then
        if [ "$HAVE_NODE" = 1 ]; then
            if node "$TMP/example.js" | grep -q 'padded: 00005'; then
                ok "examples/app bundles and runs"
            else
                bad "examples/app bundles and runs" "unexpected output"
            fi
        else
            ok "examples/app bundles (not executed)"
        fi
    else
        bad "examples/app bundles and runs" "bundling failed"
    fi
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
