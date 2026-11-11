fn main() {
    println!("cargo:rerun-if-changed=../../release/version.txt");
    let version = std::fs::read_to_string("../../release/version.txt")
        .expect("release/version.txt is required");
    println!("cargo:rustc-env=MIPSYNC_RELEASE_VERSION={}", version.trim());
    // Tauri embeds the built frontend through generated context. Without an
    // explicit dependency, a CSS-only rebuild can leave the previous dist in
    // MipsyncHub.exe even though the executable was relinked.
    println!("cargo:rerun-if-changed=../dist");
    println!("cargo:rerun-if-changed=../src");
    println!("cargo:rerun-if-changed=tauri.conf.json");
    tauri_build::build()
}
