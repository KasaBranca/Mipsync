fn main() {
    // Tauri embeds the built frontend through generated context. Without an
    // explicit dependency, a CSS-only rebuild can leave the previous dist in
    // MipsyncHub.exe even though the executable was relinked.
    println!("cargo:rerun-if-changed=../dist");
    println!("cargo:rerun-if-changed=../src");
    println!("cargo:rerun-if-changed=tauri.conf.json");
    tauri_build::build()
}
