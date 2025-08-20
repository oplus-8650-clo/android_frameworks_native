/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

use android_hardware_serialservice::aidl::android::hardware::serialservice::SerialPortInfo::SerialPortInfo;
use futures::stream::BoxStream;
use std::sync::{Arc, Mutex};
use tokio::task::JoinHandle;
use tokio_stream::StreamExt;
use ueventd::event::{DeviceEvent, DeviceType, EventType};

use crate::driver_type_finder::DriverTypeFinder;

#[mockall::automock]
pub trait DeviceEventCallback {
    fn on_device_added(&mut self, info: SerialPortInfo);
    fn on_device_removed(&mut self, name: &str);
}

/// Handles the stream of /dev events coming from ueventd Watcher.
pub struct DeviceEventsHandler {
    stream: BoxStream<'static, DeviceEvent>,
    callback: Box<dyn DeviceEventCallback + Send>,
    driver_type_finder: Arc<Mutex<dyn DriverTypeFinder + Send>>,
}

impl DeviceEventsHandler {
    pub async fn start_new(
        stream: BoxStream<'static, DeviceEvent>,
        callback: Box<dyn DeviceEventCallback + Send>,
        driver_type_finder: Arc<Mutex<dyn DriverTypeFinder + Send>>,
    ) -> JoinHandle<()> {
        let handler = DeviceEventsHandler { stream, callback, driver_type_finder };
        tokio::spawn(handler.run())
    }

    async fn run(mut self) {
        while let Some(device_event) = self.stream.next().await {
            self.handle_device_event(device_event).await;
        }
    }

    async fn handle_device_event(&mut self, device_event: DeviceEvent) {
        let DeviceType::DeviceNode { devnode_path } = device_event.device_type else {
            log::error!("unexpected device type {:?}", device_event.device_type);
            return;
        };
        let Some(name) = devnode_path.file_name() else {
            log::error!("no device name in {}", devnode_path.display());
            return;
        };
        let name = name.to_str().expect("Device paths should not have non-UTF-8 characters");
        match device_event.event_type {
            EventType::Add => {
                let Ok(driver_type) = ({
                    let driver_type_finder = self.driver_type_finder.lock().unwrap();
                    driver_type_finder.find(&devnode_path)
                }) else {
                    log::debug!("unsupported device type {}", devnode_path.display());
                    return;
                };
                // E.g. /sys/<device-dir>/device/subsystem -> .../bus/usb
                // If such a dir doesn't exist, we report "virtual" subsystem
                let subsystem_opt = device_event.device.device().and_then(|d| d.subsystem());
                let info = SerialPortInfo {
                    name: name.to_string(),
                    subsystem: subsystem_opt.unwrap_or("virtual".to_string()),
                    driverType: driver_type,
                    vendorId: -1,
                    productId: -1,
                };
                self.callback.on_device_added(info);
            }
            EventType::Remove => {
                self.callback.on_device_removed(name);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::anyhow;
    use futures::StreamExt;
    use mockall::predicate::eq;
    use std::collections::HashMap;
    use std::path::Path;
    use ueventd::device::Device;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    use crate::driver_type_finder::MockDriverTypeFinder;

    #[tokio::test]
    async fn test_handle_add_serial_device() {
        let (device, _sysfs_dir) = create_usb_device_in_mock_sysfs();
        let stream = tokio_stream::iter(vec![DeviceEvent {
            event_type: EventType::Add,
            device_type: DeviceType::DeviceNode {
                devnode_path: Path::new("/dev/ttyACM0").to_path_buf(),
            },
            device,
        }])
        .boxed();
        let mut callback = MockDeviceEventCallback::new();
        callback.expect_on_device_added().times(1).returning(|info| {
            assert_eq!(info.name, "ttyACM0".to_string());
            assert_eq!(info.subsystem, "usb".to_string());
            assert_eq!(info.driverType, "serial".to_string());
        });
        callback.expect_on_device_removed().never();
        let mut driver_type_finder = MockDriverTypeFinder::new();
        driver_type_finder
            .expect_find()
            .with(eq(Path::new("/dev/ttyACM0")))
            .times(1)
            .returning(|_| Ok("serial".to_string()));

        let handle = DeviceEventsHandler::start_new(
            stream,
            Box::new(callback),
            Arc::new(Mutex::new(driver_type_finder)) as Arc<Mutex<dyn DriverTypeFinder + Send>>,
        )
        .await;
        handle.await.unwrap();
    }

    #[tokio::test]
    async fn test_handle_add_and_remove_serial_device() {
        let (device, _sysfs_dir) = create_usb_device_in_mock_sysfs();
        let stream = tokio_stream::iter(vec![
            DeviceEvent {
                event_type: EventType::Add,
                device_type: DeviceType::DeviceNode {
                    devnode_path: Path::new("/dev/ttyACM0").to_path_buf(),
                },
                device: device.clone(),
            },
            DeviceEvent {
                event_type: EventType::Remove,
                device_type: DeviceType::DeviceNode {
                    devnode_path: Path::new("/dev/ttyACM0").to_path_buf(),
                },
                device,
            },
        ])
        .boxed();
        let mut callback = MockDeviceEventCallback::new();
        callback.expect_on_device_added().times(1).returning(|info| {
            assert_eq!(info.name, "ttyACM0".to_string());
            assert_eq!(info.subsystem, "usb".to_string());
            assert_eq!(info.driverType, "serial".to_string());
        });
        callback.expect_on_device_removed().times(1).returning(|name| {
            assert_eq!(name, "ttyACM0");
        });
        let mut driver_type_finder = MockDriverTypeFinder::new();
        driver_type_finder
            .expect_find()
            .with(eq(Path::new("/dev/ttyACM0")))
            .times(1)
            .returning(|_| Ok("serial".to_string()));

        let handle = DeviceEventsHandler::start_new(
            stream,
            Box::new(callback),
            Arc::new(Mutex::new(driver_type_finder)) as Arc<Mutex<dyn DriverTypeFinder + Send>>,
        )
        .await;
        handle.await.unwrap();
    }

    #[tokio::test]
    async fn test_handle_add_alien_device() {
        let (device, _sysfs_dir) = create_usb_device_in_mock_sysfs();
        let stream = tokio_stream::iter(vec![DeviceEvent {
            event_type: EventType::Add,
            device_type: DeviceType::DeviceNode {
                devnode_path: Path::new("/dev/alien").to_path_buf(),
            },
            device,
        }])
        .boxed();
        let mut callback = MockDeviceEventCallback::new();
        callback.expect_on_device_added().never();
        callback.expect_on_device_removed().never();
        let mut driver_type_finder = MockDriverTypeFinder::new();
        driver_type_finder
            .expect_find()
            .with(eq(Path::new("/dev/alien")))
            .times(1)
            .returning(|_| Err(anyhow!("Driver type not found")));

        let handle = DeviceEventsHandler::start_new(
            stream,
            Box::new(callback),
            Arc::new(Mutex::new(driver_type_finder)) as Arc<Mutex<dyn DriverTypeFinder + Send>>,
        )
        .await;
        handle.await.unwrap();
    }

    fn create_usb_device_in_mock_sysfs() -> (Device, MockSysfs) {
        let sysfs = SysfsFile::Dir(HashMap::from([
            (
                "class/tty",
                SysfsFile::Dir(HashMap::from([(
                    "ttyACM0",
                    SysfsFile::Dir(HashMap::from([
                        (
                            "device",
                            SysfsFile::Dir(HashMap::from([
                                ("subsystem", SysfsFile::Symlink("../../../../bus/usb")),
                                ("uevent", SysfsFile::RegularFile("")),
                            ])),
                        ),
                        ("subsystem", SysfsFile::Symlink("../..")),
                        ("uevent", SysfsFile::RegularFile("")),
                    ])),
                )])),
            ),
            ("bus/usb", SysfsFile::Dir(HashMap::new())),
        ]));
        let sysfs_dir = match MockSysfs::new(sysfs) {
            Ok(ms) => ms,
            Err(e) => panic!("Could not create mock sysfs: {}", e),
        };
        let sysfs_path = sysfs_dir.path().join("class/tty/ttyACM0");
        let device = Device::with_root_and_syspath(sysfs_dir.path(), &sysfs_path).unwrap();
        (device, sysfs_dir)
    }
}
