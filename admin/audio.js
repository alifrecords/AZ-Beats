// AZ BETAS admin — in-browser audio tools: decode, LUFS / true peak, quality
// rating, trimming and WAV (24-bit) encoding. Works in the browser and in node
// (for tests; decode() is browser-only).
(function (root) {
  "use strict";

  // ---------------------------------------------------------------- decode --
  function headerSampleRate(bytes) {
    const s = (o, n) => String.fromCharCode(...bytes.subarray(o, o + n));
    if (s(0, 4) === "RIFF" && s(8, 4) === "WAVE") {
      let p = 12;
      while (p + 8 <= bytes.length) {
        const id = s(p, 4);
        const size = bytes[p + 4] | (bytes[p + 5] << 8) | (bytes[p + 6] << 16) | (bytes[p + 7] << 24);
        if (id === "fmt ") return (bytes[p + 12] | (bytes[p + 13] << 8) | (bytes[p + 14] << 16) | (bytes[p + 15] << 24)) >>> 0;
        p += 8 + size + (size & 1);
      }
    }
    if (s(0, 4) === "fLaC") return (bytes[18] << 12) | (bytes[19] << 4) | (bytes[20] >> 4);
    return 0;
  }

  async function decode(file) {
    const buf = await file.arrayBuffer();
    const bytes = new Uint8Array(buf);
    let sr = headerSampleRate(bytes) || 44100;
    if (sr < 8000 || sr > 384000) sr = 44100;
    const Ctx = root.AudioContext || root.webkitAudioContext;
    let ctx;
    try { ctx = new Ctx({ sampleRate: sr }); } catch (e) { ctx = new Ctx(); }
    const audio = await ctx.decodeAudioData(buf.slice(0));
    const channels = [];
    for (let c = 0; c < Math.min(2, audio.numberOfChannels); c++) channels.push(audio.getChannelData(c).slice());
    if (channels.length === 1) channels.push(channels[0]);
    try { ctx.close(); } catch (e) {}
    return { channels, sampleRate: audio.sampleRate, bytes };
  }

  // -------------------------------------------------------------- loudness --
  function biquad(b0, b1, b2, a0, a1, a2) {
    return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
  }
  function filter(x, f) {
    const y = new Float32Array(x.length);
    let z1 = 0, z2 = 0;
    for (let i = 0; i < x.length; i++) {
      const v = f.b0 * x[i] + z1;
      z1 = f.b1 * x[i] - f.a1 * v + z2;
      z2 = f.b2 * x[i] - f.a2 * v;
      y[i] = v;
    }
    return y;
  }
  function kWeighting(sr) {
    // same coefficients as the plug-in (BS.1770, any sample rate)
    const A = Math.pow(10, 4 / 40);
    let w = 2 * Math.PI * 1500 / sr, c = Math.cos(w), al = Math.sin(w) / (2 * Math.SQRT1_2);
    const sA = 2 * Math.sqrt(A) * al;
    const shelf = biquad(A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
      (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    w = 2 * Math.PI * 38 / sr; c = Math.cos(w); al = Math.sin(w) / (2 * 0.5);
    const hp = biquad((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    return [shelf, hp];
  }

  function lufs(channels, sr, start = 0, end) {
    end = end ?? channels[0].length;
    const len = end - start;
    if (len <= 0) return -70;
    const [shelf, hp] = kWeighting(sr);
    const weighted = channels.map((ch) => filter(filter(ch.subarray(start, end), shelf), hp));
    const block = Math.round(0.4 * sr), step = Math.round(0.1 * sr);
    const z = [];
    const ms = (p, n) => { let s = 0; for (const w of weighted) for (let i = p; i < p + n; i++) s += w[i] * w[i]; return s / n; };
    if (len < block) z.push(ms(0, len));
    else for (let p = 0; p + block <= len; p += step) z.push(ms(p, block));
    const L = (v) => -0.691 + 10 * Math.log10(Math.max(v, 1e-12));
    let abs = z.filter((v) => L(v) > -70);
    if (!abs.length) return -70;
    const rel = L(abs.reduce((a, b) => a + b, 0) / abs.length) - 10;
    const gated = abs.filter((v) => L(v) > rel);
    if (!gated.length) return -70;
    return L(gated.reduce((a, b) => a + b, 0) / gated.length);
  }

  // true peak: 4x windowed-sinc interpolation (sample peak for very long files)
  function truePeakDb(channels, start = 0, end) {
    end = end ?? channels[0].length;
    let peak = 0, clips = 0;
    for (const ch of channels) for (let i = start; i < end; i++) {
      const a = Math.abs(ch[i]);
      if (a > peak) peak = a;
      if (a >= 0.999) clips++;
    }
    if ((end - start) * channels.length <= 6e6) {
      const taps = 8, kern = [];
      for (let ph = 1; ph < 4; ph++) {
        const k = [];
        for (let t = -taps + 1; t <= taps; t++) {
          const x = t - ph / 4, s = x === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x);
          const w = 0.5 + 0.5 * Math.cos(Math.PI * x / taps);
          k.push(s * w);
        }
        kern.push(k);
      }
      for (const ch of channels)
        for (let i = start + taps; i < end - taps; i++) {
          if (Math.abs(ch[i]) < peak * 0.5) continue; // only near peaks
          for (const k of kern) {
            let v = 0;
            for (let j = 0; j < k.length; j++) v += ch[i - taps + 1 + j] * k[j];
            const a = Math.abs(v);
            if (a > peak) peak = a;
          }
        }
    }
    return { db: 20 * Math.log10(Math.max(peak, 1e-6)), clips };
  }

  // ------------------------------------------------------------ rating ------
  // good (yashil) / mid (sariq) / bad (qizil)
  function rate(l, tp, clips, seconds) {
    const reasons = [];
    let score = 2;
    const worse = (lvl, why) => { score = Math.min(score, lvl); reasons.push(why); };
    if (l < -20) worse(0, "juda past (" + l.toFixed(1) + " LUFS)");
    else if (l < -15) worse(1, "past (" + l.toFixed(1) + " LUFS)");
    else if (l > -6) worse(0, "haddan baland, siqilgan (" + l.toFixed(1) + " LUFS)");
    else if (l > -8) worse(1, "juda baland (" + l.toFixed(1) + " LUFS)");
    if (tp > 0.0) worse(0, "True Peak 0 dB dan oshgan (" + tp.toFixed(1) + " dBTP)");
    else if (tp > -0.5) worse(1, "True Peak chegarada (" + tp.toFixed(1) + " dBTP)");
    if (clips > 8) worse(0, clips + " ta klipping");
    if (seconds < 0.05) worse(0, "juda qisqa");
    const level = ["bad", "mid", "good"][score];
    const text = { good: "Yaxshi", mid: "O'rta", bad: "Yomon" }[level];
    if (!reasons.length) reasons.push("zamonaviy darajada");
    return { level, text, reasons };
  }

  function analyse(channels, sr, start = 0, end) {
    end = end ?? channels[0].length;
    const l = lufs(channels, sr, start, end);
    const tp = truePeakDb(channels, start, end);
    return { lufs: l, tp: tp.db, clips: tp.clips, seconds: (end - start) / sr, rating: rate(l, tp.db, tp.clips, (end - start) / sr) };
  }

  // ---------------------------------------------------------- WAV encode ----
  function encodeWav24(channels, sr, start = 0, end, fadeMs = 0) {
    end = end ?? channels[0].length;
    const n = end - start, nc = channels.length, bps = 3;
    const dataLen = n * nc * bps;
    const out = new Uint8Array(44 + dataLen);
    const dv = new DataView(out.buffer);
    const str = (o, s) => { for (let i = 0; i < s.length; i++) out[o + i] = s.charCodeAt(i); };
    str(0, "RIFF"); dv.setUint32(4, 36 + dataLen, true); str(8, "WAVE");
    str(12, "fmt "); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, nc, true);
    dv.setUint32(24, sr, true); dv.setUint32(28, sr * nc * bps, true); dv.setUint16(32, nc * bps, true); dv.setUint16(34, 24, true);
    str(36, "data"); dv.setUint32(40, dataLen, true);
    const fade = Math.min(Math.floor(n / 2), Math.round(fadeMs * sr / 1000));
    let o = 44;
    for (let i = 0; i < n; i++) {
      let g = 1;
      if (fade > 0) { if (i < fade) g = i / fade; else if (i >= n - fade) g = (n - 1 - i) / fade; }
      for (let c = 0; c < nc; c++) {
        let v = Math.max(-1, Math.min(1, channels[c][start + i] * g));
        let x = Math.round(v < 0 ? v * 8388608 : v * 8388607);
        if (x < 0) x += 16777216;
        out[o++] = x & 255; out[o++] = (x >> 8) & 255; out[o++] = (x >> 16) & 255;
      }
    }
    return out;
  }

  // ---------------------------------------------------------- waveform ------
  function peaks(channels, points, start = 0, end) {
    end = end ?? channels[0].length;
    const n = end - start, p = new Float32Array(points * 2);
    for (let i = 0; i < points; i++) {
      const a = start + Math.floor(i * n / points), b = Math.max(a + 1, start + Math.floor((i + 1) * n / points));
      let lo = 0, hi = 0;
      for (const ch of channels) for (let j = a; j < b; j++) { const v = ch[j]; if (v < lo) lo = v; if (v > hi) hi = v; }
      p[i * 2] = lo; p[i * 2 + 1] = hi;
    }
    return p;
  }

  root.AZAudio = { decode, headerSampleRate, lufs, truePeakDb, rate, analyse, encodeWav24, peaks };
  if (typeof module !== "undefined") module.exports = root.AZAudio;
})(typeof window !== "undefined" ? window : globalThis);
