{ pkgs }:

let
  lib = pkgs.lib;
  ftetwildSource = pkgs.callPackage ./ftetwild-source.nix {};
  openmpPkg =
    if pkgs ? openmp then pkgs.openmp
    else if pkgs ? llvmPackages && pkgs.llvmPackages ? openmp then pkgs.llvmPackages.openmp
    else null;

  gmpDev = lib.getDev pkgs.gmp;
  gmpLib = lib.getLib pkgs.gmp;
  mpfrDev = lib.getDev pkgs.mpfr;
  mpfrLib = lib.getLib pkgs.mpfr;
  tbbDev = lib.getDev pkgs.tbb;
  tbbLib = lib.getLib pkgs.tbb;
  openmpDev = if openmpPkg != null then lib.getDev openmpPkg else null;
  openmpLib = if openmpPkg != null then lib.getLib openmpPkg else null;
  boostDev = lib.getDev pkgs.boost;
  eigenDev = lib.getDev pkgs.eigen;
  zlibDev = lib.getDev pkgs.zlib;
  zlibLib = lib.getLib pkgs.zlib;

  packages = [
    pkgs.cmake
    pkgs.ninja
    pkgs.gnumake
    pkgs.pkg-config
    pkgs.git
    pkgs.gcc
    pkgs.gmp
    pkgs.mpfr
    pkgs.tbb
    pkgs.boost
    pkgs.eigen
    pkgs.zlib
  ] ++ lib.optional (openmpPkg != null) openmpPkg;

  runtimeLibs = [
    gmpLib
    mpfrLib
    tbbLib
    zlibLib
    pkgs.stdenv.cc.cc.lib
  ] ++ lib.optional (openmpLib != null) openmpLib;

  cmakePrefixEntries = [
    gmpDev
    gmpLib
    mpfrDev
    mpfrLib
    tbbDev
    tbbLib
    boostDev
    eigenDev
    zlibDev
    zlibLib
  ] ++ lib.optional (openmpDev != null) openmpDev
    ++ lib.optional (openmpLib != null) openmpLib;

  libraryPathEntries = [
    "${gmpLib}/lib"
    "${mpfrLib}/lib"
    "${tbbLib}/lib"
    "${zlibLib}/lib"
  ] ++ lib.optional (openmpLib != null) "${openmpLib}/lib";

  includePathEntries = [
    "${gmpDev}/include"
    "${mpfrDev}/include"
    "${boostDev}/include"
    "${eigenDev}/include/eigen3"
  ];

  shellHook = ''
    if [ -z "''${FTETWILD_SOURCE_DIR:-}" ]; then
      export FTETWILD_SOURCE_DIR="${ftetwildSource}"
    fi

    if [ -n "''${FTETWILD_SOURCE_DIR:-}" ] && [ -z "''${FTETWILD_BUILD_DIR:-}" ]; then
      export FTETWILD_BUILD_DIR="$PWD/.ftetwild-build"
    fi

    export GMP_INC="${gmpDev}/include"
    export GMP_LIB="${gmpLib}/lib"
    export MPFR_ROOT="${mpfrDev}"
    export TBBROOT="${tbbDev}"
    if [ -n "${if openmpDev != null then toString openmpDev else ""}" ]; then
      export OpenMP_ROOT="${if openmpDev != null then toString openmpDev else ""}"
    fi

    old_cmake_prefix_path="''${CMAKE_PREFIX_PATH:-}"
    export CMAKE_PREFIX_PATH="${lib.concatStringsSep ":" (map toString cmakePrefixEntries)}"
    if [ -n "$old_cmake_prefix_path" ]; then
      export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:$old_cmake_prefix_path"
    fi

    old_ld_library_path="''${LD_LIBRARY_PATH:-}"
    export LD_LIBRARY_PATH="${lib.makeLibraryPath runtimeLibs}"
    if [ -n "$old_ld_library_path" ]; then
      export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$old_ld_library_path"
    fi

    export LIBRARY_PATH="${lib.concatStringsSep ":" libraryPathEntries}:''${LIBRARY_PATH:-}"
    export CPATH="${lib.concatStringsSep ":" includePathEntries}:''${CPATH:-}"
    export CPLUS_INCLUDE_PATH="${lib.concatStringsSep ":" includePathEntries}:''${CPLUS_INCLUDE_PATH:-}"

    if [ -d "$FTETWILD_SOURCE_DIR/build/_deps" ]; then
      export FETCHCONTENT_SOURCE_DIR_CATCH2="$FTETWILD_SOURCE_DIR/build/_deps/catch2-src"
      export FETCHCONTENT_SOURCE_DIR_CLI11="$FTETWILD_SOURCE_DIR/build/_deps/cli11-src"
      export FETCHCONTENT_SOURCE_DIR_EIGEN="$FTETWILD_SOURCE_DIR/build/_deps/eigen-src"
      export FETCHCONTENT_SOURCE_DIR_FMT="$FTETWILD_SOURCE_DIR/build/_deps/fmt-src"
      export FETCHCONTENT_SOURCE_DIR_GEOGRAM="$FTETWILD_SOURCE_DIR/build/_deps/geogram-src"
      export FETCHCONTENT_SOURCE_DIR_JSON="$FTETWILD_SOURCE_DIR/build/_deps/json-src"
      export FETCHCONTENT_SOURCE_DIR_LIBIGL="$FTETWILD_SOURCE_DIR/build/_deps/libigl-src"
      export FETCHCONTENT_SOURCE_DIR_PREDICATES="$FTETWILD_SOURCE_DIR/build/_deps/predicates-src"
      export FETCHCONTENT_SOURCE_DIR_SPDLOG="$FTETWILD_SOURCE_DIR/build/_deps/spdlog-src"
      export FETCHCONTENT_SOURCE_DIR_TBB="$FTETWILD_SOURCE_DIR/build/_deps/tbb-src"

      export FTETWILD_FETCHCONTENT_CMAKE_ARGS="\
-DFETCHCONTENT_SOURCE_DIR_EIGEN=$FETCHCONTENT_SOURCE_DIR_EIGEN \
-DFETCHCONTENT_SOURCE_DIR_FMT=$FETCHCONTENT_SOURCE_DIR_FMT \
-DFETCHCONTENT_SOURCE_DIR_GEOGRAM=$FETCHCONTENT_SOURCE_DIR_GEOGRAM \
-DFETCHCONTENT_SOURCE_DIR_JSON=$FETCHCONTENT_SOURCE_DIR_JSON \
-DFETCHCONTENT_SOURCE_DIR_LIBIGL=$FETCHCONTENT_SOURCE_DIR_LIBIGL \
-DFETCHCONTENT_SOURCE_DIR_PREDICATES=$FETCHCONTENT_SOURCE_DIR_PREDICATES \
-DFETCHCONTENT_SOURCE_DIR_SPDLOG=$FETCHCONTENT_SOURCE_DIR_SPDLOG \
-DFETCHCONTENT_SOURCE_DIR_TBB=$FETCHCONTENT_SOURCE_DIR_TBB"

      export FTETWILD_TOPLEVEL_FETCHCONTENT_CMAKE_ARGS="$FTETWILD_FETCHCONTENT_CMAKE_ARGS \
-DFETCHCONTENT_SOURCE_DIR_CATCH2=$FETCHCONTENT_SOURCE_DIR_CATCH2 \
-DFETCHCONTENT_SOURCE_DIR_CLI11=$FETCHCONTENT_SOURCE_DIR_CLI11"
    else
      export FTETWILD_FETCHCONTENT_CMAKE_ARGS=""
      export FTETWILD_TOPLEVEL_FETCHCONTENT_CMAKE_ARGS=""
    fi

    export FTETWILD_SOURCE_CMAKE_ARG="-DFTETWILD_SOURCE_DIR=$FTETWILD_SOURCE_DIR"
  '';
in
{
  inherit packages runtimeLibs shellHook;

  shell = pkgs.mkShell {
    inherit packages;

    shellHook = ''
      export CMAKE_GENERATOR="''${CMAKE_GENERATOR:-Ninja}"
      ${shellHook}
      echo "fTetWild shell"
      echo "Source: ''${FTETWILD_SOURCE_DIR:-unset}"
      echo "Build:  ''${FTETWILD_BUILD_DIR:-unset}"
    '';
  };
}
