use serde::{Deserialize, Serialize};
use reqwest::header::{HeaderMap, HeaderValue, USER_AGENT, ACCEPT};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::LazyLock;
use sha2::{Digest, Sha256};
use hex;
use tauri::Manager;

static CLIENT: LazyLock<reqwest::Client> = LazyLock::new(|| {
    reqwest::Client::new()
});

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Release {
    pub tag_name: String,
    pub name: Option<String>,
    pub assets: Vec<Asset>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Asset {
    pub name: String,
    pub browser_download_url: String,
}

impl Release {
    pub fn find_asset_for_region(&self, region: &str) -> Option<&Asset> {
        let search_suffix = format!("{}.bin", region.to_lowercase());
        self.assets.iter().find(|a| a.name.to_lowercase().ends_with(&search_suffix))
    }
}

pub async fn fetch_releases() -> Result<Vec<Release>, String> {
    let mut headers = HeaderMap::new();
    headers.insert(USER_AGENT, HeaderValue::from_static("thanda-lora-flasher"));
    headers.insert(ACCEPT, HeaderValue::from_static("application/vnd.github+json"));

    // FIXED: Using the dedicated firmware repository
    let url = "https://api.github.com/repos/warwickchapman/lora-rs-firmware/releases";
    let response = CLIENT
        .get(url)
        .headers(headers)
        .send()
        .await
        .map_err(|e| format!("Network error: {}", e))?;

    let status = response.status();
    let body = response.text().await.map_err(|e| format!("Failed to read body: {}", e))?;

    if !status.is_success() {
        return Err(format!("GitHub API error ({}): {}", status, body));
    }

    let releases: Vec<Release> = serde_json::from_str(&body).map_err(|e| {
        format!("JSON parse error: {}. Body snippet: {}", e, &body[..std::cmp::min(body.len(), 200)])
    })?;
    
    Ok(releases)
}

pub async fn download_firmware(app: &tauri::AppHandle, url: &str, filename: &str) -> Result<PathBuf, String> {
    let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let firmware_dir = app_data_dir.join("firmware");
    
    if !firmware_dir.exists() {
        fs::create_dir_all(&firmware_dir).map_err(|e| e.to_string())?;
    }

    let dest_path = firmware_dir.join(filename);
    
    if dest_path.exists() && dest_path.is_file() {
        return Ok(dest_path);
    }
    
    let mut headers = HeaderMap::new();
    headers.insert(USER_AGENT, HeaderValue::from_static("thanda-lora-flasher"));

    let response = CLIENT.get(url).headers(headers).send().await.map_err(|e| e.to_string())?;
    let content = response.bytes().await.map_err(|e| e.to_string())?;
    
    fs::write(&dest_path, content).map_err(|e| e.to_string())?;
    
    Ok(dest_path)
}

pub fn calculate_sha256(path: &Path) -> Result<String, String> {
    let mut file = fs::File::open(path).map_err(|e| e.to_string())?;
    let mut hasher = Sha256::new();
    std::io::copy(&mut file, &mut hasher).map_err(|e| e.to_string())?;
    let hash = hasher.finalize();
    Ok(hex::encode(hash))
}

pub async fn resolve_firmware_path(
    app: &tauri::AppHandle,
    firmware_path: &str,
    region: Option<String>,
    progress_log: Option<&(dyn Fn(String) + Send + Sync)>,
) -> Result<PathBuf, String> {
    let local_path = PathBuf::from(firmware_path);
    if local_path.exists() && local_path.is_file() {
        if let Some(log) = progress_log {
            log(format!("Using local firmware file at {}...", firmware_path));
        }
        return Ok(local_path);
    }
    
    if region.is_none() {
        return Err(format!(
            "Local firmware file not found: {}. Build firmware first or choose a local .bin file.",
            firmware_path
        ));
    }
    
    let reg = region.unwrap();
    if let Some(log) = progress_log {
        log(format!("Starting cloud flash for {} (Region: {})...", firmware_path, reg));
    }
    
    // 1. Fetch releases
    if let Some(log) = progress_log {
        log("Fetching release details...".into());
    }
    let releases = fetch_releases().await?;
    let release = releases.into_iter()
        .find(|r| r.tag_name == firmware_path)
        .ok_or_else(|| format!("Release {} not found", firmware_path))?;

    // 2. Find asset
    let asset = release.find_asset_for_region(&reg)
        .ok_or_else(|| format!("No asset found for region {} in release {}", reg, firmware_path))?;

    // 3. Download
    if let Some(log) = progress_log {
        log(format!("Downloading asset: {}...", asset.name));
    }
    let path = download_firmware(app, &asset.browser_download_url, &asset.name).await?;

    // 4. Verify SHA256
    if let Some(log) = progress_log {
        log("Verifying checksum...".into());
    }
    let sha256 = calculate_sha256(&path)?;
    if let Some(log) = progress_log {
        log(format!("SHA256: {}", sha256));
    }
    
    Ok(path)
}
