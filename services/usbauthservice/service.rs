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

//! This module implements the IUsbAuthManager AIDL interface.

use crate::manager::UsbDeviceManager;
use android_hardware_usb_auth::aidl::android::hardware::usb::IUsbAuthManager::{
    BnUsbAuthManager, IUsbAuthManager,
};
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationStatus::UsbAuthorizationStatus;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use binder::{Interface, SpIBinder, Status};
use log::debug;
use std::sync::{Arc, Mutex};

/// Implementation of the `IUsbAuthManager` binder service.
pub struct UsbAuthServiceImpl {
    device_manager: Arc<Mutex<UsbDeviceManager>>,
}

impl Interface for UsbAuthServiceImpl {}

impl UsbAuthServiceImpl {
    /// Creates a new binder service.
    pub fn new_binder(device_manager: Arc<Mutex<UsbDeviceManager>>) -> Option<SpIBinder> {
        let service = UsbAuthServiceImpl { device_manager };
        Some(BnUsbAuthManager::new_binder(service, binder::BinderFeatures::default()).as_binder())
    }
}

impl IUsbAuthManager for UsbAuthServiceImpl {
    fn setSystemState(&self, state: UsbAuthorizationSystemState) -> binder::Result<()> {
        debug!("System state changed to: {:?}", state);
        let mut manager = self.device_manager.lock().unwrap();
        manager.handle_system_state_change(state);
        Ok(())
    }

    fn getAuthorizedUsbDevices(&self) -> binder::Result<Vec<UsbAuthDeviceInfo>> {
        debug!("getAuthorizedUsbDevices called");
        let manager = self.device_manager.lock().unwrap();
        let devices = manager.get_authorized_devices();
        Ok(devices)
    }

    fn getDeferredUsbDevices(&self) -> binder::Result<Vec<UsbAuthDeviceInfo>> {
        debug!("getDeferredUsbDevices called");
        let manager = self.device_manager.lock().unwrap();
        let devices = manager.deferred_devices().into_iter().map(|d| d.info).collect();
        Ok(devices)
    }

    fn getDevicesAwaitingAuthorization(&self) -> binder::Result<Vec<UsbAuthDeviceInfo>> {
        debug!("getDevicesAwaitingAuthorization called");
        let manager = self.device_manager.lock().unwrap();
        let devices = manager.ask_devices().into_iter().map(|d| d.info).collect();
        Ok(devices)
    }

    fn getAuthorizationStatus(
        &self,
        device: &UsbAuthDeviceInfo,
    ) -> binder::Result<UsbAuthorizationStatus> {
        debug!("getAuthorizationStatus called for {:?}", device.syspath);
        let manager = self.device_manager.lock().unwrap();
        Ok(manager.get_authorization_status(&device.syspath))
    }

    fn setAuthorizationStatus(
        &self,
        device: &UsbAuthDeviceInfo,
        status: UsbAuthorizationStatus,
    ) -> binder::Result<()> {
        debug!("setAuthorizationStatus called for {:?} with status {:?}", device.syspath, status);
        let mut manager = self.device_manager.lock().unwrap();
        let authorized = status == UsbAuthorizationStatus::AUTHORIZED;
        manager.update_authorization_status(&device.syspath, authorized).map_err(|e| {
            Status::new_exception_str(binder::ExceptionCode::ILLEGAL_ARGUMENT, Some(&e.to_string()))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device_info::UsbDeviceInfoWithState;
    use std::collections::HashMap;
    use std::default::Default;
    use std::fs;
    use tempfile::tempdir;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    fn create_test_device(syspath: &str) -> UsbAuthDeviceInfo {
        UsbAuthDeviceInfo {
            syspath: syspath.to_string(),
            deviceClass: 0,
            bDeviceSubClass: 0,
            bDeviceProtocol: 0,
            vendorId: 0,
            productId: 0,
            ..Default::default()
        }
    }

    fn init_logger() {
        // `try_init` ignores errors from initializing the logger multiple times,
        // which can happen when tests run in parallel.
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
                "bus/usb/devices/usb1",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "bus/usb/devices/usb2",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "bus/usb/devices/1-1",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "ask_device",
                SysfsFile::Dir(HashMap::from([("authorized", SysfsFile::RegularFile("1"))])),
            ),
        ])))
        .unwrap()
    }

    // A test helper to create the service.
    fn create_test_service() -> (UsbAuthServiceImpl, Arc<Mutex<UsbDeviceManager>>) {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let device_manager = Arc::new(Mutex::new(
            UsbDeviceManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap(),
        ));
        let service = UsbAuthServiceImpl { device_manager: device_manager.clone() };
        (service, device_manager)
    }

    #[test]
    fn test_set_system_state() {
        let (service, device_manager) = create_test_service();
        service.setSystemState(UsbAuthorizationSystemState::SCREEN_LOCKED).unwrap();
        let manager = device_manager.lock().unwrap();
        assert_eq!(*manager.system_state(), UsbAuthorizationSystemState::SCREEN_LOCKED);
    }

    #[test]
    fn test_authorization_flow() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        // Ask for everything in SCREEN_UNLOCKED state
        fs::write(&policy_file, "ask when LoggedIn").unwrap();

        let device_manager = Arc::new(Mutex::new(
            UsbDeviceManager::with_paths(mock_sys.path(), mock_etc.path()).unwrap(),
        ));
        let service = UsbAuthServiceImpl { device_manager: device_manager.clone() };

        let device_syspath = mock_sys.path().join("ask_device").display().to_string();
        let device_info = create_test_device(&device_syspath);
        let device_with_state = UsbDeviceInfoWithState {
            info: device_info.clone(),
            authorized: false,
            is_deferred: false,
        };

        // Process the device, which should put it in the "ask" list.
        let mut manager = device_manager.lock().unwrap();
        manager.handle_system_state_change(UsbAuthorizationSystemState::LOGGED_IN);
        manager.process_usb_device(device_with_state);
        assert_eq!(manager.ask_devices().len(), 1);
        assert!(manager.processed_devices().is_empty());
        drop(manager);

        // Verify that the service reports it as awaiting authorization and denied.
        assert_eq!(service.getDevicesAwaitingAuthorization().unwrap().len(), 1);
        assert_eq!(
            service.getAuthorizationStatus(&device_info).unwrap(),
            UsbAuthorizationStatus::DENIED
        );

        // Authorize the device via the service.
        service.setAuthorizationStatus(&device_info, UsbAuthorizationStatus::AUTHORIZED).unwrap();

        // Verify that the device is now in the processed list and authorized.
        let manager = device_manager.lock().unwrap();
        assert!(manager.ask_devices().is_empty());
        assert_eq!(manager.processed_devices().len(), 1);
        assert!(manager.processed_devices()[0].authorized);
        assert_eq!(manager.processed_devices()[0].info.syspath, device_syspath);
        drop(manager);

        // Verify the new status via the service.
        assert_eq!(service.getDevicesAwaitingAuthorization().unwrap().len(), 0);
        assert_eq!(
            service.getAuthorizationStatus(&device_info).unwrap(),
            UsbAuthorizationStatus::AUTHORIZED
        );
    }
}
