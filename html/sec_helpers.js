const pbkdf_iterations = 100000;
const session_pbkdf_iterations = 100;
const session_salt_half_size = 16;
const sha256_size = 32;
const filecrypt_salt = new Uint8Array([
    0x98, 0xC2, 0x5A, 0xF2, 0xB7, 0x0F, 0xA4, 0xB3,
    0x42, 0xB4, 0x64, 0xE5, 0xEE, 0xD6, 0xFF, 0x3D,
    0x0D, 0xD8, 0x21, 0x9C, 0x9D, 0x7B, 0x16, 0xB4,
    0xCE, 0xDE, 0xCF, 0xFA, 0xCA, 0x4E, 0xF3, 0x2F,
]);
const network_salt = new Uint8Array([
    0x36, 0x22, 0x5C, 0x86, 0xC1, 0x70, 0xF2, 0xC1,
    0x11, 0xD3, 0xD6, 0xDF, 0x57, 0x6E, 0x28, 0x93,
    0x71, 0x4E, 0x52, 0x6C, 0xD8, 0x43, 0xB2, 0x6B,
    0xDF, 0xEE, 0x1F, 0x12, 0xC9, 0xA3, 0xE5, 0xB2,
]);
const header_session_salt_from_client = "X-TheFly-Session-Salt-From-Client";
const header_session_response_from_client = "X-TheFly-Session-Response-From-Client";

const session_security = {
    sessionChallenge: null,
    sessionResponseFromServer: null,
    sessionResponseFromClientHex: "",
    sessionSaltFromServer: null,
    sessionSaltFromClient: null,
    sessionAesKey: null,
    nonceCounter: 0,
    securityReady: false,
};

let current_security_level = null;
let cached_webcrypto_provider = null;
let webcrypto_shim_load_promise = null;

const sha256_k = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

const sha256_initial_state = [
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
];

function bytes_view(data)
{
    if (data instanceof Uint8Array)
    {
        return data;
    }
    if (ArrayBuffer.isView(data))
    {
        return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    return new Uint8Array(data);
}

function concat_bytes(first, second)
{
    const first_bytes = bytes_view(first);
    const second_bytes = bytes_view(second);
    const result = new Uint8Array(first_bytes.length + second_bytes.length);
    result.set(first_bytes);
    result.set(second_bytes, first_bytes.length);
    return result;
}

function sha256_rotr(value, bits)
{
    return (value >>> bits) | (value << (32 - bits));
}

function sha256_bytes(data)
{
    const bytes = bytes_view(data);
    const padded_length = Math.ceil((bytes.length + 9) / 64) * 64;
    const padded = new Uint8Array(padded_length);
    padded.set(bytes);
    padded[bytes.length] = 0x80;

    const view = new DataView(padded.buffer);
    const bit_length = bytes.length * 8;
    view.setUint32(padded_length - 8, Math.floor(bit_length / 0x100000000), false);
    view.setUint32(padded_length - 4, bit_length >>> 0, false);

    const hash = sha256_initial_state.slice();
    const words = new Uint32Array(64);

    for (let chunk = 0; chunk < padded_length; chunk += 64)
    {
        for (let i = 0; i < 16; ++i)
        {
            words[i] = view.getUint32(chunk + (i * 4), false);
        }
        for (let i = 16; i < 64; ++i)
        {
            const s0 = sha256_rotr(words[i - 15], 7) ^ sha256_rotr(words[i - 15], 18) ^ (words[i - 15] >>> 3);
            const s1 = sha256_rotr(words[i - 2], 17) ^ sha256_rotr(words[i - 2], 19) ^ (words[i - 2] >>> 10);
            words[i] = (words[i - 16] + s0 + words[i - 7] + s1) >>> 0;
        }

        let a = hash[0];
        let b = hash[1];
        let c = hash[2];
        let d = hash[3];
        let e = hash[4];
        let f = hash[5];
        let g = hash[6];
        let h = hash[7];

        for (let i = 0; i < 64; ++i)
        {
            const s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
            const ch = (e & f) ^ (~e & g);
            const temp1 = (h + s1 + ch + sha256_k[i] + words[i]) >>> 0;
            const s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
            const maj = (a & b) ^ (a & c) ^ (b & c);
            const temp2 = (s0 + maj) >>> 0;

            h = g;
            g = f;
            f = e;
            e = (d + temp1) >>> 0;
            d = c;
            c = b;
            b = a;
            a = (temp1 + temp2) >>> 0;
        }

        hash[0] = (hash[0] + a) >>> 0;
        hash[1] = (hash[1] + b) >>> 0;
        hash[2] = (hash[2] + c) >>> 0;
        hash[3] = (hash[3] + d) >>> 0;
        hash[4] = (hash[4] + e) >>> 0;
        hash[5] = (hash[5] + f) >>> 0;
        hash[6] = (hash[6] + g) >>> 0;
        hash[7] = (hash[7] + h) >>> 0;
    }

    const out = new Uint8Array(sha256_size);
    const out_view = new DataView(out.buffer);
    for (let i = 0; i < hash.length; ++i)
    {
        out_view.setUint32(i * 4, hash[i], false);
    }
    return out;
}

function make_hmac_sha256(key)
{
    let key_bytes = bytes_view(key);
    if (key_bytes.length > 64)
    {
        key_bytes = sha256_bytes(key_bytes);
    }

    const inner_pad = new Uint8Array(64);
    const outer_pad = new Uint8Array(64);
    for (let i = 0; i < 64; ++i)
    {
        const key_byte = i < key_bytes.length ? key_bytes[i] : 0;
        inner_pad[i] = key_byte ^ 0x36;
        outer_pad[i] = key_byte ^ 0x5c;
    }

    return function(data) {
        const inner_hash = sha256_bytes(concat_bytes(inner_pad, data));
        return sha256_bytes(concat_bytes(outer_pad, inner_hash));
    };
}

function pbkdf2_hmac_sha256_fallback(secret, salt, iterations, out_size)
{
    if (iterations <= 0 || out_size <= 0)
    {
        throw new Error("Invalid PBKDF2 parameters");
    }

    const hmac = make_hmac_sha256(secret);
    const salt_bytes = bytes_view(salt);
    const block_count = Math.ceil(out_size / sha256_size);
    const out = new Uint8Array(out_size);
    let out_offset = 0;

    for (let block = 1; block <= block_count; ++block)
    {
        const block_salt = new Uint8Array(salt_bytes.length + 4);
        block_salt.set(salt_bytes);
        new DataView(block_salt.buffer).setUint32(salt_bytes.length, block, false);

        let u = hmac(block_salt);
        const t = new Uint8Array(u);
        for (let i = 1; i < iterations; ++i)
        {
            u = hmac(u);
            for (let j = 0; j < t.length; ++j)
            {
                t[j] ^= u[j];
            }
        }

        const copy_size = Math.min(t.length, out_size - out_offset);
        out.set(t.subarray(0, copy_size), out_offset);
        out_offset += copy_size;
    }

    return out;
}

function secure_random_bytes(size)
{
    const crypto = random_provider();
    if (!crypto || !crypto.getRandomValues)
    {
        throw new Error("Browser random number generator is unavailable");
    }

    const bytes = new Uint8Array(size);
    crypto.getRandomValues(bytes);
    return bytes;
}

function webcrypto_shim_provider()
{
    if (window.deviceCrypto && window.deviceCrypto.subtle)
    {
        return window.deviceCrypto;
    }

    if (window.deviceCrypto && window.deviceSubtle)
    {
        return {
            subtle: window.deviceSubtle,
            getRandomValues: function(buffer) {
                return window.deviceCrypto.getRandomValues(buffer);
            },
        };
    }

    if (window.deviceSubtle)
    {
        return {
            subtle: window.deviceSubtle,
            getRandomValues: function(buffer) {
                if (window.crypto && window.crypto.getRandomValues)
                {
                    return window.crypto.getRandomValues(buffer);
                }
                throw new Error("Browser random number generator is unavailable");
            },
        };
    }

    if (window.WebCryptoBundle)
    {
        const bundle_crypto = window.WebCryptoBundle.crypto || window.WebCryptoBundle;
        if (bundle_crypto && bundle_crypto.subtle)
        {
            return bundle_crypto;
        }
    }

    return null;
}

function random_provider()
{
    if (window.crypto && window.crypto.getRandomValues)
    {
        return window.crypto;
    }

    const shim = webcrypto_shim_provider();
    if (shim && shim.getRandomValues)
    {
        return shim;
    }
    return null;
}

function webcrypto_provider()
{
    if (cached_webcrypto_provider && cached_webcrypto_provider.subtle)
    {
        return cached_webcrypto_provider;
    }
    if (window.crypto && window.crypto.subtle)
    {
        cached_webcrypto_provider = window.crypto;
        return cached_webcrypto_provider;
    }

    const shim = webcrypto_shim_provider();
    if (shim && shim.subtle)
    {
        cached_webcrypto_provider = shim;
        return cached_webcrypto_provider;
    }
    return null;
}

function load_webcrypto_shim()
{
    if (webcrypto_provider())
    {
        return Promise.resolve();
    }
    if (webcrypto_shim_load_promise)
    {
        return webcrypto_shim_load_promise;
    }

    webcrypto_shim_load_promise = new Promise((resolve, reject) => {
        const script = document.createElement("script");
        script.src = "webcrypto-shim.min.js";
        script.async = true;
        script.onload = () => resolve();
        script.onerror = () => reject(new Error("Could not load webcrypto-shim.min.js"));
        document.head.appendChild(script);
    });
    return webcrypto_shim_load_promise;
}

async function require_webcrypto()
{
    let crypto = webcrypto_provider();
    if (!crypto || !crypto.subtle)
    {
        await load_webcrypto_shim();
        crypto = webcrypto_provider();
    }
    if (!crypto || !crypto.subtle)
    {
        throw new Error("WebCrypto is unavailable; webcrypto-shim.min.js did not provide a usable fallback.");
    }
    return crypto.subtle;
}

async function derive_pbkdf2_bytes(secret, salt, iterations)
{
    const rounds = iterations || pbkdf_iterations;
    try
    {
        const subtle = await require_webcrypto();
        const base_key = await subtle.importKey("raw", secret, "PBKDF2", false, ["deriveBits"]);
        const bits = await subtle.deriveBits(
            {
                name: "PBKDF2",
                salt: salt,
                iterations: rounds,
                hash: "SHA-256",
            },
            base_key,
            sha256_size * 8);
        return new Uint8Array(bits);
    }
    catch (error)
    {
        console.warn("WebCrypto PBKDF2 failed; using JavaScript fallback.", error);
        return pbkdf2_hmac_sha256_fallback(secret, salt, rounds, sha256_size);
    }
}

async function import_hmac_key(key_bytes)
{
    return (await require_webcrypto()).importKey(
        "raw",
        key_bytes,
        { name: "HMAC", hash: "SHA-256" },
        false,
        ["sign"]);
}

async function import_aes_key(key_bytes, usages)
{
    return (await require_webcrypto()).importKey(
        "raw",
        key_bytes,
        { name: "AES-GCM" },
        false,
        usages || ["decrypt"]);
}

async function hmac_sha256(key, data)
{
    const signature = await (await require_webcrypto()).sign("HMAC", key, data);
    return new Uint8Array(signature);
}

function next_session_nonce()
{
    const nonce = new Uint8Array(12);
    let counter = session_security.nonceCounter++;
    for (let i = 0; i < 8; ++i)
    {
        nonce[11 - i] = counter & 0xFF;
        counter = Math.floor(counter / 256);
    }
    return nonce;
}

function remember_session_info(info)
{
    current_security_level = Number(info["security-level"]);
    if (!Number.isFinite(current_security_level))
    {
        current_security_level = null;
    }
    session_security.sessionChallenge = hex_to_bytes(info.session_challenge || "", 32);
    session_security.sessionResponseFromServer = hex_to_bytes(info.session_response_from_server || "", sha256_size);
    session_security.sessionSaltFromServer = hex_to_bytes(info.session_salt_from_server || "", session_salt_half_size);
    session_security.sessionResponseFromClientHex = "";
    session_security.sessionAesKey = null;
    session_security.nonceCounter = 0;
    session_security.securityReady = Boolean(info.security_ready);
}
