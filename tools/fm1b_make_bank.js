// SPDX-License-Identifier: GPL-3.0-only
// fm1-sf2's make_bank.js with ten more fields in the modulation record (60 bytes instead of 40), for the M1's velocity, delay, chorus and filter key tracking:
//   offset 40: i16 velocity to level, the M1's VDA velocity sense -99..99 (99 = velocity squared as before, 0 = none)
//   offset 42: i16 velocity to the filter EG's depth, the M1's VDF EG intensity velocity sense -99..99
//   offset 44, 46: i16 the program's delay, left and right, ms (0..500); 48: i16 feedback -99..99 %
//   offset 50: i16 bit 15 = the record has the program's effect, bit 14 crossed feedback, bit 7..13 high damp 0..99, bit 0..6 share 0..100
// The converter is read from ../fm1-sf2/index.html (untouched) and changed here with checked text replacements. The values come from the SF2 file
//   offset 52: i16 the chorus's rate in 10 mHz (bit 0..11) and depth 0..15 (bit 12..15); 54: i16 the chorus's delay in 0.1 ms
//   offset 56: i16 the filter's key tracking in 0.1 cent per key; 58: i16 center key (MIDI note); 60: i16 program number + 1
// in generators 14, 18, 19, 20, 42, 49, 55, 59, 63 and 64, which SF2 leaves unused (tools/m1_sf2.py writes them; the level as sense + 100, so that
// 0 = none).
//   node tools/fm1b_make_bank.js bfp8 32000 in.sf2 out.fm1b
const fs = require('fs'), path = require('path');
const [format, rateArg, inArg, outArg] = process.argv.slice(2);
const options = { bfp8: { bfp: true }, pcm16: { bits: 16 } }[format];
if (!options || !inArg || !outArg) { console.error('node tools/fm1b_make_bank.js <bfp8|pcm16> rate in.sf2 out.fm1b'); process.exit(1); }
const sf2dir = path.join(__dirname, '..', '..', 'fm1-sf2');
let core = fs.readFileSync(path.join(sf2dir, 'index.html'), 'utf8').match(/<script id="core">([\s\S]*?)<\/script>/)[1].replace(/\r\n/g, '\n');
function patch(from, to) {
  if (core.split(from).length !== 2) { console.error('the fm1-sf2 converter has changed, cannot find: ' + from.slice(0, 80)); process.exit(1); }
  core = core.replace(from, to);
}
const EXTRA = ['velLevel', 'velFc', 'fxLeft', 'fxRight', 'fxFeedback', 'fxFlags', 'choRate', 'choDelay', 'keyFc', 'keyCenter', 'progIdx'];
patch("envSustain: n(29) + np(29), envRelease: tc(30, -12000), envPitch: n(7) + np(7),",
      "envSustain: n(29) + np(29), envRelease: tc(30, -12000), envPitch: n(7) + np(7),\n" +
      "          velLevel: n(14) + np(14) ? n(14) + np(14) - 100 : 99, velFc: n(18) + np(18),\n" +
      "          fxLeft: n(19), fxRight: n(20), fxFeedback: n(42), fxFlags: n(49), choRate: n(55), choDelay: n(59), keyFc: n(63), keyCenter: n(64), progIdx: n(65),");
patch("    envPitch: c(m.envPitch, -12000, 12000),\n  };",
      "    envPitch: c(m.envPitch, -12000, 12000),\n" +
      "    velLevel: c(m.velLevel, -99, 99, 99), velFc: c(m.velFc, -99, 99),\n" +
      "    fxLeft: c(m.fxLeft, 0, 500), fxRight: c(m.fxRight, 0, 500), fxFeedback: c(m.fxFeedback, -99, 99),\n" +
      "    fxFlags: c(m.fxFlags, -32768, 32767), choRate: c(m.choRate, -32768, 32767), choDelay: c(m.choDelay, 0, 2000),\n" +
      "    keyFc: c(m.keyFc, -1000, 1000), keyCenter: c(m.keyCenter, 0, 127), progIdx: c(m.progIdx, 0, 255),\n  };\n" +
      "  if (!r.envFc) r.velFc = 0;");
patch("return r.lfoVolume || r.lfoPitch || r.vibPitch || r.keyHold || r.keyDecay || filter || r.envPitch ? r : null;",
      "return r.lfoVolume || r.lfoPitch || r.vibPitch || r.keyHold || r.keyDecay || filter || r.envPitch ||\n" +
      "    r.velLevel !== 99 || r.velFc || r.fxFlags || r.keyFc || r.progIdx ? r : null;");
patch("'envDecay', 'envSustain', 'envRelease', 'envPitch'];",
      "'envDecay', 'envSustain', 'envRelease', 'envPitch', " + EXTRA.map(f => `'${f}'`).join(', ') + "];");
patch("const MOD_GENS = [13, 5, 6, 21, 22, 23, 24, 39, 40, 8, 9, 11, 10, 25, 26, 27, 28, 29, 30, 7];",
      "const MOD_GENS = [13, 5, 6, 21, 22, 23, 24, 39, 40, 8, 9, 11, 10, 25, 26, 27, 28, 29, 30, 7, 14, 18, 19, 20, 42, 49, 55, 59, 63, 64, 65];");
patch("const MOD_DEFAULTS = [0, 0, 0, -12000, 0, -12000, 0, 0, 0, 13500, 0, 0, 0, -12000, -12000, -12000, -12000, 0, -12000, 0];",
      "const MOD_DEFAULTS = [0, 0, 0, -12000, 0, -12000, 0, 0, 0, 13500, 0, 0, 0, -12000, -12000, -12000, -12000, 0, -12000, 0, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];");
patch("FM1B_ENV = 12, FM1B_MOD = 40;", "FM1B_ENV = 12, FM1B_MOD = 64;");
eval(core + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo, FM1_SAMPLE_BANK };');
const A = globalThis.api;
const b = fs.readFileSync(inArg);
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const bin = A.writeFm1(A.buildBank(sf, -2, { rate: Number(rateArg), ...options }), A.bankInfo(path.basename(outArg)));
fs.mkdirSync(path.dirname(path.resolve(outArg)), { recursive: true });
fs.writeFileSync(outArg, bin);
console.log(`wrote ${outArg}: ${bin.length} bytes (modulation records of 60 bytes: velocity, delay, chorus, filter key tracking, program number)`);
