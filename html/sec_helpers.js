const pbkdf_iterations = 100000;
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
    networkHmacKey: null,
    networkAesKey: null,
    sessionAesKey: null,
    nonceCounter: 0,
    securityReady: false,
};

let current_security_level = null;
let cached_webcrypto_provider = null;
let webcrypto_shim_load_promise = null;

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

async function derive_pbkdf2_bytes(secret, salt)
{
    const subtle = await require_webcrypto();
    const base_key = await subtle.importKey("raw", secret, "PBKDF2", false, ["deriveBits"]);
    const bits = await subtle.deriveBits(
        {
            name: "PBKDF2",
            salt: salt,
            iterations: pbkdf_iterations,
            hash: "SHA-256",
        },
        base_key,
        sha256_size * 8);
    return new Uint8Array(bits);
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
    session_security.networkHmacKey = null;
    session_security.networkAesKey = null;
    session_security.sessionAesKey = null;
    session_security.nonceCounter = 0;
    session_security.securityReady = Boolean(info.security_ready);
}
