// Fait tourner le moteur du prototype « Labo Reverb » (copie exacte : reverb_ref.js) sur un signal.
// Usage : node ref_run.js <fs> <params.json> <in.f32> <out.f32> [bloc]
// params.json : { "p": {mode, predelay, decay, mix, size, damp, width}, "changes": [[échantillon, {réglages}], …] }
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'reverb_ref.js'), 'utf8');
const { ReverbEngine, MODES } = new Function(src + '\nreturn { ReverbEngine, MODES };')();
const [sr, pfile, inFile, outFile, blk] = process.argv.slice(2);
const cfg = JSON.parse(fs.readFileSync(pfile, 'utf8')), block = parseInt(blk || '128', 10);
const raw = fs.readFileSync(inFile), inp = new Float32Array(raw.buffer, raw.byteOffset, raw.length / 4), N = inp.length / 2;
const e = new ReverbEngine(parseFloat(sr), MODES);
e.setParams(cfg.p); e.snap();
const L = new Float32Array(N), R = new Float32Array(N), oL = new Float32Array(N), oR = new Float32Array(N);
for (let i = 0; i < N; i++) { L[i] = inp[2 * i]; R[i] = inp[2 * i + 1]; }
const ch = cfg.changes || []; let ci = 0;
for (let s = 0; s < N; s += block) {
  while (ci < ch.length && ch[ci][0] <= s) { e.setParams(ch[ci][1]); ci++; }
  const n = Math.min(block, N - s);
  e.process(L.subarray(s, s + n), R.subarray(s, s + n), oL.subarray(s, s + n), oR.subarray(s, s + n), n);
}
const out = new Float32Array(2 * N);
for (let i = 0; i < N; i++) { out[2 * i] = oL[i]; out[2 * i + 1] = oR[i]; }
fs.writeFileSync(outFile, Buffer.from(out.buffer));
