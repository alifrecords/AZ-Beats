// AZ BETAS admin panel
(() => {
  "use strict";
  const PACK_LIMIT = 10;
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const store = {
    get: (k, d) => { try { const v = localStorage.getItem("azb_" + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
    set: (k, v) => { try { localStorage.setItem("azb_" + k, JSON.stringify(v)); } catch {} },
  };
  const log = (msg, cls) => {
    const el = $("log");
    const line = document.createElement("div");
    if (cls) line.className = cls;
    line.textContent = new Date().toLocaleTimeString() + "  " + msg;
    if (el.textContent === "Tayyor.") el.textContent = "";
    el.appendChild(line); el.scrollTop = el.scrollHeight;
  };
  const isNet = (e) => e && (e.name === "TypeError" || e.name === "AbortError");
  const slug = (t) => t.normalize("NFKD").replace(/[^\w\s-]/g, "").trim().replace(/[\s_]+/g, "-").replace(/-+/g, "-").toLowerCase() || "pack";
  const safeName = (t) => t.replace(/[\\/:*?"<>|]+/g, "-").replace(/\s+/g, " ").trim();
  const stem = (p) => p.replace(/\.[^./]+$/, "");

  // ------------------------------------------------------------- tabs ----
  document.querySelectorAll("nav button").forEach((b) => b.onclick = () => {
    document.querySelectorAll("nav button").forEach((x) => x.classList.toggle("on", x === b));
    document.querySelectorAll("main section").forEach((s) => s.classList.toggle("on", s.id === b.dataset.tab));
    stopAll();
    if (b.dataset.tab === "library") loadLibrary();
    if (b.dataset.tab === "packs") loadPacks();
    if (b.dataset.tab === "keys") renderKeys();
    if (b.dataset.tab === "upload") refreshPackSelect();
  });

  // ------------------------------------------------------- GitHub API ----
  $("repo").value = store.get("repo", "alifrecords/AZ-Beats");
  $("branch").value = store.get("branch", "main");
  $("token").value = store.get("token", "");
  const cfg = () => ({ repo: $("repo").value.trim(), branch: $("branch").value.trim() || "main", token: $("token").value.trim() });
  const encPath = (p) => p.split("/").map(encodeURIComponent).join("/");
  const rawUrl = (libPath) => `https://raw.githubusercontent.com/${cfg().repo}/${cfg().branch}/library/${encPath(libPath)}`;

  async function gh(method, path, body) {
    const c = cfg();
    if (!c.token) throw new Error("Token kiritilmagan (Sozlamalar)");
    const url = `https://api.github.com/repos/${c.repo}/contents/${encPath(path)}` + (method === "GET" ? `?ref=${encodeURIComponent(c.branch)}&t=${Date.now()}` : "");
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 120000);
    let r;
    try {
      r = await fetch(url, { method, signal: ctl.signal, headers: { Authorization: "Bearer " + c.token, Accept: "application/vnd.github+json" }, body: body ? JSON.stringify({ ...body, branch: c.branch }) : undefined });
    } finally { clearTimeout(timer); }
    if (r.status === 404 && method === "GET") return null;
    if (!r.ok) { const e = new Error(`${method} ${path}: ${r.status} ${(await r.text()).slice(0, 160)}`); e.status = r.status; throw e; }
    return r.status === 204 ? {} : r.json();
  }
  const b64FromText = (t) => btoa(unescape(encodeURIComponent(t)));
  const textFromB64 = (b) => decodeURIComponent(escape(atob(b.replace(/\n/g, ""))));
  async function getJson(path, fallback) {
    const f = await gh("GET", path);
    if (!f) return { data: fallback, sha: null };
    return { data: JSON.parse(textFromB64(f.content)), sha: f.sha };
  }
  async function putText(path, text, message, sha) {
    if (sha === undefined) { const f = await gh("GET", path); sha = f ? f.sha : null; }
    return gh("PUT", path, { message, content: b64FromText(text), ...(sha ? { sha } : {}) });
  }
  async function deletePath(path, message) {
    const f = await gh("GET", path);
    if (f) await gh("DELETE", path, { message, sha: f.sha });
  }
  async function putBinary(path, bytes) {
    let lastErr;
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        const existing = await gh("GET", path);
        let bin = ""; const step = 0x8000;
        for (let i = 0; i < bytes.length; i += step) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + step));
        return await gh("PUT", path, { message: "Yuklash: " + path.split("/").pop(), content: btoa(bin), ...(existing ? { sha: existing.sha } : {}) });
      } catch (e) {
        lastErr = e;
        if (!isNet(e) && ![409, 422, 500, 502, 503].includes(e.status)) throw e;
        await new Promise((r) => setTimeout(r, 2000 * (attempt + 1)));
      }
    }
    throw lastErr;
  }
  // retries + automatic chunking when the network cuts big requests
  async function putFileRobust(path, bytes, onProgress) {
    const MIN = 128 * 1024;
    let chunk = store.get("chunkSize", 4 * 1024 * 1024);
    for (;;) {
      try {
        if (bytes.length <= chunk) { await putBinary(path, bytes); onProgress && onProgress(1); return; }
        const n = Math.ceil(bytes.length / chunk), pad = (x) => String(x).padStart(3, "0");
        log(`  ${n} bo'lakda yuborilmoqda (${Math.round(chunk / 1024)} KB)`);
        for (let i = 0; i < n; i++) {
          await putBinary(`${path}.part-${pad(i + 1)}-of-${pad(n)}`, bytes.subarray(i * chunk, Math.min(bytes.length, (i + 1) * chunk)));
          onProgress && onProgress((i + 1) / n);
        }
        return;
      } catch (e) {
        if (!isNet(e) || chunk <= MIN) throw e;
        chunk = Math.max(MIN, chunk >> 1);
        store.set("chunkSize", chunk);
        log(`  Tarmoq so'rovni uzdi — kichikroq bo'laklarga o'tilmoqda (${Math.round(chunk / 1024)} KB)`, "err");
      }
    }
  }

  // ------------------------------------------------------------ meta ----
  let meta = { freeLimit: 10, packs: {}, items: {} }, catalog = { items: [] };
  const normMeta = (m) => { m.items = m.items || {}; m.packs = m.packs || {}; m.freeLimit = m.freeLimit ?? 10; return m; };
  async function loadMeta() {
    meta = normMeta((await getJson("library/meta.json", {})).data);
    $("freeLimit").value = meta.freeLimit;
    const genres = new Set([...Object.values(meta.items), ...Object.values(meta.packs)].map((e) => e.genre).filter(Boolean));
    $("genres").innerHTML = [...genres].map((g) => `<option>${esc(g)}</option>`).join("");
    return meta;
  }
  // find an item key even if the bot renamed .wav -> .flac meanwhile
  const findKey = (m, key) => (m.items[key] ? key : Object.keys(m.items).find((k) => stem(k) === stem(key)));
  // load the freshest meta.json, apply changes, save; retry on conflicts
  async function updateMeta(message, mutate) {
    for (let attempt = 0; attempt < 4; attempt++) {
      const fresh = await getJson("library/meta.json", {});
      const m = normMeta(fresh.data);
      mutate(m);
      try {
        await putText("library/meta.json", JSON.stringify(m, null, 2), message, fresh.sha);
        meta = m;
        return m;
      } catch (e) {
        if (![409, 422].includes(e.status) || attempt === 3) throw e;
        await new Promise((r) => setTimeout(r, 1500));
      }
    }
  }
  const packList = (m = meta) => Object.entries(m.packs).sort((a, b) => (a[1].order ?? 999) - (b[1].order ?? 999) || a[1].name.localeCompare(b[1].name));
  const packCount = (id, m = meta) => Object.values(m.items).filter((e) => e.pack === id).length;

  $("saveConn").onclick = async () => {
    const c = cfg(); store.set("repo", c.repo); store.set("branch", c.branch); store.set("token", c.token);
    try {
      const r = await fetch(`https://api.github.com/repos/${c.repo}`, { headers: { Authorization: "Bearer " + c.token } });
      const j = await r.json();
      if (!r.ok) throw new Error(j.message);
      if (!j.permissions || !j.permissions.push) throw new Error("Tokenda yozish huquqi yo'q (Contents: Read and write kerak)");
      alert("Ulandi: " + j.full_name + (j.private ? " (private — kutubxona plaginga ko'rinmaydi, Public qiling!)" : ""));
      await loadMeta(); refreshPackSelect();
    } catch (e) { alert("Xato: " + e.message); }
  };
  $("saveLimit").onclick = async () => {
    try {
      const v = Math.max(0, parseInt($("freeLimit").value || "10", 10));
      await updateMeta("Bepul limit: " + v, (m) => { m.freeLimit = v; });
      alert("Saqlandi");
    } catch (e) { alert(e.message); }
  };

  // ------------------------------------------------------- playback ----
  let actx = null, playing = null;
  const player = $("player");
  let libPlaying = null;
  function stopAll() {
    stop();
    if (libPlaying) { player.pause(); libPlaying.btn.classList.remove("on"); libPlaying.btn.innerHTML = "&#9654;"; libPlaying = null; }
  }

  // ============================================================ UPLOAD ====
  let items = [];
  const drop = $("drop");
  const fmtT = (s) => s.toFixed(2) + " s";
  drop.onclick = () => $("files").click();
  $("files").onchange = (e) => { addFiles([...e.target.files]); e.target.value = ""; };
  drop.ondragover = (e) => { e.preventDefault(); drop.classList.add("over"); };
  drop.ondragleave = () => drop.classList.remove("over");
  drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove("over"); addFiles([...e.dataTransfer.files]); };

  function bpmFromName(s) {
    const m = s.match(/(?:^|\D)(\d{2,3})\s*(?:bpm)?(?!\d)/i);
    const v = m ? parseInt(m[1], 10) : 0;
    return v >= 60 && v <= 200 ? v : 0;
  }
  function keyFromName(s) {
    const m = s.match(/(?:^|[\s_\-(])([A-G](?:#|b)?)(m|min)?(?=$|[\s_\-)])/);
    return m ? m[1] + (m[2] ? "m" : "") : "";
  }

  async function addFiles(list) {
    for (const f of list) {
      if (!/\.(wav|aiff?|flac|mp3|ogg)$/i.test(f.name)) { log(`${f.name}: audio emas, o'tkazildi`, "err"); continue; }
      if (f.size > 50e6) { log(`${f.name}: 50 MB dan katta`, "err"); continue; }
      const base = stem(f.name);
      const it = { id: Math.random().toString(36).slice(2), file: f, name: f.name, title: base, trimmed: false, fixed: false, target: "auto" };
      items.push(it);
      renderItem(it, true);
      try {
        const d = await AZAudio.decode(f);
        it.orig = d; it.channels = d.channels; it.sr = d.sampleRate;
        it.sel = [0, it.channels[0].length];
        it.bpm = bpmFromName(base); it.key = keyFromName(base);
        it.an = AZAudio.analyse(it.channels, it.sr);
      } catch (e) { it.error = "Faylni o'qib bo'lmadi: " + e.message; }
      renderItem(it);
    }
    refreshUploadState();
  }
  function refreshUploadState() {
    $("uploadBtn").disabled = !items.some((i) => i.channels);
    $("bulkRow").style.display = items.filter((i) => i.channels).length > 1 ? "flex" : "none";
  }

  function qualHtml(a) {
    return `<div class="qual q-${a.rating.level}"><i></i>${a.lufs.toFixed(1)} LUFS · ${a.tp.toFixed(1)} dBTP · ${a.rating.text}</div>`;
  }

  function renderItem(it, loading) {
    let el = document.getElementById("it_" + it.id);
    if (!el) { el = document.createElement("div"); el.className = "item"; el.id = "it_" + it.id; $("items").appendChild(el); }
    if (loading || !it.channels) {
      el.innerHTML = `<div class="ihead"><div class="meta"><b>${esc(it.name)}</b><small>${it.error ? `<span class="err">${esc(it.error)}</span>` : "Tahlil qilinmoqda..."}</small></div>
        <button class="btn" data-rm>Olib tashlash</button></div>`;
      el.querySelector("[data-rm]").onclick = () => removeItem(it);
      return;
    }
    const a = it.an, len = it.channels[0].length / it.sr, modified = it.trimmed || it.fixed;
    const size = modified ? it.channels[0].length * 6 + 44 : it.file.size;
    const targets = Object.entries(AZAudio.TARGETS).map(([k, t]) => `<option value="${k}" ${k === it.target ? "selected" : ""}>${esc(t.name)}</option>`).join("");
    el.innerHTML = `
      <div class="ihead">
        <button class="round" data-play title="Tinglash">&#9654;</button>
        <div class="meta">
          <input type="text" data-title value="${esc(it.title)}" placeholder="Nom" title="Kutubxonada ko'rinadigan nom">
          <small>${esc(it.name)} · ${(it.sr / 1000).toFixed(1)} kHz · ${fmtT(len)} · ${(size / 1e6).toFixed(1)} MB${modified ? " · <b style='color:var(--accentTxt)'>o'zgartirilgan → WAV 24-bit</b>" : ""}</small>
        </div>
        ${qualHtml(a)}
        <button class="btn" data-rm>Olib tashlash</button>
      </div>
      <div class="chips" data-chips></div>
      <canvas class="wave"></canvas>
      <div class="itools">
        <span data-seltxt></span>
        <label class="chk" style="font-size:12px">BPM <input type="number" min="0" max="250" step="0.01" data-bpm value="${it.bpm || ""}" placeholder="—"></label>
        <label class="chk" style="font-size:12px"><input type="checkbox" data-snap ${it.snap ? "checked" : ""}>Taktga moslash</label>
        <label class="chk" style="font-size:12px"><input type="checkbox" data-fade ${it.fade !== false ? "checked" : ""}>Fade 3 ms</label>
        <span style="flex:1"></span>
        <button class="btn" data-selplay>Tanlovni tinglash</button>
        <button class="btn" data-all>Hammasini tanlash</button>
        <button class="btn teal" data-cut>Kesish</button>
        ${it.trimmed || it.fixed ? '<button class="btn" data-undo>Asl holiga</button>' : ""}
      </div>
      <div class="itools">
        <select data-target title="Qaysi standartga keltirish">${targets}</select>
        <button class="btn gold" data-fix>Avto sozlash</button>
        ${it.fixed ? `<button class="btn" data-ab>${it.listenOrig ? "Eshitilmoqda: ASL" : "Eshitilmoqda: SOZLANGAN"} (A/B)</button><button class="btn" data-unfix>Sozlashni bekor qilish</button>` : ""}
        <span style="flex:1"></span>
        <button class="btn" data-name>Avto nom</button>
      </div>
      ${it.fixInfo ? `<div class="fixbox">${esc(it.fixInfo.targetName)}: ${it.fixInfo.before.lufs.toFixed(1)} → <b>${it.an.lufs.toFixed(1)} LUFS</b>, True Peak ${it.fixInfo.before.tp.toFixed(1)} → <b>${it.an.tp.toFixed(1)} dBTP</b>. ${esc(it.fixInfo.steps.join(" · "))}</div>` : ""}
      <div class="reasons">Sifat: ${esc(a.rating.reasons.join(", "))}${a.rating.level === "bad" && !it.fixed ? " — <b>Avto sozlash</b>ni bosing" : ""}</div>`;
    it.canvas = el.querySelector("canvas");
    it.peaks = null;
    const q = (s) => el.querySelector(s);
    q("[data-rm]").onclick = () => removeItem(it);
    q("[data-play]").onclick = () => togglePlay(it, false);
    q("[data-selplay]").onclick = () => togglePlay(it, true);
    q("[data-all]").onclick = () => { it.sel = [0, it.channels[0].length]; updateSel(it); };
    q("[data-cut]").onclick = () => cut(it);
    q("[data-title]").oninput = (e) => { it.title = e.target.value; };
    q("[data-target]").onchange = (e) => { it.target = e.target.value; };
    q("[data-fix]").onclick = () => autoFix(it);
    q("[data-name]").onclick = () => showNames(it);
    if (q("[data-undo]")) q("[data-undo]").onclick = () => {
      stop(); it.channels = it.orig.channels; it.trimmed = false; it.fixed = false; it.fixInfo = null; it.preFix = null;
      it.sel = [0, it.channels[0].length]; it.an = AZAudio.analyse(it.channels, it.sr); renderItem(it);
    };
    if (q("[data-unfix]")) q("[data-unfix]").onclick = () => {
      stop(); it.channels = it.preFix; it.fixed = false; it.fixInfo = null; it.preFix = null;
      it.sel = [0, it.channels[0].length]; it.an = AZAudio.analyse(it.channels, it.sr); renderItem(it);
    };
    if (q("[data-ab]")) q("[data-ab]").onclick = () => {
      it.listenOrig = !it.listenOrig;
      const wasPlaying = playing && playing.it === it ? playing : null;
      renderItem(it);
      if (wasPlaying) play(it, wasPlaying.from, wasPlaying.from + wasPlaying.n, wasPlaying.loop, wasPlaying.pos());
    };
    q("[data-bpm]").onchange = (e) => { it.bpm = parseFloat(e.target.value) || 0; snapSel(it); updateSel(it); };
    q("[data-snap]").onchange = (e) => { it.snap = e.target.checked; snapSel(it); updateSel(it); };
    q("[data-fade]").onchange = (e) => { it.fade = e.target.checked; };
    if (it.names) renderChips(it);
    attachCanvas(it);
    updateSel(it);
  }

  function removeItem(it) {
    if (playing && playing.it === it) stop();
    items = items.filter((x) => x !== it);
    const el = document.getElementById("it_" + it.id); if (el) el.remove();
    refreshUploadState();
  }

  // ---- auto-fix & names
  function autoFix(it) {
    stop();
    const src = it.channels;
    const before = it.an;
    const res = AZAudio.autoFix(src, it.sr, it.target);
    it.preFix = src;
    it.channels = res.channels;
    it.fixed = true; it.listenOrig = false;
    it.sel = [0, it.channels[0].length];
    it.an = AZAudio.analyse(it.channels, it.sr);
    it.fixInfo = { before, steps: res.steps, targetName: AZAudio.TARGETS[res.target].name };
    renderItem(it);
    log(`${it.title}: avto sozlandi (${it.fixInfo.targetName}) ${before.lufs.toFixed(1)} → ${it.an.lufs.toFixed(1)} LUFS`, "ok");
  }
  function showNames(it) {
    it.names = AZAudio.nameIdeas(it.channels, it.sr, { bpm: it.bpm, key: it.key, target: it.target === "auto" ? null : it.target });
    renderChips(it);
  }
  function renderChips(it) {
    const el = document.getElementById("it_" + it.id); if (!el) return;
    const box = el.querySelector("[data-chips]");
    box.innerHTML = it.names.map((n) => `<span class="chip">${esc(n)}</span>`).join("") + `<span class="chip" data-more>↻ boshqa</span>`;
    box.querySelectorAll(".chip").forEach((c) => c.onclick = () => {
      if (c.dataset.more !== undefined) return showNames(it);
      it.title = c.textContent;
      el.querySelector("[data-title]").value = it.title;
    });
  }
  $("fixAll").onclick = () => items.filter((i) => i.channels).forEach(autoFix);
  $("nameAll").onclick = () => items.filter((i) => i.channels).forEach((it) => {
    showNames(it); it.title = it.names[0];
    const el = document.getElementById("it_" + it.id); if (el) el.querySelector("[data-title]").value = it.title;
  });

  // ---- waveform + selection
  function draw(it, head) {
    const cv = it.canvas; if (!cv || !cv.isConnected) return;
    const dpr = window.devicePixelRatio || 1, w = cv.clientWidth, h = cv.clientHeight;
    if (!w) return;
    if (cv.width !== Math.round(w * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); it.peaks = null; }
    const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const n = it.channels[0].length, pts = Math.max(50, Math.floor(w / 2));
    if (!it.peaks) it.peaks = AZAudio.peaks(it.channels, pts);
    g.clearRect(0, 0, w, h);
    const x0 = it.sel[0] / n * w, x1 = it.sel[1] / n * w;
    g.fillStyle = "rgba(242,163,58,0.10)"; g.fillRect(x0, 0, x1 - x0, h);
    let mx = 1e-4; for (const v of it.peaks) mx = Math.max(mx, Math.abs(v));
    if (it.fixed) mx = 1; // show real level after fixing
    const bw = w / pts;
    for (let i = 0; i < pts; i++) {
      const lo = it.peaks[i * 2] / mx, hi = it.peaks[i * 2 + 1] / mx, x = i * bw;
      g.fillStyle = x >= x0 && x <= x1 ? (it.fixed ? "#3bc9b0" : "#f2a33a") : "#4a525c";
      g.fillRect(x, h / 2 - hi * h * 0.45, Math.max(1, bw - 0.5), Math.max(1, (hi - lo) * h * 0.45));
    }
    if (it.bpm > 0) {
      const bar = 240 / it.bpm * it.sr;
      g.fillStyle = "rgba(255,255,255,0.12)";
      for (let p = bar; p < n; p += bar) g.fillRect(p / n * w, 0, 1, h);
    }
    g.fillStyle = "#e8eaed"; g.fillRect(x0, 0, 2, h); g.fillRect(x1 - 2, 0, 2, h);
    if (head != null) { g.fillStyle = "#ffffff"; g.fillRect(head / n * w, 0, 2, h); }
  }
  function snapSel(it) {
    if (!it.snap || !(it.bpm > 0)) return;
    const n = it.channels[0].length, beat = 60 / it.bpm * it.sr, bar = beat * 4;
    const a = Math.round(it.sel[0] / beat) * beat;
    let bars = Math.max(1, Math.round((it.sel[1] - a) / bar));
    if (a + bars * bar > n + beat * 0.25) bars = Math.max(1, Math.floor((n - a) / bar + 0.01));
    it.sel = [Math.max(0, Math.round(a)), Math.min(n, Math.round(a + bars * bar))];
  }
  function updateSel(it) {
    const el = document.getElementById("it_" + it.id); if (!el) return;
    const [a, b] = it.sel, sr = it.sr;
    let t = `Tanlov: ${fmtT(a / sr)} – ${fmtT(b / sr)} (${fmtT((b - a) / sr)})`;
    if (it.bpm > 0) t += ` · ${((b - a) / sr * it.bpm / 240).toFixed(2)} takt`;
    el.querySelector("[data-seltxt]").textContent = t;
    draw(it);
  }
  function attachCanvas(it) {
    const cv = it.canvas;
    let mode = null, anchor = 0, moved = false;
    const pos = (e) => { const r = cv.getBoundingClientRect(); return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * it.channels[0].length; };
    cv.onpointerdown = (e) => {
      cv.setPointerCapture(e.pointerId);
      const p = pos(e), n = it.channels[0].length, tol = n * 8 / cv.clientWidth;
      moved = false;
      if (Math.abs(p - it.sel[0]) < tol && it.sel[0] > 0) mode = "a";
      else if (Math.abs(p - it.sel[1]) < tol && it.sel[1] < n) mode = "b";
      else { mode = "new"; anchor = p; }
    };
    cv.onpointermove = (e) => {
      if (!mode) return;
      const p = Math.round(pos(e));
      if (mode === "new" && Math.abs(p - anchor) < it.channels[0].length * 3 / cv.clientWidth) return;
      moved = true;
      if (mode === "a") it.sel[0] = Math.min(p, it.sel[1] - 1);
      else if (mode === "b") it.sel[1] = Math.max(p, it.sel[0] + 1);
      else it.sel = [Math.round(Math.min(anchor, p)), Math.round(Math.max(anchor, p))];
      updateSel(it);
    };
    cv.onpointerup = (e) => {
      if (mode === "new" && !moved) play(it, Math.round(pos(e)), it.channels[0].length, false);
      else { snapSel(it); updateSel(it); }
      mode = null;
    };
    new ResizeObserver(() => draw(it)).observe(cv);
  }

  // ---- playback (Web Audio)
  function stop() {
    if (!playing) return;
    try { playing.src.stop(); } catch (e) {}
    const el = document.getElementById("it_" + playing.it.id);
    if (el) { const b = el.querySelector("[data-play]"); if (b) b.classList.remove("on"); }
    const it = playing.it; playing = null; draw(it);
  }
  function play(it, from, to, loop, offset = 0) {
    stop();
    actx = actx || new (window.AudioContext || window.webkitAudioContext)();
    const chans = it.listenOrig && it.preFix ? it.preFix : it.channels;
    to = Math.min(to, chans[0].length); from = Math.min(from, to - 1);
    const n = to - from; if (n <= 0) return;
    const buf = actx.createBuffer(2, n, it.sr);
    for (let c = 0; c < 2; c++) buf.copyToChannel(chans[c].subarray(from, to), c);
    const src = actx.createBufferSource(); src.buffer = buf; src.loop = !!loop;
    src.connect(actx.destination);
    const off = Math.max(0, Math.min(n - 1, offset)) / it.sr;
    src.start(0, off);
    const t0 = actx.currentTime - off;
    playing = { it, src, from, n, loop, pos: () => { let e = (actx.currentTime - t0) * it.sr; if (loop) e %= n; return Math.min(e, n); } };
    const el = document.getElementById("it_" + it.id);
    el.querySelector("[data-play]").classList.add("on");
    src.onended = () => { if (playing && playing.src === src) stop(); };
    const tick = () => { if (!playing || playing.src !== src) return; draw(it, from + playing.pos()); requestAnimationFrame(tick); };
    tick();
  }
  function togglePlay(it, selOnly) {
    if (playing && playing.it === it) { stop(); return; }
    stopAll();
    if (selOnly) play(it, it.sel[0], it.sel[1], true);
    else play(it, 0, it.channels[0].length, false);
  }
  function cut(it) {
    const [a, b] = it.sel, n = it.channels[0].length;
    if (a <= 0 && b >= n) { alert("Avval to'lqin ustida sichqoncha bilan qism tanlang"); return; }
    stop();
    const fade = it.fade !== false ? Math.min(Math.floor((b - a) / 2), Math.round(0.003 * it.sr)) : 0;
    const sl = (chs) => chs.map((ch) => {
      const out = ch.slice(a, b);
      for (let i = 0; i < fade; i++) { const g = i / fade; out[i] *= g; out[out.length - 1 - i] *= g; }
      return out;
    });
    it.channels = sl(it.channels);
    if (it.preFix && it.preFix[0].length === n) it.preFix = sl(it.preFix);
    it.trimmed = true;
    it.sel = [0, it.channels[0].length];
    it.an = AZAudio.analyse(it.channels, it.sr);
    renderItem(it);
    log(`${it.title}: kesildi (${fmtT((b - a) / it.sr)}), LUFS ${it.an.lufs.toFixed(1)}`, "ok");
  }

  // ---- pack selector
  function refreshPackSelect() {
    const sel = $("upPack"), cur = sel.value;
    sel.innerHTML = `<option value="">— Paketsiz —</option>` + packList().map(([id, p]) => {
      const n = packCount(id), full = n >= PACK_LIMIT;
      return `<option value="${esc(id)}" ${full ? "disabled" : ""}>${esc(p.name)} (${n}/${PACK_LIMIT}${full ? " — to'la" : ""})</option>`;
    }).join("");
    if ([...sel.options].some((o) => o.value === cur && !o.disabled)) sel.value = cur;
  }

  $("testConn").onclick = async () => {
    const c = cfg();
    if (!c.token) return alert("Avval Sozlamalar bo'limida tokenni kiriting");
    try {
      const r = await fetch(`https://api.github.com/repos/${c.repo}`, { headers: { Authorization: "Bearer " + c.token } });
      const j = await r.json();
      if (!r.ok) throw new Error(r.status + " " + j.message);
      if (!j.permissions || !j.permissions.push) throw new Error("Tokenda yozish huquqi yo'q (Contents: Read and write kerak)");
      log(`Ulanish yaxshi: ${j.full_name}, yozish huquqi bor`, "ok");
    } catch (e) {
      log("Ulanish xatosi: " + (isNet(e) ? "internet / VPN / antivirus GitHub'ga ulanishni to'sdi" : e.message), "err");
    }
  };

  $("uploadBtn").onclick = async () => {
    const cat = ($("upCat").value.trim() || "Loops").replace(/[\\/]/g, "-");
    const genre = $("upGenre").value.trim();
    const tags = $("upTags").value.split(",").map((s) => s.trim()).filter(Boolean);
    const packId = $("upPack").value || null;
    const ready = items.filter((i) => i.channels);
    if (!ready.length) return;
    stopAll();
    $("uploadBtn").disabled = true;
    const prog = $("prog"); prog.style.width = "0";
    try {
      await loadMeta();
      if (packId) {
        const free = PACK_LIMIT - packCount(packId);
        if (ready.length > free) throw new Error(`"${meta.packs[packId].name}" paketida ${free} ta joy qoldi, siz ${ready.length} ta fayl tanladingiz`);
      }
      const today = new Date().toISOString().slice(0, 10);
      let done = 0;
      const added = {};
      let nextOrder = Math.max(0, ...Object.values(meta.items).filter((e) => (e.pack || null) === packId).map((e) => e.order || 0)) + 1;
      for (const it of ready) {
        const modified = it.trimmed || it.fixed;
        const ext = modified ? ".wav" : (it.name.match(/\.[^.]+$/) || [".wav"])[0];
        const title = safeName(it.title || stem(it.name)) || stem(it.name);
        const fname = title + ext;
        const bytes = modified ? AZAudio.encodeWav24(it.channels, it.sr) : it.orig.bytes;
        const key = `files/${cat}/${fname}`;
        log(`Yuklanmoqda: ${fname} (${(bytes.length / 1e6).toFixed(1)} MB, ${it.an.lufs.toFixed(1)} LUFS)`);
        await putFileRobust("library/" + key, bytes, (f) => { prog.style.width = ((done + f) / ready.length * 100).toFixed(0) + "%"; });
        added[key] = { name: title, category: cat, genre, tags, free: $("upFree").checked, demo: $("upDemo").checked, added: today,
          ...(packId ? { pack: packId } : {}), order: nextOrder++, lufs: +it.an.lufs.toFixed(1), tp: +it.an.tp.toFixed(1),
          ...(it.bpm > 0 ? { bpm: it.bpm } : {}), ...(it.key ? { key: it.key } : {}) };
        done++;
        log(`OK: ${fname}`, "ok");
        removeItem(it);
      }
      await updateMeta(`Kutubxona: ${done} ta fayl`, (m) => {
        for (const [k, v] of Object.entries(added)) {
          const old = m.items[findKey(m, k)] || {};
          m.items[k] = { ...old, ...v, genre: v.genre || old.genre || "", tags: v.tags.length ? v.tags : (old.tags || []), added: old.added || v.added };
        }
      });
      prog.style.width = "100%";
      refreshPackSelect();
      log("Tayyor. GitHub BPM/tonallikni aniqlab, katalogni 1-2 daqiqada yangilaydi.", "ok");
    } catch (e) {
      log((isNet(e) ? "Tarmoq xatosi: so'rov GitHub'ga yetib bormadi (internet, VPN yoki antivirus). Qayta urinib ko'ring. " : "Xato: ") + e.message, "err");
    }
    refreshUploadState();
  };

  // ============================================================ LIBRARY ===
  let libEdits = {}, libOrder = null;
  function lufsCell(l, tp, seconds) {
    if (l == null) return '<span class="sub">tahlil kutilmoqda</span>';
    const r = AZAudio.rate(l, tp ?? -6, 0, seconds ?? 10);
    const col = { good: "#3ccf6e", mid: "#f2c94c", bad: "#ef5350" }[r.level];
    return `<span title="${esc(r.reasons.join(", "))}"><i class="dot" style="background:${col}"></i>${Number(l).toFixed(1)}</span>`;
  }
  async function loadLibrary() {
    $("libInfo").textContent = "Yuklanmoqda...";
    try {
      await loadMeta();
      catalog = (await getJson("library/catalog.json", { items: [] })).data;
      libEdits = {};
      $("saveMeta").disabled = true;
      renderLibrary();
    } catch (e) { $("libInfo").textContent = e.message; }
  }
  const effective = (file) => ({ ...meta.items[file], ...(libEdits[file] || {}) });
  function setEdit(file, field, value) {
    libEdits[file] = { ...(libEdits[file] || {}), [field]: value };
    $("saveMeta").disabled = false;
  }
  function renderLibrary() {
    const byFile = {}; (catalog.items || []).forEach((it) => (byFile[it.file] = it));
    const files = Object.keys(meta.items).filter((f) => !(libEdits[f] && libEdits[f].__deleted));
    const groups = [...packList().map(([id, p]) => ({ id, p })), { id: null, p: { name: "Paketsiz" } }];
    $("libInfo").textContent = `${files.length} ta fayl · ${packList().length} ta paket · bepul limit: ${meta.freeLimit}`;
    const packOpts = (cur) => `<option value="">— Paketsiz —</option>` + packList().map(([id, p]) => `<option value="${esc(id)}" ${cur === id ? "selected" : ""}>${esc(p.name)}</option>`).join("");
    $("libGroups").innerHTML = groups.map((g) => {
      const rows = files.filter((f) => (effective(f).pack || null) === g.id)
        .sort((a, b) => (effective(a).order ?? 1e9) - (effective(b).order ?? 1e9) || (effective(b).added || "").localeCompare(effective(a).added || ""));
      if (!rows.length && g.id === null) return "";
      const cover = g.p.cover ? `<img src="${esc(rawUrl(g.p.cover))}" alt="">` : `<div class="cover"></div>`;
      const cnt = g.id ? `<span class="count ${rows.length >= PACK_LIMIT ? "full" : ""}">${rows.length}/${PACK_LIMIT}</span>` : `<span class="count">${rows.length} ta</span>`;
      return `<div class="group"><div class="ghead">${g.id ? cover : ""}<div style="flex:1"><h3>${esc(g.p.name)}</h3><div class="sub" style="margin:0">${esc(g.p.genre || "")} ${g.p.free ? '<span class="badge b-free">BEPUL PAKET</span>' : ""}</div></div>${cnt}</div>
        <div class="tbl"><table><thead><tr><th></th><th>NOMI</th><th>PAKET</th><th>JANR</th><th>BPM</th><th>TON</th><th>LUFS</th><th>BEPUL</th><th>NAMUNA</th><th>TARTIB</th><th></th></tr></thead><tbody>
        ${rows.map((file, idx) => {
          const e = effective(file), c = byFile[file] || {};
          const isNew = e.added && (Date.now() - new Date(e.added)) / 864e5 <= 14;
          return `<tr data-file="${esc(file)}">
            <td><button class="round sm" data-lplay title="Tinglash">&#9654;</button></td>
            <td><input type="text" data-f="name" value="${esc(e.name)}"> ${isNew ? '<span class="badge b-new">YANGI</span>' : ""}</td>
            <td><select data-f="pack">${packOpts(e.pack || "")}</select></td>
            <td><input type="text" data-f="genre" value="${esc(e.genre)}"></td>
            <td><input type="text" data-f="bpm" value="${esc(e.bpm ?? c.bpm ?? "")}" style="width:64px"></td>
            <td><input type="text" data-f="key" value="${esc(e.key ?? c.key ?? "")}" style="width:52px"></td>
            <td style="white-space:nowrap">${lufsCell(e.lufs ?? c.lufs, e.tp ?? c.tp, e.duration ?? c.duration)}</td>
            <td><input type="checkbox" data-f="free" ${e.free ? "checked" : ""}></td>
            <td><input type="checkbox" data-f="demo" ${e.demo ? "checked" : ""}></td>
            <td style="white-space:nowrap"><button class="btn sm" data-up ${idx === 0 ? "disabled" : ""}>▲</button> <button class="btn sm" data-down ${idx === rows.length - 1 ? "disabled" : ""}>▼</button></td>
            <td><button class="btn sm danger" data-del>O'chirish</button></td></tr>`;
        }).join("") || `<tr><td colspan="11" class="sub">Bu paketda hali fayl yo'q</td></tr>`}
        </tbody></table></div></div>`;
    }).join("") || `<p class="sub">Hali fayl yo'q</p>`;

    $("libGroups").querySelectorAll("tr[data-file]").forEach((tr) => {
      const file = tr.dataset.file;
      tr.querySelectorAll("[data-f]").forEach((inp) => inp.onchange = () => {
        const f = inp.dataset.f;
        let v = inp.type === "checkbox" ? inp.checked : inp.value.trim();
        if (f === "bpm") v = parseFloat(v) || 0;
        if (f === "pack") {
          v = v || null;
          if (v) {
            const n = Object.keys(meta.items).filter((k) => !(libEdits[k] && libEdits[k].__deleted) && effective(k).pack === v).length;
            if (n >= PACK_LIMIT) { alert(`Paketda allaqachon ${PACK_LIMIT} ta fayl bor`); renderLibrary(); return; }
          }
          setEdit(file, "order", 1e6);
        }
        setEdit(file, f, v);
        if (f === "pack") { renumber(v); renderLibrary(); }
      });
      tr.querySelector("[data-up]").onclick = () => move(file, -1);
      tr.querySelector("[data-down]").onclick = () => move(file, +1);
      tr.querySelector("[data-del]").onclick = () => {
        if (!confirm("O'chirilsinmi: " + effective(file).name + " ? (saqlaganingizda o'chadi)")) return;
        setEdit(file, "__deleted", true); renderLibrary();
      };
      tr.querySelector("[data-lplay]").onclick = (ev) => libPlay(file, ev.currentTarget);
    });
  }
  function groupFiles(pack) {
    return Object.keys(meta.items).filter((f) => !(libEdits[f] && libEdits[f].__deleted) && (effective(f).pack || null) === (pack || null))
      .sort((a, b) => (effective(a).order ?? 1e9) - (effective(b).order ?? 1e9) || (effective(b).added || "").localeCompare(effective(a).added || ""));
  }
  function renumber(pack) { groupFiles(pack).forEach((f, i) => { if (effective(f).order !== i + 1) setEdit(f, "order", i + 1); }); }
  function move(file, dir) {
    const pack = effective(file).pack || null;
    renumber(pack);
    const list = groupFiles(pack), i = list.indexOf(file), j = i + dir;
    if (j < 0 || j >= list.length) return;
    const other = list[j];
    const oi = effective(file).order, oj = effective(other).order;
    setEdit(file, "order", oj); setEdit(other, "order", oi);
    renderLibrary();
  }
  function libPlay(file, btn) {
    if (libPlaying && libPlaying.file === file) { stopAll(); return; }
    stopAll();
    player.src = rawUrl(file);
    player.play().catch((e) => log("Tinglab bo'lmadi: " + e.message, "err"));
    btn.classList.add("on"); btn.innerHTML = "&#9632;";
    libPlaying = { file, btn };
    player.onended = () => stopAll();
  }
  $("reloadLib").onclick = loadLibrary;
  $("saveMeta").onclick = async () => {
    const edits = libEdits;
    const deleted = Object.keys(edits).filter((f) => edits[f].__deleted);
    try {
      $("saveMeta").disabled = true;
      for (const f of deleted) {
        await deletePath("library/" + f, "O'chirildi: " + f);
      }
      await updateMeta("Kutubxona sozlamalari", (m) => {
        for (const [f, ch] of Object.entries(edits)) {
          const k = findKey(m, f); if (!k) continue;
          if (ch.__deleted) { delete m.items[k]; continue; }
          const v = { ...m.items[k], ...ch };
          if (!v.pack) delete v.pack;
          m.items[k] = v;
        }
      });
      libEdits = {};
      alert("Saqlandi. Katalog 1-2 daqiqada yangilanadi.");
      renderLibrary();
    } catch (e) { alert(e.message); $("saveMeta").disabled = false; }
  };

  // ============================================================ PACKS =====
  let editingPack = null, coverBytes = null, packOrderDirty = false;
  async function loadPacks() {
    try { await loadMeta(); packOrderDirty = false; $("savePackOrder").disabled = true; renderPacks(); }
    catch (e) { $("packInfo").textContent = e.message; }
  }
  function renderPacks() {
    const list = packList();
    $("packInfo").textContent = list.length ? `${list.length} ta paket` : "Hali paket yo'q — yuqorida birinchisini yarating";
    $("packGrid").innerHTML = list.map(([id, p], i) => {
      const n = packCount(id);
      return `<div class="pack" data-id="${esc(id)}">
        <div class="img" style="${p.cover ? `background-image:url('${esc(rawUrl(p.cover))}')` : ""}">${p.cover ? "" : "muqova yo'q"}</div>
        <div class="body"><h3>${esc(p.name)}</h3>
          <div class="sub" style="margin:0">${esc(p.description || "")}</div>
          <div class="count ${n >= PACK_LIMIT ? "full" : ""}">${n}/${PACK_LIMIT} ta ${p.genre ? "· " + esc(p.genre) : ""} ${p.free ? '<span class="badge b-free">BEPUL</span>' : ""}</div>
          <div class="acts">
            <button class="btn sm" data-pup ${i === 0 ? "disabled" : ""}>▲</button>
            <button class="btn sm" data-pdown ${i === list.length - 1 ? "disabled" : ""}>▼</button>
            <button class="btn sm" data-pedit>Tahrirlash</button>
            <button class="btn sm danger" data-pdel>O'chirish</button>
          </div></div></div>`;
    }).join("");
    $("packGrid").querySelectorAll(".pack").forEach((card) => {
      const id = card.dataset.id;
      card.querySelector("[data-pup]").onclick = () => movePack(id, -1);
      card.querySelector("[data-pdown]").onclick = () => movePack(id, +1);
      card.querySelector("[data-pedit]").onclick = () => editPack(id);
      card.querySelector("[data-pdel]").onclick = () => deletePack(id);
    });
  }
  function movePack(id, dir) {
    const list = packList().map(([k]) => k);
    const i = list.indexOf(id), j = i + dir;
    if (j < 0 || j >= list.length) return;
    [list[i], list[j]] = [list[j], list[i]];
    list.forEach((k, idx) => (meta.packs[k].order = idx + 1));
    packOrderDirty = true; $("savePackOrder").disabled = false;
    renderPacks();
  }
  $("savePackOrder").onclick = async () => {
    const order = Object.fromEntries(packList().map(([k], i) => [k, i + 1]));
    try {
      await updateMeta("Paketlar tartibi", (m) => { for (const [k, o] of Object.entries(order)) if (m.packs[k]) m.packs[k].order = o; });
      $("savePackOrder").disabled = true; renderPacks();
    } catch (e) { alert(e.message); }
  };
  function resetPackForm() {
    editingPack = null; coverBytes = null;
    $("packFormTitle").textContent = "Yangi sample pack";
    ["pName", "pGenre", "pDesc"].forEach((k) => ($(k).value = ""));
    $("pFree").checked = false; $("pCancel").hidden = true;
    $("coverPreview").style.backgroundImage = "";
  }
  function editPack(id) {
    const p = meta.packs[id]; editingPack = id; coverBytes = null;
    $("packFormTitle").textContent = "Tahrirlash: " + p.name;
    $("pName").value = p.name || ""; $("pGenre").value = p.genre || ""; $("pDesc").value = p.description || "";
    $("pFree").checked = !!p.free; $("pCancel").hidden = false;
    $("coverPreview").style.backgroundImage = p.cover ? `url('${rawUrl(p.cover)}')` : "";
    window.scrollTo({ top: 0, behavior: "smooth" });
  }
  $("pCancel").onclick = resetPackForm;
  // square-crop the cover to 800x800 JPEG
  $("pCover").onchange = async (e) => {
    const f = e.target.files[0]; e.target.value = "";
    if (!f) return;
    try {
      const img = await createImageBitmap(f);
      const s = Math.min(img.width, img.height), size = Math.min(800, s);
      const cv = document.createElement("canvas"); cv.width = cv.height = size;
      cv.getContext("2d").drawImage(img, (img.width - s) / 2, (img.height - s) / 2, s, s, 0, 0, size, size);
      const blob = await new Promise((r) => cv.toBlob(r, "image/jpeg", 0.86));
      coverBytes = new Uint8Array(await blob.arrayBuffer());
      $("coverPreview").style.backgroundImage = `url('${URL.createObjectURL(blob)}')`;
    } catch (err) { alert("Rasmni o'qib bo'lmadi: " + err.message); }
  };
  $("pSave").onclick = async () => {
    const name = $("pName").value.trim();
    if (!name) return alert("Paket nomini kiriting");
    $("pSave").disabled = true;
    try {
      await loadMeta();
      let id = editingPack;
      if (!id) { id = slug(name); let n = 2; while (meta.packs[id]) id = slug(name) + "-" + n++; }
      const old = meta.packs[id] || {};
      let cover = old.cover;
      if (coverBytes) {
        cover = `packs/${id}/cover-${Date.now()}.jpg`;
        await putFileRobust("library/" + cover, coverBytes);
        if (old.cover) deletePath("library/" + old.cover, "Eski muqova").catch(() => {});
      }
      const order = old.order ?? (Math.max(0, ...Object.values(meta.packs).map((p) => p.order || 0)) + 1);
      const pack = { ...old, name, genre: $("pGenre").value.trim(), description: $("pDesc").value.trim(), free: $("pFree").checked,
        order, created: old.created || new Date().toISOString().slice(0, 10), ...(cover ? { cover } : {}) };
      await updateMeta((editingPack ? "Paket yangilandi: " : "Yangi paket: ") + name, (m) => { m.packs[id] = pack; });
      resetPackForm(); renderPacks(); refreshPackSelect();
      alert("Paket saqlandi");
    } catch (e) { alert("Xato: " + e.message); }
    $("pSave").disabled = false;
  };
  async function deletePack(id) {
    const p = meta.packs[id];
    if (!confirm(`"${p.name}" paketi o'chirilsinmi? Ichidagi fayllar o'chmaydi, "Paketsiz" bo'limiga o'tadi.`)) return;
    try {
      if (p.cover) await deletePath("library/" + p.cover, "Paket muqovasi o'chirildi").catch(() => {});
      await updateMeta("Paket o'chirildi: " + p.name, (m) => {
        delete m.packs[id];
        for (const e of Object.values(m.items)) if (e.pack === id) delete e.pack;
      });
      renderPacks(); refreshPackSelect();
    } catch (e) { alert(e.message); }
  }

  // ============================================================ KEYS ======
  const kp = () => store.get("privateJwk", null);
  function renderKeys() {
    const has = !!kp();
    $("noKey").hidden = has;
    $("issue").disabled = !has;
    $("kpState").innerHTML = has ? '<span class="ok">Maxfiy kalit shu brauzerda bor.</span>' : '<span class="err">Kalitlar juftligi yo\'q.</span>';
    const issued = store.get("issued", []);
    $("issuedBody").innerHTML = issued.slice().reverse().map((k, i) =>
      `<tr><td class="mono">${esc(k.i)}</td><td>${esc(k.u)}</td><td>${esc(k.t.toUpperCase())}</td><td class="mono">${esc(k.e)} gacha</td><td><button class="btn sm" data-copy="${issued.length - 1 - i}">Nusxa</button></td></tr>`).join("")
      || `<tr><td colspan="5" class="sub">Hali kalit berilmagan</td></tr>`;
    $("issuedBody").querySelectorAll("[data-copy]").forEach((b) => b.onclick = () => navigator.clipboard.writeText(issued[+b.dataset.copy].key));
  }
  function downloadBackup() {
    const j = kp(); if (!j) return alert("Kalit yo'q");
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([JSON.stringify(j)], { type: "application/json" }));
    a.download = "AZ-BETAS-private-key-BACKUP.json"; a.click();
  }
  $("genKp").onclick = async () => {
    if (kp() && !confirm("Yangi juftlik yaratilsa ESKI kalitlar ishlamay qoladi (plagin yangilangach). Davom etasizmi?")) return;
    try {
      const pair = await AZLicense.generateKeyPair();
      store.set("privateJwk", pair.privateJwk);
      downloadBackup();
      await putText("config/license_public_key.txt", pair.publicKeyText + "\n", "Litsenziya ochiq kaliti");
      alert("Tayyor! Ochiq kalit repoga yozildi — GitHub plaginni 10-15 daqiqada qayta yig'adi. Zaxira fayli yuklab olindi.");
      renderKeys();
    } catch (e) { alert("Xato: " + e.message); }
  };
  $("backupKp").onclick = downloadBackup;
  $("restoreKp").onchange = async (e) => {
    const f = e.target.files[0]; if (!f) return;
    try { const j = JSON.parse(await f.text()); if (!j.d || !j.n) throw new Error("Bu maxfiy kalit fayli emas"); store.set("privateJwk", j); renderKeys(); alert("Tiklandi"); }
    catch (err) { alert(err.message); }
  };
  const addDays = (d) => { const t = new Date(); t.setDate(t.getDate() + d); return t.toISOString().slice(0, 10); };
  let lastKey = null;
  $("issue").onclick = async () => {
    const u = $("kUser").value.trim();
    if (!u) return alert("Foydalanuvchini kiriting");
    const payload = { u, t: $("kTier").value, e: addDays(parseInt($("kDays").value, 10)), i: new Date().toISOString().slice(0, 10) };
    const key = await AZLicense.issueKey(kp(), payload);
    lastKey = { ...payload, key };
    const issued = store.get("issued", []); issued.push(lastKey); store.set("issued", issued);
    $("keyOut").value = key; $("keyBox").hidden = false;
    renderKeys();
  };
  $("copyKey").onclick = () => navigator.clipboard.writeText($("keyOut").value);
  $("copyMsg").onclick = () => {
    if (!lastKey) return;
    navigator.clipboard.writeText(`Assalomu alaykum! AZ BETAS ${lastKey.t.toUpperCase()} obunangiz faollashtirildi (${lastKey.e} gacha).\n\n` +
      `Plaginda "Akkaunt" bo'limiga quyidagi kalitni qo'ying va "Faollashtirish"ni bosing:\n\n${lastKey.key}\n\nAzimxodjayev Music`);
  };
  $("exportCsv").onclick = () => {
    const rows = [["sana", "foydalanuvchi", "tarif", "muddat", "kalit"], ...store.get("issued", []).map((k) => [k.i, k.u, k.t, k.e, k.key])];
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([rows.map((r) => r.map((c) => `"${String(c).replace(/"/g, '""')}"`).join(",")).join("\n")], { type: "text/csv" }));
    a.download = "az-betas-kalitlar.csv"; a.click();
  };

  // ------------------------------------------------------- diagnostics ----
  let diagText = "";
  $("diagRun").onclick = async () => {
    const c = cfg();
    const body = $("diagBody"); body.innerHTML = ""; diagText = `AZ BETAS diagnostika ${new Date().toISOString()}\n${navigator.userAgent}\n`;
    $("diagRun").disabled = true; $("diagCopy").disabled = true;
    const row = (i, name) => {
      const tr = document.createElement("tr");
      tr.innerHTML = `<td>${i}</td><td>${esc(name)}</td><td>...</td><td></td>`;
      body.appendChild(tr); return tr;
    };
    const run = async (i, name, fn) => {
      const tr = row(i, name), t0 = performance.now();
      let ok = false, msg;
      try { msg = await fn(); ok = true; }
      catch (e) { msg = (e.name || "Error") + ": " + e.message; }
      const ms = Math.round(performance.now() - t0);
      tr.children[2].innerHTML = ok ? `<span class="ok">OK</span> ${esc(msg || "")}` : `<span class="err">XATO</span> ${esc(msg)}`;
      tr.children[3].textContent = ms + " ms";
      diagText += `${i}. ${name}: ${ok ? "OK" : "XATO"} ${msg || ""} (${ms} ms)\n`;
      return ok;
    };
    const auth = { Authorization: "Bearer " + c.token, Accept: "application/vnd.github+json" };
    const testPut = async (size) => {
      const path = `library/.diag/test-${size}-${Date.now()}.bin`;
      const bytes = new Uint8Array(size); for (let i = 0; i < size; i++) bytes[i] = (i * 31) & 255;
      await putBinary(path, bytes);
      await deletePath(path, "diagnostika").catch(() => {});
      return `${(size / 1024).toFixed(0)} KB yuborildi`;
    };
    await run(1, "Internet (api.github.com, tokensiz)", async () => { const r = await fetch("https://api.github.com/zen"); return "HTTP " + r.status; });
    await run(2, "Token va repo", async () => {
      if (!c.token) throw new Error("token kiritilmagan");
      const r = await fetch(`https://api.github.com/repos/${c.repo}`, { headers: auth });
      const j = await r.json();
      if (!r.ok) throw new Error(r.status + " " + j.message);
      return `${j.full_name}, yozish: ${j.permissions && j.permissions.push ? "bor" : "YO'Q"}`;
    });
    await run(3, "O'qish (meta.json)", async () => { const f = await gh("GET", "library/meta.json"); return f ? "o'qildi" : "fayl yo'q"; });
    await run(4, "Yozish: 1 KB", () => testPut(1024));
    await run(5, "Yozish: 256 KB", () => testPut(256 * 1024));
    await run(6, "Yozish: 2 MB", () => testPut(2 * 1024 * 1024));
    $("diagRun").disabled = false; $("diagCopy").disabled = false;
  };
  $("diagCopy").onclick = () => navigator.clipboard.writeText(diagText);

  // ------------------------------------------------------------- init ----
  if (store.get("token", "")) loadMeta().then(refreshPackSelect).catch(() => {});
  refreshPackSelect();
  renderKeys();
})();
