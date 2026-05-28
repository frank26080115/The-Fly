const info_retry_ms = 1500;
const get_cfg_magic = [0x54, 0x46, 0x47, 0x43];
const get_cfg_version = 2;
const set_cfg_magic = [0x54, 0x46, 0x47, 0x43];
const set_cfg_version = 1;
const stored_password_placeholder = "********";

let file_row_template = null;
let current_default_soft_ap = true;

function body_onload()
{
    fill_in_timezone_options();
    fill_in_icon_selectors();
    if (!is_offline_test())
    {
        hide_all_test_divs();
        hide_login_panel();
    }
    fetch_info();
}

function handle_info(info)
{
    current_default_soft_ap = !info || info.default_soft_ap !== false;
    remember_session_info(info);
    render_info(info);
    hide_loading();
    setup_config_tables_from_info(info);
    apply_reconfiguration_visibility();
    if (reconfiguration_allowed() && current_security_level === 0)
    {
        fetch_cfg().catch((error) => set_login_error(error.message || "Config load failed."));
    }
    fetch_file_list();
}

function fetch_file_list()
{
    const request = new XMLHttpRequest();
    request.open("GET", "/list_files.json", true);
    request.responseType = "json";
    request.timeout = 5000;

    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            try
            {
                const files = request.response || JSON.parse(request.responseText);
                render_file_list(files);
            }
            catch (error)
            {
                return;
            }
        }
    };

    request.send();
}

function show_file_upload_status(message, class_name)
{
    const status_panel = document.getElementById("file_upload_status");
    const message_node = document.getElementById("file_upload_message");
    if (!status_panel || !message_node)
    {
        return;
    }

    status_panel.classList.remove("notice", "error");
    if (class_name)
    {
        status_panel.classList.add(class_name);
    }
    message_node.textContent = message || "";
    status_panel.style.display = "";
}

function set_file_upload_progress(percent)
{
    const progress = document.getElementById("file_upload_progress");
    const label = document.getElementById("file_upload_percent");
    const clamped = Math.max(0, Math.min(100, Math.round(percent)));
    if (progress)
    {
        progress.value = clamped;
    }
    if (label)
    {
        label.textContent = clamped + "%";
    }
}

function file_upload_onsubmit()
{
    const input = document.getElementById("file_upload_input");
    const button = document.getElementById("btn_file_upload");
    const file = input && input.files && input.files.length > 0 ? input.files[0] : null;

    if (!file)
    {
        set_file_upload_progress(0);
        show_file_upload_status("Choose a file to upload.", "error");
        return;
    }

    set_file_upload_progress(0);
    show_file_upload_status("Uploading " + file.name + "...", "");
    if (button)
    {
        button.disabled = true;
    }

    const request = new XMLHttpRequest();
    request.open("POST", "/file_upload?file_name=" + encodeURIComponent(file.name), true);
    request.timeout = 120000;
    request.setRequestHeader("Content-Type", "application/octet-stream");

    request.upload.onprogress = function(event) {
        if (event.lengthComputable)
        {
            set_file_upload_progress((event.loaded / event.total) * 100);
        }
    };

    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            set_file_upload_progress(100);
            show_file_upload_status("Upload complete.", "notice");
            if (input)
            {
                input.value = "";
            }
            fetch_file_list();
            return;
        }
        show_file_upload_status(request.responseText || ("Upload failed: " + request.status), "error");
    };
    request.onerror = () => show_file_upload_status("Upload failed.", "error");
    request.ontimeout = () => show_file_upload_status("Upload timed out.", "error");
    request.onloadend = function() {
        if (button)
        {
            button.disabled = false;
        }
    };
    request.send(file);
}

function render_file_list(files)
{
    const container = document.getElementById("files_test");
    if (!container)
    {
        return;
    }

    if (!file_row_template)
    {
        const template = document.getElementById("file_row_1");
        if (!template)
        {
            return;
        }
        file_row_template = template.cloneNode(true);
    }

    const list = Array.isArray(files) ? files : [];
    const rows = make_rows_from_template("files_table_body", file_row_template, list.length);
    rows.forEach((row, index) => {
        const file_name = String(list[index] || "");
        const encoded_name = encodeURIComponent(file_name);
        const file_link = row.querySelector(".file-link");
        const delete_link = row.querySelector(".delete-link");

        if (file_link)
        {
            file_link.textContent = file_name;
            file_link.href = "/download_file?file_name=" + encoded_name;
        }
        if (delete_link)
        {
            delete_link.href = "/delete_file?file_name=" + encoded_name;
            delete_link.setAttribute("aria-label", "Delete " + file_name);
        }
    });

    const please_wait = document.getElementById("files_please_wait");
    if (please_wait)
    {
        please_wait.style.display = "none";
    }
    container.style.display = "";
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
                handle_info(info);
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

function apply_login_panel_visibility()
{
    if (current_security_level === 0 || !reconfiguration_allowed())
    {
        hide_login_panel();
    }
    else
    {
        show_login_panel();
    }
}

function apply_reconfiguration_visibility()
{
    update_logged_out_marker_text();
    apply_login_panel_visibility();
    apply_save_button_visibility();
}

function apply_save_button_visibility()
{
    if (reconfiguration_allowed() && config_is_unlocked())
    {
        show_save_button();
    }
    else
    {
        hide_save_button();
    }
}

function reconfiguration_allowed()
{
    return current_default_soft_ap !== false;
}

function config_is_unlocked()
{
    return current_security_level === 0 || Boolean(session_security.sessionAesKey);
}

function update_logged_out_marker_text()
{
    const allowed = reconfiguration_allowed();
    const message = allowed ? "Please Log In First" : "reconfiguration not allowed";
    for (const marker of document.querySelectorAll(".please-login-first"))
    {
        marker.textContent = message;
        if (!allowed)
        {
            marker.style.display = "";
        }
    }
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

    update_password_reset_entry(current_default_soft_ap);
    if (!is_offline_test())
    {
        hide_all_test_divs();
    }
}

function update_password_reset_entry(default_soft_ap)
{
    const entry = document.getElementById("password_reset_entry");
    if (entry)
    {
        entry.style.display = default_soft_ap ? "" : "none";
    }
}

function set_password_reset_message(message, notice)
{
    const node = document.getElementById("password_reset_error");
    if (!node)
    {
        return;
    }

    node.textContent = message || "";
    node.classList.toggle("notice", Boolean(notice));
    node.style.display = message ? "" : "none";
}

function set_memory_reset_message(message, notice)
{
    const node = document.getElementById("memory_reset_error");
    if (!node)
    {
        return;
    }

    node.textContent = message || "";
    node.classList.toggle("notice", Boolean(notice));
    node.style.display = message ? "" : "none";
}

function set_login_error(message, notice)
{
    const node = document.getElementById("login_error");
    if (!node)
    {
        return;
    }

    node.textContent = message || "";
    node.classList.toggle("notice", Boolean(notice));
    node.style.display = message ? "" : "none";
}

function set_input_by_id(id, value)
{
    const input = document.getElementById(id);
    if (input)
    {
        input.value = value || "";
    }
}

function set_password_placeholder_by_id(id, has_stored_password, empty_placeholder)
{
    const input = document.getElementById(id);
    if (!input)
    {
        return;
    }

    input.placeholder = has_stored_password ? stored_password_placeholder : (empty_placeholder || "");
    input.classList.toggle("stored-secret-placeholder", Boolean(has_stored_password));
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
    if (!reconfiguration_allowed())
    {
        update_logged_out_marker_text();
        return;
    }

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

function hide_login_panel()
{
    set_login_error("");

    const password = document.getElementById("password");
    if (password)
    {
        password.value = "";
    }

    const panel = document.getElementById("login_panel");
    if (panel)
    {
        panel.hidden = true;
    }
}

function show_login_panel()
{
    const panel = document.getElementById("login_panel");
    if (panel)
    {
        panel.hidden = false;
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

function hide_save_button()
{
    const save = document.getElementById("btn_save");
    if (save)
    {
        save.style.display = "none";
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

function fill_in_icon_selectors()
{
    const shared_options = `
                            <option value="smartphone">&#x1F4F1; Smartphone</option>
                            <option value="laptop">&#x1F4BB; Laptop</option>
                            <option value="tablet">&#x1F4F2; Tablet</option>
                            <option value="home">&#x1F3E0; Home</option>
                            <option value="work">&#128188; Work</option>
                            <option value="car">&#128663; Car</option>
                            <option value="plane">&#x2708;&#xFE0F; Plane</option>
                            <option value="cat">&#x1F431; Cat</option>
                            <option value="dog">&#x1F436; Dog</option>
                            <option value="bird">&#x1F426; Bird</option>
                            <option value="circle">&#x26AA; Circle</option>
                            <option value="square">&#x2B1C; Square</option>
                            <option value="triangle">&#x1F53A; Triangle</option>`;

    for (const selector of document.querySelectorAll(".icon-selector"))
    {
        const selected_value = selector.value || "unknown";
        selector.innerHTML = default_icon_option_for_selector(selector) + shared_options;
        selector.value = selected_value;
        if (selector.value !== selected_value)
        {
            selector.value = "unknown";
        }
    }
}

function default_icon_option_for_selector(selector)
{
    if (selector.closest(".bluetooth-device-list, .bluetooth-device-row"))
    {
        return `<option value="unknown">&#x1F535; Default</option>`;
    }
    if (selector.closest(".cloud-dest-list, .cloud-dest-row"))
    {
        return `<option value="unknown">&#9729; Default</option>`;
    }
    return `<option value="unknown">&#128732; Default</option>`;
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
        const suffix = index + 1;
        const ssid = item.ssid || "";
        const password_placeholder = id_prefix === "wifi_ap" ? "Access point password" : "Wi-Fi password";
        set_input_by_id(id_prefix + "_ssid_" + suffix, ssid);
        set_input_by_id(id_prefix + "_password_" + suffix, "");
        set_password_placeholder_by_id(id_prefix + "_password_" + suffix, Boolean(ssid), password_placeholder);
        set_select_by_id(id_prefix + "_icon_" + suffix, item.icon || "unknown");
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
        const suffix = index + 1;
        const url = item.url || "";
        set_input_by_id("cloud_url_" + suffix, url);
        set_input_by_id("cloud_password_" + suffix, "");
        set_password_placeholder_by_id("cloud_password_" + suffix, Boolean(url), "Security level 0 password");
        set_select_by_id("cloud_icon_" + suffix, item.icon || "unknown");
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
        set_input_by_id("bt_name_custom_" + (index + 1), item.name_custom || item.name_reported || item.name || "");
        set_select_by_id("bt_icon_" + (index + 1), item.icon || "unknown");
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

        hide_login_panel();
        apply_save_button_visibility();
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

async function derive_password_reset_payload(password_text)
{
    let password_bytes = null;
    let network_key_bytes = null;
    let filecrypt_key_bytes = null;

    try
    {
        password_bytes = new TextEncoder().encode(password_text);
        network_key_bytes = await derive_pbkdf2_bytes(password_bytes, network_salt);

        let body = "network_key=" + encodeURIComponent(bytes_to_hex(network_key_bytes));
        if (current_security_level === 1)
        {
            filecrypt_key_bytes = await derive_pbkdf2_bytes(password_bytes, filecrypt_salt);
            body += "&filecrypt_key=" + encodeURIComponent(bytes_to_hex(filecrypt_key_bytes));
        }
        return body;
    }
    finally
    {
        clear_bytes(password_bytes);
        clear_bytes(network_key_bytes);
        clear_bytes(filecrypt_key_bytes);
    }
}

function password_reset_validation_error(password_text)
{
    if (password_text.length < 6)
    {
        return "Password must be at least 6 characters.";
    }
    if (/\s/.test(password_text))
    {
        return "Password must not contain spaces.";
    }
    if (!/[A-Za-z]/.test(password_text))
    {
        return "Password must contain at least one letter.";
    }
    if (!/[0-9]/.test(password_text))
    {
        return "Password must contain at least one number.";
    }
    return "";
}

function show_reset_success_page(title_text)
{
    const title = document.createElement("h2");
    title.textContent = title_text;
    document.body.replaceChildren(title, document.createTextNode("please allow the device to reboot"));
}

function show_password_reset_success_page()
{
    show_reset_success_page("Password Reset Sucessful");
}

function show_memory_reset_success_page()
{
    show_reset_success_page("Memory Reset Sucessful");
}

async function passreset_onsubmit()
{
    const password_input = document.getElementById("new_password");
    const confirm_input = document.getElementById("new_password_confirm");
    const button = document.getElementById("btn_password_reset");
    const password_text = password_input ? password_input.value : "";
    const confirm_text = confirm_input ? confirm_input.value : "";

    set_password_reset_message("", false);
    if (!password_text)
    {
        set_password_reset_message("Enter a new password.", false);
        return;
    }
    const validation_error = password_reset_validation_error(password_text);
    if (validation_error)
    {
        set_password_reset_message(validation_error, false);
        return;
    }
    if (password_text !== confirm_text)
    {
        set_password_reset_message("Passwords do not match.", false);
        return;
    }
    if (!Number.isFinite(current_security_level))
    {
        set_password_reset_message("Security level is not available yet.", false);
        return;
    }

    let body = "";
    try
    {
        if (button)
        {
            button.disabled = true;
        }
        if (current_security_level > 0)
        {
            body = await derive_password_reset_payload(password_text);
        }

        const request = new XMLHttpRequest();
        request.open("POST", "/reset_password", true);
        request.timeout = 15000;
        request.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
        request.onload = function() {
            if (request.status >= 200 && request.status < 300)
            {
                if (password_input)
                {
                    password_input.value = "";
                }
                if (confirm_input)
                {
                    confirm_input.value = "";
                }
                show_password_reset_success_page();
                return;
            }
            set_password_reset_message(request.responseText || ("Password reset failed: " + request.status), false);
        };
        request.onerror = () => set_password_reset_message("Password reset failed.", false);
        request.ontimeout = () => set_password_reset_message("Password reset timed out.", false);
        request.onloadend = function() {
            if (button)
            {
                button.disabled = false;
            }
        };
        request.send(body);
    }
    catch (error)
    {
        if (button)
        {
            button.disabled = false;
        }
        set_password_reset_message(error.message || "Password reset failed.", false);
    }
}

function memreset_onsubmit()
{
    const button = document.getElementById("btn_memory_reset");
    set_memory_reset_message("", false);

    if (current_security_level !== 0)
    {
        set_memory_reset_message("Memory reset is only available under security level 0.", false);
        return;
    }
    if (!window.confirm("Reset memory and erase Bluetooth, Wi-Fi, and cloud settings?"))
    {
        return;
    }

    if (button)
    {
        button.disabled = true;
    }

    const request = new XMLHttpRequest();
    request.open("POST", "/reset_memory", true);
    request.timeout = 15000;
    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            show_memory_reset_success_page();
            return;
        }
        set_memory_reset_message(request.responseText || ("Memory reset failed: " + request.status), false);
    };
    request.onerror = () => set_memory_reset_message("Memory reset failed.", false);
    request.ontimeout = () => set_memory_reset_message("Memory reset timed out.", false);
    request.onloadend = function() {
        if (button)
        {
            button.disabled = false;
        }
    };
    request.send("");
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
        const password = input_value("cloud_password_" + suffix);
        if (current_security_level === 0 && password)
        {
            item.password = password;
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
