# Maintainer: Walther <martinwalther.1989@gmail.com>
_pkgname=vantaviewer
pkgname=vantaviewer-git
pkgver=r0.0
pkgrel=1
pkgdesc="HDR-native image viewer for Hyprland with true blacks on OLED"
arch=('x86_64')
url="https://github.com/Waltherion/vantaviewer"
license=('GPL-3.0-or-later')
depends=(
  'qt6-base'
  'qt6-imageformats'   # WebP, TIFF and other SDR formats via Qt plugins
  'kimageformats'      # OpenEXR, Radiance HDR, PFM, QOI, JP2, TGA, PSD, PCX, RAW, ...
  'qt6-svg'            # SVG
  'libavif'
  'libjxl'
  'libheif'            # also pulls x265, used for HEIC/HEIF encoding
  'libultrahdr'
  'lcms2'
  'wayland'
  'vulkan-icd-loader'
)
makedepends=(
  'git'
  'cmake'
  'ninja'
  'qt6-shadertools'
  'wayland-protocols'
  'pkgconf'
)
provides=('vantaviewer')
conflicts=('vantaviewer')
source=("git+$url.git")
sha256sums=('SKIP')

pkgver() {
  cd "$srcdir/$_pkgname"
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cmake -S "$_pkgname" -B build -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
  install -Dm644 "$_pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
