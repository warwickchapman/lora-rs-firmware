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
pub async fn get_default_local_firmware() -> Result<Option<String>, String> {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let repo_root = match manifest_dir.join("../../..").canonicalize() {
        Ok(path) => path,
        Err(_) => return Ok(None),
    };
    let build_dir = repo_root.join(DEV_BUILD_DIR);

    // Prefer versioned binary (e.g. lrs-firmware-0.9.2~51.bin) so the
    // Flasher shows the version that was actually compiled.
    if let Ok(entries) = std::fs::read_dir(&build_dir) {
        let mut versioned: Vec<PathBuf> = entries
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| {
                p.extension().map_or(false, |ext| ext == "bin")
                    && p.file_name()
                        .and_then(|n| n.to_str())
                        .map_or(false, |n| n.starts_with("lrs-firmware-"))
            })
            .collect();
        // Sort descending so the newest version (highest dev build) wins
        versioned.sort();
        if let Some(latest) = versioned.pop() {
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
