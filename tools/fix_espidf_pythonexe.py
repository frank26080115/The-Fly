"""
Work around a local PlatformIO ESP-IDF builder state where ESPIDF_PYTHONEXE can
be prepended to itself, producing an invalid path like python.exepython.exe.
"""

Import("env")


def dedupe_exact_concatenation(value):
    if not isinstance(value, str) or len(value) < 2 or len(value) % 2:
        return value

    midpoint = len(value) // 2
    if value[:midpoint] == value[midpoint:]:
        return value[:midpoint]

    return value


python_exe = env.get("ESPIDF_PYTHONEXE")
fixed_python_exe = dedupe_exact_concatenation(python_exe)

if fixed_python_exe != python_exe:
    print("Fixed duplicated ESPIDF_PYTHONEXE")
    env.Replace(ESPIDF_PYTHONEXE=fixed_python_exe)
