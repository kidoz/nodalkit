#!/usr/bin/env python3
"""Run native tests with Vulkan validation; turn validation messages into failures."""

import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: run_vulkan_validation.py COMMAND [ARG ...]")
    # An unavailable validation layer must fail this gate rather than silently
    # running ordinary tests. Enumeration honors VK_LAYER_PATH for local SDKs.
    class Layer(ctypes.Structure):
        _fields_ = [("name", ctypes.c_char * 256), ("spec_version", ctypes.c_uint32),
                    ("version", ctypes.c_uint32), ("description", ctypes.c_char * 256)]

    if os.name == "nt":
        # SDK layers are not necessarily registered system-wide (e.g. Scoop).
        sdk = os.environ.get("VULKAN_SDK")
        if sdk and (Path(sdk) / "Bin" / "VkLayer_khronos_validation.json").is_file():
            os.environ.setdefault("VK_LAYER_PATH", str(Path(sdk) / "Bin"))
        loader = ctypes.WinDLL("vulkan-1.dll")
    else:
        loader = ctypes.CDLL(ctypes.util.find_library("vulkan") or "libvulkan.so.1")
    enumerate_layers = loader.vkEnumerateInstanceLayerProperties
    enumerate_layers.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Layer)]
    enumerate_layers.restype = ctypes.c_int32
    count = ctypes.c_uint32()
    if enumerate_layers(ctypes.byref(count), None) != 0:
        sys.exit("Cannot enumerate Vulkan validation layers")
    available = (Layer * count.value)()
    if enumerate_layers(ctypes.byref(count), available) != 0 or not any(
            layer.name == b"VK_LAYER_KHRONOS_validation" for layer in available):
        sys.exit("VK_LAYER_KHRONOS_validation is required for this gate")
    env = os.environ.copy()
    layers = env.get("VK_INSTANCE_LAYERS", "").split(os.pathsep)
    if "VK_LAYER_KHRONOS_validation" not in layers:
        layers.append("VK_LAYER_KHRONOS_validation")
    env["VK_INSTANCE_LAYERS"] = os.pathsep.join(layer for layer in layers if layer)
    env["VK_VALIDATION_VALIDATE_SYNC"] = "1"
    env["NK_REQUIRE_VULKAN"] = "1"
    # The settings-file key also works with SDKs predating the current
    # environment-variable name. Avoid mixing deprecated "enables" settings.
    with tempfile.TemporaryDirectory(prefix="nodalkit-vulkan-validation-") as settings:
        (Path(settings) / "vk_layer_settings.txt").write_text(
            "khronos_validation.validate_sync = true\n", encoding="utf-8")
        env["VK_LAYER_SETTINGS_PATH"] = settings
        result = subprocess.run(sys.argv[1:], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, check=False)
    sys.stdout.write(result.stdout)
    errors = ("Validation Error", "Validation Warning", "VUID-", "SYNC-HAZARD-",
              "Failed to find layer", "Failed to load layer", "Layer not found")
    if any(marker in result.stdout for marker in errors):
        print("Vulkan validation reported an error or warning.", file=sys.stderr)
        return 1
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
