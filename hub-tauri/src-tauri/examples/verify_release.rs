//! Verify a staged or published release with the Hub's production manifest reader.
//! cargo run --release --example verify_release -- v2026.10.12 [manifest.json]
#[path = "../src/manifest.rs"]
mod manifest;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args().skip(1);
    let expected = args.next().ok_or("expected release tag is required")?;
    let feed: manifest::Manifest = if let Some(path) = args.next() {
        let bytes = std::fs::read(path)?;
        serde_json::from_slice(&bytes)?
    } else {
        manifest::fetch_latest()?
    };
    assert_eq!(feed.schema_version, 1, "unsupported feed schema");
    assert_eq!(feed.hub.version, expected, "Hub update points at another version");
    assert_eq!(feed.hub.asset_name, "MipsyncHub_Windows_x64.zip");
    assert_eq!(feed.editor.asset_name, "MipsyncEngine_Windows_x64.zip");
    let base = format!("https://github.com/{}/releases/download/{expected}", manifest::repo());
    assert_eq!(feed.hub.download_url, format!("{base}/{}", feed.hub.asset_name));
    let releases = feed.editor.releases.into_vec();
    assert_eq!(releases.first().map(|r| r.version.as_str()), Some(expected.as_str()), "latest editor must be first");
    assert_eq!(releases.iter().filter(|r| r.version == expected).count(), 1);
    let editor = &releases[0];
    assert_eq!(editor.download_url, format!("{base}/{}", feed.editor.asset_name));
    assert!(releases.iter().any(|r| r.version == "v2026.9.1"), "previous install history was lost");
    println!("Hub detects {} and editor {}; {} editor releases retained.", feed.hub.version, editor.version, releases.len());
    Ok(())
}
