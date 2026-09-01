use std::fs;
use std::path::{Path, PathBuf};

pub fn normalize_path(path: &str) -> String {
    let input = PathBuf::from(path);
    let resolved = fs::canonicalize(&input).unwrap_or_else(|_| {
        if input.is_absolute() {
            input
        } else {
            std::env::current_dir()
                .unwrap_or_else(|_| PathBuf::from("."))
                .join(input)
        }
    });
    strip_extended_prefix(&resolved.to_string_lossy())
}

pub fn paths_equal(a: &str, b: &str) -> bool {
    normalize_path(a).eq_ignore_ascii_case(&normalize_path(b))
}

fn strip_extended_prefix(s: &str) -> String {
    let mut out = s.to_string();
    if let Some(rest) = out.strip_prefix(r"\\?\") {
        out = rest.to_string();
    }
    if let Some(rest) = out.strip_prefix(r"UNC\") {
        out = format!(r"\\{}", rest);
    }
    out
}

pub fn normalize_path_buf(path: &Path) -> String {
    normalize_path(&path.to_string_lossy())
}
