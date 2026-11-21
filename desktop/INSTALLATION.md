# Fedora installation

First install the DSP runtime through the existing project installer:

```bash
./scripts/install.sh --enable
systemctl --user restart pipewire pipewire-pulse wireplumber
```

Install the Fedora desktop build prerequisites if they are not already present:

```bash
sudo dnf install gcc-c++ pkgconf-pkg-config webkit2gtk4.1-devel libsoup3-devel gtk3-devel libappindicator-gtk3-devel openssl-devel rpm-build
```

Build the desktop bundle:

```bash
cd desktop
npm --prefix frontend install
cargo tauri build --bundles rpm
```

Install the resulting RPM using your normal package workflow. The desktop app
does not require root at runtime and only writes under the current user's
`~/.config/dtsx/` directory.

To remove the DSP runtime while retaining profiles, use the existing project
uninstaller:

```bash
./scripts/uninstall.sh
```
