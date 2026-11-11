#[derive(Clone, Copy, Debug)]
pub struct ReleaseVersion {
    major: u32,
    minor: u32,
    patch: u32,
    channel: Option<char>,
    build: u32,
}
fn number(text: &str) -> Option<u32> {
    if text.is_empty() || !text.bytes().all(|c| c.is_ascii_digit()) { return None; }
    text.parse().ok()
}
pub fn parse(version: &str) -> Option<ReleaseVersion> {
    let trimmed = version.trim();
    let stripped = trimmed.strip_prefix('v').or_else(|| trimmed.strip_prefix('V')).unwrap_or(trimmed);
    let mut parts = stripped.split('.');
    let major = number(parts.next()?)?;
    let minor = number(parts.next()?)?;
    let tail = parts.next()?;
    if parts.next().is_some() { return None; }
    let split = tail.find(|c: char| !c.is_ascii_digit()).unwrap_or(tail.len());
    let patch = number(&tail[..split])?;
    let suffix = &tail[split..];
    let (channel, build) = if suffix.is_empty() { (None, 0) } else {
        let channel = suffix.chars().next()?;
        if !matches!(channel, 'a' | 'b' | 'f') || major == 0 || major >= 2000 { return None; }
        if suffix.as_bytes().get(1) == Some(&b'0') { return None; }
        let build = number(&suffix[1..])?;
        if build == 0 { return None; }
        (Some(channel), build)
    };
    Some(ReleaseVersion { major, minor, patch, channel, build })
}
pub fn canonical_version(version: &str) -> Option<String> {
    let v = parse(version)?;
    let base = format!("v{}.{}.{}", v.major, v.minor, v.patch);
    Some(match v.channel { Some(channel) => format!("{base}{channel}{}", v.build), None => base })
}
pub fn is_editor_release(version: &str) -> bool {
    matches!(parse(version), Some(v) if v.major >= 1)
}
pub fn is_prerelease(version: &str) -> bool {
    matches!(parse(version), Some(v) if matches!(v.channel, Some('a' | 'b')))
}
fn release_key(v: ReleaseVersion) -> (u8, u32, u32, u32, u8, u32) {
    let era = if v.major == 0 { 0 } else if v.major >= 2000 { 1 } else { 2 };
    let stage = match v.channel { Some('a') => 0, Some('b') => 1, _ => 2 };
    (era, v.major, v.minor, v.patch, stage, v.build)
}
pub fn is_less(current: &str, latest: &str) -> bool {
    match (parse(current), parse(latest)) { (Some(a), Some(b)) => release_key(a) < release_key(b), _ => false }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn formal_releases_supersede_calendar_versions() {
        assert!(is_less("v2026.10.18", "v1.0.0f1"));
        assert!(!is_less("v1.0.0f1", "v2026.10.18"));
        assert!(is_less("1.0.0f1", "v1.0.1f1"));
        assert!(is_less("v1.9.0f1", "v1.10.0f1"));
        assert!(is_less("v2026.9.1", "2026.10.18"));
        assert!(is_less("0.1.16", "2026.9.1"));
    }
    #[test]
    fn compares_channels_and_builds_numerically() {
        assert!(is_less("1.0.0a10", "1.0.0b1"));
        assert!(is_less("1.0.0b10", "1.0.0f1"));
        assert!(is_less("1.0.0f2", "1.0.0f10"));
        assert!(is_less("1.0.0", "1.0.0f1"));
        assert!(!is_less("V1.0.0f1", "1.0.0f1"));
        assert!(is_prerelease("1.0.0b1"));
        assert!(!is_prerelease("1.0.0f1"));
    }
    #[test]
    fn retains_history_and_rejects_invalid_versions() {
        assert!(is_editor_release("v1.0.0f1"));
        assert!(is_editor_release("v2026.10.18"));
        assert_eq!(canonical_version(" 1.0.0f1 ").as_deref(), Some("v1.0.0f1"));
        for version in ["v0.1.16", "1.0.0f0", "1.0.0f", "1.0.0f1/../", "1.0.0-preview", "1.0.0.1", "2026.10.18f1", "invalid"] {
            assert!(!is_editor_release(version), "{version}");
        }
    }
}
