"""
This script resolves a build error involving not being able to find `Network.h`
"""

from os.path import join

Import("env")

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if framework_dir:
    env.Append(CPPPATH=[join(framework_dir, "libraries", "Network", "src")])
