// Entry point of the example application.
const { add, PI } = require('./math');
const greet = require('./greet');
const config = require('./config.json');
const leftpad = require('leftpad');

// A regex containing the word require must not confuse the scanner:
const looksLikeCode = /require\('nope'\)/;

console.log(greet(config.user));
console.log('2 + 3 =', add(2, 3));
console.log('pi ~', PI);
console.log('padded:', leftpad(String(add(2, 3)), 5, '0'));
console.log('regex intact:', looksLikeCode.source.length > 0);
