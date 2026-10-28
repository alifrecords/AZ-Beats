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
    const oneShot = seconds < 2.5;   // one-shots are judged by peak, not LUFS
    if (oneShot) {
      if (tp < -9) worse(1, "past (peak " + tp.toFixed(1) + " dBTP)");
    } else if (l < -20) worse(0, "juda past (" + l.toFixed(1) + " LUFS)");
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


  // ------------------------------------------------------ auto-fix ---------
  // Universal "bring to standard" chain (rule based, offline):
  //   DC removal -> 25 Hz high-pass -> (one-shot: trim silence + fade) ->
  //   loudness / peak normalisation -> look-ahead true-peak limiter.
  const TARGETS = {
    auto:    { name: "Avto (turini o'zi aniqlaydi)" },
    drums:   { name: "Drum / perkussiya loop · -11 LUFS", lufs: -11, loop: true },
    music:   { name: "Melodik / bass loop · -13 LUFS", lufs: -13, loop: true },
    oneshot: { name: "One-shot (kick, snare...) · peak -1 dBTP", peak: -1, loop: false },
    fx:      { name: "FX / ambient · -16 LUFS", lufs: -16, loop: false },
  };

  function features(channels, sr) {
    const n = channels[0].length;
    const mono = new Float32Array(n);
    for (let i = 0; i < n; i++) mono[i] = 0.5 * (channels[0][i] + channels[1][i]);
    // zero-crossing rate ~ brightness
    let zc = 0; for (let i = 1; i < n; i++) if ((mono[i - 1] < 0) !== (mono[i] < 0)) zc++;
    const zcr = zc / Math.max(1, n) * sr / 2;
    // low-end share via one-pole low-pass at 120 Hz
    const a = Math.exp(-2 * Math.PI * 120 / sr); let lp = 0, eLow = 0, eAll = 0;
    for (let i = 0; i < n; i++) { lp = (1 - a) * mono[i] + a * lp; eLow += lp * lp; eAll += mono[i] * mono[i]; }
    // onset density
    const hop = Math.round(sr * 0.01); const env = [];
    for (let p = 0; p + hop <= n; p += hop) { let s = 0; for (let i = p; i < p + hop; i++) s += mono[i] * mono[i]; env.push(Math.sqrt(s / hop)); }
    const mx = Math.max(1e-6, ...env); let onsets = 0, last = -100;
    for (let i = 1; i < env.length; i++) if (env[i] - env[i - 1] > 0.18 * mx && i - last > 8) { onsets++; last = i; }
    const seconds = n / sr;
    return { seconds, brightness: zcr, lowShare: eAll > 0 ? eLow / eAll : 0, density: onsets / Math.max(0.1, seconds) };
  }

  function guessTarget(channels, sr) {
    const f = features(channels, sr);
    if (f.seconds < 2.5) return "oneshot";
    if (f.density > 2.2 || f.brightness > 2500) return "drums";
    if (f.density < 0.6 && f.seconds > 6) return "fx";
    return "music";
  }

  function hp2(x, sr, fc) {
    const w = 2 * Math.PI * fc / sr, c = Math.cos(w), al = Math.sin(w) / (2 * Math.SQRT1_2);
    return filter(x, biquad((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al));
  }

  // per-sample required gain from an estimated (4x interpolated) true peak
  function limiterGain(channels, sr, ceilingDb, wrap) {
    const n = channels[0].length, ceil = Math.pow(10, ceilingDb / 20);
    const taps = 8, kern = [];
    for (let ph = 1; ph < 4; ph++) {
      const k = [];
      for (let t = -taps + 1; t <= taps; t++) {
        const x = t - ph / 4, s = x === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x);
        k.push(s * (0.5 + 0.5 * Math.cos(Math.PI * x / taps)));
      }
      kern.push(k);
    }
    const at = (ch, i) => wrap ? ch[((i % n) + n) % n] : (i < 0 || i >= n ? 0 : ch[i]);
    const req = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      let pk = 0;
      for (const ch of channels) {
        const v0 = Math.abs(ch[i]); if (v0 > pk) pk = v0;
        if (v0 > ceil * 0.5 || Math.abs(at(ch, i + 1)) > ceil * 0.5)
          for (const k of kern) {
            let v = 0;
            for (let j = 0; j < k.length; j++) v += at(ch, i - taps + 1 + j) * k[j];
            v = Math.abs(v); if (v > pk) pk = v;
          }
      }
      req[i] = pk > ceil ? ceil / pk : 1;
    }
    const L = Math.max(1, Math.round(0.0015 * sr)), rel = 1 - Math.exp(-1 / (0.08 * sr));
    // look-ahead min filter (window [i, i+L]) + release + moving average (window L)
    const h = new Float32Array(n), dq = [];
    let prev = 1, head = 0;
    const m = new Float32Array(n);
    for (let i = 0; i < n + L; i++) {
      if (i < n) { while (dq.length > head && req[dq[dq.length - 1]] >= req[i]) dq.pop(); dq.push(i); }
      const j = i - L; // output index whose window [j, j+L] is complete
      if (j < 0) continue;
      while (dq[head] < j) head++;
      m[j] = req[dq[head]];
    }
    for (let i = 0; i < n; i++) { prev = Math.min(m[i], prev + (1 - prev) * rel); h[i] = prev; }
    // moving average looking backwards keeps g[i] <= min over [i-L, i] >= ... ; use centred window on m
    const g = new Float32Array(n); let acc = 0;
    for (let i = 0; i < n; i++) {
      acc += h[i]; if (i > L) acc -= h[i - L - 1];
      g[i] = Math.min(h[i], acc / Math.min(i + 1, L + 1));
    }
    return g;
  }

  function autoFix(src, sr, targetKey) {
    const key = targetKey === "auto" || !TARGETS[targetKey] ? guessTarget(src, sr) : targetKey;
    const T = TARGETS[key];
    const steps = [];
    // 1) DC + rumble
    let ch = src.map((x) => {
      let mean = 0; for (let i = 0; i < x.length; i++) mean += x[i]; mean /= x.length;
      const y = new Float32Array(x.length); for (let i = 0; i < x.length; i++) y[i] = x[i] - mean;
      return y;
    });
    steps.push("DC ofset olib tashlandi");
    // HPF: for loops process a wrapped copy so the loop point stays seamless
    const hp = (x) => {
      if (!T.loop) return hp2(x, sr, 25);
      const pad = Math.min(x.length, Math.round(sr));
      const w = new Float32Array(x.length + pad);
      w.set(x.subarray(x.length - pad), 0); w.set(x, pad);
      return hp2(w, sr, 25).subarray(pad);
    };
    ch = ch.map((x) => Float32Array.from(hp(x)));
    steps.push("25 Hz past shovqin (rumble) kesildi");
    // 2) one-shots: trim leading / trailing silence, short fades
    if (!T.loop) {
      const n = ch[0].length, thr = Math.pow(10, -60 / 20);
      let a = 0, b = n - 1;
      const loud = (i) => Math.abs(ch[0][i]) > thr || Math.abs(ch[1][i]) > thr;
      while (a < n - 1 && !loud(a)) a++;
      while (b > a && !loud(b)) b--;
      a = Math.max(0, a - Math.round(0.002 * sr)); b = Math.min(n, b + Math.round(0.05 * sr));
      if (a > 0 || b < n) { ch = ch.map((x) => x.slice(a, b)); steps.push("boshi va oxiridagi sukunat olib tashlandi"); }
      const fi = Math.min(Math.round(0.001 * sr), ch[0].length >> 2), fo = Math.min(Math.round(0.01 * sr), ch[0].length >> 2);
      for (const x of ch) { for (let i = 0; i < fi; i++) x[i] *= i / fi; for (let i = 0; i < fo; i++) x[x.length - 1 - i] *= i / fo; }
    }
    // 3) level: LUFS target (or peak for one-shots), then true-peak limiting, iterate
    const ceiling = -1.0;
    let gainDb = T.peak != null ? T.peak - truePeakDb(ch).db : T.lufs - lufs(ch, sr);
    gainDb = Math.max(-30, Math.min(36, gainDb));
    let out = ch;
    for (let it = 0; it < 7; it++) {
      const g = Math.pow(10, gainDb / 20);
      const amp = ch.map((x) => x.map((v) => v * g));
      const lg = limiterGain(amp, sr, ceiling - 0.2, T.loop);
      out = amp.map((x) => x.map((v, i) => v * lg[i]));
      if (T.peak != null) break;
      const diff = T.lufs - lufs(out, sr);
      if (Math.abs(diff) < 0.15) break;
      gainDb = Math.max(-30, Math.min(40, gainDb + diff * 1.3));
    }
    const tp = truePeakDb(out).db;
    if (tp > ceiling) { const g = Math.pow(10, (ceiling - tp - 0.05) / 20); out = out.map((x) => x.map((v) => v * g)); }
    steps.push(T.peak != null ? "peak -1 dBTP ga keltirildi" : `loudness ${T.lufs} LUFS ga keltirildi`);
    steps.push("True Peak limiter (-1 dBTP)");
    return { channels: out, target: key, targetName: T.name, steps };
  }

  // ---------------------------------------------------- name ideas ---------
  const WORDS = {
    darkLow:  ["Tun", "Soya", "Chuqur", "Qora Tog'", "Zulmat", "Qadimiy"],
    darkHigh: ["Oy Nuri", "Sirli", "Tumanli", "Kumush", "Shivir"],
    brightLow:["Karvon", "Bozor", "Olov", "Yurak", "Qadam", "Zarba"],
    brightHigh:["Quyosh", "Shamol", "Chaqmoq", "Yorug'", "Ko'k Osmon", "Bahor"],
    calm:     ["Ipak", "Bog'", "Sokin", "Oqshom", "Shabada", "Orzu"],
  };
  const TYPES = { drums: ["Groove", "Beat", "Shuffle", "Bounce"], music: ["Melody", "Hook", "Vibe", "Theme"],
                  oneshot: ["Hit", "Kick", "Shot", "Punch"], fx: ["Atmos", "Rise", "Space", "Texture"] };
  function nameIdeas(channels, sr, opts) {
    opts = opts || {};
    const f = features(channels, sr);
    const type = opts.target && TYPES[opts.target] ? opts.target : guessTarget(channels, sr);
    const bright = f.brightness > 1800, heavy = f.lowShare > 0.35;
    let pool = f.density < 0.6 && type !== "oneshot" ? WORDS.calm
      : bright ? (heavy ? WORDS.brightLow : WORDS.brightHigh) : (heavy ? WORDS.darkLow : WORDS.darkHigh);
    const rnd = (arr, i) => arr[(Math.floor(Math.random() * arr.length) + i) % arr.length];
    const tail = [opts.bpm > 0 ? Math.round(opts.bpm) : "", opts.key || ""].filter(Boolean).join(" ");
    const out = new Set();
    for (let i = 0; out.size < 4 && i < 20; i++)
      out.add(`Az - ${rnd(pool, i)} ${rnd(TYPES[type], i)}${tail ? " " + tail : ""}`);
    return [...out];
  }

  root.AZAudio = { decode, headerSampleRate, lufs, truePeakDb, rate, analyse, encodeWav24, peaks,
                   features, guessTarget, autoFix, nameIdeas, TARGETS };
  if (typeof module !== "undefined") module.exports = root.AZAudio;
})(typeof window !== "undefined" ? window : globalThis);
