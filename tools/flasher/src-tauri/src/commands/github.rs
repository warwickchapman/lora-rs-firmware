use crate::services::firmware;
use std::path::PathBuf;

const DEV_BUILD_DIR: &str = ".pio/build/lrs_za";

#[tauri::command]
pub async fn get_firmware_list() -> Result<Vec<String>, String> {
    match firmware::fetch_releases().await {
        Ok(releases) => {
            let tags = releases.into_iter().map(|r| r.tag_name).collect();
            Ok(tags)
        }
        Err(e) => Err(e),
    }
}

#[tauri::command]
pub async fn cache_recent_firmware_releases(app: tauri::AppHandle, limit: usize) -> Result<(), String> {
    let releases = firmware::fetch_releases().await?;
    for release in releases.into_iter().take(limit) {
        for asset in release.assets {
            if asset.name.ends_with(".bin") {
                let _ = firmware::download_firmware(&app, &asset.browser_download_url, &asset.name).await;
            }
        }
    }
    Ok(())
}


#[tauri::command]
pub async fn get_default_local_firmware() -> Result<Option<String>, String> {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let repo_root = match manifest_dir.join("../../..").canonicalize() {
        Ok(path) => path,
        Err(_) => return Ok(None),
    };
    let build_dir = repo_root.join(DEV_BUILD_DIR);

    // VERSION is the source of truth. Prefer its matching artifact so dev build
    // numbers such as ~10 are never ordered lexically behind ~9.
    if let Ok(version) = std::fs::read_to_string(repo_root.join("VERSION")) {
        let versioned_path = build_dir.join(format!("lrs-firmware-{}.bin", version.trim()));
        if versioned_path.is_file() {
            return Ok(Some(versioned_path.to_string_lossy().to_string()));
        }
    }

    // Fall back to the most recently written versioned binary when VERSION has
    // been bumped but its corresponding target has not been built yet.
    if let Ok(entries) = std::fs::read_dir(&build_dir) {
        let latest = entries
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| {
                p.extension().map_or(false, |ext| ext == "bin")
                    && p.file_name()
                        .and_then(|n| n.to_str())
                        .map_or(false, |n| n.starts_with("lrs-firmware-"))
            })
            .max_by_key(|path| path.metadata().and_then(|m| m.modified()).ok());
        if let Some(latest) = latest {
            return Ok(Some(latest.to_string_lossy().to_string()));
        }
    }

    // Fallback to generic firmware.bin
    let firmware_path = build_dir.join("firmware.bin");
    if firmware_path.is_file() {
        Ok(Some(firmware_path.to_string_lossy().to_string()))
    } else {
        Ok(None)
    }
}
