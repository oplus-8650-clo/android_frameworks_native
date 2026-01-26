// Copyright (C) 2025 The Android Open Source Project
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

//! This module provides structures and functions for managing USB device information
//! and authorization state. It includes a representation of a USB device that combines
//! sysfs information with its current authorization status.

use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use thiserror::Error;
use ueventd::device::Device;

/// Custom error type for device information parsing.
#[derive(Error, Debug)]
pub enum DeviceInfoError {
    /// An I/O error occurred, typically when reading sysfs attributes.
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    /// An error occurred while parsing an integer value from a sysfs attribute.
    #[error("Failed to parse integer for attribute '{attribute}' with value '{value}': {source}")]
    ParseInt {
        /// The name of the attribute that caused the parsing error.
        attribute: String,
        /// The string value that failed to parse.
        value: String,
        /// The underlying `ParseIntError`.
        #[source]
        source: std::num::ParseIntError,
    },
    /// A sysfs attribute was found but its value was malformed or unexpected.
    #[error("Malformed attribute '{attribute}' with value '{value}'")]
    MalformedAttribute {
        /// The name of the attribute that was malformed.
        attribute: String,
        /// The malformed string value.
        value: String,
    },
}

/// Custom error type for device authorization.
#[derive(Error, Debug)]
pub enum AuthorizationError {
    /// An I/O error occurred during authorization (e.g., writing to sysfs).
    #[error("I/O error during authorization for device '{syspath}': {source}")]
    Io {
        /// The sysfs path of the device that caused the I/O error.
        syspath: String,
        /// The underlying `std::io::Error`.
        #[source]
        source: std::io::Error,
    },
    /// The authorization sysfs path does not exist for the device.
    #[error("Authorization path '{0}' does not exist.")]
    AuthorizationPathNotFound(String),
}

/// Represents a USB device with its authorization state.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct UsbDeviceInfoWithState {
    /// The core USB device information, typically from sysfs attributes.
    pub info: UsbAuthDeviceInfo,
    /// Current authorization status (mutable). True if authorized, false otherwise.
    pub authorized: bool,
    /// Whether the device is currently in the defer list (mutable).
    /// A deferred device is one where an authorization decision was not made and will need to be
    /// re-evaluated when the system conditions change. Defer can be the result of both an explicit
    /// "Defer" action as well as an Ask request returning a negative result.
    pub is_deferred: bool,
}

impl UsbDeviceInfoWithState {
    /// Creates a `UsbDeviceInfoWithState` from a `ueventd::device::Device`.
    ///
    /// This function reads various sysfs attributes to populate the `UsbAuthDeviceInfo`
    /// and initializes `authorized` and `is_deferred` to `false`.
    /// It returns an error if essential sysfs attributes cannot be read or parsed.
    #[allow(dead_code)]
    pub fn from_device(device: &Device) -> Result<Self, DeviceInfoError> {
        let bus_num = Self::get_u16_sysattr(device, "busnum")?;
        let dev_num = Self::get_u16_sysattr(device, "devnum")?;
        let vendor_id = Self::get_hex_u16_sysattr(device, "idVendor")?;
        let product_id = Self::get_hex_u16_sysattr(device, "idProduct")?;
        let device_class = Self::get_hex_u8_sysattr(device, "bDeviceClass")?;
        let serial_number = Self::get_string_sysattr(device, "serial");
        let manufacturer = Self::get_string_sysattr(device, "manufacturer");
        let product_name = Self::get_string_sysattr(device, "product");

        let bcd_device = Self::get_hex_u16_sysattr(device, "bcdDevice")?;
        let b_device_class = Self::get_hex_u8_sysattr(device, "bDeviceClass")?;
        let b_device_sub_class = Self::get_hex_u8_sysattr(device, "bDeviceSubClass")?;
        let b_device_protocol = Self::get_hex_u8_sysattr(device, "bDeviceProtocol")?;

        let b_interface_class = Self::get_hex_u8_sysattr(device, "bInterfaceClass")?;
        let b_interface_sub_class = Self::get_hex_u8_sysattr(device, "bInterfaceSubClass")?;
        let b_interface_protocol = Self::get_hex_u8_sysattr(device, "bInterfaceProtocol")?;
        let b_interface_number = Self::get_hex_u8_sysattr(device, "bInterfaceNumber")?;

        Ok(Self {
            info: UsbAuthDeviceInfo {
                syspath: device.syspath().to_string_lossy().to_string(),
                busNumber: bus_num as i32,
                deviceNumber: dev_num as i32,
                vendorId: vendor_id as i32,
                productId: product_id as i32,
                deviceClass: device_class as i8,
                serialNumber: serial_number,
                manufacturer,
                productName: product_name,
                bcdDevice: bcd_device as i32,
                bDeviceClass: b_device_class as i8,
                bDeviceSubClass: b_device_sub_class as i8,
                bDeviceProtocol: b_device_protocol as i8,
                bInterfaceClass: b_interface_class as i8,
                bInterfaceSubClass: b_interface_sub_class as i8,
                bInterfaceProtocol: b_interface_protocol as i8,
                bInterfaceNumber: b_interface_number as i8,
            },
            authorized: false,
            is_deferred: false,
        })
    }

    /// Helper to get a sysfs attribute as a string, defaulting to "" if missing.
    fn get_string_sysattr(device: &Device, attr_name: &str) -> String {
        device.sysattrs().get(attr_name).ok().unwrap_or_default().to_string()
    }

    /// Helper to get and parse a sysfs attribute as a u16 from decimal, defaulting to 0 if missing.
    /// Returns an error if the attribute is present but malformed.
    fn get_u16_sysattr(device: &Device, attr_name: &str) -> Result<u16, DeviceInfoError> {
        let Ok(s) = device.sysattrs().get(attr_name) else {
            return Ok(0);
        };
        s.trim().parse::<u16>().map_err(|e| DeviceInfoError::ParseInt {
            attribute: attr_name.to_string(),
            value: s.to_string(),
            source: e,
        })
    }

    /// Helper to get and parse a sysfs attribute as a u16 from hex, defaulting to 0 if missing.
    /// Returns an error if the attribute is present but malformed.
    fn get_hex_u16_sysattr(device: &Device, attr_name: &str) -> Result<u16, DeviceInfoError> {
        let Ok(s) = device.sysattrs().get(attr_name) else {
            return Ok(0);
        };
        let trimmed_s = s.trim().trim_start_matches("0x");
        u16::from_str_radix(trimmed_s, 16).map_err(|e| DeviceInfoError::ParseInt {
            attribute: attr_name.to_string(),
            value: s.to_string(),
            source: e,
        })
    }

    /// Helper to get and parse a sysfs attribute as a u8 from hex, defaulting to 0 if missing.
    /// Returns an error if the attribute is present but malformed.
    fn get_hex_u8_sysattr(device: &Device, attr_name: &str) -> Result<u8, DeviceInfoError> {
        let Ok(s) = device.sysattrs().get(attr_name) else {
            return Ok(0);
        };
        let trimmed_s = s.trim().trim_start_matches("0x");
        u8::from_str_radix(trimmed_s, 16).map_err(|e| DeviceInfoError::ParseInt {
            attribute: attr_name.to_string(),
            value: s.to_string(),
            source: e,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use ueventd::device::Device;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    // Creates a mock sysfs with a single USB device for testing.
    #[allow(clippy::too_many_arguments)]
    fn create_mock_sysfs_with_device(
        name: &'static str,
        bus_num: &'static str,
        dev_num: &'static str,
        id_vendor: &'static str,
        id_product: &'static str,
        device_class: &'static str,
        device_sub_class: &'static str,
        device_protocol: &'static str,
        device_product: &'static str,
        if_class: &'static str,
        if_sub_class: &'static str,
        if_protocol: &'static str,
    ) -> MockSysfs {
        let interface_name = format!("{}:1.0", name);
        let leaked_interface_name = Box::leak(interface_name.into_boxed_str());
        let device_files = HashMap::from([
            ("busnum", SysfsFile::RegularFile(bus_num)),
            ("devnum", SysfsFile::RegularFile(dev_num)),
            ("idVendor", SysfsFile::RegularFile(id_vendor)),
            ("idProduct", SysfsFile::RegularFile(id_product)),
            ("bDeviceClass", SysfsFile::RegularFile(device_class)),
            ("bcdDevice", SysfsFile::RegularFile(device_product)),
            ("bDeviceSubClass", SysfsFile::RegularFile(device_sub_class)),
            ("bDeviceProtocol", SysfsFile::RegularFile(device_protocol)),
            ("serial", SysfsFile::RegularFile("12345")),
            ("manufacturer", SysfsFile::RegularFile("Google")),
            ("product", SysfsFile::RegularFile("Mock USB Device")),
            ("bInterfaceClass", SysfsFile::RegularFile(if_class)),
            ("bInterfaceSubClass", SysfsFile::RegularFile(if_sub_class)),
            ("bInterfaceProtocol", SysfsFile::RegularFile(if_protocol)),
            ("bInterfaceNumber", SysfsFile::RegularFile("00")),
            (
                leaked_interface_name,
                SysfsFile::Dir(HashMap::from([
                    ("bInterfaceClass", SysfsFile::RegularFile(if_class)),
                    ("bInterfaceSubClass", SysfsFile::RegularFile(if_sub_class)),
                    ("bInterfaceProtocol", SysfsFile::RegularFile(if_protocol)),
                    ("bInterfaceNumber", SysfsFile::RegularFile("00")),
                ])),
            ),
            ("subsystem", SysfsFile::Symlink("../../../../bus/usb")),
            ("uevent", SysfsFile::RegularFile("DEVTYPE=usb_device\nSUBSYSTEM=usb")),
        ]);
        MockSysfs::new(SysfsFile::Dir(HashMap::from([(
            "bus/usb/devices",
            SysfsFile::Dir(HashMap::from([
                (
                    "usb1",
                    SysfsFile::Dir(HashMap::from([(
                        "authorized_default",
                        SysfsFile::RegularFile("1"),
                    )])),
                ),
                (name, SysfsFile::Dir(device_files)),
            ])),
        )])))
        .unwrap()
    }

    #[test]
    fn test_from_device_success() -> Result<(), DeviceInfoError> {
        let mock_sys = create_mock_sysfs_with_device(
            "1-1", "1", "2", "18d1", "4ee7", "09", "00", "00", "0223", "03", "01", "02",
        );
        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();
        let device_info = UsbDeviceInfoWithState::from_device(&device)?;

        assert_eq!(device_info.info.busNumber, 1);
        assert_eq!(device_info.info.deviceNumber, 2);
        assert_eq!(device_info.info.vendorId, 0x18d1);
        assert_eq!(device_info.info.productId, 0x4ee7);
        assert_eq!(device_info.info.deviceClass, 0x09);
        assert_eq!(device_info.info.serialNumber, "12345");
        assert_eq!(device_info.info.manufacturer, "Google");
        assert_eq!(device_info.info.productName, "Mock USB Device");
        assert_eq!(device_info.info.bcdDevice, 0x0223);
        assert_eq!(device_info.info.bDeviceClass, 0x09);
        assert_eq!(device_info.info.bDeviceSubClass, 0x00);
        assert_eq!(device_info.info.bDeviceProtocol, 0x00);
        assert_eq!(device_info.info.bInterfaceClass, 0x03);
        assert_eq!(device_info.info.bInterfaceSubClass, 0x01);
        assert_eq!(device_info.info.bInterfaceProtocol, 0x02);
        assert_eq!(device_info.info.bInterfaceNumber, 0);
        assert!(!device_info.authorized);
        assert!(!device_info.is_deferred);

        Ok(())
    }

    #[test]
    fn test_from_device_missing_attributes() -> Result<(), DeviceInfoError> {
        let mock_sys = create_mock_sysfs_with_device(
            "1-1", "1", "2", "18d1", "4ee7", "", "00", "00", "0223", "03", "01", "02",
        ); // HID
        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();
        let device_info_err = UsbDeviceInfoWithState::from_device(&device).unwrap_err();

        assert!(
            matches!(device_info_err, DeviceInfoError::ParseInt { attribute, value, source } if attribute == "bDeviceClass" && value.is_empty() && source.to_string() == "cannot parse integer from empty string")
        );

        Ok(())
    }

    #[test]
    fn test_from_device_malformed_hex() -> Result<(), DeviceInfoError> {
        let mock_sys = create_mock_sysfs_with_device(
            "1-1",
            "1",
            "2",
            "18d1",
            "4ee7",
            "09",
            "00",
            "00",
            "not-a-hex-value",
            "03",
            "01",
            "02",
        );
        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();
        let result_err = UsbDeviceInfoWithState::from_device(&device).unwrap_err();

        assert!(
            matches!(result_err, DeviceInfoError::ParseInt { attribute, value, source } if attribute == "bcdDevice" && value == "not-a-hex-value" && source.to_string() == "invalid digit found in string")
        );

        Ok(())
    }
}
