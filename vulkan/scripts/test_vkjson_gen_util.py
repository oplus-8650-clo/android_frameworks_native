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
This file contains unit tests for vkjson_gen_util.py
Each test class focuses on one specific util function.

Temporary location for this file, pending CI/CD integration.
"""

import unittest
from unittest.mock import Mock, patch

import vkjson_gen_util as src


class TestGetCopyrightWarnings(unittest.TestCase):

    def test_checks_for_c_comment_format(self):
        stripped_block = src.get_copyright_warnings().strip()
        is_block_comment = stripped_block.startswith("/*") and stripped_block.endswith("*/")

        is_line_comment = all(
            not line.strip() or line.strip().startswith("//")
            for line in stripped_block.splitlines()
        )

        self.assertTrue(is_block_comment or is_line_comment)

    def test_checks_for_essential_components(self):
        result = src.get_copyright_warnings()

        self.assertTrue("Copyright (c) 2015-2016 The Khronos Group Inc." in result)
        self.assertTrue("Copyright (c) 2015-2016 Valve Corporation" in result)
        self.assertTrue("Copyright (c) 2015-2016 LunarG, Inc." in result)
        self.assertTrue("Copyright (c) 2015-2016 Google, Inc." in result)
        self.assertTrue("Licensed under the Apache License, Version 2.0" in result)
        self.assertTrue("http://www.apache.org/licenses/LICENSE-2.0" in result)
        self.assertTrue("WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND" in result)


class TestGetVkjsonStructName(unittest.TestCase):

    def test_KHR_extension(self):
        self.assertEqual(
            "VkJsonKHRShaderFloat16Int8",
            src.get_vkjson_struct_name("VK_KHR_shader_float16_int8")
        )

    def test_EXT_extension(self):
        self.assertEqual(
            "VkJsonExtTransformFeedback",
            src.get_vkjson_struct_name("VK_EXT_transform_feedback"),
        )

    def test_IMG_extension(self):
        self.assertEqual(
            "VkJsonIMGRelaxedLineRasterization",
            src.get_vkjson_struct_name("VK_IMG_relaxed_line_rasterization")
        )

    def test_extension_with_unknown_prefix(self):
        self.assertEqual(
            "VkJsonExtVKNVRayTracing",
            src.get_vkjson_struct_name("VK_NV_ray_tracing")
        )

    def test_extension_with_numeric_suffixes(self):
        self.assertEqual(
            "VkJsonExtImage2dViewOf3d",
            src.get_vkjson_struct_name("VK_EXT_image_2d_view_of_3d")
        )


class TestGetVkjsonStructVariableName(unittest.TestCase):

    def test_KHR_extension(self):
        self.assertEqual(
            "khr_shader_float16_int8",
            src.get_vkjson_struct_variable_name("VK_KHR_shader_float16_int8"),
        )

    def test_EXT_extension(self):
        self.assertEqual(
            "ext_transform_feedback",
            src.get_vkjson_struct_variable_name("VK_EXT_transform_feedback"),
        )

    def test_IMG_extension(self):
        self.assertEqual(
            "img_relaxed_line_rasterization",
            src.get_vkjson_struct_variable_name("VK_IMG_relaxed_line_rasterization")
        )

    def test_extension_with_unknown_prefix(self):
        self.assertEqual(
            "vk_nv_ray_tracing",
            src.get_vkjson_struct_variable_name("VK_NV_ray_tracing")
        )

    def test_extension_with_numeric_prefixes(self):
        self.assertEqual(
            "ext_image_2d_view_of_3d",
            src.get_vkjson_struct_variable_name("VK_EXT_image_2d_view_of_3d"),
        )


class TestGetStructName(unittest.TestCase):

    def test_single_word_suffix(self):
        self.assertEqual(
            "properties",
            src.get_struct_name("VkPhysicalDeviceProperties"),
        )

    def test_multi_word_suffix(self):
        self.assertEqual(
            "sampler_ycbcr_conversion_features",
            src.get_struct_name("VkPhysicalDeviceSamplerYcbcrConversionFeatures"),
        )

    def test_KHR_suffix(self):
        self.assertEqual(
            "float_controls_properties_khr",
            src.get_struct_name("VkPhysicalDeviceFloatControlsPropertiesKHR"),
        )

    def test_EXT_suffix(self):
        self.assertEqual(
            "line_rasterization_features_ext",
            src.get_struct_name("VkPhysicalDeviceLineRasterizationFeaturesEXT")
        )

    def test_IMG_suffix(self):
        self.assertEqual(
            "relaxed_line_rasterization_features_img",
            src.get_struct_name("VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG")
        )

    def test_suffix_starting_with_ID(self):
        self.assertEqual(
            "id_properties",
            src.get_struct_name("VkPhysicalDeviceIDProperties")
        )
        # TODO - Fix source code
        # self.assertEqual(
        #     "id_features",
        #     src.get_struct_name("VkPhysicalDeviceIDFeatures")
        # )

    def test_suffix_containing_number(self):
        self.assertEqual(
            "vulkan11_properties",
            src.get_struct_name("VkPhysicalDeviceVulkan11Properties"),
        )

    def test_suffix_starting_with_2d(self):
        self.assertEqual(
            "_2d_view_features_ext",
            src.get_struct_name("VkPhysicalDevice2DViewFeaturesEXT"),
        )

    def test_suffix_starting_with_3d(self):
        self.assertEqual(
            "_3d_features_ext",
            src.get_struct_name("VkPhysicalDevice3DFeaturesEXT"),
        )

    def test_suffix_containing_2d_3d(self):
        self.assertEqual(
            "image_2d_view_of_3d_features_ext",
            src.get_struct_name("VkPhysicalDeviceImage2DViewOf3DFeaturesEXT")
        )

    def test_suffix_starting_with_8bit_16bit(self):
        self.assertEqual(
            "bit8_storage_features_khr",
            src.get_struct_name("VkPhysicalDevice8BitStorageFeaturesKHR"),
        )
        # TODO - Fix source code
        # self.assertEqual(
        #     "bit16_storage_properties",
        #     src.get_struct_name("VkPhysicalDevice16BitStorageProperties")
        # )

    def test_special_case_of_memory_properties(self):
        self.assertEqual(
            "memory",
            src.get_struct_name("VkPhysicalDeviceMemoryProperties"),
        )


class TestEmitStructVisitsByVkVersion(unittest.TestCase):

    def setUp(self):
        self.mock_file = Mock()
        self.mock_vk_patcher = patch('vkjson_gen_util.VK')
        self.mock_get_struct_name_patcher = patch('vkjson_gen_util.get_struct_name')

        get_struct_name_return_map = {
            "VkPhysicalDeviceLineFeatures": "line_features",
            "VkPhysicalDeviceSubgroupProperties": "subgroup_properties",
            "VkPhysicalDeviceMultiviewProperties": "multiview_properties",
            "VkPhysicalDeviceIDProperties": "id_properties",
            "VkPhysicalDeviceMultiviewFeatures": "multiview_features"
        }
        self.mock_get_struct_name = self.mock_get_struct_name_patcher.start()
        self.mock_get_struct_name.side_effect = lambda struct_name: get_struct_name_return_map[struct_name]

        mock_vk = self.mock_vk_patcher.start()
        mock_vk.VULKAN_VERSIONS_AND_STRUCTS_MAPPING = {
            "VK_VERSION_1_0": [
                {"VkPhysicalDeviceLineFeatures": ""}
            ],
            "VK_VERSION_1_1": [
                {
                    "VkPhysicalDeviceSubgroupProperties": "VK_STRUCT_SUBGROUP"
                }
            ],
            "VK_VERSION_1_2": [
                {
                    "VkPhysicalDeviceMultiviewProperties": "VK_STRUCT_MULTIVIEW",
                    "VkPhysicalDeviceIDProperties": "VK_STRUCT_DEVICE_ID"
                },
                {
                    "VkPhysicalDeviceMultiviewFeatures": "VK_STRUCT_MULTIVIEW_FEATURES"
                }
            ]
        }

    def tearDown(self):
        self.mock_vk_patcher.stop()
        self.mock_get_struct_name_patcher.stop()

    def test_handles_single_matching_struct(self):
        expected_lines = (
            f'visitor->Visit("subgroupProperties", &device->subgroup_properties) &&\n'
        )

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_1")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )

    def test_handles_multiple_matching_structs(self):
        expected_lines = (
            f'visitor->Visit("multiviewProperties", &device->multiview_properties) &&\n'
            f'visitor->Visit("idProperties", &device->id_properties) &&\n'
            f'visitor->Visit("multiviewFeatures", &device->multiview_features) &&\n'
        )

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_2")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )

    def test_handles_empty_struct_type(self):
        expected_lines = f'visitor->Visit("lineFeatures", &device->line_features) &&\n'

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_0")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )

    def test_handles_struct_var_with_single_word(self):
        self.mock_get_struct_name.side_effect = None
        self.mock_get_struct_name.return_value = "memory"
        expected_lines = f'visitor->Visit("memory", &device->memory) &&\n'

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_1")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )

    # TODO - Fix source code
    def handles_struct_var_contains_number(self):
        self.mock_get_struct_name.side_effect = None
        self.mock_get_struct_name.return_value = "image_2d_view_of_3d_features_ext"
        expected_lines = f'visitor->Visit("image2dViewOf3dFeaturesExt", &device->image_2d_view_of_3d_features_ext) &&\n'

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_1")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )

    # TODO - Fix source code
    def handles_struct_var_starts_with_underscore(self):
        self.mock_get_struct_name.side_effect = None
        self.mock_get_struct_name.return_value = "_3d_features_ext"
        expected_lines = f'visitor->Visit("3dFeaturesExt", &device->_3d_features_ext) &&\n'

        src.emit_struct_visits_by_vk_version(self.mock_file, "VK_VERSION_1_1")
        self.assertEqual(
            expected_lines, "".join([c.args[0] for c in self.mock_file.write.call_args_list])
        )


class TestGenerateVkCoreStructsInitCode(unittest.TestCase):

    def setUp(self):
        self.mock_vk_patcher = patch('vkjson_gen_util.VK')
        mock_vk = self.mock_vk_patcher.start()
        mock_vk.VULKAN_CORES_AND_STRUCTS_MAPPING = {
            "versions": {
                "Core11": [
                    {"VkPhysicalDeviceVulkan11Properties": "VK_TYPE_11_PROPS"},
                    {"VkPhysicalDeviceVulkan11Features": "VK_TYPE_11_FEATS"}
                ],
                "Core12": [
                    {"VkPhysicalDeviceVulkan12Properties1": "VK_TYPE_12_PROPS_1"},
                    {"VkPhysicalDeviceVulkan12Features1": "VK_TYPE_12_FEATS_1"},
                    {"VkPhysicalDeviceVulkan12Properties2": "VK_TYPE_12_PROPS_2"},
                    {"VkPhysicalDeviceVulkan12Features2": "VK_TYPE_12_FEATS_2"},
                    {"SomeThingElse": "SOME_OTHER_STRUCT"}
                ]
            }
        }

    def tearDown(self):
        self.mock_vk_patcher.stop()

    def test_handles_single_matching_struct(self):
        actual_props, actual_feats = src.generate_vk_core_structs_init_code("Core11")

        self.assertEqual(
            (
                "device.core11.properties.sType = VK_TYPE_11_PROPS;\n"
                "device.core11.properties.pNext = properties.pNext;\n"
                "properties.pNext = &device.core11.properties;\n\n"
            ),
            actual_props
        )
        self.assertEqual(
            (
                "device.core11.features.sType = VK_TYPE_11_FEATS;\n"
                "device.core11.features.pNext = features.pNext;\n"
                "features.pNext = &device.core11.features;\n\n"
            ),
            actual_feats
        )

    def test_handles_multiple_matching_structs(self):
        actual_props, actual_feats = src.generate_vk_core_structs_init_code("Core12")

        self.assertEqual(
            (
                "device.core12.properties.sType = VK_TYPE_12_PROPS_1;\n"
                "device.core12.properties.pNext = properties.pNext;\n"
                "properties.pNext = &device.core12.properties;\n\n\n"
                "device.core12.properties.sType = VK_TYPE_12_PROPS_2;\n"
                "device.core12.properties.pNext = properties.pNext;\n"
                "properties.pNext = &device.core12.properties;\n\n"
            ),
            actual_props
        )
        self.assertEqual(
            (
                "device.core12.features.sType = VK_TYPE_12_FEATS_1;\n"
                "device.core12.features.pNext = features.pNext;\n"
                "features.pNext = &device.core12.features;\n\n\n"
                "device.core12.features.sType = VK_TYPE_12_FEATS_2;\n"
                "device.core12.features.pNext = features.pNext;\n"
                "features.pNext = &device.core12.features;\n\n"
            ),
            actual_feats
        )

    def test_handles_no_matching_struct(self):
        actual_props, actual_feats = src.generate_vk_core_structs_init_code("Core13")

        self.assertEqual("", actual_props)
        self.assertEqual("", actual_feats)


class TestGenerateVkExtensionStructsInitCode(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.mapping = {
            "VK_KHR_driver_state": [
                {"VkPhysicalDeviceDriverPropertiesKHR": "VK_STRUCT_DRIVER"}
            ],
            "VK_KHR_variable_pointers": [
                {"VkPhysicalDeviceVariablePointerFeaturesKHR": "VK_STRUCT_VARIABLE"},
                {"VkPhysicalDeviceVariablePointersFeaturesKHR": "VK_STRUCT_VARIABLES"},
                {"SomeThingElse": "SOME_OTHER_STRUCT"}
            ],
            "VK_EXT_custom_border_color": [
                {"VkPhysicalDeviceCustomBorderColorFeatsEXT": "VK_STRUCT_COLOR"}
            ],
            "VK_KHR_shader_float_controls": [
                {"VkPhysicalDeviceFloatControlsFeatsKHR": "VK_STRUCT_FLOAT"}
            ]
        }

    def test_handles_single_extension_single_struct(self):
        extension_var = src.get_vkjson_struct_variable_name("VK_KHR_driver_state")
        struct_name = src.get_struct_name("VkPhysicalDeviceDriverPropertiesKHR")

        self.assertEqual(
            (
                '  if (HasExtension("VK_KHR_driver_state", device.extensions)) {\n'
                f"    device.{extension_var}.reported = true;\n"
                f"    device.{extension_var}.{struct_name}.sType = VK_STRUCT_DRIVER;\n"
                f"    device.{extension_var}.{struct_name}.pNext = prop.pNext;\n"
                f"    prop.pNext = &device.{extension_var}.{struct_name};\n"
                '  }\n'
            ),
            src.generate_vk_extension_structs_init_code(self.mapping, "Properties", "prop")
        )

    def test_handles_single_extension_multiple_structs(self):
        extension_var = src.get_vkjson_struct_variable_name("VK_KHR_variable_pointers")
        struct_name1 = src.get_struct_name("VkPhysicalDeviceVariablePointerFeaturesKHR")
        struct_name2 = src.get_struct_name("VkPhysicalDeviceVariablePointersFeaturesKHR")

        self.assertEqual(
            (
                '  if (HasExtension("VK_KHR_variable_pointers", device.extensions)) {\n'
                f"    device.{extension_var}.reported = true;\n"
                f"    device.{extension_var}.{struct_name1}.sType = VK_STRUCT_VARIABLE;\n"
                f"    device.{extension_var}.{struct_name1}.pNext = feat.pNext;\n"
                f"    feat.pNext = &device.{extension_var}.{struct_name1};\n"
                f"    device.{extension_var}.{struct_name2}.sType = VK_STRUCT_VARIABLES;\n"
                f"    device.{extension_var}.{struct_name2}.pNext = feat.pNext;\n"
                f"    feat.pNext = &device.{extension_var}.{struct_name2};\n"
                '  }\n'
            ),
            src.generate_vk_extension_structs_init_code(self.mapping, "Features", "feat")
        )

    def test_handles_no_matching_struct(self):
        self.assertEqual(
            "",
            src.generate_vk_extension_structs_init_code(self.mapping, "Extension", "ext")
        )

    def test_handles_multiple_extensions(self):
        extension_var1 = src.get_vkjson_struct_variable_name("VK_EXT_custom_border_color")
        struct_name1 = src.get_struct_name("VkPhysicalDeviceCustomBorderColorFeatsEXT")

        extension_var2 = src.get_vkjson_struct_variable_name("VK_KHR_shader_float_controls")
        struct_name2 = src.get_struct_name("VkPhysicalDeviceFloatControlsFeatsKHR")

        self.assertEqual(
            (
                '  if (HasExtension("VK_EXT_custom_border_color", device.extensions)) {\n'
                f"    device.{extension_var1}.reported = true;\n"
                f"    device.{extension_var1}.{struct_name1}.sType = VK_STRUCT_COLOR;\n"
                f"    device.{extension_var1}.{struct_name1}.pNext = feat.pNext;\n"
                f"    feat.pNext = &device.{extension_var1}.{struct_name1};\n"
                '  }\n\n'
                '  if (HasExtension("VK_KHR_shader_float_controls", device.extensions)) {\n'
                f"    device.{extension_var2}.reported = true;\n"
                f"    device.{extension_var2}.{struct_name2}.sType = VK_STRUCT_FLOAT;\n"
                f"    device.{extension_var2}.{struct_name2}.pNext = feat.pNext;\n"
                f"    feat.pNext = &device.{extension_var2}.{struct_name2};\n"
                '  }\n'
            ),
            src.generate_vk_extension_structs_init_code(self.mapping, "Feats", "feat")
        )


class TestGenerateVkVersionStructsInitialization(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.version_data = [
            {
                "VkPhysicalDeviceIDProperties": "VK_STRUCTURE_TYPE_DEVICE_ID",
                "VkPhysicalDeviceMultiviewFeatures": "VK_STRUCTURE_TYPE_MULTIVIEW"
            },
            {
                "VkPhysicalDeviceMaintenance3Properties": "VK_STRUCTURE_TYPE_MAINTENANCE_3"
            }
        ]

        cls.prop1_struct_name = src.get_struct_name("VkPhysicalDeviceIDProperties")
        cls.prop2_struct_name = src.get_struct_name("VkPhysicalDeviceMaintenance3Properties")
        cls.feat_struct_name = src.get_struct_name("VkPhysicalDeviceMultiviewFeatures")

    def test_handles_single_matching_struct(self):
        self.assertEqual(
            (
                f"device.{self.feat_struct_name}.sType = VK_STRUCTURE_TYPE_MULTIVIEW;\n"
                f"device.{self.feat_struct_name}.pNext = feat.pNext;\n"
                f"feat.pNext = &device.{self.feat_struct_name};\n"
            ),
            src.generate_vk_version_structs_initialization(self.version_data, "Features", "feat")
        )

    def test_handles_multiple_matching_structs(self):
        self.assertEqual(
            (
                f"device.{self.prop1_struct_name}.sType = VK_STRUCTURE_TYPE_DEVICE_ID;\n"
                f"device.{self.prop1_struct_name}.pNext = prop.pNext;\n"
                f"prop.pNext = &device.{self.prop1_struct_name};\n\n"
                f"device.{self.prop2_struct_name}.sType = VK_STRUCTURE_TYPE_MAINTENANCE_3;\n"
                f"device.{self.prop2_struct_name}.pNext = prop.pNext;\n"
                f"prop.pNext = &device.{self.prop2_struct_name};\n"
            ),
            src.generate_vk_version_structs_initialization(self.version_data, "Properties", "prop")
        )

    def test_handles_no_matching_struct(self):
        self.assertEqual(
            "",
            src.generate_vk_version_structs_initialization(self.version_data, "Custom", "cust")
        )
