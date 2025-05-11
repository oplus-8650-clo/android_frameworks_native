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
This file contains all supporting utility functions for vkjson_generator.py
"""

import dataclasses
import re
import vk as VK
from typing import get_origin

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


def generate_extension_struct_definition(f):
    """Generates struct definition code for extension based structs
    Example:
    struct VkJsonKHRShaderFloatControls {
      VkJsonKHRShaderFloatControls() {
        reported = false;
        memset(&float_controls_properties_khr, 0,
              sizeof(VkPhysicalDeviceFloatControlsPropertiesKHR));
      }
      bool reported;
      VkPhysicalDeviceFloatControlsPropertiesKHR float_controls_properties_khr;
    };
    """
    vkjson_entries = []

    for extension_name, struct_list in (VK.VULKAN_EXTENSIONS_AND_STRUCTS_MAPPING["extensions"]
            .items()):
        vkjson_struct_name = get_vkjson_struct_name(extension_name)
        vkjson_struct_variable_name = get_vkjson_struct_variable_name(extension_name)
        vkjson_entries.append(f"{vkjson_struct_name} {vkjson_struct_variable_name}")

        struct_entries = []

        f.write(f"struct {vkjson_struct_name} {{\n")
        f.write(f"  {vkjson_struct_name}() {{\n")
        f.write("    reported = false;\n")

        for struct_map in struct_list:
            for struct_name, _ in struct_map.items():
                variable_name = get_struct_name(struct_name)
                f.write(f"    memset(&{variable_name}, 0, sizeof({struct_name}));\n")
                struct_entries.append(f"{struct_name} {variable_name}")

        f.write("  }\n")  # End of constructor
        f.write("  bool reported;\n")

        for entry in struct_entries:
            f.write(f"  {entry};\n")

        f.write("};\n\n")  # End of struct

    return vkjson_entries


def generate_vk_core_struct_definition(f):
    """Generates struct definition code for vulkan cores
    Example:
    struct VkJsonCore11 {
      VkPhysicalDeviceVulkan11Properties properties;
      VkPhysicalDeviceVulkan11Features features;
    };
    """
    vkjson_core_entries = []

    for version, items in VK.VULKAN_CORES_AND_STRUCTS_MAPPING["versions"].items():
        struct_name = f"VkJson{version}"
        vkjson_core_entries.append(f"{struct_name} {version.lower()}")

        f.write(f"struct {struct_name} {{\n")
        f.write(f"  {struct_name}() {{\n")  # Start of constructor
        for item in items:
            for struct_type, _ in item.items():
                field_name = "properties" if "Properties" in struct_type else "features"
                f.write(f" memset(&{field_name}, 0, sizeof({struct_type}));\n")
        f.write("  }\n")  # End of constructor

        for item in items:
            for struct_type, _ in item.items():
                field_name = "properties" if "Properties" in struct_type else "features"
                f.write(f"  {struct_type} {field_name};\n")

        if version == "Core14":
            f.write(f"std::vector<VkImageLayout> copy_src_layouts;\n")
            f.write(f"std::vector<VkImageLayout> copy_dst_layouts;\n")

        f.write("};\n\n")

    return vkjson_core_entries


def generate_memset_statements(f):
    """Generates memset statements for all independent Vulkan structs and core Vulkan versions.
    This initializes struct instances to zero before use.

    Example:
      memset(&properties, 0, sizeof(VkPhysicalDeviceProperties));
      VkPhysicalDeviceProperties properties;
    """
    entries = []

    # Process independent structs
    for dataclass_type in VK.EXTENSION_INDEPENDENT_STRUCTS:
        class_name = dataclass_type.__name__
        variable_name = get_struct_name(class_name)
        f.write(f"memset(&{variable_name}, 0, sizeof({class_name}));\n")
        entries.append(f"{class_name} {variable_name}")

    return entries


def generate_extension_struct_template():
    """Generates templates for extensions
    Example:
      template <typename Visitor>
      inline bool Iterate(Visitor* visitor, VkJsonKHRVariablePointers* structs) {
        return visitor->Visit("variablePointerFeaturesKHR",
                              &structs->variable_pointer_features_khr) &&
              visitor->Visit("variablePointersFeaturesKHR",
                              &structs->variable_pointers_features_khr);
      }
    """
    template_code = []

    for extension, struct_mappings in (VK.VULKAN_EXTENSIONS_AND_STRUCTS_MAPPING["extensions"]
            .items()):
        struct_type = get_vkjson_struct_name(extension)

        template_code.append(f"template <typename Visitor>")
        template_code.append(f"inline bool Iterate(Visitor* visitor, {struct_type}* structs) {{")
        template_code.append("  return ")

        visitor_calls = []
        for struct_map in struct_mappings:
            for struct_name in struct_map:
                json_field_name = struct_name.replace("VkPhysicalDevice", "")
                json_field_name = json_field_name[0].lower() + json_field_name[1:]

                # Special case renaming
                if json_field_name == "8BitStorageFeaturesKHR":
                    json_field_name = "bit8StorageFeaturesKHR"

                visitor_calls.append(
                    f'visitor->Visit("{json_field_name}", &structs->{get_struct_name(struct_name)})'
                )

        template_code.append(" &&\n         ".join(visitor_calls) + ";")
        template_code.append("}\n")

    return "\n".join(template_code)


def generate_core_template():
    """Generates templates for vulkan cores.
    template <typename Visitor>
    inline bool Iterate(Visitor* visitor, VkJsonCore11* core) {
      return visitor->Visit("properties", &core->properties) &&
            visitor->Visit("features", &core->features);
    }
    """
    template_code = []

    for version, struct_list in VK.VULKAN_CORES_AND_STRUCTS_MAPPING["versions"].items():
        struct_type = f"VkJson{version}"

        template_code.append(f"template <typename Visitor>")
        template_code.append(f"inline bool Iterate(Visitor* visitor, {struct_type}* core) {{")
        template_code.append("  return")

        visitor_calls = []
        for struct_map in struct_list:
            for struct_name in struct_map:
                member_name = "properties" if "Properties" in struct_name else "features"
                visitor_calls.append(f'visitor->Visit("{member_name}", &core->{member_name})')

        template_code.append(" &&\n         ".join(visitor_calls) + ";")
        template_code.append("}\n")

    return "\n".join(template_code)


def generate_struct_template(data_classes):
    """Generates templates for all the structs
    template <typename Visitor>
    inline bool Iterate(Visitor* visitor,
                        VkPhysicalDevicePointClippingProperties* properties) {
      return visitor->Visit("pointClippingBehavior",
                            &properties->pointClippingBehavior);
    }
    """
    template_code = []
    processed_classes = set()  # Track processed class names

    for dataclass_type in data_classes:
        struct_name = dataclass_type.__name__

        if struct_name in processed_classes:
            continue  # Skip already processed struct
        processed_classes.add(struct_name)

        struct_fields = dataclasses.fields(dataclass_type)
        template_code.append("template <typename Visitor>")

        # Determine the correct variable name based on the struct type
        struct_var = "properties" if "Properties" in struct_name else "features" if "Features" in struct_name else "limits" if "Limits" in struct_name else None

        if not struct_var:
            continue  # Skip structs that don't match expected patterns

        template_code.append(
            f"inline bool Iterate(Visitor* visitor, {struct_name}* {struct_var}) {{")
        template_code.append(f"return\n")

        visitor_calls = []
        for struct_field in struct_fields:
            field_name = struct_field.name
            field_type = struct_field.type

            if get_origin(field_type) is list:
                # Handle list types (VisitArray)
                size_field_name = VK.LIST_TYPE_FIELD_AND_SIZE_MAPPING[field_name]
                visitor_calls.append(
                    f'visitor->VisitArray("{field_name}", {struct_var}->{size_field_name}, &{struct_var}->{field_name})')
            else:
                # Handle other types (Visit)
                visitor_calls.append(f'visitor->Visit("{field_name}", &{struct_var}->{field_name})')

        template_code.append(" &&\n         ".join(visitor_calls) + ";")
        template_code.append("}\n\n")

    return "\n".join(template_code)


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
