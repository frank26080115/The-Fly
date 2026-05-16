# This PlatformIO pre-build script works around an ESP-IDF managed component
# certificate embedding problem.
#
# The problem it solves:
# PlatformIO can consume ESP-IDF managed components, but this project is built
# through PlatformIO's SCons/Arduino flow instead of a plain `idf.py` CMake
# build. Some managed components, especially ESP RainMaker and ESP Insights,
# expect certificate files under `managed_components/.../server_certs/` to be
# converted into linker-visible embedded binary symbols. In a normal ESP-IDF
# CMake build, ESP-IDF's embed-file machinery generates small assembly files
# for those certificates. In this PlatformIO path, those generated certificate
# assembly files may not be produced even though other code/libraries still
# reference the embedded certificate symbols.
#
# How the problem usually shows up:
# During link you may see undefined references to symbols whose names look like
# `_binary_managed_components_espressif__esp_rainmaker_server_certs_..._crt_start`,
# `_binary_..._crt_end`, or similar `_binary_...` certificate symbols. You may
# also see failures from this script saying `Managed component cert not found`
# if the `managed_components` tree is missing from the checkout/build machine.
#
# How this script fixes it:
# For each certificate listed in CERTS, the script calls ESP-IDF's own
# `data_file_embed_asm.cmake` helper via PlatformIO's bundled CMake. That helper
# creates the same kind of `.S` assembly file ESP-IDF would normally generate.
# The generated assembly is then made a dependency of the firmware link, so the
# expected `_binary_..._start/_end` symbols exist.
#
# What `managed_components` is:
# `managed_components` is the ESP-IDF Component Manager download/cache directory
# for this project. It contains third-party and Espressif components listed by
# `dependencies.lock` or component manifests, using directory names such as
# `espressif__esp_rainmaker`. These files are source inputs to the build, but
# the directory can be large and is commonly treated as generated dependency
# content rather than hand-written project source.
#
# What to commit for another PC:
# Commit this script, `dependencies.lock`, and `managed_components.zip`.
# Do not commit the expanded `managed_components/` directory. It is ignored by
# `.gitignore`; this script recreates it from `managed_components.zip` if the
# archive exists and the directory is missing. If you already have the directory
# but no archive, the script creates `managed_components.zip` with the top-level
# `managed_components/` directory inside it so a fresh checkout can unzip it at
# the project root and get the correct layout.

import os
import subprocess
import zipfile

from SCons.Script import Import

Import("env")

MANAGED_COMPONENTS_DIRNAME = "managed_components"
MANAGED_COMPONENTS_ARCHIVE = "managed_components.zip"

CERTS = (
    "managed_components/espressif__esp_insights/server_certs/https_server.crt",
    "managed_components/espressif__esp_rainmaker/server_certs/rmaker_mqtt_server.crt",
    "managed_components/espressif__esp_rainmaker/server_certs/rmaker_claim_service_server.crt",
    "managed_components/espressif__esp_rainmaker/server_certs/rmaker_ota_server.crt",
)


def path_is_within(child_path, parent_path):
    child_path = os.path.abspath(child_path)
    parent_path = os.path.abspath(parent_path)
    return os.path.commonpath([child_path, parent_path]) == parent_path


def ensure_safe_managed_components_archive(archive_path):
    with zipfile.ZipFile(archive_path, "r") as archive:
        for info in archive.infolist():
            name = info.filename.replace("\\", "/")
            if name.startswith("/") or name.startswith("../") or "/../" in name:
                raise RuntimeError("Unsafe path in %s: %s" % (archive_path, info.filename))
            if name and not name.startswith(MANAGED_COMPONENTS_DIRNAME + "/"):
                raise RuntimeError(
                    "%s must contain paths under %s/: %s"
                    % (archive_path, MANAGED_COMPONENTS_DIRNAME, info.filename)
                )


def extract_managed_components_archive(project_dir, archive_path, managed_dir):
    ensure_safe_managed_components_archive(archive_path)
    os.makedirs(project_dir, exist_ok=True)
    with zipfile.ZipFile(archive_path, "r") as archive:
        archive.extractall(project_dir)
    if not os.path.isdir(managed_dir):
        raise RuntimeError("%s did not recreate %s" % (archive_path, managed_dir))
    print("Extracted %s to %s" % (archive_path, managed_dir))


def create_managed_components_archive(project_dir, archive_path, managed_dir):
    tmp_archive_path = archive_path + ".tmp"
    if os.path.exists(tmp_archive_path):
        os.remove(tmp_archive_path)

    with zipfile.ZipFile(tmp_archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for root, dirs, files in os.walk(managed_dir):
            dirs.sort()
            files.sort()

            if not files and not dirs:
                rel_dir = os.path.relpath(root, project_dir).replace(os.sep, "/")
                archive.write(root, rel_dir + "/")

            for filename in files:
                full_path = os.path.join(root, filename)
                if not path_is_within(full_path, managed_dir):
                    raise RuntimeError("Refusing to archive path outside %s: %s" % (managed_dir, full_path))
                rel_path = os.path.relpath(full_path, project_dir).replace(os.sep, "/")
                archive.write(full_path, rel_path)

    os.replace(tmp_archive_path, archive_path)
    print("Created %s from %s" % (archive_path, managed_dir))


def sync_managed_components_archive(project_dir):
    managed_dir = os.path.join(project_dir, MANAGED_COMPONENTS_DIRNAME)
    archive_path = os.path.join(project_dir, MANAGED_COMPONENTS_ARCHIVE)

    if os.path.isfile(archive_path) and not os.path.isdir(managed_dir):
        extract_managed_components_archive(project_dir, archive_path, managed_dir)
    elif os.path.isdir(managed_dir) and not os.path.isfile(archive_path):
        create_managed_components_archive(project_dir, archive_path, managed_dir)


def make_generate_action(cert_relpath):
    def generate_cert_asm(target, source, env):
        project_dir = env.subst("$PROJECT_DIR")
        cert_path = os.path.join(project_dir, *cert_relpath.split("/"))
        target_path = target[0].get_abspath()
        cmake_dir = env.PioPlatform().get_package_dir("tool-cmake") or ""
        espidf_dir = env.PioPlatform().get_package_dir("framework-espidf") or ""
        cmake_path = os.path.join(cmake_dir, "bin", "cmake")
        if os.name == "nt" and os.path.exists(cmake_path + ".exe"):
            cmake_path += ".exe"
        script_path = os.path.join(
            espidf_dir,
            "tools",
            "cmake",
            "scripts",
            "data_file_embed_asm.cmake",
        )

        if not os.path.exists(cert_path):
            print("Managed component cert not found: %s" % cert_path)
            return 1

        os.makedirs(os.path.dirname(target_path), exist_ok=True)
        subprocess.run(
            [
                cmake_path,
                "-DDATA_FILE=%s" % cert_path,
                "-DSOURCE_FILE=%s" % target_path,
                "-DFILE_TYPE=TEXT",
                "-P",
                script_path,
            ],
            check=True,
        )
        return 0

    return generate_cert_asm


sync_managed_components_archive(env.subst("$PROJECT_DIR"))

generated = []
for cert in CERTS:
    target_name = os.path.basename(cert) + ".S"
    target = env.Command(
        os.path.join("$BUILD_DIR", target_name),
        [],
        make_generate_action(cert),
    )
    env.AlwaysBuild(target)
    generated.extend(target)

env.Requires("$PIOMAINPROG", generated)
