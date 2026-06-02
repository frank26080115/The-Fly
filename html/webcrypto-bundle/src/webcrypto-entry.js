import { crypto as linerCrypto } from "webcrypto-liner/build/index.es.js";

const browserWindow = typeof window !== "undefined" ? window : self;
const nativeCrypto = browserWindow.crypto;
const nativeSubtle = nativeCrypto && nativeCrypto.subtle;

if (!nativeSubtle)
{
    try
    {
        delete browserWindow.crypto;
        browserWindow.crypto = linerCrypto;
    }
    catch (error)
    {
    }
}

const selectedCrypto = browserWindow.crypto && browserWindow.crypto.subtle
    ? browserWindow.crypto
    : linerCrypto;

browserWindow.deviceCrypto = selectedCrypto;
browserWindow.deviceSubtle = selectedCrypto.subtle;
