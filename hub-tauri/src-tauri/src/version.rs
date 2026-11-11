pub fn parse_triplet(version: &str) -> Option<(u32, u32, u32)> {
    let trimmed = version.trim();
    let stripped = trimmed.strip_prefix('v').or_else(|| trimmed.strip_prefix('V')).unwrap_or(trimmed);
    let mut parts = stripped.split('.');
    let result = (parts.next()?.parse().ok()?, parts.next()?.parse().ok()?, parts.next()?.parse().ok()?);
    if parts.next().is_some() { return None; }
    Some(result)
}

pub fn is_editor_release(version: &str) -> bool {
    matches!(parse_triplet(version), Some((major, _, _)) if major >= 1)
}

// Formal releases supersede the retired calendar scheme, which in turn
// superseded the original 0.x builds. Compare numerically within each era.
fn release_key((major, minor, patch): (u32, u32, u32)) -> (u8, u32, u32, u32) {
    let era = if major == 0 { 0 } else if major >= 2000 { 1 } else { 2 };
    (era, major, minor, patch)
}

pub fn is_less(current: &str, latest: &str) -> bool {
    match (parse_triplet(current), parse_triplet(latest)) {
        (Some(a), Some(b)) => release_key(a) < release_key(b),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn formal_releases_supersede_calendar_versions() {
        assert!(is_less("v2026.10.18", "v1.0.0"));
        assert!(!is_less("v1.0.0", "v2026.10.18"));
        assert!(is_less("1.0.0", "v1.0.1"));
        assert!(is_less("v1.9.0", "v1.10.0"));
        assert!(is_less("v2026.9.1", "2026.10.18"));
        assert!(is_less("0.1.16", "2026.9.1"));
        assert!(!is_less("V1.0.0", "1.0.0"));
    }
    #[test]
    fn retains_calendar_history_and_hides_scaffolding() {
        assert!(is_editor_release("v1.0.0"));
        assert!(is_editor_release("v2026.10.18"));
        assert!(!is_editor_release("v0.1.16"));
        assert!(!is_editor_release("v1.0.0-preview"));
        assert!(!is_editor_release("1.0.0.1"));
        assert!(!is_editor_release("invalid"));
    }
}
