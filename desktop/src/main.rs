use serde::Serialize;
use serde_json::{Map, Value};
use std::{env, ffi::CString, fs, path::{Path, PathBuf}, process::Command};
use tauri::generate_handler;

extern "C" {
    fn dtsx_desktop_publish_profile(label: *const std::os::raw::c_char, path: *const std::os::raw::c_char) -> i32;
    fn dtsx_desktop_live_available(label: *const std::os::raw::c_char) -> i32;
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct ProfileInfo { id: String, name: String, kind: String, active: bool }
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct AudioNode { id: String, name: String, description: String, media_class: String, virtual_sink: bool }
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct Diagnostics { pipewire_available: bool, wpctl_available: bool, live_speakers_page: bool, live_headphones_page: bool, filter_chain_detected: bool, virtual_sink_detected: bool, message: String }

fn config_root() -> Result<PathBuf, String> { Ok(PathBuf::from(env::var_os("HOME").ok_or("HOME is not set")?).join(".config/dtsx")) }
fn bundled_profiles() -> PathBuf { PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../configs") }
fn valid_id(id: &str) -> bool { !id.is_empty() && id.len() <= 80 && id.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '-' | '_' | '.')) }
fn active_path(mode: &str) -> Result<PathBuf, String> { if mode != "speakers" && mode != "headphones" { Err("Invalid audio mode".into()) } else { Ok(config_root()?.join(format!("{mode}.json"))) } }
fn active_source_path(mode: &str) -> Result<PathBuf, String> { let active = active_path(mode)?; if active.exists() { Ok(active) } else { Ok(bundled_profiles().join(if mode == "speakers" { "speakers-music.json" } else { "headphones-music.json" })) } }
fn profile_path(id: &str) -> Result<PathBuf, String> {
    if !valid_id(id) { return Err("Invalid profile id".into()); }
    if id == "speakers" || id == "headphones" { return active_source_path(id); }
    let user = config_root()?.join("profiles").join(format!("{id}.json"));
    if user.exists() { return Ok(user); }
    let bundled = bundled_profiles().join(format!("{id}.json"));
    if bundled.exists() { Ok(bundled) } else { Err("Profile not found".into()) }
}
fn number(profile: &Map<String, Value>, key: &str, min: f64, max: f64) -> Result<(), String> {
    if let Some(value) = profile.get(key) { let value = value.as_f64().ok_or_else(|| format!("{key} must be a number"))?; if !value.is_finite() || value < min || value > max { return Err(format!("{key} must be between {min} and {max}")); } }
    Ok(())
}
fn validate_profile(profile: &Value) -> Result<(), String> {
    let object = profile.as_object().ok_or("Profile must be a JSON object")?;
    number(object, "bass_amount", 0.0, 0.55)?; number(object, "bass_drive", 0.5, 4.0)?; number(object, "bass_extend_db", 0.0, 12.0)?;
    number(object, "loud_amount", 0.0, 1.0)?; number(object, "loud_volume", 0.0, 1.0)?; number(object, "width", 0.0, 2.0)?; number(object, "crossfeed", 0.0, 1.0)?;
    number(object, "ceiling_db", -60.0, -0.01)?; number(object, "input_gain_db", -36.0, 24.0)?; number(object, "output_gain_db", -36.0, 24.0)?;
    if let Some(gains) = object.get("user_eq_g") { let gains = gains.as_array().ok_or("user_eq_g must be an array")?; if gains.len() != 10 { return Err("user_eq_g must have exactly 10 bands".into()); } for gain in gains { let gain = gain.as_f64().ok_or("user_eq_g entries must be numbers")?; if !gain.is_finite() || !(-12.0..=12.0).contains(&gain) { return Err("user_eq_g values must be between -12 and 12 dB".into()); } } }
    Ok(())
}
fn parse_profile(text: &str) -> Result<Value, String> { let value: Value = serde_json::from_str(text).map_err(|_| "Profile is not valid JSON".to_string())?; validate_profile(&value)?; Ok(value) }
fn atomic_write(path: &Path, contents: &str) -> Result<(), String> {
    let parent = path.parent().ok_or("Invalid profile destination")?; fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    if path.exists() { fs::copy(path, path.with_extension("json.bak")).map_err(|e| e.to_string())?; }
    let temporary = path.with_extension("json.new"); fs::write(&temporary, contents).map_err(|e| e.to_string())?; fs::rename(temporary, path).map_err(|e| e.to_string())
}
fn publish(mode: &str, path: &Path) -> Result<(), String> {
    let label = CString::new(mode).map_err(|_| "Invalid mode")?; let path = CString::new(path.to_string_lossy().as_bytes()).map_err(|_| "Invalid profile path")?;
    let rc = unsafe { dtsx_desktop_publish_profile(label.as_ptr(), path.as_ptr()) }; if rc == 0 { Ok(()) } else { Err(format!("DSP publish failed ({rc})")) }
}
fn command_exists(name: &str) -> bool { Command::new(name).arg("--help").output().map(|o| o.status.success()).unwrap_or(false) }
fn pw_dump() -> Result<Value, String> { let output = Command::new("pw-dump").output().map_err(|_| "pw-dump is unavailable")?; if !output.status.success() { return Err("PipeWire did not return graph data".into()); } serde_json::from_slice(&output.stdout).map_err(|_| "PipeWire returned invalid graph data".into()) }
fn devices_from_graph(graph: &Value) -> Vec<AudioNode> { graph.as_array().into_iter().flatten().filter_map(|node| { let info = node.get("info")?; let props = info.get("props")?.as_object()?; let media_class = props.get("media.class")?.as_str()?; if media_class != "Audio/Sink" { return None; } let id = node.get("id")?.as_i64()?.to_string(); let name = props.get("node.name").and_then(Value::as_str).unwrap_or("unnamed").to_string(); let description = props.get("node.description").or_else(|| props.get("node.nick")).and_then(Value::as_str).unwrap_or(&name).to_string(); let virtual_sink = name.contains("dtsx") || description.to_ascii_lowercase().contains("dts:x-inspired"); Some(AudioNode { id, name, description, media_class: media_class.to_string(), virtual_sink }) }).collect() }
#[tauri::command] fn list_profiles() -> Result<Vec<ProfileInfo>, String> {
    let mut profiles = Vec::new();
    for mode in ["speakers", "headphones"] { let path = active_source_path(mode)?; let name = fs::read_to_string(&path).ok().and_then(|t| serde_json::from_str::<Value>(&t).ok()).and_then(|v| v.get("name").and_then(Value::as_str).map(str::to_owned)).unwrap_or_else(|| format!("Active {mode}")); profiles.push(ProfileInfo { id: mode.into(), name, kind: "active".into(), active: true }); }
    let config = config_root()?; for directory in [bundled_profiles(), config.join("profiles")] { if let Ok(entries) = fs::read_dir(&directory) { for entry in entries.flatten() { let path = entry.path(); if path.extension().and_then(|x| x.to_str()) != Some("json") { continue; } let Some(id) = path.file_stem().and_then(|x| x.to_str()).filter(|id| valid_id(id)) else { continue; }; if profiles.iter().any(|p| p.id == id) { continue; } let name = fs::read_to_string(&path).ok().and_then(|t| serde_json::from_str::<Value>(&t).ok()).and_then(|v| v.get("name").and_then(Value::as_str).map(str::to_owned)).unwrap_or_else(|| id.to_string()); profiles.push(ProfileInfo { id: id.to_string(), name, kind: if path.starts_with(&config) { "custom".into() } else { "built-in".into() }, active: false }); } } }
    profiles.sort_by(|a, b| a.name.cmp(&b.name)); Ok(profiles)
}
#[tauri::command] fn load_profile(id: String) -> Result<Value, String> { let path = profile_path(&id)?; parse_profile(&fs::read_to_string(path).map_err(|_| "Could not read profile")?) }
#[tauri::command] fn save_profile(id: String, profile: Value) -> Result<(), String> { if !valid_id(&id) { return Err("Invalid profile id".into()); } validate_profile(&profile)?; let path = if id == "speakers" || id == "headphones" { active_path(&id)? } else { config_root()?.join("profiles").join(format!("{id}.json")) }; atomic_write(&path, &(serde_json::to_string_pretty(&profile).map_err(|e| e.to_string())? + "\n"))?; if id == "speakers" || id == "headphones" { publish(&id, &path)?; } Ok(()) }
#[tauri::command] fn activate_profile(mode: String, source_id: String) -> Result<(), String> { let source = profile_path(&source_id)?; let profile = parse_profile(&fs::read_to_string(source).map_err(|_| "Could not read source profile")?)?; save_profile(mode, profile) }
#[tauri::command] fn duplicate_profile(source_id: String, target_id: String) -> Result<(), String> { if !valid_id(&target_id) || target_id == "speakers" || target_id == "headphones" { return Err("Choose a valid custom profile name".into()); } save_profile(target_id, load_profile(source_id)?) }
#[tauri::command] fn restore_default(mode: String) -> Result<(), String> { let source = bundled_profiles().join(if mode == "speakers" { "speakers-music.json" } else if mode == "headphones" { "headphones-music.json" } else { return Err("Invalid audio mode".into()) }); let profile = parse_profile(&fs::read_to_string(source).map_err(|_| "Bundled default is unavailable")?)?; save_profile(mode, profile) }
#[tauri::command] fn import_profile(target_id: String, content: String) -> Result<(), String> { save_profile(target_id, parse_profile(&content)?) }
#[tauri::command] fn export_profile(id: String) -> Result<String, String> { serde_json::to_string_pretty(&load_profile(id)?).map_err(|e| e.to_string()) }
#[tauri::command] fn list_devices() -> Result<Vec<AudioNode>, String> { Ok(devices_from_graph(&pw_dump()?)) }
#[tauri::command] fn select_device(id: String) -> Result<(), String> { if !id.chars().all(|c| c.is_ascii_digit()) { return Err("Invalid device id".into()); } if !list_devices()?.iter().any(|d| d.id == id) { return Err("The selected device is no longer available".into()); } let status = Command::new("wpctl").args(["set-default", &id]).status().map_err(|_| "wpctl is unavailable")?; if status.success() { Ok(()) } else { Err("WirePlumber rejected the output selection".into()) } }
fn live_page(label: &str) -> bool { CString::new(label).ok().map(|v| unsafe { dtsx_desktop_live_available(v.as_ptr()) != 0 }).unwrap_or(false) }
#[tauri::command] fn diagnostics() -> Diagnostics { let nodes = pw_dump().map(|g| devices_from_graph(&g)).unwrap_or_default(); let filter_chain_detected = nodes.iter().any(|n| n.name.contains("dtsx") || n.description.to_ascii_lowercase().contains("dts:x-inspired")); let virtual_sink_detected = nodes.iter().any(|n| n.virtual_sink); Diagnostics { pipewire_available: command_exists("pw-dump"), wpctl_available: command_exists("wpctl"), live_speakers_page: live_page("speakers"), live_headphones_page: live_page("headphones"), filter_chain_detected, virtual_sink_detected, message: if virtual_sink_detected { "A DTS virtual sink is visible to PipeWire.".into() } else if filter_chain_detected { "A DTS filter is present, but no selectable DTS sink was found.".into() } else { "No DTS virtual sink was detected. Install and enable the PipeWire filter-chain first.".into() } } }
fn main() { tauri::Builder::default().invoke_handler(generate_handler![list_profiles, load_profile, save_profile, activate_profile, duplicate_profile, restore_default, import_profile, export_profile, list_devices, select_device, diagnostics]).run(tauri::generate_context!()).expect("error while running DTS Control Center"); }
