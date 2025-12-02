// Copyright 2025, The Android Open Source Project
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

//! Wrapper over IPackageManagerNative.

use anyhow::{anyhow, bail, Context, Result};
use binder::{wait_for_interface, ThreadState};
use packagemanager_aidl::aidl::android::content::pm::{
    IPackageManagerNative::IPackageManagerNative, PackageInfoNative::PackageInfoNative,
};

const PACKAGE_MANAGER_NATIVE_SERVICE: &str = "package_native";

const USER_SYSTEM: i32 = 0;
pub struct PackageManager(binder::Strong<dyn IPackageManagerNative>);

impl PackageManager {
    pub fn new() -> Result<Self> {
        let pm = wait_for_interface::<dyn IPackageManagerNative>(PACKAGE_MANAGER_NATIVE_SERVICE)
            .context("Failed to get package manager native service")?;
        Ok(Self(pm))
    }

    pub fn get_package_info(&self, package_name: &str) -> Result<PackageInfoNative> {
        self.0
            .getPackageInfoWithSigningInfo(package_name, USER_SYSTEM)
            .context(format!("getPackageInfoWithSigningInfo failed for {package_name}"))?
            .ok_or(anyhow!("Package {package_name} is not found"))
    }

    pub fn get_calling_package(&self) -> Result<String> {
        let uid = ThreadState::get_calling_uid();
        let uid: i32 = uid.try_into().context(format!("Failed to convert {uid} to i32"))?;
        let names = self
            .0
            .getNamesForUids(&[uid])
            .context(format!("getNamesForUids failed for UID {uid}"))?;
        if names.len() == 1 {
            Ok(names[0].clone())
        } else {
            bail!("getNamesForUids returned unexpected list of packages: {names:?}")
        }
    }
}
