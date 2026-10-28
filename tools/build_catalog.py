#!/usr/bin/env python3
"""
AZ BETAS - library catalog builder.

Scans library/files/<Category>/<file>, converts WAV/AIFF to FLAC (smaller,
lossless), detects BPM / key / bars for new files and writes:
  library/meta.json     - admin-editable data (name, genre, free, demo, bpm, key ...)
  library/catalog.json  - what the plug-in downloads

Admin flags live in meta.json and are never overwritten by detection.
Run:  python tools/build_catalog.py            (needs: numpy soundfile librosa)
"""
import json, os, re, sys, datetime, unicodedata

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LIB = os.path.join(ROOT, "library")
FILES = os.path.join(LIB, "files")
META = os.path.join(LIB, "meta.json")
CATALOG = os.path.join(LIB, "catalog.json")
AUDIO_EXT = (".flac", ".wav", ".aif", ".aiff", ".mp3", ".ogg")

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLATS = {"Db": "C#", "Eb": "D#", "Gb": "F#", "Ab": "G#", "Bb": "A#"}


def slug(text):
    t = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    t = re.sub(r"[^a-zA-Z0-9]+", "-", t).strip("-").lower()
    return t or "item"


def parse_name(stem):
    """BPM / key written in the file name, e.g. 'Az - Doira 104 Am'."""
    bpm, key = None, None
    m = re.search(r"(?<!\d)(\d{2,3})\s*(?:bpm)?(?!\d)", stem, re.I)
    if m and 60 <= int(m.group(1)) <= 200:
        bpm = float(m.group(1))
    k = re.search(r"(?:^|[\s_\-(])([A-G](?:#|b)?)(m|min|maj)?(?=$|[\s_\-)])", stem)
    if k:
        root = FLATS.get(k.group(1), k.group(1))
        key = root + ("m" if k.group(2) in ("m", "min") else "")
    return bpm, key


def detect(path):
    import numpy as np, librosa
    y, sr = librosa.load(path, sr=22050, mono=True)
    dur = len(y) / sr
    res = {"duration": round(dur, 3)}
    # key (Krumhansl-Schmuckler)
    try:
        chroma = librosa.feature.chroma_cqt(y=librosa.effects.harmonic(y), sr=sr).mean(axis=1)
        maj = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
        mnr = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
        best = max([(np.corrcoef(chroma, np.roll(maj, i))[0, 1], NOTES[i]) for i in range(12)] +
                   [(np.corrcoef(chroma, np.roll(mnr, i))[0, 1], NOTES[i] + "m") for i in range(12)])
        res["key"] = best[1] if best[0] > 0.55 else ""
    except Exception:
        res["key"] = ""
    if dur < 1.5:
        res["bpm"], res["bars"] = 0, 0          # one-shot
        return res
    tempo = float(np.atleast_1d(librosa.feature.tempo(y=y, sr=sr))[0])
    # a loop is a whole number of bars: pick the bar count whose tempo best
    # matches the detected tempo (allowing half/double/triplet confusions)
    variants = [tempo, tempo / 2, tempo * 2, tempo * 2 / 3, tempo * 3 / 2, tempo * 4 / 3, tempo * 3 / 4]
    best = None
    for bars in (1, 2, 4, 8, 16, 32):
        bpm = bars * 4 * 60.0 / dur
        if not 50 <= bpm <= 200:
            continue
        err = min(abs(np.log2(bpm / v)) for v in variants if v > 0)
        if not 70 <= bpm <= 170:
            err += 0.05   # typical loop tempos first
        if best is None or err < best[0]:
            best = (err, bpm, bars)
    if best and best[0] < 0.08:
        res["bpm"], res["bars"] = round(best[1], 2), best[2]
    else:
        res["bpm"], res["bars"] = round(tempo, 2), 0
    return res


def loudness(path):
    """Integrated LUFS (BS.1770, same filters as the plug-in) and true peak."""
    import numpy as np, soundfile as sf
    from scipy.signal import lfilter, resample_poly
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x[:, :2] if x.shape[1] > 1 else np.repeat(x, 2, axis=1)
    def hs():
        A = 10 ** (4 / 40); w = 2 * np.pi * 1500 / sr; c = np.cos(w); al = np.sin(w) / (2 * np.sqrt(0.5)); sA = 2 * np.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA)]
        a = [(A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA]
        return b, a
    def hp():
        w = 2 * np.pi * 38 / sr; c = np.cos(w); al = np.sin(w) / (2 * 0.5)
        return [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al]
    y = x.copy()
    for b, a in (hs(), hp()):
        y = lfilter(b, a, y, axis=0)
    blk, stp = int(0.4 * sr), int(0.1 * sr)
    if len(y) < blk:
        z = [np.mean(np.sum(y ** 2, axis=1))]
    else:
        z = [np.mean(np.sum(y[p:p + blk] ** 2, axis=1)) for p in range(0, len(y) - blk + 1, stp)]
    z = np.array(z); L = -0.691 + 10 * np.log10(np.maximum(z, 1e-12))
    g = z[L > -70]
    if len(g) == 0:
        lufs = -70.0
    else:
        rel = -0.691 + 10 * np.log10(g.mean()) - 10
        g2 = z[(L > -70) & (L > rel)]
        lufs = float(-0.691 + 10 * np.log10(g2.mean())) if len(g2) else -70.0
    up = resample_poly(x, 4, 1, axis=0) if len(x) * 2 < 2e7 else x
    tp = float(20 * np.log10(max(np.abs(up).max(), 1e-6)))
    return round(lufs, 1), round(tp, 1)


def assemble_parts():
    """Admin panel uploads big files in parts (<file>.part-001-of-004 ...);
    join complete sets back into the real file."""
    sets = {}
    for dirpath, _, names in os.walk(FILES):
        for name in names:
            m = re.match(r"^(.*)\.part-(\d+)-of-(\d+)$", name)
            if m:
                sets.setdefault((dirpath, m.group(1), int(m.group(3))), {})[int(m.group(2))] = os.path.join(dirpath, name)
    for (dirpath, base, total), parts in sets.items():
        if len(parts) != total or set(parts) != set(range(1, total + 1)):
            print("Bo'laklar hali to'liq emas:", base, f"{len(parts)}/{total}")
            continue
        target = os.path.join(dirpath, base)
        with open(target, "wb") as out:
            for i in range(1, total + 1):
                with open(parts[i], "rb") as f:
                    out.write(f.read())
        for p in parts.values():
            os.remove(p)
        # remove stale incomplete sets of the same file
        for (d2, b2, t2), p2 in sets.items():
            if d2 == dirpath and b2 == base and t2 != total:
                for q in p2.values():
                    if os.path.exists(q):
                        os.remove(q)
        print("Bo'laklardan yig'ildi:", os.path.relpath(target, LIB))


def to_flac(path):
    import soundfile as sf
    base, ext = os.path.splitext(path)
    if ext.lower() not in (".wav", ".aif", ".aiff"):
        return path
    data, sr = sf.read(path, always_2d=True)
    info = sf.info(path)
    subtype = "PCM_24" if "24" in info.subtype or "FLOAT" in info.subtype or "32" in info.subtype else "PCM_16"
    out = base + ".flac"
    sf.write(out, data, sr, subtype=subtype)
    os.remove(path)
    print("  -> FLAC:", os.path.relpath(out, LIB))
    return out


def main():
    meta = {"freeLimit": 10, "items": {}}
    if os.path.exists(META):
        with open(META, encoding="utf-8") as f:
            meta = json.load(f)
    meta.setdefault("freeLimit", 10)
    items = meta.setdefault("items", {})
    today = datetime.date.today().isoformat()

    assemble_parts()
    found = set()
    for dirpath, _, names in os.walk(FILES):
        for name in sorted(names):
            if not name.lower().endswith(AUDIO_EXT):
                continue
            path = os.path.join(dirpath, name)
            path = to_flac(path)
            rel = os.path.relpath(path, LIB).replace(os.sep, "/")
            found.add(rel)
            # meta may still be keyed by the original wav name
            old = None
            for k in list(items.keys()):
                if os.path.splitext(k)[0] == os.path.splitext(rel)[0] and k != rel:
                    old = items.pop(k)
            entry = items.setdefault(rel, old or {})
            stem = os.path.splitext(os.path.basename(rel))[0]
            category = os.path.relpath(dirpath, FILES).replace(os.sep, "/")
            entry.setdefault("name", stem)
            entry.setdefault("category", "" if category == "." else category)
            entry.setdefault("genre", "")
            entry.setdefault("tags", [])
            entry.setdefault("free", False)
            entry.setdefault("demo", False)
            entry.setdefault("added", today)
            if "bpm" not in entry or "key" not in entry or "duration" not in entry:
                print("Tahlil:", rel)
                d = detect(path)
                nb, nk = parse_name(stem)
                entry.setdefault("bpm", nb if nb else d["bpm"])
                entry.setdefault("key", nk if nk else d["key"])
                if "bars" not in entry:
                    bpm = entry["bpm"]
                    bars = round(d["duration"] * bpm / 240.0) if bpm else 0
                    exact = bpm and abs(d["duration"] * bpm / 240.0 - bars) < 0.05
                    entry["bars"] = int(bars) if exact else d.get("bars", 0)
                entry["duration"] = d["duration"]
            if "lufs" not in entry:
                try:
                    entry["lufs"], entry["tp"] = loudness(path)
                except Exception as e:
                    print("LUFS o'lchanmadi:", e)

    # drop entries whose file was deleted
    for k in [k for k in items if k not in found]:
        print("O'chirildi:", k)
        del items[k]

    out = []
    used = set()
    for rel, e in sorted(items.items(), key=lambda kv: (kv[1].get("added", ""), kv[0]), reverse=True):
        base = slug(e.get("name") or os.path.basename(rel))
        iid, n = base, 2
        while iid in used:
            iid, n = f"{base}-{n}", n + 1
        used.add(iid)
        out.append({
            "id": iid, "name": e.get("name"), "file": rel,
            "category": e.get("category", ""), "genre": e.get("genre", ""),
            "tags": e.get("tags", []), "bpm": e.get("bpm", 0), "bars": e.get("bars", 0),
            "key": e.get("key", ""), "free": bool(e.get("free")), "demo": bool(e.get("demo")),
            "added": e.get("added", today), "size": os.path.getsize(os.path.join(LIB, rel)),
            "duration": e.get("duration", 0),
            "lufs": e.get("lufs"), "tp": e.get("tp"),
        })

    with open(META, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    catalog = {"version": 1, "updated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
               "freeLimit": int(meta.get("freeLimit", 10)), "items": out}
    with open(CATALOG, "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=1)
    print(f"catalog.json: {len(out)} ta")


if __name__ == "__main__":
    sys.exit(main())
