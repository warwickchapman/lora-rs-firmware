use crate::services::firmware;
use std::path::PathBuf;

const DEV_LRS_ZA_FIRMWARE_RELATIVE_PATH: &str = ".pio/build/lrs_za/firmware.bin";

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
    let firmware_path = repo_root.join(DEV_LRS_ZA_FIRMWARE_RELATIVE_PATH);
    if firmware_path.is_file() {
        Ok(Some(firmware_path.to_string_lossy().to_string()))
    } else {
        Ok(None)
    }
}
