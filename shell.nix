{ pkgs ? (import <nixpkgs> { }) }:

pkgs.llvmPackages_latest.stdenv.mkDerivation {
  name = "dev-shell";
  nativeBuildInputs = with pkgs; [ clang-tools meson ninja pkgconfig ];
  buildInputs = with pkgs; [ liburing.dev sqlite.dev ];
  strictDeps = false;
}
