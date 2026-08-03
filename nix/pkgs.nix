{ pkgs }:

let
  gslib = pkgs.callPackage ./gslib.nix {};
  nelderMead = pkgs.callPackage ./nelder-mead.nix {};
  indicators = pkgs.callPackage ./indicators.nix {};
  ftetwildSource = pkgs.callPackage ./ftetwild-source.nix {};
  gmsh = pkgs.callPackage ./gmsh.nix {
    enablePython = true;
  };
  parmetis = pkgs.parmetis.overrideAttrs (old: {
    postPatch = (old.postPatch or "") + ''
      substituteInPlace CMakeLists.txt \
        --replace-fail 'cmake_minimum_required(VERSION 2.8)' \
                       'cmake_minimum_required(VERSION 3.5)'
    '';
  });
  mumps = pkgs.callPackage ./mumps.nix {
    inherit parmetis;
    mpiSupport = true;
    withParmetis = true;
    withPtScotch = true;
  };
  mfem = pkgs.callPackage ./mfem.nix {
    inherit gslib mumps parmetis;
    scalapack = pkgs.scalapack;
  };
in
{
  inherit gslib nelderMead indicators ftetwildSource gmsh parmetis mumps mfem;
}
