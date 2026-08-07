{ pkgs }:

let
  sourceRoot =
    if builtins.pathExists ../geometry/fTetWild then
      ../geometry/fTetWild
    else
      builtins.fetchGit {
        url = "https://github.com/wildmeshing/fTetWild.git";
        rev = "d7d99bb4387a07895b9adce058dc7305f6b6e5ab";
      };

  dependencySources = [
    "catch2-src"
    "cli11-src"
    "eigen-src"
    "fmt-src"
    "geogram-src"
    "json-src"
    "libigl-src"
    "predicates-src"
    "spdlog-src"
    "tbb-src"
  ];
in
pkgs.runCommandLocal "ftetwild-source-patched" {
  nativeBuildInputs = [ pkgs.patch ];
  preferLocalBuild = true;
  allowSubstitutes = false;
} ''
  set -euo pipefail

  mkdir -p "$out"

  cp -r ${sourceRoot}/cmake "$out/"
  cp -r ${sourceRoot}/figs "$out/"
  cp -r ${sourceRoot}/python "$out/"
  cp -r ${sourceRoot}/src "$out/"
  cp -r ${sourceRoot}/tests "$out/"

  cp ${sourceRoot}/CMakeLists.txt "$out/"
  cp ${sourceRoot}/LICENSE.MPL2 "$out/"
  cp ${sourceRoot}/README.md "$out/"
  cp ${sourceRoot}/replicability_instructions.htm "$out/"

  if [ -d ${sourceRoot}/build/_deps ]; then
    mkdir -p "$out/build/_deps"
    for dep in ${builtins.concatStringsSep " " dependencySources}; do
      if [ -d ${sourceRoot}/build/_deps/"$dep" ]; then
        cp -r ${sourceRoot}/build/_deps/"$dep" "$out/build/_deps/"
      fi
    done
  fi

  chmod -R u+w "$out"
  patch -d "$out" -p1 < ${./patches/ftetwild-progress.patch}
  patch -d "$out" -p1 < ${./patches/ftetwild-cutting-tet-traversal.patch}
  patch -d "$out" -p1 < ${./patches/ftetwild-tetid64.patch}
  sed -i 's/if(NOT ''${GMP_FOUND})/if(NOT GMPfTetWild_FOUND)/' "$out/CMakeLists.txt"
  sed -i \
    's/FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG GMP_INCLUDE_DIRS GMP_LIBRARIES)/find_package_handle_standard_args(GMPfTetWild DEFAULT_MSG GMP_INCLUDE_DIRS GMP_LIBRARIES GMPXX_LIBRARIES)/' \
    "$out/cmake/FindGMPfTetWild.cmake"
  chmod -R a-w "$out"
''
