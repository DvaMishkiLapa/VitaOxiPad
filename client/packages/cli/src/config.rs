use serde::Deserialize;
use config::{Config as ConfigLoader, File, Environment};
use std::path::{Path, PathBuf};
use color_eyre::eyre::eyre;
use std::fs;
use std::env;
use home::home_dir;  // Import home crate

#[derive(Deserialize)]
pub struct Config {
    pub ip: Option<String>,
    pub port: Option<u16>,
    pub configuration: Option<String>,
    pub polling_interval: Option<u64>,
    pub debug: Option<bool>,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            ip: Some("192.168.0.100".to_string()),
            port: Some(5000),
            configuration: Some("standart".to_string()),
            polling_interval: Some(6000),
            debug: Some(false),
        }
    }
}

pub fn validate_toml(file_path: &str) -> color_eyre::Result<()> {
    let content = fs::read_to_string(file_path)
        .map_err(|e| eyre!("Failed to read config file: {}", e))?;
    
    toml::de::from_str::<Config>(&content)
        .map_err(|e| eyre!("TOML validation error: {}", e))?;
    
    Ok(())
}

fn get_config_file_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();

    // Check for config.toml in the app directory (current working directory)
    paths.push(Path::new("config.toml").to_path_buf());

    // Check for vitaoxipad.toml in the user's home directory
    if let Some(home) = home_dir() {
        // Common paths
        paths.push(home.join("vitaoxipad.toml"));
        paths.push(home.join("vitaoxipad").join("config.toml"));

        // Linux specific paths
        paths.push(home.join(".vitaoxipad"));            // .vitaoxipad directory
        paths.push(home.join(".config").join("vitaoxipad.toml"));  // .config/vitaoxipad.toml

        // Additional paths in Documents folder (for Windows and others)
        paths.push(home.join("Documents").join("vitaoxipad.toml"));
        paths.push(home.join("Documents").join("vitaoxipad").join("config.toml")); // VitaOxiPad subfolder in Documents
        paths.push(home.join("Documents").join("vitaoxipad").join("vitaoxipad.toml")); // VitaOxiPad subfolder in Documents
    }

    // Windows specific path (vitaoxipad.toml in C:\Users\[username]\vitaoxipad)
    if let Some(user_dir) = env::var("USERPROFILE").ok() {
        paths.push(Path::new(&user_dir).join("vitaoxipad").join("vitaoxipad.toml"));
    }

    paths
}


pub fn load_config() -> color_eyre::Result<Config> {
    let mut settings = ConfigLoader::builder();

    let config_paths = get_config_file_paths();
    let mut config_file_found = false;

    // Check each possible config file path
    for path in config_paths {
        if path.exists() {
            // Validate the TOML file
            validate_toml(path.to_str().unwrap())?;
            println!("Using config file: {}", path.display());
            settings = settings.add_source(File::from(path));
            config_file_found = true;
            break;
        }
    }

    if !config_file_found {
        println!("No config file found. Using default configuration.");
    }

    // Add the source for environment variables
    settings = settings.add_source(Environment::with_prefix("VITA_"));

    // Check if environment variables are found
    if env::vars().any(|(key, _)| key.starts_with("VITA_")) {
        println!("Environment variables found. They will take precedence over the config file.");
    }

    // Build the settings and deserialize
    let config = settings.build()?.try_deserialize().map_err(|e| eyre!(e))?;

    Ok(config)
}

pub fn print_sample_config() {
    println!(r#"# Sample Configuration file for the VitaOxiPad
# Refer ReadMe for all available options

# The IP address to bind to
ip = "192.168.0.100"

# The port to listen on
port = 5000

# Path to another configuration file (if needed)
# Touchpad config to use:
# - standart
# - alt_triggers
# - rear_touchpad
# - front_touchpad
configuration = "standart"

# Polling interval in microseconds
polling_interval = 6000

# Enable or disable debug mode
debug = false"#);
}
