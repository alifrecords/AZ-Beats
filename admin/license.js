// AZ BETAS licence keys (browser + node).
// Key:  AZB1.<base64url(payload)>.<hex(signature)>
// signature = h^d mod n, h = SHA-256(payload) read as a LITTLE-endian integer
// (matches juce::BigInteger::loadFromMemoryBlock used by the plug-in).
(function (root) {
  const subtle = (root.crypto && root.crypto.subtle) || (typeof require !== "undefined" ? require("crypto").webcrypto.subtle : null);

  function b64urlToBytes(s) {
    s = s.replace(/-/g, "+").replace(/_/g, "/");
    while (s.length % 4) s += "=";
    const bin = typeof atob !== "undefined" ? atob(s) : Buffer.from(s, "base64").toString("binary");
    return Uint8Array.from(bin, (c) => c.charCodeAt(0));
  }
  function bytesToB64url(bytes) {
    let bin = "";
    bytes.forEach((b) => (bin += String.fromCharCode(b)));
    const b64 = typeof btoa !== "undefined" ? btoa(bin) : Buffer.from(bin, "binary").toString("base64");
    return b64.replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }
  function bytesToBigBE(bytes) {
    let x = 0n;
    for (const b of bytes) x = (x << 8n) | BigInt(b);
    return x;
  }
  function bytesToBigLE(bytes) {
    let x = 0n;
    for (let i = bytes.length - 1; i >= 0; i--) x = (x << 8n) | BigInt(bytes[i]);
    return x;
  }
  function modPow(b, e, m) {
    let r = 1n;
    b %= m;
    while (e > 0n) {
      if (e & 1n) r = (r * b) % m;
      e >>= 1n;
      b = (b * b) % m;
    }
    return r;
  }

  // New RSA-2048 key pair. Returns { privateJwk, publicKeyText } where
  // publicKeyText is "<e hex>,<n hex>" for config/license_public_key.txt
  async function generateKeyPair() {
    const kp = await subtle.generateKey(
      { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
      true, ["sign", "verify"]);
    const jwk = await subtle.exportKey("jwk", kp.privateKey);
    return { privateJwk: jwk, publicKeyText: publicTextFromJwk(jwk) };
  }

  function publicTextFromJwk(jwk) {
    const e = bytesToBigBE(b64urlToBytes(jwk.e));
    const n = bytesToBigBE(b64urlToBytes(jwk.n));
    return e.toString(16) + "," + n.toString(16);
  }

  async function sha256LE(bytes) {
    const h = new Uint8Array(await subtle.digest("SHA-256", bytes));
    return bytesToBigLE(h);
  }

  // payload: { u: user, t: "start"|"pro", e: "YYYY-MM-DD", i: "YYYY-MM-DD" }
  async function issueKey(privateJwk, payload) {
    const n = bytesToBigBE(b64urlToBytes(privateJwk.n));
    const d = bytesToBigBE(b64urlToBytes(privateJwk.d));
    const json = JSON.stringify(payload);
    const bytes = new TextEncoder().encode(json);
    const h = await sha256LE(bytes);
    const s = modPow(h, d, n);
    return "AZB1." + bytesToB64url(bytes) + "." + s.toString(16);
  }

  async function verifyKey(publicKeyText, key) {
    const [eh, nh] = publicKeyText.trim().split(",");
    const e = BigInt("0x" + eh), n = BigInt("0x" + nh);
    const parts = key.trim().split(".");
    if (parts.length !== 3 || parts[0] !== "AZB1") return { valid: false, error: "format" };
    const bytes = b64urlToBytes(parts[1]);
    const h = await sha256LE(bytes);
    const ok = modPow(BigInt("0x" + parts[2]), e, n) === h;
    const payload = JSON.parse(new TextDecoder().decode(bytes));
    return { valid: ok, payload };
  }

  root.AZLicense = { generateKeyPair, publicTextFromJwk, issueKey, verifyKey };
  if (typeof module !== "undefined") module.exports = root.AZLicense;
})(typeof window !== "undefined" ? window : globalThis);
