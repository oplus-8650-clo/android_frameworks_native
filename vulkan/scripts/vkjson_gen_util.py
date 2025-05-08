#!/usr/bin/env python3
#
# Copyright 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
This file contains utility functions for vkjson_generator.py
that are reused in various parts of the same script.
"""

import re
import vk as VK

COPYRIGHT_WARNINGS = """///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015-2016 The Khronos Group Inc.
// Copyright (c) 2015-2016 Valve Corporation
// Copyright (c) 2015-2016 LunarG, Inc.
// Copyright (c) 2015-2016 Google, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
///////////////////////////////////////////////////////////////////////////////
"""


def get_copyright_warnings():
    return COPYRIGHT_WARNINGS


def get_vkjson_struct_name(extension_name):
    """Gets the corresponding structure name from a Vulkan extension name.
    Example: "VK_KHR_shader_float16_int8" → "VkJsonKHRShaderFloat16Int8"
    """
    prefix_map = {
        "VK_KHR": "VkJsonKHR",
        "VK_EXT": "VkJsonExt",
        "VK_IMG": "VkJsonIMG"
    }

    for prefix, replacement in prefix_map.items():
        if extension_name.startswith(prefix):
            struct_name = replacement + extension_name[len(prefix):]
            break
    else:
        struct_name = f"VkJsonExt{extension_name}"

    # Convert underscores to camel case
    # Example: "VK_KHR_shader_float16_int8" → "VkJsonKHRShaderFloat16Int8"
    struct_name = re.sub(r"_(.)", lambda m: m.group(1).upper(), struct_name)

    return struct_name


def get_vkjson_struct_variable_name(extension_name):
    """Gets corresponding instance name from a Vulkan extension name.
    Example: "VK_KHR_shader_float16_int8" → "khr_shader_float16_int8"
    """
    prefix_map = {
        "VK_KHR_": "khr_",
        "VK_EXT_": "ext_",
        "VK_IMG_": "img_"
    }

    for prefix, replacement in prefix_map.items():
        if extension_name.startswith(prefix):
            return replacement + extension_name[len(prefix):]

    return extension_name.lower()  # Default case if no known prefix matches


def get_struct_name(struct_name):
    """Gets corresponding instance name
    Example: "VkPhysicalDeviceShaderFloat16Int8FeaturesKHR" → "shader_float16_int8_features_khr"
    """
    # Remove "VkPhysicalDevice" prefix and any of the known suffixes
    base_name = (struct_name.removeprefix("VkPhysicalDevice").removesuffix("KHR")
                 .removesuffix("EXT").removesuffix("IMG"))

    # Convert CamelCase to snake_case
    # Example: "ShaderFloat16Int8Features" → "shader_float16_int8_features"
    variable_name = re.sub(r"(?<!^)(?=[A-Z])", "_", base_name).lower()

    # Fix special cases
    variable_name = variable_name.replace("2_d_", "_2d_").replace("3_d_", "_3d_")

    # Add back the correct suffix if it was removed
    suffix_map = {"KHR": "_khr", "EXT": "_ext", "IMG": "_img"}
    for suffix, replacement in suffix_map.items():
        if struct_name.endswith(suffix):
            variable_name += replacement
            break

    # Handle specific exceptions
    special_cases = {
        "8_bit_storage_features_khr": "bit8_storage_features_khr",
        "memory_properties": "memory",
        "16_bit_storage_features": "bit16_storage_features",
        "i_d_properties": "id_properties"
    }

    return special_cases.get(variable_name, variable_name)


def emit_struct_visits_by_vk_version(f, version):
    """Emits visitor calls for Vulkan version structs
    """
    for struct_map in VK.VULKAN_VERSIONS_AND_STRUCTS_MAPPING[version]:
        for struct_name, _ in struct_map.items():
            struct_var = get_struct_name(struct_name)
            # Converts struct_var from snake_case (e.g., point_clipping_properties)
            # to camelCase (e.g., pointClippingProperties) for struct_display_name.
            struct_display_name = re.sub(r"_([a-z])", lambda match: match.group(1).upper(), struct_var)
            f.write(f'visitor->Visit("{struct_display_name}", &device->{struct_var}) &&\n')


def generate_vk_core_structs_init_code(version):
    """Generates code to initialize properties and features
    for structs based on its vulkan API version dependency.
    """
    properties_code, features_code = [], []

    for item in VK.VULKAN_CORES_AND_STRUCTS_MAPPING["versions"].get(version, []):
        for struct_name, struct_type in item.items():
            version_lower = version.lower()

            if "Properties" in struct_name:
                properties_code.extend([
                    f"device.{version_lower}.properties.sType = {struct_type};",
                    f"device.{version_lower}.properties.pNext = properties.pNext;",
                    f"properties.pNext = &device.{version_lower}.properties;\n\n"
                ])

            elif "Features" in struct_name:
                features_code.extend([
                    f"device.{version_lower}.features.sType = {struct_type};",
                    f"device.{version_lower}.features.pNext = features.pNext;",
                    f"features.pNext = &device.{version_lower}.features;\n\n"
                ])

    return "\n".join(properties_code), "\n".join(features_code)


def generate_vk_extension_structs_init_code(mapping, struct_category, next_pointer):
    """Generates Vulkan struct initialization code for given struct category (Properties/Features)
    based on its extension dependency.
    """
    generated_code = []

    for extension, struct_mappings in mapping.items():
        struct_var_name = get_vkjson_struct_variable_name(extension)
        extension_code = [
            f"  if (HasExtension(\"{extension}\", device.extensions)) {{",
            f"    device.{struct_var_name}.reported = true;"
        ]

        struct_count = 0
        for struct_mapping in struct_mappings:
            for struct_name, struct_type in struct_mapping.items():
                if struct_category in struct_name:
                    struct_count += 1
                    struct_instance = get_struct_name(struct_name)
                    extension_code.extend([
                        f"    device.{struct_var_name}.{struct_instance}.sType = {struct_type};",
                        f"    device.{struct_var_name}.{struct_instance}.pNext = {next_pointer}.pNext;",
                        f"    {next_pointer}.pNext = &device.{struct_var_name}.{struct_instance};"
                    ])

        extension_code.append("  }\n")

        if struct_count > 0:
            generated_code.extend(extension_code)

    return "\n".join(generated_code)


def generate_vk_version_structs_initialization(version_data, struct_type_keyword, next_pointer):
    """Generates Vulkan struct initialization code for given struct category (Properties/Features)
    of vulkan api version s.
    """
    struct_initialization_code = []

    for struct_mapping in version_data:
        for struct_name, struct_type in struct_mapping.items():
            if struct_type_keyword in struct_name:
                struct_variable = get_struct_name(struct_name)
                struct_initialization_code.extend([
                    f"device.{struct_variable}.sType = {struct_type};",
                    f"device.{struct_variable}.pNext = {next_pointer}.pNext;",
                    f"{next_pointer}.pNext = &device.{struct_variable};\n"
                ])

    return "\n".join(struct_initialization_code)
