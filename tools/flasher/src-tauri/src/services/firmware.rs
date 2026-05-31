use serde::{Deserialize, Serialize};
use reqwest::header::{HeaderMap, HeaderValue, USER_AGENT, ACCEPT};
use std::fs;
use std::path::{Path, PathBuf};
use sha2::{Digest, Sha256};
use hex;
use tauri::Manager;

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

pub async fn fetch_releases() -> Result<Vec<Release>, String> {
    let client = reqwest::Client::new();
    let mut headers = HeaderMap::new();
    headers.insert(USER_AGENT, HeaderValue::from_static("thanda-lora-flasher"));
    headers.insert(ACCEPT, HeaderValue::from_static("application/vnd.github+json"));

    // FIXED: Using the dedicated firmware repository
    let url = "https://api.github.com/repos/warwickchapman/lora-rs-firmware/releases";
    let response = client
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
    
    let client = reqwest::Client::new();
    let mut headers = HeaderMap::new();
    headers.insert(USER_AGENT, HeaderValue::from_static("thanda-lora-flasher"));

    let response = client.get(url).headers(headers).send().await.map_err(|e| e.to_string())?;
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
