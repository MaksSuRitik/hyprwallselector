# AUR submission

The GitHub source repository is public. The AUR recipe is in `packaging/aur`.
AUR submission is a separate step; no submission is performed by the installer.

1. Add your public SSH key in the AUR account settings. Keep the private key out
   of both the source repository and the package repository.
2. Clone the package repository:
   `git clone ssh://aur@aur.archlinux.org/hyprwallselector-git.git`
3. Copy `packaging/aur/PKGBUILD` and `.SRCINFO` into that checkout.
4. Build with `makepkg` on a Linux filesystem. After a successful build, refresh
   metadata with `makepkg --printsrcinfo > .SRCINFO`.
5. Review `git diff`, then commit and push only `PKGBUILD` and `.SRCINFO`.

Do not upload binaries, personal settings, wallpaper files, SSH keys or build
folders to AUR. Build artifacts can be kept in the ignored `dist/` directory of
the source checkout. For release packaging, replace the VCS source with a tested
release archive and its real checksum; never invent an upstream release URL.
