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

//! AiSeal specific configuration

use crate::package_manager::PackageManager;
use anyhow::{anyhow, Context, Result};
use log::info;
use microdroid_payload_config::VmPayloadConfig;
use rustutils::android::system_properties;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{BufReader, Read, Seek};
use zip::ZipArchive;

const ABILIST_PROPERTY: &str = "ro.product.cpu.abilist";
const DEBUGGABLE_PROPERTY: &str = "ro.debuggable";
const TENANT_CONFIG_PACKAGE_PROPERTY: &str = "service.aiseal.tenant_config_package";
const TENANT_CONFIG_PATH_PROPERTY: &str = "service.aiseal.tenant_config_path";
const AISEAL_CONFIG_PATH_PROPERTY: &str = "service.aiseal.aiseal_config_path";
const AISEAL_PROTECTED_VM_FLAG: &str = "service.aiseal.protected_vm";
const AISEAL_PROTECTED_VM_FLAG_DEFAULT: bool = true;
const AISEAL_DEBUGGABLE_DEFAULT: bool = false;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AiSealConfig {
    pub debuggable: bool,
    pub protected_vm: bool,
    pub abis: Vec<String>,
    pub payload_config_package_name: String,
    pub payload_config_package_path: String,
    pub vm_payload_config: VmPayloadConfig,
    pub vm_payload_config_path: String,
    pub aiseal_payload_config: AiSealPayloadConfig,
}

/// AiSeal-specific VM configuration, stored inside main payload APK
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub struct AiSealPayloadConfig {
    /// Version of the config
    #[serde(default)]
    pub version: i32,

    /// List of tenants in the VM
    #[serde(default)]
    pub tenants: Vec<AiSealTenant>,
}

/// AiSeal tenant configuration, that is not part of AVF payload configuration
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AiSealTenant {
    /// Package name of the tenant
    pub name: String,

    /// List of host services provided by the tenant
    #[serde(default)]
    pub host_services: Vec<HostService>,
}

/// Configuration of service provided by VM tenant to its host application
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct HostService {
    /// Name of the service
    pub name: String,

    /// Vsock port that is used to serve a service
    pub port: i32,
}

pub struct HostServiceWithOwner {
    pub owner: String,
    pub service: HostService,
}

impl AiSealPayloadConfig {
    pub fn get_service_name_map(&self) -> HashMap<String, HostServiceWithOwner> {
        self.tenants
            .iter()
            .flat_map(|tenant| {
                tenant.host_services.iter().map(move |service| {
                    (
                        service.name.clone(),
                        HostServiceWithOwner {
                            owner: tenant.name.clone(),
                            service: service.clone(),
                        },
                    )
                })
            })
            .collect()
    }
}

impl AiSealConfig {
    pub fn load(pm: &PackageManager) -> Result<AiSealConfig> {
        let debuggable = get_debuggable()?;
        let protected_vm = get_protected_vm_flag()?;
        let abis = get_abis()?;
        let config_package = find_payload_config_package()?;
        let vm_payload_config_path = find_payload_config_path()?;
        let aiseal_payload_config_path = find_aiseal_payload_config_path()?;

        info!("Loading payload config from {config_package}");

        let config_package_info = pm
            .get_package_info(&config_package)
            .context(format!("Failed to get config APK info: {config_package}"))?;
        let config_apk_path = config_package_info
            .sourceDir
            .ok_or(anyhow!("Failed to get config APK path: {config_package}"))?;
        let mut config_apk_zip = ZipArchive::new(BufReader::new(
            fs::File::open(&config_apk_path)
                .context(format!("Failed to open config APK: {config_apk_path}"))?,
        ))
        .context(format!("Failed to unzip config APK: {config_apk_path}"))?;

        let vm_payload_config: VmPayloadConfig =
            get_config(&mut config_apk_zip, &vm_payload_config_path).context(
                "Failed to load VM payload config from {config_package}:{vm_payload_config_path}",
            )?;
        let aiseal_payload_config: AiSealPayloadConfig =
            get_config(&mut config_apk_zip, &aiseal_payload_config_path).context(
                "Failed to load AiSeal config from {config_package}:{aiseal_payload_config_path}",
            )?;

        Ok(AiSealConfig {
            debuggable,
            protected_vm,
            abis,
            payload_config_package_name: config_package,
            payload_config_package_path: config_apk_path,
            vm_payload_config,
            vm_payload_config_path,
            aiseal_payload_config,
        })
    }
}

fn get_debuggable() -> Result<bool> {
    system_properties::read_bool(DEBUGGABLE_PROPERTY, AISEAL_DEBUGGABLE_DEFAULT)
        .context(format!("Failed to get debuggable property {DEBUGGABLE_PROPERTY}"))
}

fn get_protected_vm_flag() -> Result<bool> {
    system_properties::read_bool(AISEAL_PROTECTED_VM_FLAG, AISEAL_PROTECTED_VM_FLAG_DEFAULT)
        .context(format!("Failed to get protected VM flag {AISEAL_PROTECTED_VM_FLAG}"))
}

fn get_abis() -> Result<Vec<String>> {
    let value = system_properties::read(ABILIST_PROPERTY)
        .context(format!("Failed to get list of ABIs {ABILIST_PROPERTY}"))?
        .context(format!("List of ABIs {ABILIST_PROPERTY} is not set"))?;
    Ok(value.trim().split(',').map(|s| s.to_string()).collect())
}

fn find_payload_config_package() -> Result<String> {
    system_properties::read(TENANT_CONFIG_PACKAGE_PROPERTY)
        .context(format!("Failed to get tenant config package {TENANT_CONFIG_PACKAGE_PROPERTY}"))?
        .context(format!("Tenant config package {TENANT_CONFIG_PACKAGE_PROPERTY} is not set"))
}

fn find_payload_config_path() -> Result<String> {
    system_properties::read(TENANT_CONFIG_PATH_PROPERTY)
        .context(format!("Failed to get tenant config path {TENANT_CONFIG_PATH_PROPERTY}"))?
        .context(format!("Tenant config path {TENANT_CONFIG_PATH_PROPERTY} is not set"))
}

fn find_aiseal_payload_config_path() -> Result<String> {
    system_properties::read(AISEAL_CONFIG_PATH_PROPERTY)
        .context(format!("Failed to get AiSeal config path {AISEAL_CONFIG_PATH_PROPERTY}"))?
        .context(format!("AiSeal config path {AISEAL_CONFIG_PATH_PROPERTY} is not set"))
}

fn get_config<T: for<'de> Deserialize<'de>, R: Read + Seek>(
    config_apk_zip: &mut ZipArchive<R>,
    config_path: &str,
) -> Result<T> {
    let config_file = config_apk_zip.by_name(config_path).context("Failed to read config")?;
    serde_json::from_reader(config_file).context("Failed to parse config")
}
