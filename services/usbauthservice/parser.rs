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

//! A parser for the USB authorization policy language.
//!
//! This module provides functionality to parse a text-based policy file
//! into a structured `Policy` object that can be used to make authorization
//! decisions.

use crate::rules::*;
use std::fmt;
use std::fs;
use std::path::Path;
use thiserror::Error;

/// Encapsulates different types of errors that can occur during policy loading.
#[derive(Debug, Error, PartialEq)]
pub enum PolicyInnerError {
    /// An error occurred during the parsing of a rule.
    #[error(transparent)]
    Parse(#[from] ParseError),
    /// An error occurred while adding a parsed rule to the policy.
    #[error(transparent)]
    AddRule(#[from] AddRuleError),
    /// An I/O error occurred, with a descriptive error message.
    #[error("I/O error: {0}")]
    Io(String),
}

/// Represents an error that occurred during policy loading.
#[derive(Debug, Error, PartialEq)]
pub struct PolicyLoadError {
    /// The file path from which the policy was attempted to be loaded.
    pub file_path: String,
    /// The encapsulated error.
    pub error: PolicyInnerError,
}

impl fmt::Display for PolicyLoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.file_path.is_empty() {
            write!(f, "{}", self.error)
        } else {
            write!(f, "Error loading policy from {}: {}", self.file_path, self.error)
        }
    }
}

impl PolicyLoadError {
    /// Returns a new `PolicyLoadError` with the given file path.
    pub fn with_file_path(mut self, path: &Path) -> Self {
        self.file_path = path.to_string_lossy().to_string();
        self
    }
}

impl From<PolicyInnerError> for PolicyLoadError {
    fn from(error: PolicyInnerError) -> Self {
        Self { file_path: String::new(), error }
    }
}

/// Parses a USB authorization policy from a string content.
#[derive(Debug)]
pub struct Parser {
    /// The parsed policy.
    policy: Policy,
}

impl Parser {
    /// Parses a policy directly from a string content.
    /// This acts as a factory method for `Policy`.
    pub fn new(content: &str) -> Result<Self, PolicyLoadError> {
        let mut policy = Policy::new();
        for line in content.lines() {
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let rule = Parser::parse_rule(trimmed).map_err(PolicyInnerError::from)?;
            policy.add_rule(rule).map_err(PolicyInnerError::from)?;
        }
        Ok(Self { policy })
    }

    /// Returns a reference to the parsed policy.
    pub fn policy(&self) -> &Policy {
        &self.policy
    }

    /// Parses rules from a given file path.
    pub fn parse_rules_from_file(path: &Path) -> Result<Self, PolicyLoadError> {
        let content = fs::read_to_string(path).map_err(|e| PolicyLoadError {
            file_path: path.to_string_lossy().to_string(),
            error: PolicyInnerError::Io(e.to_string()),
        })?;
        Parser::new(&content).map_err(|e| e.with_file_path(path))
    }

    /// Parses a single rule line.
    fn parse_rule(line: &str) -> Result<Rule, ParseError> {
        // Pre-process the line to ensure delimiters are separated by spaces
        // for easier parsing.
        let processed_line = line.replace('{', " { ").replace('}', " } ");
        let mut parts = processed_line.split_whitespace().peekable();

        let action = Action::parse(&mut parts)?;
        let attributes = DeviceAttributes::parse(&mut parts)?;
        let condition = SystemCondition::parse(&mut parts)?;

        // We should have consumed all tokens, otherwise return an error with all remaining tokens.
        let remaining_tokens: Vec<&str> = parts.collect();
        if !remaining_tokens.is_empty() {
            return Err(ParseError::UnexpectedToken(format!(
                "Unexpected tokens at end of rule: '{}'",
                remaining_tokens.join(" ")
            )));
        }

        Ok(Rule { action, attributes, condition })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[test]
    fn test_parse_missing_action() {
        let rule_str = "with-id 18d1:4ee7";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError {
                file_path: "".to_string(),
                error: PolicyInnerError::Parse(ParseError::InvalidAction("with-id".to_string())),
            }
        );
    }

    #[test]
    fn test_parse_simple_allow_rule() {
        let rule_str = "allow with-id 18d1:4ee7";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        assert_eq!(
            rule.attributes.as_ref().unwrap().with_id,
            Some(DeviceId { vendor_id: Some(0x18d1), product_id: Some(0x4ee7) })
        );
    }

    #[test]
    fn test_parse_interface_rule() {
        let rule_str = "allow with-interface 03:*:*";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        let if_attr = rule.attributes.as_ref().unwrap().with_interface.as_ref().unwrap();
        let interfaces = &if_attr.interfaces;
        assert_eq!(if_attr.operator, Operator::Equals); // Default for single value
        assert_eq!(interfaces.len(), 1);
        assert_eq!(interfaces[0], InterfaceType { class: 0x03, subclass: None, protocol: None });
    }

    #[test]
    fn test_parse_multiple_interfaces_rule() {
        let rule_str = "deny with-interface any-of { 03:*:*, 08:06:* }";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Deny);
        let if_attr = rule.attributes.as_ref().unwrap().with_interface.as_ref().unwrap();
        assert_eq!(if_attr.operator, Operator::OneOf);
        let interfaces = &if_attr.interfaces;
        assert_eq!(interfaces.len(), 2);
        assert_eq!(interfaces[0], InterfaceType { class: 0x03, subclass: None, protocol: None });
        assert_eq!(
            interfaces[1],
            InterfaceType { class: 0x08, subclass: Some(0x06), protocol: None }
        );
    }

    #[test]
    fn test_parse_rule_with_system_condition() {
        let rule_str = "ask with-id 1234:5678 when Booted ScreenLocked";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Ask);
        let attributes = rule.attributes.as_ref().unwrap();
        assert_eq!(
            attributes.with_id,
            Some(DeviceId { vendor_id: Some(0x1234), product_id: Some(0x5678) })
        );
        let condition = rule.condition.as_ref().unwrap();
        assert_eq!(condition.operator, Operator::Equals); // Default
        assert_eq!(condition.states.len(), 2);
        assert_eq!(condition.states[0], UsbAuthorizationSystemState::BOOTED);
        assert_eq!(condition.states[1], UsbAuthorizationSystemState::SCREEN_LOCKED);
    }

    #[test]
    fn test_parse_invalid_rule() {
        let rule_str = "allow with-id 1234:5678 extra-token";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::UnexpectedToken(
                "Unexpected tokens at end of rule: 'extra-token'".to_string()
            )))
        );
    }

    #[test]
    fn test_parse_file_with_comments_and_empty_lines() {
        let content = "
# This is a comment
allow with-id 1111:2222

defer with-interface 08:*:*
";
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "{}", content).unwrap();
        let parser = Parser::parse_rules_from_file(file.path()).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 2);
        assert_eq!(policy.all_rules[0].action, Action::Allow);
        assert_eq!(policy.all_rules[1].action, Action::Defer);
    }

    #[test]
    fn test_parse_via_port_rule() {
        let rule_str = "allow via-port any-of { usb1, usb2 }";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        let port_attr = rule.attributes.as_ref().unwrap().via_port.as_ref().unwrap();
        assert_eq!(port_attr.operator, Operator::OneOf);
        let ports = &port_attr.ports;
        assert_eq!(ports, &vec!["usb1".to_string(), "usb2".to_string()]);
    }

    #[test]
    fn test_parse_complex_rule() {
        let rule_str = "allow name MyDevice serial 123 with-id 1234:5678 with-interface 03:01:02 internal-device when LoggedIn";
        let parser = Parser::new(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        let attributes = rule.attributes.as_ref().unwrap();
        assert_eq!(attributes.name, Some("MyDevice".to_string()));
        assert_eq!(attributes.serial, Some("123".to_string()));
        assert_eq!(
            attributes.with_id,
            Some(DeviceId { vendor_id: Some(0x1234), product_id: Some(0x5678) })
        );
        let if_attr = attributes.with_interface.as_ref().unwrap();
        let interfaces = &if_attr.interfaces;
        assert_eq!(
            interfaces[0],
            InterfaceType { class: 0x03, subclass: Some(0x01), protocol: Some(0x02) }
        );
        assert_eq!(attributes.internal_device, Some(true));
        let condition = rule.condition.as_ref().unwrap();
        assert_eq!(condition.states[0], UsbAuthorizationSystemState::LOGGED_IN);
    }

    #[test]
    fn test_parse_duplicate_default_rule() {
        let content = "allow\ndeny";
        let error = Parser::new(content).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::AddRule(AddRuleError::DuplicateDefaultRule)),
        );
    }

    #[test]
    fn test_parse_empty_attribute() {
        let rule_str = "allow name";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::MissingValue(
                "name".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_vendor_id() {
        let rule_str = "allow with-id invalid:1234";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_product_id() {
        let rule_str = "allow with-id 1234:invalid";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_class() {
        let rule_str = "allow with-interface invalid:*:*";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_subclass() {
        let rule_str = "allow with-interface 03:invalid:*";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_protocol() {
        let rule_str = "allow with-interface 03:01:invalid";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_system_state() {
        let rule_str = "allow when InvalidState";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::UnknownSystemState(
                "InvalidState".to_string()
            ))),
        );
    }

    #[test]
    fn test_invalid_rule_order() {
        let rule_str = "allow when Booted with-id 1234:5678";
        let error = Parser::new(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::UnknownSystemState(
                "with-id".to_string()
            )))
        );
    }
}
