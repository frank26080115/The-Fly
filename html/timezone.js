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

const iana_timezone_to_posix_tz = Object.freeze({
    "UTC": "UTC0",
    "Etc/UTC": "UTC0",
    "Etc/UCT": "UTC0",
    "Etc/GMT": "UTC0",
    "GMT": "UTC0",

    "Etc/GMT+12": "UTC12",
    "Etc/GMT+11": "UTC11",
    "Etc/GMT+10": "HST10",
    "Etc/GMT+9": "AKST9",
    "Etc/GMT+8": "PST8",
    "Etc/GMT+7": "MST7",
    "Etc/GMT+6": "CST6",
    "Etc/GMT+5": "EST5",
    "Etc/GMT+4": "AST4",
    "Etc/GMT+3": "BRT3",
    "Etc/GMT+2": "UTC2",
    "Etc/GMT+1": "UTC1",
    "Etc/GMT-1": "CET-1",
    "Etc/GMT-2": "EET-2",
    "Etc/GMT-3": "MSK-3",
    "Etc/GMT-4": "GST-4",
    "Etc/GMT-5": "PKT-5",
    "Etc/GMT-6": "BST-6",
    "Etc/GMT-7": "ICT-7",
    "Etc/GMT-8": "CST-8",
    "Etc/GMT-9": "JST-9",
    "Etc/GMT-10": "AEST-10",
    "Etc/GMT-11": "SBT-11",
    "Etc/GMT-12": "NZST-12",
    "Etc/GMT-13": "NZDT-13",
    "Etc/GMT-14": "LINT-14",

    "America/Los_Angeles": "PST8PDT,M3.2.0,M11.1.0",
    "America/Vancouver": "PST8PDT,M3.2.0,M11.1.0",
    "America/Tijuana": "PST8PDT,M3.2.0,M11.1.0",
    "America/Denver": "MST7MDT,M3.2.0,M11.1.0",
    "America/Edmonton": "MST7MDT,M3.2.0,M11.1.0",
    "America/Boise": "MST7MDT,M3.2.0,M11.1.0",
    "America/Phoenix": "MST7",
    "America/Dawson_Creek": "MST7",
    "America/Fort_Nelson": "MST7",
    "America/Whitehorse": "MST7",
    "America/Dawson": "MST7",
    "America/Chicago": "CST6CDT,M3.2.0,M11.1.0",
    "America/Winnipeg": "CST6CDT,M3.2.0,M11.1.0",
    "America/Indiana/Knox": "CST6CDT,M3.2.0,M11.1.0",
    "America/Indiana/Tell_City": "CST6CDT,M3.2.0,M11.1.0",
    "America/Menominee": "CST6CDT,M3.2.0,M11.1.0",
    "America/North_Dakota/Beulah": "CST6CDT,M3.2.0,M11.1.0",
    "America/North_Dakota/Center": "CST6CDT,M3.2.0,M11.1.0",
    "America/North_Dakota/New_Salem": "CST6CDT,M3.2.0,M11.1.0",
    "America/New_York": "EST5EDT,M3.2.0,M11.1.0",
    "America/Detroit": "EST5EDT,M3.2.0,M11.1.0",
    "America/Toronto": "EST5EDT,M3.2.0,M11.1.0",
    "America/Montreal": "EST5EDT,M3.2.0,M11.1.0",
    "America/Nassau": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Indianapolis": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Marengo": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Petersburg": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Vevay": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Vincennes": "EST5EDT,M3.2.0,M11.1.0",
    "America/Indiana/Winamac": "EST5EDT,M3.2.0,M11.1.0",
    "America/Kentucky/Louisville": "EST5EDT,M3.2.0,M11.1.0",
    "America/Kentucky/Monticello": "EST5EDT,M3.2.0,M11.1.0",
    "America/Anchorage": "AKST9AKDT,M3.2.0,M11.1.0",
    "America/Juneau": "AKST9AKDT,M3.2.0,M11.1.0",
    "America/Metlakatla": "AKST9AKDT,M3.2.0,M11.1.0",
    "America/Nome": "AKST9AKDT,M3.2.0,M11.1.0",
    "America/Sitka": "AKST9AKDT,M3.2.0,M11.1.0",
    "America/Yakutat": "AKST9AKDT,M3.2.0,M11.1.0",
    "Pacific/Honolulu": "HST10",
    "America/Halifax": "AST4ADT,M3.2.0,M11.1.0",
    "America/Moncton": "AST4ADT,M3.2.0,M11.1.0",
    "Atlantic/Bermuda": "AST4ADT,M3.2.0,M11.1.0",
    "America/St_Johns": "NST3:30NDT,M3.2.0,M11.1.0",

    "Europe/London": "GMT0BST,M3.5.0/1,M10.5.0",
    "Europe/Jersey": "GMT0BST,M3.5.0/1,M10.5.0",
    "Europe/Guernsey": "GMT0BST,M3.5.0/1,M10.5.0",
    "Europe/Isle_of_Man": "GMT0BST,M3.5.0/1,M10.5.0",
    "Europe/Dublin": "GMT0BST,M3.5.0/1,M10.5.0",
    "Europe/Lisbon": "WET0WEST,M3.5.0/1,M10.5.0",
    "Atlantic/Canary": "WET0WEST,M3.5.0/1,M10.5.0",
    "Atlantic/Faroe": "WET0WEST,M3.5.0/1,M10.5.0",
    "Atlantic/Madeira": "WET0WEST,M3.5.0/1,M10.5.0",
    "Europe/Amsterdam": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Andorra": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Belgrade": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Berlin": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Bratislava": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Brussels": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Budapest": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Copenhagen": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Gibraltar": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Ljubljana": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Luxembourg": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Madrid": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Malta": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Monaco": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Oslo": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Paris": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Podgorica": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Prague": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Rome": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/San_Marino": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Sarajevo": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Skopje": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Stockholm": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Tirane": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Vaduz": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Vatican": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Vienna": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Warsaw": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Zagreb": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Zurich": "CET-1CEST,M3.5.0,M10.5.0/3",
    "Europe/Athens": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Bucharest": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Chisinau": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Helsinki": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Kyiv": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Mariehamn": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Nicosia": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Riga": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Sofia": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Tallinn": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Uzhgorod": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Vilnius": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Zaporozhye": "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "Europe/Moscow": "MSK-3",
    "Europe/Volgograd": "MSK-3",
    "Europe/Kirov": "MSK-3",

    "Asia/Dubai": "GST-4",
    "Asia/Muscat": "GST-4",
    "Asia/Kabul": "AFT-4:30",
    "Asia/Karachi": "PKT-5",
    "Asia/Kolkata": "IST-5:30",
    "Asia/Calcutta": "IST-5:30",
    "Asia/Colombo": "IST-5:30",
    "Asia/Kathmandu": "NPT-5:45",
    "Asia/Katmandu": "NPT-5:45",
    "Asia/Dhaka": "BST-6",
    "Asia/Yangon": "MMT-6:30",
    "Asia/Rangoon": "MMT-6:30",
    "Asia/Bangkok": "ICT-7",
    "Asia/Ho_Chi_Minh": "ICT-7",
    "Asia/Saigon": "ICT-7",
    "Asia/Jakarta": "ICT-7",
    "Asia/Phnom_Penh": "ICT-7",
    "Asia/Vientiane": "ICT-7",
    "Asia/Shanghai": "CST-8",
    "Asia/Chongqing": "CST-8",
    "Asia/Chungking": "CST-8",
    "Asia/Harbin": "CST-8",
    "Asia/Hong_Kong": "CST-8",
    "Asia/Macau": "CST-8",
    "Asia/Macao": "CST-8",
    "Asia/Manila": "CST-8",
    "Asia/Singapore": "CST-8",
    "Asia/Taipei": "CST-8",
    "Asia/Tokyo": "JST-9",
    "Asia/Seoul": "KST-9",

    "Australia/Perth": "AWST-8",
    "Australia/Eucla": "ACWST-8:45",
    "Australia/Adelaide": "ACST-9:30ACDT,M10.1.0,M4.1.0/3",
    "Australia/Broken_Hill": "ACST-9:30ACDT,M10.1.0,M4.1.0/3",
    "Australia/Darwin": "ACST-9:30",
    "Australia/Brisbane": "AEST-10",
    "Australia/Lindeman": "AEST-10",
    "Australia/Melbourne": "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "Australia/Sydney": "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "Australia/Hobart": "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "Australia/Currie": "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "Australia/Lord_Howe": "LHST-10:30LHDT-11,M10.1.0,M4.1.0",
    "Pacific/Auckland": "NZST-12NZDT,M9.5.0,M4.1.0/3",
    "Pacific/Chatham": "CHAST-12:45CHADT,M9.5.0/2:45,M4.1.0/3:45",

    "America/Mexico_City": "CST6",
    "America/Merida": "CST6",
    "America/Monterrey": "CST6",
    "America/Bogota": "COT5",
    "America/Lima": "COT5",
    "America/Guayaquil": "COT5",
    "America/Caracas": "VET4",
    "America/La_Paz": "BOT4",
    "America/Santiago": "CLT4CLST,M9.1.6/24,M4.1.6/24",
    "America/Argentina/Buenos_Aires": "ART3",
    "America/Argentina/Cordoba": "ART3",
    "America/Argentina/Mendoza": "ART3",
    "America/Buenos_Aires": "ART3",
    "America/Sao_Paulo": "BRT3",

    "Africa/Lagos": "WAT-1",
    "Africa/Kinshasa": "WAT-1",
    "Africa/Luanda": "WAT-1",
    "Africa/Tripoli": "CAT-2",
    "Africa/Maputo": "CAT-2",
    "Africa/Johannesburg": "SAST-2",
    "Africa/Nairobi": "EAT-3",
    "Asia/Jerusalem": "IST-2IDT,M3.4.4/26,M10.5.0",
    "Asia/Tel_Aviv": "IST-2IDT,M3.4.4/26,M10.5.0",
    "Europe/Istanbul": "<+03>-3",
    "Asia/Istanbul": "<+03>-3",
});

function posix_tz_from_iana_timezone(iana_timezone)
{
    return iana_timezone_to_posix_tz[String(iana_timezone || "")] || "";
}

function current_browser_iana_timezone()
{
    try
    {
        if (typeof Intl === "undefined" || !Intl.DateTimeFormat)
        {
            return "";
        }

        return Intl.DateTimeFormat().resolvedOptions().timeZone || "";
    }
    catch (error)
    {
        return "";
    }
}

function browser_timezone_observes_offset_changes(year)
{
    const observed_year = Number.isFinite(year) ? year : (new Date()).getFullYear();
    const offsets = [];

    for (let month = 0; month < 12; ++month)
    {
        offsets.push((new Date(observed_year, month, 1, 12, 0, 0)).getTimezoneOffset());
    }

    return Math.min(...offsets) !== Math.max(...offsets);
}

function posix_tz_from_utc_offset_minutes(offset_minutes)
{
    const minutes_east = Number(offset_minutes);
    if (!Number.isFinite(minutes_east))
    {
        return "";
    }

    const rounded_minutes_east = Math.round(minutes_east);
    if (rounded_minutes_east === 0)
    {
        return "UTC0";
    }

    const name_sign = rounded_minutes_east >= 0 ? "+" : "-";
    const name_abs_minutes = Math.abs(rounded_minutes_east);
    const name_hours = String(Math.floor(name_abs_minutes / 60)).padStart(2, "0");
    const name_minutes = name_abs_minutes % 60;
    const name = "<" + name_sign + name_hours + (name_minutes ? String(name_minutes).padStart(2, "0") : "") + ">";

    const posix_offset_minutes = -rounded_minutes_east;
    const offset_sign = posix_offset_minutes < 0 ? "-" : "";
    const offset_abs_minutes = Math.abs(posix_offset_minutes);
    const offset_hours = Math.floor(offset_abs_minutes / 60);
    const offset_minutes_part = offset_abs_minutes % 60;
    return name + offset_sign + offset_hours + (offset_minutes_part ? ":" + String(offset_minutes_part).padStart(2, "0") : "");
}

function get_current_browser_posix_tz_string()
{
    const iana_timezone = current_browser_iana_timezone();
    const mapped_timezone = posix_tz_from_iana_timezone(iana_timezone);
    if (mapped_timezone)
    {
        return mapped_timezone;
    }

    if (browser_timezone_observes_offset_changes())
    {
        return "";
    }

    return posix_tz_from_utc_offset_minutes(-((new Date()).getTimezoneOffset()));
}

