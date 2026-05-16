use reqwest::header::{HeaderMap, CONTENT_TYPE, COOKIE, USER_AGENT};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeSet;
use std::net::Ipv4Addr;
use std::path::PathBuf;
use std::time::Duration;

const PRODUCT_SECRET: &str = "LRS-v1-rotate-this-secret";
const UDP_LOG_PORT: u16 = 5514;

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct NetworkDevice {
    pub ip: String,
    pub identity: String,
    pub chip_id: String,
    pub derived_password: String,
    pub auth_status: String,
    pub fw_version: String,
    pub fw_display: String,
    pub role: String,
    pub mode: String,
    pub lan_hostname: String,
    pub message: String,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct OtaOptions {
    pub password: String,
    pub firmware_path: String,
    pub region: Option<String>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct UdpLogOptions {
    pub password: String,
    pub ttl_s: Option<u32>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct LanSubnet {
    pub interface_name: String,
    pub address: String,
    pub netmask: String,
    pub cidr: u8,
    pub network: String,
    pub broadcast: String,
    pub host_count: u32,
}

pub fn udp_log_port() -> u16 {
    UDP_LOG_PORT
}

pub fn local_lan_subnets() -> Result<Vec<LanSubnet>, String> {
    let interfaces = get_if_addrs::get_if_addrs().map_err(|e| e.to_string())?;
    let mut seen = BTreeSet::new();
    let mut subnets = Vec::new();

    for iface in interfaces {
        let get_if_addrs::IfAddr::V4(v4) = iface.addr else {
            continue;
        };

        let ip = v4.ip;
        let mask = v4.netmask;
        if ip.is_loopback() || ip.is_link_local() || ip.octets()[0] == 0 {
            continue;
        }

        let mask_u32 = u32::from(mask);
        if mask_u32 == 0 || mask_u32 == u32::MAX {
            continue;
        }

        let network = u32::from(ip) & mask_u32;
        let broadcast = network | !mask_u32;
        if broadcast <= network + 1 {
            continue;
        }
        let cidr = mask_u32.count_ones() as u8;
        let key = (network, mask_u32);
        if !seen.insert(key) {
            continue;
        }

        subnets.push(LanSubnet {
            interface_name: iface.name,
            address: ip.to_string(),
            netmask: mask.to_string(),
            cidr,
            network: Ipv4Addr::from(network).to_string(),
            broadcast: Ipv4Addr::from(broadcast).to_string(),
            host_count: broadcast.saturating_sub(network).saturating_sub(1),
        });
    }

    if subnets.is_empty() {
        return Err("No usable IPv4 LAN subnet found".into());
    }
    Ok(subnets)
}

pub fn scan_targets_for_subnets(subnets: &[LanSubnet]) -> Vec<String> {
    let mut targets = BTreeSet::new();
    for subnet in subnets {
        let Ok(network) = subnet.network.parse::<Ipv4Addr>() else {
            continue;
        };
        let Ok(broadcast) = subnet.broadcast.parse::<Ipv4Addr>() else {
            continue;
        };
        let start = u32::from(network).saturating_add(1);
        let end = u32::from(broadcast).saturating_sub(1);
        for raw in start..=end {
            targets.insert(Ipv4Addr::from(raw).to_string());
        }
    }
    targets.into_iter().collect()
}

pub fn derive_ota_password(chip_id_hex: &str) -> String {
    let normalized = normalize_chip_id(chip_id_hex);
    let payload = format!("{}:{}", PRODUCT_SECRET, normalized);
    let hash = Sha256::digest(payload.as_bytes());
    hex::encode(hash)[..8].to_string()
}

pub fn normalize_chip_id(value: &str) -> String {
    let raw = value
        .trim()
        .trim_start_matches("lrs-")
        .trim_start_matches("lrs_")
        .trim_start_matches("0x")
        .to_ascii_lowercase();
    format!("{:0>8}", raw)
}

fn extract_between<'a>(haystack: &'a str, start: &str, end: &str) -> Option<&'a str> {
    let start_idx = haystack.find(start)? + start.len();
    let tail = &haystack[start_idx..];
    let end_idx = tail.find(end)?;
    Some(&tail[..end_idx])
}

fn chip_from_identity(identity: &str) -> String {
    if identity.trim().is_empty() {
        return String::new();
    }
    normalize_chip_id(identity)
}

fn parse_login_identity(body: &str) -> String {
    extract_between(body, r#"<meta name="lrs-device-id" content=""#, r#"""#)
        .or_else(|| extract_between(body, r#"id="deviceSlug">"#, "<"))
        .map(|s| s.trim().to_string())
        .unwrap_or_default()
}

fn looks_like_lrs_login(body: &str) -> bool {
    body.contains("LRS Device Console Login")
        || body.contains("LRS Console")
        || body.contains("lrs-device-id")
        || body.contains("/api/login")
}

fn cookie_from_headers(headers: &HeaderMap) -> Option<String> {
    let raw = headers.get("set-cookie")?.to_str().ok()?;
    raw.split(';')
        .find(|part| part.trim().starts_with("lrs_session="))
        .map(|part| part.trim().to_string())
}

pub async fn login(client: &reqwest::Client, ip: &str, password: &str) -> Result<String, String> {
    let url = format!("http://{}/api/login", ip);
    let response = client
        .post(url)
        .header(USER_AGENT, "thanda-lora-flasher")
        .json(&serde_json::json!({ "password": password }))
        .send()
        .await
        .map_err(|e| e.to_string())?;
    if !response.status().is_success() {
        return Err(format!("login failed: {}", response.status()));
    }
    cookie_from_headers(response.headers())
        .ok_or_else(|| "login failed: missing session cookie".into())
}

async fn fetch_status_static(
    client: &reqwest::Client,
    ip: &str,
    cookie: &str,
) -> Result<serde_json::Value, String> {
    let url = format!("http://{}/api/status-static", ip);
    let response = client
        .get(url)
        .header(USER_AGENT, "thanda-lora-flasher")
        .header(COOKIE, cookie)
        .send()
        .await
        .map_err(|e| e.to_string())?;
    if !response.status().is_success() {
        return Err(format!("status failed: {}", response.status()));
    }
    response.json().await.map_err(|e| e.to_string())
}

fn enrich_from_status(device: &mut NetworkDevice, status: &serde_json::Value) {
    device.chip_id = status["chip_id"]
        .as_str()
        .unwrap_or(&device.chip_id)
        .to_string();
    if !device.chip_id.is_empty() {
        device.identity = format!("lrs-{}", normalize_chip_id(&device.chip_id));
        device.derived_password = derive_ota_password(&device.chip_id);
    }
    device.fw_version = status["fw_version"].as_str().unwrap_or("").to_string();
    device.fw_display = status["fw_display"].as_str().unwrap_or("").to_string();
    device.role = status["role"].as_str().unwrap_or("").to_string();
    device.mode = status["mode"].as_str().unwrap_or("").to_string();
    device.lan_hostname = status["lan_hostname"].as_str().unwrap_or("").to_string();
}

pub async fn probe_device(client: reqwest::Client, ip: String) -> Option<NetworkDevice> {
    let login_url = format!("http://{}/login", ip);
    let login_response = client
        .get(login_url)
        .header(USER_AGENT, "thanda-lora-flasher")
        .send()
        .await
        .ok()?;
    if !login_response.status().is_success() {
        return None;
    }
    let body = login_response.text().await.ok()?;
    let identity = parse_login_identity(&body);
    if !identity.starts_with("lrs-") && !looks_like_lrs_login(&body) {
        return None;
    }

    let chip_id = chip_from_identity(&identity);
    let derived_password = if chip_id.is_empty() {
        String::new()
    } else {
        derive_ota_password(&chip_id)
    };
    let mut device = NetworkDevice {
        ip,
        identity: if identity.is_empty() {
            "LRS device".into()
        } else {
            identity
        },
        chip_id,
        derived_password: derived_password.clone(),
        auth_status: "auth_needed".into(),
        message: if derived_password.is_empty() {
            "Older LRS login page found; enter admin password for version and OTA.".into()
        } else {
            "Device found; admin password needed for version and OTA.".into()
        },
        ..Default::default()
    };

    if derived_password.is_empty() {
        return Some(device);
    }

    match login(&client, &device.ip, &derived_password).await {
        Ok(cookie) => match fetch_status_static(&client, &device.ip, &cookie).await {
            Ok(status) => {
                device.auth_status = "derived_ok".into();
                device.message = "Derived password accepted.".into();
                enrich_from_status(&mut device, &status);
            }
            Err(err) => {
                device.auth_status = "auth_error".into();
                device.message = err;
            }
        },
        Err(err) => {
            device.auth_status = "auth_needed".into();
            device.message = err;
        }
    }

    Some(device)
}

pub async fn authenticated_status(ip: &str, password: &str) -> Result<NetworkDevice, String> {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| e.to_string())?;
    let cookie = login(&client, ip, password).await?;
    let status = fetch_status_static(&client, ip, &cookie).await?;
    let chip_id = status["chip_id"].as_str().unwrap_or("").to_string();
    let mut device = NetworkDevice {
        ip: ip.to_string(),
        identity: if chip_id.is_empty() {
            ip.to_string()
        } else {
            format!("lrs-{}", normalize_chip_id(&chip_id))
        },
        chip_id,
        auth_status: "custom_ok".into(),
        message: "Password accepted.".into(),
        ..Default::default()
    };
    if !device.chip_id.is_empty() {
        device.derived_password = derive_ota_password(&device.chip_id);
    }
    enrich_from_status(&mut device, &status);
    Ok(device)
}

pub async fn enable_udp_logging(ip: &str, options: UdpLogOptions) -> Result<String, String> {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| e.to_string())?;
    let cookie = login(&client, ip, &options.password).await?;
    let ttl_s = options.ttl_s.unwrap_or(300).clamp(1, 1800);
    let url = format!("http://{}/api/logging/udp", ip);
    let response = client
        .post(url)
        .header(USER_AGENT, "thanda-lora-flasher")
        .header(COOKIE, cookie)
        .json(&serde_json::json!({
            "enabled": true,
            "port": UDP_LOG_PORT,
            "ttl_s": ttl_s
        }))
        .send()
        .await
        .map_err(|e| e.to_string())?;
    if !response.status().is_success() {
        return Err(format!("UDP logging failed: {}", response.status()));
    }
    Ok(format!("UDP logging enabled on {} for {}s", ip, ttl_s))
}

pub async fn upload_ota(ip: &str, password: &str, firmware: PathBuf) -> Result<String, String> {
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(90))
        .build()
        .map_err(|e| e.to_string())?;
    let cookie = login(&client, ip, password).await?;
    let fw_bytes = tokio::fs::read(&firmware)
        .await
        .map_err(|e| e.to_string())?;
    let filename = firmware
        .file_name()
        .map(|f| f.to_string_lossy().to_string())
        .unwrap_or_else(|| "firmware.bin".into());
    let (content_type, body) = build_multipart(&filename, fw_bytes);
    let url = format!("http://{}/api/ota", ip);
    let response = client
        .post(url)
        .header(USER_AGENT, "thanda-lora-flasher")
        .header(COOKIE, cookie)
        .header(CONTENT_TYPE, content_type)
        .body(body)
        .send()
        .await
        .map_err(|e| {
            format!(
                "OTA upload response was lost after sending request to {}: {}",
                ip, e
            )
        })?;
    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(format!("HTTP OTA failed: {} {}", status, text));
    }
    Ok(format!("OTA upload accepted by {} ({})", ip, filename))
}

fn build_multipart(filename: &str, file_bytes: Vec<u8>) -> (String, Vec<u8>) {
    let boundary = "----lrs-ota-boundary-1";
    let mut body = Vec::new();
    body.extend_from_slice(format!("--{}\r\n", boundary).as_bytes());
    body.extend_from_slice(
        format!(
            "Content-Disposition: form-data; name=\"firmware\"; filename=\"{}\"\r\n",
            filename
        )
        .as_bytes(),
    );
    body.extend_from_slice(b"Content-Type: application/octet-stream\r\n\r\n");
    body.extend_from_slice(&file_bytes);
    body.extend_from_slice(format!("\r\n--{}--\r\n", boundary).as_bytes());
    (format!("multipart/form-data; boundary={}", boundary), body)
}
