fn main() {
    tauri_build::build();
    println!("cargo:rerun-if-changed=../include/dtsx.h");
    println!("cargo:rerun-if-changed=../include/dtsx_live.h");
    println!("cargo:rerun-if-changed=../src/live/live.c");
    println!("cargo:rerun-if-changed=../src/core/config.c");
    println!("cargo:rerun-if-changed=src/live_bridge.c");
    cc::Build::new()
        .file("../src/live/live.c")
        .file("../src/core/config.c")
        .file("src/live_bridge.c")
        .include("../include")
        .flag_if_supported("-std=c11")
        .compile("dtsx_live");
    println!("cargo:rustc-link-lib=m");
}
