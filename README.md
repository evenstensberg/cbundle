# cbundle

A small CommonJS module bundler written in C99, with no dependencies beyond
libc and POSIX.

It reads an entry `.js` file, follows every `require("...")` call it can
resolve statically, and writes one self-contained JavaScript file that runs in
Node or a browser.

```console
$ make
$ ./build/cbundle examples/app/index.js -o bundle.js
$ node bundle.js
Hello, world! (this bundle knows pi = 3.14159)
2 + 3 = 5
pi ~ 3.14159
padded: 00005
regex intact: true
```

## Building

```bash
make            # release build -> build/cbundle
make BUILD=debug
make test       # run the test suite
make help       # every target, with descriptions
```

The build needs only a C99 compiler and `make`. `node` is optional; without it
the test suite checks that bundling succeeds but skips executing the output.

## Usage

```
cbundle [options] <entry.js>

  -o, --output FILE    write the bundle to FILE (default: stdout)
  -g, --global NAME    also assign the entry module's exports to a global
  -r, --root DIR       project root used to shorten paths in the output
      --graph          print the dependency graph instead of a bundle
      --no-banner      omit the generated-by header comment
  -v, --verbose        list each module as it is resolved, on stderr
  -h, --help           show usage
  -V, --version        show the version
```

Inspecting the graph is often more useful than reading the bundle:

```console
$ ./build/cbundle examples/app/index.js --graph
[0] index.js
      ./math                       -> [1] math.js
      ./greet                      -> [2] greet.js
      ./config.json                -> [3] config.json
      leftpad                      -> [4] node_modules/leftpad/lib/index.js
[1] math.js
[2] greet.js
      ./math                       -> [1] math.js
[3] config.json
[4] node_modules/leftpad/lib/index.js

5 modules
```

## How it works

Four stages, one source file each.

**1. Scan** (`src/lexer.c`). Rather than parsing JavaScript, the scanner walks
the source and skips the constructs in which a `require` must be ignored:
comments, single- and double-quoted strings, template literals (including
nested `${ ... }` substitutions, where a real `require` *is* still found), and
regular-expression literals. Telling `/` as a regex from `/` as division needs
the previous significant token, so the scanner tracks just enough state to
answer that question. `obj.require(x)` and `myrequire(x)` are rejected because
the identifier must stand alone.

**2. Resolve** (`src/resolve.c`). The practical subset of Node's algorithm:
relative and absolute specifiers resolve against the requiring file's
directory; bare specifiers walk up through `node_modules/`. Each candidate is
tried as a file (exact, then `.js`, `.cjs`, `.json`) and then as a directory
(`package.json`'s `main`, then `index.*`). The `main` field is read with a
small scanner that only accepts keys at the top level, so a `"main"` nested
inside `"scripts"` is not mistaken for it.

**3. Build the graph** (`src/bundler.c`). A depth-first walk assigns each
module an integer id — the entry is always `0`. A module is registered
*before* its dependencies are visited, so a cycle terminates on its own, and
a module required from several places is included exactly once.

**4. Emit** (`src/bundler.c`). Each module becomes an entry in an object
literal:

```js
(function (modules, entryId) { /* ~30-line runtime */ })({
  0: {
    file: "index.js",
    dir: ".",
    deps: { "./math": 1, "leftpad": 4 },
    fn: function (module, exports, require, __filename, __dirname) {
      /* the original source, byte for byte */
    }
  },
  ...
}, 0);
```

Because the resolved ids live in the `deps` table, module source is never
rewritten — it is copied verbatim into a function body. That keeps the
generator simple and keeps stack traces pointing at recognisable code.

The runtime caches a module's `exports` object *before* running its body, which
is what gives circular requires the same partial-exports behaviour they have in
Node.

## Supported

- `require()` with a string literal, `module.exports`, `exports`
- `__filename` and `__dirname` (relative to the project root)
- JSON modules
- `node_modules` lookup, `package.json` `main`, directory `index` files
- circular dependencies, shared dependencies, `require.resolve`
- a `#!` line on the entry file

## Not supported

These are deliberate omissions, not oversights — the point of the project is a
readable implementation of the core idea.

- ESM (`import` / `export`); input must be CommonJS
- computed requires, e.g. `require(name)` or `require('./' + x)` — the bundler
  cannot see the target, so it is skipped at build time and throws a clear
  error if it is reached at runtime
- a `require` shadowed by a local binding is still treated as a module request
- minification, tree shaking, source maps, code splitting, watch mode
- Node built-ins (`fs`, `path`, ...) are not shimmed; requiring one fails to
  resolve unless a `node_modules` package of that name exists
- `package.json` `exports`/`imports` maps and conditional exports

## Layout

```
include/     public headers
src/
  main.c     CLI argument handling
  lexer.c    the require() scanner
  resolve.c  Node-style module resolution
  bundler.c  module graph and code generation
  path.c     path manipulation
  util.c     allocation, growable buffer, file I/O
examples/    a sample project (`make example`)
tests/       shell-driven test suite (`make test`)
```

## Testing

`make test` runs 16 cases covering resolution, JSON modules, circular and
shared dependencies, the lexer's false-positive traps (a `require` in a
comment, a string, a regex, a member expression), shebang handling, a file
ending in a line comment, error reporting for an unresolvable dependency,
`--global`, `--graph`, and byte-for-byte deterministic output.

`make sanitize` reruns the same suite under AddressSanitizer and
UndefinedBehaviorSanitizer; `make analyze` runs the compiler's static analyzer.
