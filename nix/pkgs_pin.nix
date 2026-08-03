let
  rev = "1766437c5509f444c1b15331e82b8b6a9b967000";
  pkgs = import (builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/${rev}.tar.gz";
    sha256 = "sha256:0lx8fqqvskvki35j16xb750z62q0s8d4x4h0vlpnxv32adwh1d0m";
  }) {
    config.allowUnfreePredicate = pkg:
      let
        pkgName =
          if pkg ? pname then pkg.pname
          else if pkg ? name then (builtins.parseDrvName pkg.name).name
          else "";
      in
      builtins.elem pkgName [ "parmetis" ];
  };
in
pkgs