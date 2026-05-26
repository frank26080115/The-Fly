function is_offline_test()
{
    if (!window.location)
    {
        return false;
    }

    const protocol = window.location.protocol;
    if (protocol === "file:")
    {
        return true;
    }

    const hostname = window.location.hostname;
    return hostname === "localhost" || hostname === "127.0.0.1" || hostname === "::1";
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

function clear_node(node)
{
    while (node.firstChild)
    {
        node.removeChild(node.firstChild);
    }
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

function hide_all_test_divs()
{
    for (const node of document.querySelectorAll("[id$='_test']"))
    {
        node.style.display = "none";
    }
}
