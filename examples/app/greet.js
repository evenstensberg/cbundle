const { PI } = require('./math');

module.exports = function greet(name) {
  // Template literals, including substitutions, are scanned correctly.
  return `Hello, ${name}! (this bundle knows pi = ${PI})`;
};
