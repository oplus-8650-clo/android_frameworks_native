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

//! Manages USB device authorization policies and decisions.

use crate::authorization;
use crate::device_info::UsbDeviceInfoWithState;
use crate::parser::{Parser, PolicyLoadError};
use crate::rules::{
    Action, DeviceAttributes, DeviceId, InterfaceAttribute, InterfaceType, Policy, Rule,
};
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationStatus::UsbAuthorizationStatus;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use log::debug;
use std::fs;
use std::path::{Path, PathBuf};
use thiserror::Error;
use ueventd::device::Device;

/// Represents the possible errors that can occur in the `UsbDeviceAuthManager`.
#[derive(Error, Debug)]
pub enum Error {
    /// An I/O error occurred while interacting with the file system.
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    /// An error occurred during device authorization.
    #[error("Authorization error: {0}")]
    Authorization(#[from] crate::device_info::AuthorizationError),
    /// An error occurred while parsing the USB authorization policy.
    #[error("Policy parsing error: {0}")]
    Parse(#[from] PolicyLoadError),
    /// The specified device was not found.
    #[error("Device not found: {0}")]
    DeviceNotFound(String),
}

/// Path to the usbcore authorized_default parameter.
const USBCORE_AUTHORIZED_DEFAULT_PATH: &str = "module/usbcore/parameters/authorized_default";

/// Path to the USB devices directory.
const USB_DEVICES_PATH: &str = "bus/usb/devices/";

/// Relative path to the static USB authorization policy file.
const USB_AUTH_POLICY_CONF_RELATIVE_PATH: &str = "usb_auth/policy.conf";

/// Relative path to the internal devices configuration file.
const USB_AUTH_INTERNAL_DEVICES_CONF_RELATIVE_PATH: &str = "usb_auth/internal_devices.conf";

// Placeholder for the list of internal devices.
struct InternalDevices;

/// Manages the lists of USB devices based on their authorization state.
pub struct UsbDeviceAuthManager {
    /// The root directory for the system files. Typically /sys, but might be
    /// different for testing.
    root_sys_dir: PathBuf,
    /// The root directory for the etc files. Typically /etc, but might be
    /// different for testing.
    root_etc_dir: PathBuf,
    /// Devices that have been processed and their authorization state determined.
    processed_devices: Vec<UsbDeviceInfoWithState>,
    /// Devices whose authorization was deferred pending a system state change.
    deferred_devices: Vec<UsbDeviceInfoWithState>,
    /// Devices that require user interaction for authorization.
    ask_devices: Vec<UsbDeviceInfoWithState>,
    /// The current system authorization state.
    system_state: UsbAuthorizationSystemState,
    /// The static policy rules used for device authorization.
    policy: Policy,
}

impl UsbDeviceAuthManager {
    /// Returns a clone of the list of processed devices.
    pub fn processed_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.processed_devices.clone()
    }

    /// Returns a clone of the list of deferred devices.
    pub fn deferred_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.deferred_devices.clone()
    }

    /// Returns a clone of the list of devices requiring user interaction.
    pub fn ask_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.ask_devices.clone()
    }

    /// Returns a reference to the current system state for USB authorization.
    pub fn system_state(&self) -> &UsbAuthorizationSystemState {
        &self.system_state
    }

    /// Returns a reference to the static policy rules for USB device authorization.
    pub fn policy(&self) -> &Policy {
        &self.policy
    }

    /// Returns a list of authorized devices from the processed devices list.
    pub fn get_authorized_devices(&self) -> Vec<UsbAuthDeviceInfo> {
        self.processed_devices.iter().filter(|d| d.authorized).map(|d| d.info.clone()).collect()
    }

    /// Returns the authorization status of a device.
    pub fn get_authorization_status(&self, device_syspath: &str) -> UsbAuthorizationStatus {
        if self.processed_devices.iter().any(|d| d.info.syspath == device_syspath && d.authorized) {
            return UsbAuthorizationStatus::AUTHORIZED;
        }
        UsbAuthorizationStatus::DENIED
    }

    /// Creates a new `UsbDeviceAuthManager` and performs initial setup.
    pub fn new() -> Result<Self, Error> {
        Self::with_paths("/sys", "/etc")
    }

    /// Creates a new `UsbDeviceAuthManager` with specified root directories and performs initial setup.
    /// This function is useful for testing with mock file systems.
    pub fn with_paths<P: AsRef<Path>, Q: AsRef<Path>>(
        root_sys_dir_path: P,
        root_etc_dir_path: Q,
    ) -> Result<Self, Error> {
        let mut manager = Self {
            processed_devices: Vec::new(),
            deferred_devices: Vec::new(),
            ask_devices: Vec::new(),
            system_state: UsbAuthorizationSystemState::BOOTED,
            root_sys_dir: root_sys_dir_path.as_ref().to_path_buf(),
            root_etc_dir: root_etc_dir_path.as_ref().to_path_buf(),
            policy: create_default_policy(),
        };
        debug!("Setting initial USB authorization state to deny all devices.");
        manager.set_default_to_deny_for_new_devices()?;
        debug!("Loading static policy");
        manager.load_static_policy()?;
        debug!("Loading internal devices list");
        manager.load_internal_devices()?;
        debug!("System state set to Booted");
        debug!("Binder service will be set up in main.");
        Ok(manager)
    }

    /// Loads the list of internal devices from a file or ACPI.
    fn load_internal_devices(&mut self) -> Result<(), Error> {
        debug!("Loading internal devices list");
        let internal_devices_file_path =
            self.root_etc_dir.join(USB_AUTH_INTERNAL_DEVICES_CONF_RELATIVE_PATH);
        if internal_devices_file_path.exists() {
            let _ = load_internal_devices_from_file(&internal_devices_file_path)?;
        } else {
            debug!("Internal devices file not found. Assuming no internal devices.");
        }
        Ok(())
    }
    /// Loads the static USB policy from a file or creates a default one if the file does not exist.
    fn load_static_policy(&mut self) -> Result<(), Error> {
        debug!("Loading static USB policy");
        let policy_file_path = self.root_etc_dir.join(USB_AUTH_POLICY_CONF_RELATIVE_PATH);
        if policy_file_path.exists() {
            debug!("Policy file found at {:?}. Loading static policy.", policy_file_path);
            self.policy = Parser::parse_rules_from_file(&policy_file_path)?.policy().clone();
        } else {
            debug!("Policy file not found. Creating default policy to allow HID, HUB and ethernet devices.");
            self.policy = create_default_policy();
        }
        Ok(())
    }
    /// Iterates through existing USB devices and sets their 'authorized_default' to '0' (deny).
    /// This is called during the initial setup of the UsbDeviceAuthManager.
    fn set_default_to_deny_for_new_devices(&mut self) -> Result<(), Error> {
        debug!("Setting default to deny for new devices");
        self.set_module_default_to_deny()?;
        debug!("Setting default to deny for existing USB devices");
        let usb_devices_path = self.root_sys_dir.join(USB_DEVICES_PATH);
        for path in fs::read_dir(usb_devices_path)?.filter_map(|e| e.ok().map(|entry| entry.path()))
        {
            let name = if let Some(name) = path.file_name().and_then(|s| s.to_str()) {
                name
            } else {
                continue;
            };
            // Filter for device directories like "usbX"
            if !name.starts_with("usb")
                || name.len() <= 3
                || !name[3..].chars().all(|c| c.is_ascii_digit())
            {
                continue;
            }
            let auth_path = path.join("authorized_default");
            if auth_path.exists() {
                fs::write(&auth_path, "0").map_err(|e| {
                    log::error!(
                        "Failed to set authorized_default for device {}: {}",
                        auth_path.display(),
                        e
                    );
                    e
                })?;
            }
        }
        Ok(())
    }

    /// Sets the global usbcore authorized_default parameter to '0' (deny).
    fn set_module_default_to_deny(&self) -> Result<(), Error> {
        let path = self.root_sys_dir.join(USBCORE_AUTHORIZED_DEFAULT_PATH);
        if let Err(e) = fs::write(&path, "0") {
            log::error!(
                "Failed to set usbcore authorized_default parameter at {}: {}",
                path.display(),
                e
            );
            return Err(Error::Io(e));
        }
        Ok(())
    }

    /// Re-evaluates deferred devices when the system state changes.
    fn reevaluate_deferred_devices(&mut self) {
        let deferred = std::mem::take(&mut self.deferred_devices);
        for device in deferred {
            self.process_usb_device(device);
        }
    }

    /// Re-evaluates devices requiring user interaction when the system state changes.
    fn reevaluate_ask_devices(&mut self) {
        let ask = std::mem::take(&mut self.ask_devices);
        for device in ask {
            self.process_usb_device(device);
        }
    }

    /// Re-evaluates authorized devices when the system state changes.
    fn reevaluate_authorized_devices(&mut self) {
        let processed = std::mem::take(&mut self.processed_devices);
        for device in processed {
            self.process_usb_device(device);
        }
    }

    /// Handles system state changes by re-evaluating all device lists.
    pub fn handle_system_state_change(&mut self, system_state: UsbAuthorizationSystemState) {
        if system_state == self.system_state {
            return;
        }
        self.system_state = system_state;
        // If the system state changes to booted, re-evaluate authorized devices. This is done
        // because the system state is set to booted  when the user logs out of the system.
        if system_state == UsbAuthorizationSystemState::BOOTED {
            self.reevaluate_authorized_devices();
        }
        self.reevaluate_deferred_devices();
        self.reevaluate_ask_devices();
    }

    /// Processes a newly added USB device, determines its authorization state, and adds it to the
    /// appropriate list within the `UsbDeviceManager`.
    pub fn process_usb_device(&mut self, mut device_with_state: UsbDeviceInfoWithState) {
        let action =
            authorization::authorize_device(&device_with_state, &self.policy, self.system_state);
        device_with_state.authorized = action == Action::Allow;
        device_with_state.is_deferred = action == Action::Defer;
        match action {
            Action::Defer => self.deferred_devices.push(device_with_state),
            Action::Ask => self.ask_devices.push(device_with_state),
            _ => self.processed_devices.push(device_with_state),
        }
    }

    /// Updates the authorization status of a device that is awaiting user authorization.
    ///
    /// If the device is found in the `ask_devices` list, it is moved to the `processed_devices`
    /// list with its authorization status updated.
    ///
    /// # Returns
    ///
    /// * `Ok(())` if the device was found and updated.
    /// * `Err(Error::DeviceNotFound)` if the device was not found in the `ask_devices` list.
    pub fn update_authorization_status(
        &mut self,
        device_syspath: &str,
        authorized: bool,
    ) -> Result<(), Error> {
        if let Some(pos) = self.ask_devices.iter().position(|d| d.info.syspath == device_syspath) {
            let mut device_with_state = self.ask_devices.remove(pos);
            device_with_state.authorized = authorized;
            authorization::authorize_device_via_sysfs(&device_with_state.info.syspath, authorized)?;
            self.processed_devices.push(device_with_state);
            Ok(())
        } else {
            Err(Error::DeviceNotFound(device_syspath.to_string()))
        }
    }

    /// Adds a new USB device to the manager.
    ///
    /// This function converts the provided `Device` into a `UsbDeviceInfoWithState`,
    /// processes it to determine its authorization state, and adds it to the
    /// appropriate internal list (`processed_devices`, `deferred_devices`, or `ask_devices`).
    ///
    /// # Arguments
    /// * `device` - A reference to the `Device` object representing the newly added USB device.
    ///
    /// # Returns
    /// * `Ok(())` if the device was successfully added and processed.
    /// * `Err(Error::DeviceNotFound)` if there was an issue getting device info from the `Device` object.
    pub fn add_usb_device(&mut self, device: &Device) -> Result<(), Error> {
        debug!("Add USB device: {:?}", device.name());
        match UsbDeviceInfoWithState::from_device(device) {
            Ok(device_with_state) => {
                self.process_usb_device(device_with_state);
                Ok(())
            }
            Err(_) => Err(Error::DeviceNotFound(device.syspath().display().to_string())),
        }
    }

    /// Removes a USB device from all internal lists (`processed_devices`, `deferred_devices`,
    /// and `ask_devices`).
    ///
    /// This function is typically called when a USB device is disconnected from the system.
    ///
    /// # Arguments
    /// * `device` - The `Device` object representing the USB device to be removed.
    pub fn remove_usb_device(&mut self, device: &Device) -> Result<(), Error> {
        if let Some(device_syspath) = device.syspath().to_str() {
            self.processed_devices.retain(|d| d.info.syspath != device_syspath);
            self.deferred_devices.retain(|d| d.info.syspath != device_syspath);
            self.ask_devices.retain(|d| d.info.syspath != device_syspath);
            Ok(())
        } else {
            debug!("Failed to get syspath for device: {:?}", device.name());
            Err(Error::DeviceNotFound(device.syspath().display().to_string()))
        }
    }
}

/// Loads the list of internal devices from the given file path.
fn load_internal_devices_from_file(_path: &Path) -> Result<InternalDevices, Error> {
    // TODO: Implement device list parsing from the file.
    Ok(InternalDevices)
}

/// Creates a default policy allowing HID, HUB, Ethernet devices, and a specific Realtek USB Ethernet adapter.
fn create_default_policy() -> Policy {
    let mut policy = Policy::new();
    policy
        .add_rule(Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![
                        InterfaceType { class: 0x03, subclass: None, protocol: None }, // HID
                        InterfaceType { class: 0x09, subclass: None, protocol: None }, // HUB
                        // Communications and CDC Control (Ethernet)
                        InterfaceType { class: 0x02, subclass: None, protocol: None },
                    ],
                )),
                ..Default::default()
            }),
            condition: None, // No specific condition for this rule.
        })
        .expect("Failed to add default rule for HID, HUB, and Ethernet devices");
    policy
        .add_rule(Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_id: Some(DeviceId { vendor_id: Some(0x0bda), product_id: Some(0x8153) }),
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![InterfaceType { class: 0xff, subclass: None, protocol: None }],
                )),
                ..Default::default()
            }),
            condition: None, // No specific condition for this rule.
        })
        .expect("Failed to add default rule for Realtek USB Ethernet adapter");
    policy
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rules::Action;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
    use std::collections::HashMap;
    use std::fs;
    use tempfile::tempdir;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    fn init_logger() {
        let _ = env_logger::try_init();
    }

    fn create_mock_sysfs_for_init() -> MockSysfs {
        MockSysfs::new(SysfsFile::Dir(HashMap::from([
            (
                "module/usbcore/parameters",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "bus/usb/devices",
                SysfsFile::Dir(HashMap::from([
                    (
                        "usb1",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                    (
                        "usb2",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                    (
                        "1-1",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                ])),
            ),
        ])))
        .unwrap()
    }

    #[test]
    fn with_paths_sets_deny_and_creates_default_policy() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let manager = UsbDeviceAuthManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap();

        let authorized_default_path = mock_sys.path().join(USBCORE_AUTHORIZED_DEFAULT_PATH);
        assert_eq!(fs::read_to_string(authorized_default_path).unwrap(), "0");

        let usb1_auth_path = mock_sys.path().join("bus/usb/devices/usb1/authorized_default");
        assert_eq!(fs::read_to_string(usb1_auth_path).unwrap(), "0");
        let usb2_auth_path = mock_sys.path().join("bus/usb/devices/usb2/authorized_default");
        assert_eq!(fs::read_to_string(usb2_auth_path).unwrap(), "0");

        assert!(!manager.policy.all_rules.is_empty());
        assert_eq!(manager.policy.all_rules.len(), 2);
    }

    #[test]
    fn with_paths_loads_static_policy() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        fs::write(&policy_file, "allow with-interface any-of { 03:*:* }").unwrap();

        let manager = UsbDeviceAuthManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap();

        assert_eq!(manager.policy.all_rules.len(), 1);
        assert_eq!(manager.policy.all_rules[0].action, Action::Allow);
    }

    #[test]
    fn test_get_authorized_devices() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mut manager =
            UsbDeviceAuthManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap();

        manager.processed_devices.push(UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "authorized".to_string(), ..Default::default() },
            authorized: true,
            is_deferred: false,
        });
        manager.processed_devices.push(UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "unauthorized".to_string(), ..Default::default() },
            authorized: false,
            is_deferred: false,
        });

        let authorized_devices = manager.get_authorized_devices();
        assert_eq!(authorized_devices.len(), 1);
        assert_eq!(authorized_devices[0].syspath, "authorized");
    }

    #[test]
    fn test_process_usb_device_moves_to_correct_list() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        // Defer everything in BOOTED state
        fs::write(&policy_file, "defer when Booted").unwrap();

        let mut manager =
            UsbDeviceAuthManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap();
        assert_eq!(manager.policy.all_rules.len(), 1);

        let device = UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "test_device".to_string(), ..Default::default() },
            authorized: false,
            is_deferred: false,
        };

        manager.handle_system_state_change(UsbAuthorizationSystemState::BOOTED);
        manager.process_usb_device(device);

        assert!(manager.processed_devices().is_empty());
        assert_eq!(manager.deferred_devices().len(), 1);
        assert!(manager.ask_devices().is_empty());
        assert_eq!(manager.deferred_devices()[0].info.syspath, "test_device");
    }
}
