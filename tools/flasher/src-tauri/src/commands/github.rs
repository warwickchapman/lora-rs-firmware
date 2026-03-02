use crate::services::firmware;

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
