const info_retry_ms = 1500;
const pbkdf_iterations = 100000;
const session_salt_half_size = 16;
const sha256_size = 32;
const get_cfg_magic = [0x54, 0x46, 0x47, 0x43];
const get_cfg_version = 2;
const set_cfg_magic = [0x54, 0x46, 0x47, 0x43];
const set_cfg_version = 1;
const header_session_salt_from_client = "X-TheFly-Session-Salt-From-Client";
const header_session_response_from_client = "X-TheFly-Session-Response-From-Client";
const network_salt = new Uint8Array([
    0x36, 0x22, 0x5C, 0x86, 0xC1, 0x70, 0xF2, 0xC1,
    0x11, 0xD3, 0xD6, 0xDF, 0x57, 0x6E, 0x28, 0x93,
    0x71, 0x4E, 0x52, 0x6C, 0xD8, 0x43, 0xB2, 0x6B,
    0xDF, 0xEE, 0x1F, 0x12, 0xC9, 0xA3, 0xE5, 0xB2,
]);

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

const timezone_option_groups = [
    {
        label: "United States and Canada",
        zones: [
            ["Pacific (PST/PDT/-8)", "PST8PDT,M3.2.0,M11.1.0"],
            ["Mountain (MST/MDT/-7)", "MST7MDT,M3.2.0,M11.1.0"],
            ["Mountain (MST/-7), no DST", "MST7"],
            ["Central (CST/CDT/-6)", "CST6CDT,M3.2.0,M11.1.0"],
            ["Eastern (EST/EDT/-5)", "EST5EDT,M3.2.0,M11.1.0"],
            ["Alaska (AKST/AKDT/-9)", "AKST9AKDT,M3.2.0,M11.1.0"],
            ["Hawaii (HST/-10)", "HST10"],
            ["Atlantic (AST/ADT/-4)", "AST4ADT,M3.2.0,M11.1.0"],
            ["Newfoundland (NST/NDT/-3:30)", "NST3:30NDT,M3.2.0,M11.1.0"],
        ],
    },
    {
        label: "UTC and fixed offsets",
        zones: [
            ["UTC (UTC/0)", "UTC0"],
            ["UTC-12:00 (UTC/-12)", "UTC12"],
            ["UTC-11:00 (UTC/-11)", "UTC11"],
            ["UTC-10:00 (HST/-10)", "HST10"],
            ["UTC-09:30 (UTC/-9:30)", "UTC9:30"],
            ["UTC-09:00 (AKST/-9)", "AKST9"],
            ["UTC-08:00 (PST/-8)", "PST8"],
            ["UTC-07:00 (MST/-7)", "MST7"],
            ["UTC-06:00 (CST/-6)", "CST6"],
            ["UTC-05:00 (EST/-5)", "EST5"],
            ["UTC-04:00 (AST/-4)", "AST4"],
            ["UTC-03:30 (NST/-3:30)", "NST3:30"],
            ["UTC-03:00 (BRT/-3)", "BRT3"],
            ["UTC-02:00 (UTC/-2)", "UTC2"],
            ["UTC-01:00 (UTC/-1)", "UTC1"],
            ["UTC+01:00 (CET/+1)", "CET-1"],
            ["UTC+02:00 (EET/+2)", "EET-2"],
            ["UTC+03:00 (MSK/+3)", "MSK-3"],
            ["UTC+03:30 (IRST/+3:30)", "IRST-3:30"],
            ["UTC+04:00 (GST/+4)", "GST-4"],
            ["UTC+04:30 (AFT/+4:30)", "AFT-4:30"],
            ["UTC+05:00 (PKT/+5)", "PKT-5"],
            ["UTC+05:30 (IST/+5:30)", "IST-5:30"],
            ["UTC+05:45 (NPT/+5:45)", "NPT-5:45"],
            ["UTC+06:00 (BST/+6)", "BST-6"],
            ["UTC+06:30 (MMT/+6:30)", "MMT-6:30"],
            ["UTC+07:00 (ICT/+7)", "ICT-7"],
            ["UTC+08:00 (CST/+8)", "CST-8"],
            ["UTC+08:45 (ACWST/+8:45)", "ACWST-8:45"],
            ["UTC+09:00 (JST/+9)", "JST-9"],
            ["UTC+09:30 (ACST/+9:30)", "ACST-9:30"],
            ["UTC+10:00 (AEST/+10)", "AEST-10"],
            ["UTC+10:30 (LHST/+10:30)", "LHST-10:30"],
            ["UTC+11:00 (SBT/+11)", "SBT-11"],
            ["UTC+12:00 (NZST/+12)", "NZST-12"],
            ["UTC+12:45 (CHAST/+12:45)", "CHAST-12:45"],
            ["UTC+13:00 (NZDT/+13)", "NZDT-13"],
            ["UTC+14:00 (LINT/+14)", "LINT-14"],
        ],
    },
    {
        label: "Europe",
        zones: [
            ["UK, Ireland, Portugal (GMT/BST/0)", "GMT0BST,M3.5.0/1,M10.5.0"],
            ["Western Europe (WET/WEST/0)", "WET0WEST,M3.5.0/1,M10.5.0"],
            ["Central Europe (CET/CEST/+1)", "CET-1CEST,M3.5.0,M10.5.0/3"],
            ["Eastern Europe (EET/EEST/+2)", "EET-2EEST,M3.5.0/3,M10.5.0/4"],
            ["Moscow (MSK/+3), no DST", "MSK-3"],
        ],
    },
    {
        label: "Asia",
        zones: [
            ["Dubai / Gulf (GST/+4)", "GST-4"],
            ["Afghanistan (AFT/+4:30)", "AFT-4:30"],
            ["Pakistan (PKT/+5)", "PKT-5"],
            ["India / Sri Lanka (IST/+5:30)", "IST-5:30"],
            ["Nepal (NPT/+5:45)", "NPT-5:45"],
            ["Bangladesh (BST/+6)", "BST-6"],
            ["Myanmar (MMT/+6:30)", "MMT-6:30"],
            ["Thailand / Vietnam / West Indonesia (ICT/+7)", "ICT-7"],
            ["China / Singapore / Taiwan / Philippines (CST/+8)", "CST-8"],
            ["Japan (JST/+9)", "JST-9"],
            ["Korea (KST/+9)", "KST-9"],
        ],
    },
    {
        label: "Australia, New Zealand, and Pacific",
        zones: [
            ["Western Australia (AWST/+8)", "AWST-8"],
            ["South Australia (ACST/ACDT/+9:30)", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
            ["Northern Territory (ACST/+9:30)", "ACST-9:30"],
            ["Queensland (AEST/+10)", "AEST-10"],
            ["NSW / Victoria / Tasmania (AEST/AEDT/+10)", "AEST-10AEDT,M10.1.0,M4.1.0/3"],
            ["Lord Howe Island (LHST/LHDT/+10:30)", "LHST-10:30LHDT-11,M10.1.0,M4.1.0"],
            ["New Zealand (NZST/NZDT/+12)", "NZST-12NZDT,M9.5.0,M4.1.0/3"],
            ["Chatham Islands (CHAST/CHADT/+12:45)", "CHAST-12:45CHADT,M9.5.0/2:45,M4.1.0/3:45"],
        ],
    },
    {
        label: "Latin America",
        zones: [
            ["Mexico Pacific (MST/MDT/-7)", "MST7MDT,M4.1.0,M10.5.0"],
            ["Mexico Central (CST/-6), no DST", "CST6"],
            ["Colombia / Peru / Ecuador (COT/-5)", "COT5"],
            ["Venezuela (VET/-4)", "VET4"],
            ["Bolivia (BOT/-4)", "BOT4"],
            ["Chile continental (CLT/CLST/-4)", "CLT4CLST,M9.1.6/24,M4.1.6/24"],
            ["Argentina (ART/-3)", "ART3"],
            ["Brazil, Brasilia (BRT/-3)", "BRT3"],
        ],
    },
    {
        label: "Africa and Middle East",
        zones: [
            ["Morocco (<+01>/+1)", "<+01>-1"],
            ["West Africa (WAT/+1)", "WAT-1"],
            ["Central Africa (CAT/+2)", "CAT-2"],
            ["South Africa (SAST/+2)", "SAST-2"],
            ["East Africa (EAT/+3)", "EAT-3"],
            ["Egypt (EET/+2)", "EET-2"],
            ["Israel (IST/IDT/+2)", "IST-2IDT,M3.4.4/26,M10.5.0"],
            ["Turkey (<+03>/+3)", "<+03>-3"],
        ],
    },
];

function body_onload()
{
    fill_in_timezone_options();
    fetch_info();
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

function bytes_to_hex(bytes)
{
    let text = "";
    for (const byte of bytes)
    {
        text += byte.toString(16).padStart(2, "0");
    }
    return text;
}

function hex_to_bytes(text, expected_size)
{
    if (typeof text !== "string" || text.length !== expected_size * 2 || !/^[0-9a-fA-F]+$/.test(text))
    {
        throw new Error("Invalid hex string");
    }

    const bytes = new Uint8Array(expected_size);
    for (let i = 0; i < expected_size; ++i)
    {
        bytes[i] = parseInt(text.substr(i * 2, 2), 16);
    }
    return bytes;
}

function concat_bytes(first, second)
{
    const combined = new Uint8Array(first.length + second.length);
    combined.set(first, 0);
    combined.set(second, first.length);
    return combined;
}

function clear_bytes(bytes)
{
    if (bytes && bytes.fill)
    {
        bytes.fill(0);
    }
}

function constant_time_equal(left, right)
{
    if (!left || !right || left.length !== right.length)
    {
        return false;
    }

    let diff = 0;
    for (let i = 0; i < left.length; ++i)
    {
        diff |= left[i] ^ right[i];
    }
    return diff === 0;
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

function fetch_info()
{
    const request = new XMLHttpRequest();
    request.open("GET", "/get_info", true);
    request.responseType = "json";
    request.timeout = 5000;

    try
    {
        session_security.sessionSaltFromClient = secure_random_bytes(session_salt_half_size);
        request.setRequestHeader(header_session_salt_from_client, bytes_to_hex(session_security.sessionSaltFromClient));
    }
    catch (error)
    {
        retry_info();
        return;
    }

    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            try
            {
                const info = request.response || JSON.parse(request.responseText);
                remember_session_info(info);
                render_info(info);
                hide_loading();
                setup_config_tables_from_info(info);
                if (current_security_level === 0)
                {
                    remove_login_panel();
                    show_save_button();
                    fetch_cfg().catch((error) => set_login_error(error.message || "Config load failed."));
                }
            }
            catch (error)
            {
                retry_info();
            }
            return;
        }
        retry_info();
    };

    request.onerror = retry_info;
    request.ontimeout = retry_info;
    request.send();
}

function retry_info()
{
    window.setTimeout(fetch_info, info_retry_ms);
}

function hide_loading()
{
    const loading = document.getElementById("loading");
    if (loading)
    {
        loading.style.display = "none";
    }
}

function clear_node(node)
{
    while (node.firstChild)
    {
        node.removeChild(node.firstChild);
    }
}

function append_info_row(parent, label, value)
{
    const row = document.createElement("div");
    row.className = "info-row";

    const label_node = document.createElement("div");
    label_node.className = "info-label";
    label_node.textContent = label;

    const value_node = document.createElement("div");
    value_node.className = "info-value";
    value_node.textContent = value || "Unknown";

    row.appendChild(label_node);
    row.appendChild(value_node);
    parent.appendChild(row);
}

function format_bytes(value)
{
    const bytes = Number(value) || 0;
    const units = ["B", "KB", "MB", "GB"];
    let scaled = bytes;
    let unit_index = 0;
    while (scaled >= 1024 && unit_index < units.length - 1)
    {
        scaled /= 1024;
        ++unit_index;
    }

    const digits = unit_index === 0 ? 0 : 1;
    return scaled.toFixed(digits) + " " + units[unit_index];
}

function disk_summary(disk)
{
    if (!disk)
    {
        return "Unknown";
    }
    if (!disk.ready)
    {
        return disk.health || "NotReady";
    }
    return format_bytes(disk.used_bytes) +
           " / " +
           format_bytes(disk.total_bytes) +
           " (" +
           format_bytes(disk.free_bytes) +
           " free)";
}

function make_rows_from_template(parent_container_id, first_child_row, num_of_rows)
{
    const parent = document.getElementById(parent_container_id);
    if (!parent)
    {
        return [];
    }

    if (num_of_rows <= 0)
    {
        clear_node(parent);
        return [];
    }

    const template = typeof first_child_row === "string" ? document.getElementById(first_child_row) : first_child_row;
    if (!template)
    {
        return [];
    }

    const update_trailing_number = (value, index) => {
        if (typeof value !== "string" || value.length === 0)
        {
            return value;
        }
        return value.replace(/([_-])\d+$/, "$1" + index);
    };

    const clear_row_value = (node) => {
        if (node instanceof HTMLInputElement)
        {
            if (node.type === "checkbox" || node.type === "radio")
            {
                node.checked = false;
            }
            else
            {
                node.value = "";
            }
        }
        else if (node instanceof HTMLTextAreaElement)
        {
            node.value = "";
        }
        else if (node instanceof HTMLSelectElement)
        {
            node.selectedIndex = 0;
        }
    };

    const reindex_row = (row, index) => {
        const nodes = [row, ...row.querySelectorAll("*")];
        for (const node of nodes)
        {
            for (const attr of ["id", "name", "for", "aria-label", "aria-labelledby", "aria-describedby"])
            {
                if (node.hasAttribute && node.hasAttribute(attr))
                {
                    node.setAttribute(attr, update_trailing_number(node.getAttribute(attr), index));
                }
            }
            clear_row_value(node);
        }
    };

    const rows = [];
    clear_node(parent);

    for (let i = 1; i <= num_of_rows; ++i)
    {
        const row = template.cloneNode(true);
        reindex_row(row, i);
        parent.appendChild(row);
        rows.push(row);
    }

    return rows;

}

function config_limit(info, key, fallback)
{
    const limits = info && info.config_limits ? info.config_limits : {};
    const value = Number(limits[key]);
    return Number.isFinite(value) && value >= 0 ? value : fallback;
}

function setup_config_tables_from_info(info)
{
    make_rows_from_template("bluetooth_devices", "bt_row_1", Math.max(2, config_limit(info, "bluetooth_hosts", 8)));
    make_rows_from_template("wifi_routers", "wifi_routers_row_1", Math.max(2, config_limit(info, "stations", 8)));
    make_rows_from_template("wifi_ap_row_1", document.querySelector("#wifi_ap_row_1 .wifi-config-row"), Math.max(1, config_limit(info, "access_points", 1)));
    make_rows_from_template("cloud_dest", "cloud_row_1", Math.max(1, config_limit(info, "cloud_uploads", 1)));
    apply_security_level_visibility();
}

function apply_security_level_visibility()
{
    const show_level_0 = current_security_level === 0;
    for (const node of document.querySelectorAll(".security-level-0-only"))
    {
        node.style.display = show_level_0 ? "" : "none";
    }
}

function hide_all_test_divs()
{

}

function fill_in_timezone_options()
{
    const timezone = document.getElementById("timezone");
    if (!timezone)
    {
        return;
    }

    const selected = timezone.value || "UTC0";
    clear_node(timezone);

    for (const group of timezone_option_groups)
    {
        const optgroup = document.createElement("optgroup");
        optgroup.label = group.label;

        for (const zone of group.zones)
        {
            const option = document.createElement("option");
            option.value = zone[1];
            option.textContent = zone[0];
            option.title = zone[1];
            optgroup.appendChild(option);
        }

        timezone.appendChild(optgroup);
    }

    timezone.value = selected;
    if (timezone.value !== selected)
    {
        const option = document.createElement("option");
        option.value = selected;
        option.textContent = "Configured custom - " + selected;
        timezone.insertBefore(option, timezone.firstChild);
        timezone.value = selected;
    }
}

function render_info(info)
{
    const container = document.getElementById("info");
    if (!container)
    {
        return;
    }

    clear_node(container);
    container.className = "info-grid";
    append_info_row(container, "Device Name", info.device_name);
    append_info_row(container, "BDADDR", info.bdaddr);
    append_info_row(container, "Wi-Fi MAC", info.wifi_mac);
    append_info_row(container, "Self IP", info.self_ip);
    append_info_row(container, "Firmware", info.firmware);
    append_info_row(container, "Compiled", info.compiled);
    append_info_row(container, "Security", "Level " + (Number.isFinite(current_security_level) ? current_security_level : "unknown"));
    if (current_security_level !== 0)
    {
        append_info_row(container, "Session Security", info.security_ready ? "Ready" : "Network key unavailable");
    }
    append_info_row(container, "Disk Storage", disk_summary(info.disk));

    update_password_reset_entry(Boolean(info.default_soft_ap));
    hide_all_test_divs();
}

function update_password_reset_entry(default_soft_ap)
{
    const entry = document.getElementById("password_reset_entry");
    if (entry)
    {
        entry.style.display = default_soft_ap ? "" : "none";
    }
}

function set_login_error(message)
{
    window.alert(message);
}

function set_input_by_id(id, value)
{
    const input = document.getElementById(id);
    if (input)
    {
        input.value = value || "";
    }
}

function set_select_by_id(id, value)
{
    const select = document.getElementById(id);
    if (!select)
    {
        return;
    }

    select.value = value || "unknown";
    if (select.value !== (value || "unknown"))
    {
        select.value = "unknown";
    }
}

function set_readonly_value(row, class_name, value)
{
    const container = row ? row.querySelector("." + class_name) : null;
    const value_node = container ? container.querySelector(".readonly-value") : null;
    if (value_node)
    {
        value_node.textContent = value || "";
    }
}

function reindex_config_row(row, index)
{
    const suffix = "_" + index;
    for (const node of row.querySelectorAll("[id]"))
    {
        node.id = node.id.replace(/_\d+$/, suffix);
    }
    for (const node of row.querySelectorAll("[name]"))
    {
        node.name = node.name.replace(/_\d+$/, suffix);
    }
    for (const node of row.querySelectorAll("label[for]"))
    {
        node.htmlFor = node.htmlFor.replace(/_\d+$/, suffix);
    }
}

function ensure_config_rows(container_id, row_class, count)
{
    const container = document.getElementById(container_id);
    if (!container)
    {
        return [];
    }

    let rows = Array.from(container.querySelectorAll("." + row_class));
    if (rows.length === 0)
    {
        return [];
    }

    while (rows.length < count)
    {
        const clone = rows[0].cloneNode(true);
        container.appendChild(clone);
        rows.push(clone);
    }
    while (rows.length > count)
    {
        const row = rows.pop();
        row.remove();
    }

    rows.forEach((row, index) => reindex_config_row(row, index + 1));
    return rows;
}

function hide_logged_out_markers()
{
    const ids = [
        "bluetooth_devices_please_login",
        "wifi_routers_please_login",
        "wifi_ap_please_login",
        "cloud_please_login",
        "ntp_servers_please_login",
    ];

    for (const id of ids)
    {
        const marker = document.getElementById(id);
        if (marker)
        {
            marker.style.display = "none";
        }
    }
}

function remove_login_panel()
{
    const password = document.getElementById("password");
    if (password)
    {
        password.value = "";
    }

    const panel = document.getElementById("login_panel");
    if (panel)
    {
        panel.remove();
    }
}

function show_save_button()
{
    const save = document.getElementById("btn_save");
    if (save)
    {
        save.style.display = "block";
    }
}

function format_last_used(value)
{
    const seconds = Number(value) || 0;
    if (seconds <= 0)
    {
        return "";
    }

    const date = new Date(seconds * 1000);
    const year = date.getFullYear().toString().padStart(4, "0");
    const month = (date.getMonth() + 1).toString().padStart(2, "0");
    const day = date.getDate().toString().padStart(2, "0");
    const hour = date.getHours().toString().padStart(2, "0");
    const minute = date.getMinutes().toString().padStart(2, "0");
    return year + "-" + month + "-" + day + "-" + hour + ":" + minute;
}

function fill_time_config(network)
{
    if (!network)
    {
        return;
    }

    const timezone = document.getElementById("timezone");
    if (timezone && network.timezone)
    {
        timezone.value = network.timezone;
        if (timezone.value !== network.timezone)
        {
            const option = document.createElement("option");
            option.value = network.timezone;
            option.textContent = "Configured custom - " + network.timezone;
            timezone.insertBefore(option, timezone.firstChild);
            timezone.value = network.timezone;
        }
    }

    const ntp_servers = Array.isArray(network.ntp_servers) ? network.ntp_servers : [];
    for (let i = 0; i < 3; ++i)
    {
        set_input_by_id("ntp_url_" + (i + 1), ntp_servers[i] || "");
    }
}

function fill_wifi_rows(container_id, row_class, id_prefix, items, minimum_rows)
{
    const list = Array.isArray(items) ? items : [];
    const container = document.getElementById(container_id);
    const existing_count = container ? container.querySelectorAll("." + row_class).length : 0;
    const row_count = Math.max(minimum_rows, existing_count, list.length);
    const template_id = container_id === "wifi_routers" ? "wifi_routers_row_1" : null;
    const template = template_id ? template_id : document.querySelector("#" + container_id + " ." + row_class);
    const rows = make_rows_from_template(container_id, template, row_count);

    rows.forEach((row, index) => {
        const item = list[index] || {};
        set_input_by_id(id_prefix + "_ssid_" + (index + 1), item.ssid || "");
        set_input_by_id(id_prefix + "_password_" + (index + 1), "");
        set_select_by_id(id_prefix + "_icon_" + (index + 1), item.icon || "unknown");
    });
}

function fill_cloud_rows(items)
{
    const list = Array.isArray(items) ? items : [];
    const container = document.getElementById("cloud_dest");
    const existing_count = container ? container.querySelectorAll(".cloud-dest-row").length : 0;
    const rows = make_rows_from_template("cloud_dest", "cloud_row_1", Math.max(1, existing_count, list.length));

    rows.forEach((row, index) => {
        const item = list[index] || {};
        set_input_by_id("cloud_url_" + (index + 1), item.url || "");
        set_input_by_id("cloud_password_" + (index + 1), current_security_level === 0 ? (item.password || "") : "");
        set_select_by_id("cloud_icon_" + (index + 1), item.icon || "unknown");
    });
}

function fill_bluetooth_rows(items)
{
    const list = Array.isArray(items) ? items : [];
    const container = document.getElementById("bluetooth_devices");
    const existing_count = container ? container.querySelectorAll(".bluetooth-device-row").length : 0;
    const rows = make_rows_from_template("bluetooth_devices", "bt_row_1", Math.max(2, existing_count, list.length));

    rows.forEach((row, index) => {
        const item = list[index] || {};
        set_input_by_id("bt_bdaddr_" + (index + 1), item.mac || "");
        set_input_by_id("bt_name_custom_" + (index + 1), item.name_custom || item.name || "");
        set_select_by_id("bt_icon_" + (index + 1), item.icon || "unknown");
        set_readonly_value(row, "bluetooth-reported-field", item.name_reported || "");
        set_readonly_value(row, "bluetooth-last-used-field", format_last_used(item.last_used));

        const bonded = row.querySelector(".bonded-indicator");
        if (bonded)
        {
            bonded.innerHTML = item.bonded ? "&#x1F91D;" : "";
            bonded.classList.toggle("empty", !item.bonded);
            bonded.title = item.bonded ? "Bonded" : "";
            bonded.setAttribute("aria-label", item.bonded ? "Bonded" : "");
        }
    });
}

function fill_config_page(config)
{
    const network = config && config.network ? config.network : {};
    const bluetooth = config && config.bluetooth ? config.bluetooth : {};

    fill_time_config(network);
    fill_wifi_rows("wifi_routers", "wifi-config-row", "wifi", network.stations, 2);
    fill_wifi_rows("wifi_ap_row_1", "wifi-config-row", "wifi_ap", network.access_points, 1);
    fill_cloud_rows(network.cloud_uploads);
    fill_bluetooth_rows(bluetooth.hosts);
    hide_logged_out_markers();
    apply_security_level_visibility();
}

async function decrypt_cfg_blob(buffer)
{
    const bytes = new Uint8Array(buffer);
    if (bytes.length < get_cfg_magic.length + 1 + 16)
    {
        throw new Error("Encrypted config response is too small");
    }

    for (let i = 0; i < get_cfg_magic.length; ++i)
    {
        if (bytes[i] !== get_cfg_magic[i])
        {
            throw new Error("Encrypted config response has invalid magic");
        }
    }
    if (bytes[get_cfg_magic.length] !== get_cfg_version)
    {
        throw new Error("Encrypted config response has unsupported version");
    }

    const nonce = next_session_nonce();
    const encrypted = bytes.slice(get_cfg_magic.length + 1);
    const plaintext = await (await require_webcrypto()).decrypt(
        { name: "AES-GCM", iv: nonce, tagLength: 128 },
        session_security.sessionAesKey,
        encrypted);
    return JSON.parse(new TextDecoder().decode(plaintext));
}

function fetch_cfg()
{
    return new Promise((resolve, reject) => {
        const request = new XMLHttpRequest();
        request.open("GET", "/get_cfg", true);
        request.responseType = current_security_level === 0 ? "json" : "arraybuffer";
        request.timeout = 10000;
        if (current_security_level !== 0)
        {
            request.setRequestHeader(header_session_response_from_client, session_security.sessionResponseFromClientHex);
            request.setRequestHeader(header_session_salt_from_client, bytes_to_hex(session_security.sessionSaltFromClient));
            session_security.nonceCounter = 0;
        }

        request.onload = async function() {
            if (request.status < 200 || request.status >= 300)
            {
                reject(new Error("Config request failed: " + request.status));
                return;
            }

            try
            {
                const config = current_security_level === 0
                    ? (request.response || JSON.parse(request.responseText))
                    : await decrypt_cfg_blob(request.response);
                fill_config_page(config);
                resolve(config);
            }
            catch (error)
            {
                reject(error);
            }
        };

        request.onerror = () => reject(new Error("Config request failed"));
        request.ontimeout = () => reject(new Error("Config request timed out"));
        request.send();
    });
}

async function login_onsubmit()
{
    const password_input = document.getElementById("password");
    let password_text = password_input ? password_input.value : "";
    if (!password_text)
    {
        set_login_error("Enter the device password first.");
        return;
    }
    if (!session_security.sessionChallenge || !session_security.sessionResponseFromServer || !session_security.sessionSaltFromServer)
    {
        set_login_error("The browser has not received a session challenge yet.");
        return;
    }
    if (!session_security.securityReady)
    {
        set_login_error("The device does not have a network key loaded.");
        return;
    }

    let password_bytes = null;
    let network_key_bytes = null;
    let session_key_bytes = null;
    let session_salt = null;
    let client_response = null;
    let server_response = null;

    try
    {
        password_bytes = new TextEncoder().encode(password_text);
        password_text = "";
        if (password_input)
        {
            password_input.value = "";
        }

        network_key_bytes = await derive_pbkdf2_bytes(password_bytes, network_salt);
        session_security.networkHmacKey = await import_hmac_key(network_key_bytes);
        session_security.networkAesKey = await import_aes_key(network_key_bytes, ["encrypt"]);

        client_response = await hmac_sha256(session_security.networkHmacKey, session_security.sessionChallenge);
        server_response = await hmac_sha256(session_security.networkHmacKey, client_response);
        if (!constant_time_equal(server_response, session_security.sessionResponseFromServer))
        {
            throw new Error("Password is wrong, or the device did not generate the expected session response.");
        }

        session_security.sessionResponseFromClientHex = bytes_to_hex(client_response);
        session_security.sessionSaltFromClient = secure_random_bytes(session_salt_half_size);
        session_salt = concat_bytes(session_security.sessionSaltFromServer, session_security.sessionSaltFromClient);
        session_key_bytes = await derive_pbkdf2_bytes(network_key_bytes, session_salt);
        session_security.sessionAesKey = await import_aes_key(session_key_bytes);
        session_security.nonceCounter = 0;

        remove_login_panel();
        show_save_button();
        await fetch_cfg();
    }
    catch (error)
    {
        session_security.sessionResponseFromClientHex = "";
        session_security.networkHmacKey = null;
        session_security.networkAesKey = null;
        session_security.sessionAesKey = null;
        set_login_error(error.message || "Login failed.");
    }
    finally
    {
        clear_bytes(password_bytes);
        clear_bytes(network_key_bytes);
        clear_bytes(session_key_bytes);
        clear_bytes(session_salt);
        clear_bytes(client_response);
        clear_bytes(server_response);
        password_text = "";
    }
}

function input_value(id)
{
    const input = document.getElementById(id);
    return input ? input.value.trim() : "";
}

function select_value(id, fallback)
{
    const select = document.getElementById(id);
    return select && select.value ? select.value : (fallback || "unknown");
}

function collect_wifi_rows(container_id, id_prefix)
{
    const container = document.getElementById(container_id);
    const rows = container ? Array.from(container.querySelectorAll(".wifi-config-row")) : [];
    const items = [];

    rows.forEach((_row, index) => {
        const suffix = index + 1;
        const ssid = input_value(id_prefix + "_ssid_" + suffix);
        if (!ssid)
        {
            return;
        }

        items.push({
            ssid: ssid,
            password: input_value(id_prefix + "_password_" + suffix),
            icon: select_value(id_prefix + "_icon_" + suffix, "unknown"),
        });
    });

    return items;
}

function collect_cloud_rows()
{
    const container = document.getElementById("cloud_dest");
    const rows = container ? Array.from(container.querySelectorAll(".cloud-dest-row")) : [];
    const items = [];

    rows.forEach((_row, index) => {
        const suffix = index + 1;
        const url = input_value("cloud_url_" + suffix);
        if (!url)
        {
            return;
        }

        const item = {
            url: url,
            icon: select_value("cloud_icon_" + suffix, "unknown"),
        };
        if (current_security_level === 0)
        {
            item.password = input_value("cloud_password_" + suffix);
        }
        items.push(item);
    });

    return items;
}

function collect_bluetooth_rows()
{
    const container = document.getElementById("bluetooth_devices");
    const rows = container ? Array.from(container.querySelectorAll(".bluetooth-device-row")) : [];
    const hosts = [];

    rows.forEach((_row, index) => {
        const suffix = index + 1;
        const mac = input_value("bt_bdaddr_" + suffix);
        if (!mac)
        {
            return;
        }

        hosts.push({
            mac: mac,
            name_custom: input_value("bt_name_custom_" + suffix),
            icon: select_value("bt_icon_" + suffix, "unknown"),
        });
    });

    return hosts;
}

function collect_config()
{
    return {
        network: {
            timezone: input_value("timezone") || "UTC0",
            ntp_servers: [
                input_value("ntp_url_1"),
                input_value("ntp_url_2"),
                input_value("ntp_url_3"),
            ],
            stations: collect_wifi_rows("wifi_routers", "wifi"),
            access_points: collect_wifi_rows("wifi_ap_row_1", "wifi_ap"),
            cloud_uploads: collect_cloud_rows(),
        },
        bluetooth: {
            hosts: collect_bluetooth_rows(),
        },
    };
}

async function encrypt_set_cfg_blob(config)
{
    if (!session_security.networkAesKey)
    {
        throw new Error("Network encryption key is not available.");
    }

    const nonce = secure_random_bytes(12);
    const plaintext = new TextEncoder().encode(JSON.stringify(config));
    const encrypted = new Uint8Array(await (await require_webcrypto()).encrypt(
        { name: "AES-GCM", iv: nonce, tagLength: 128 },
        session_security.networkAesKey,
        plaintext));
    const body = new Uint8Array(set_cfg_magic.length + 1 + nonce.length + encrypted.length);
    body.set(set_cfg_magic, 0);
    body[set_cfg_magic.length] = set_cfg_version;
    body.set(nonce, set_cfg_magic.length + 1);
    body.set(encrypted, set_cfg_magic.length + 1 + nonce.length);
    return body;
}

async function save_config()
{
    let body = null;
    try
    {
        const config = collect_config();
        body = current_security_level === 0
            ? new TextEncoder().encode(JSON.stringify(config))
            : await encrypt_set_cfg_blob(config);
    }
    catch (error)
    {
        set_login_error(error.message || "Config save failed.");
        return;
    }

    const request = new XMLHttpRequest();
    request.open("POST", "/set_cfg", true);
    request.timeout = 15000;
    request.setRequestHeader("Content-Type", current_security_level === 0 ? "application/json" : "application/octet-stream");

    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            window.alert("Configuration saved.");
            fetch_cfg().catch((error) => set_login_error(error.message || "Config reload failed."));
            return;
        }
        set_login_error(request.responseText || ("Config save failed: " + request.status));
    };
    request.onerror = () => set_login_error("Config save failed.");
    request.ontimeout = () => set_login_error("Config save timed out.");
    request.send(body);
}
