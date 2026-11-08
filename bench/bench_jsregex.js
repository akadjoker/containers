'use strict';
let s = 12345;
const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s >>> 8; };
function words(bytes) { let out = []; let n = 0; while (n < bytes) { const len = 1 + rnd() % 9; let w = ''; for (let i = 0; i < len; i++) w += String.fromCharCode(97 + rnd() % 26); const sp = 1 + rnd() % 3; for (let i = 0; i < sp; i++) w += (rnd() % 8 === 0) ? '\n' : ' '; out.push(w); n += w.length; } return out.join(''); }
function numbers(bytes) { let out = []; let n = 0; while (n < bytes) { const t = 'item' + (rnd() % 1000) + ': ' + (rnd() % 100000) + '-' + (rnd() % 100000) + ' ok; '; out.push(t); n += t.length; } return out.join(''); }
function time(name, f) { let best = 1e300, r = 0; for (let i = 0; i < 3; i++) { const t0 = process.hrtime.bigint(); r = f(); const t = Number(process.hrtime.bigint() - t0) / 1e6; if (t < best) best = t; } console.log(name.padEnd(44), best.toFixed(3).padStart(10), 'ms  (' + r + ')'); }
const w = words(10 << 20), nums = numbers(10 << 20);
const count = (re, t) => { re.lastIndex = 0; let n = 0, m; while ((m = re.exec(t)) !== null) { n++; if (m[0].length === 0) re.lastIndex++; } return n; };
time('split /\\s+/ over 10 MB', () => count(/\s+/g, w));
time('/(\\d+)-(\\d+)/g over 10 MB', () => count(/(\d+)-(\d+)/g, nums));
time('/\\b[a-z]+\\b/gi over 10 MB', () => count(/\b[a-z]+\b/gi, w));
const emails = []; for (let i = 0; i < 1000000; i++) emails.push('user' + (rnd() % 100000) + (i % 7 === 0 ? '.x+tag' : '') + '@site' + (i % 50) + (i % 13 === 0 ? '' : '.example.com'));
const er = /^[\w.+-]+@[\w-]+\.[\w.-]+$/;
time('email regex over 1 M strings', () => { let n = 0; for (const e of emails) if (er.test(e)) n++; return n; });
