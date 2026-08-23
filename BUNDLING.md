# Bundling Step by Step (Simple + Practical)

This guide shows what `cbundle` does from input file to final bundle.

We will use this command in the examples:

```bash
./build/cbundle examples/app/index.js -o bundle.js
```

---

## Step 1: Start from one entry file

Bundling begins with one file (the entry file).  
`cbundle` treats this file as module **0**.

**Example**

```bash
./build/cbundle examples/app/index.js -o bundle.js
```

Entry module: `examples/app/index.js`

---

## Step 2: Scan the file for `require("...")`

`cbundle` reads the entry file and finds static CommonJS imports like:

```js
const math = require("./math");
const leftpad = require("leftpad");
```

It ignores fake matches inside comments, strings, and regexes.

**Example**

```js
// require("./not-real")   <- ignored
const text = "require('./not-real-either')"; // ignored
const real = require("./math"); // collected
```

---

## Step 3: Resolve every dependency to a real file

For each collected specifier, `cbundle` resolves it to an actual file path:

- Relative import (`./math`) -> file near the current module
- Bare import (`leftpad`) -> searched in `node_modules`
- JSON import (`./config.json`) -> supported

**Example**

From `examples/app/index.js`:

- `./math` -> `examples/app/math.js`
- `./config.json` -> `examples/app/config.json`
- `leftpad` -> `examples/app/node_modules/leftpad/lib/index.js`

---

## Step 4: Repeat for newly found files

Each resolved dependency is scanned too.  
This repeats until there are no new modules to add.

If two files require the same module, it is added once and reused by id.

**Example**

If both `index.js` and `greet.js` require `./math`, they both point to the same module id (for example id `1`).

---

## Step 5: Build a module graph with ids

`cbundle` builds an internal graph:

- each module gets an integer id
- each `require("x")` maps to the target module id

You can inspect this graph directly:

```bash
./build/cbundle examples/app/index.js --graph
```

**Example output**

```text
[0] index.js
      ./math                       -> [1] math.js
      ./greet                      -> [2] greet.js
      ./config.json                -> [3] config.json
      leftpad                      -> [4] node_modules/leftpad/lib/index.js
```

---

## Step 6: Emit one JavaScript bundle

Finally, `cbundle` writes a single output file (`bundle.js`) that contains:

1. a small runtime loader (`require` implementation + module cache)
2. all modules wrapped as functions
3. the entry module id to start execution

**Example (shape of output)**

```js
(function (modules, entryId) { /* runtime */ })({
  0: { deps: { "./math": 1 }, fn: function (...) { /* index.js */ } },
  1: { deps: {}, fn: function (...) { /* math.js */ } },
  // ... ids 2, 3, 4 omitted for brevity
}, 0);
```

---

## Step 7: Run the bundle

Now you can execute the bundled output like a normal JS file.

**Example**

```bash
node bundle.js
```

Expected output for the sample app:

```text
Hello, world! (this bundle knows pi = 3.14159)
2 + 3 = 5
pi ~ 3.14159
padded: 00005
regex intact: true
```

---

## Practical notes

- Dynamic requires like `require(name)` cannot be bundled statically.
- Circular dependencies are supported (same behavior as CommonJS in Node).
- Module source code is copied into the bundle; dependency links are handled by ids.
