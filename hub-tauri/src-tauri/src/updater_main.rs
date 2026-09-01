#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant};

fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let mut args = env::args().skip(1);
    let new_exe = PathBuf::from(args.next().ok_or("missing arg: <new_exe_path>")?);
    let target_exe = PathBuf::from(args.next().ok_or("missing arg: <target_exe_path>")?);
    let restart = args.next().unwrap_or_else(|| "1".into()) == "1";

    if !new_exe.is_file() {
        return Err(format!("new exe not found: {}", new_exe.display()));
    }
    if !target_exe.is_file() {
        return Err(format!("target exe not found: {}", target_exe.display()));
    }

    // Wait for the target to fully exit and unlock.
    wait_until_unlocked(&target_exe, Duration::from_secs(60))?;

    // Replace atomically-ish: move old aside, copy new, then delete old.
    let backup = target_exe.with_extension("exe.bak");
    let _ = fs::remove_file(&backup);
    fs::rename(&target_exe, &backup).map_err(|e| format!("rename backup failed: {e}"))?;
    if let Err(copy_err) = fs::copy(&new_exe, &target_exe) {
        // Rollback
        let _ = fs::rename(&backup, &target_exe);
        return Err(format!("copy new exe failed: {copy_err}"));
    }
    let _ = fs::remove_file(&backup);

    if restart {
        let _ = Command::new(&target_exe).spawn();
    }
    Ok(())
}

fn wait_until_unlocked(path: &PathBuf, timeout: Duration) -> Result<(), String> {
    let start = Instant::now();
    loop {
        match fs::OpenOptions::new().read(true).write(true).open(path) {
            Ok(f) => {
                drop(f);
                return Ok(());
            }
            Err(_) => {
                if start.elapsed() > timeout {
                    return Err("timed out waiting for Hub to exit".into());
                }
                thread::sleep(Duration::from_millis(250));
            }
        }
    }
}
