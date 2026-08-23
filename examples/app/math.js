// A tiny math module. Note the circular dependency on ./greet, which the
// runtime handles the same way Node does.
exports.PI = 3.14159;
exports.add = function add(a, b) {
  return a + b;
};
