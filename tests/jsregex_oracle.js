'use strict';
const fs = require('fs');
const vm = require('vm');

const cases = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const ctx = vm.createContext({});
const script = new vm.Script('re.lastIndex = li; m = re.exec(t);');

function stripPrefix(msg, p, f) {
    const prefix = 'Invalid regular expression: /' + p + '/' + f + ': ';
    if (msg.startsWith(prefix)) return msg.slice(prefix.length);
    const k = msg.lastIndexOf(': ');
    return k >= 0 ? msg.slice(k + 2) : msg;
}

function run(p, f, t, li) {
    const flags = f.replace('d', '') + 'd';
    let re;
    try {
        re = new RegExp(p, flags);
    } catch (e) {
        return { e: stripPrefix(e.message, p, flags) };
    }
    ctx.re = re;
    ctx.t = t;
    ctx.li = f.includes('g') || f.includes('y') ? li : 0;
    ctx.m = null;
    try {
        script.runInContext(ctx, { timeout: 4000 });
    } catch (e) {
        return { tc: 1 };
    }
    const m = ctx.m;
    if (m === null) return null;
    const out = { r: m.indices.map((x) => (x === undefined ? null : [x[0], x[1]])) };
    if (m.indices.groups !== undefined) {
        out.g = {};
        for (const k of Object.keys(m.indices.groups)) {
            const v = m.indices.groups[k];
            out.g[k] = v === undefined ? null : [v[0], v[1]];
        }
    }
    return out;
}

const out = [];
for (const c of cases) out.push(JSON.stringify(run(c[0], c[1], c[2], c[3])));
process.stdout.write(out.join('\n') + '\n');
