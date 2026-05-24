const info_retry_ms = 1500;

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

function fetch_info()
{
    const request = new XMLHttpRequest();
    request.open("GET", "/get_info", true);
    request.responseType = "json";
    request.timeout = 5000;

    request.onload = function() {
        if (request.status >= 200 && request.status < 300)
        {
            try
            {
                const info = request.response || JSON.parse(request.responseText);
                render_info(info);
                hide_loading();
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

function login_onsubmit()
{
    console.log("login_onsubmit");
    document.getElementById("btn_save").style.display = "block";
}
