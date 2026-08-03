{ lib
, stdenv
, fetchzip
, mpi
, gfortran
, blas
, lapack
, scalapack
, scotch
, metis
, parmetis
, fixDarwinDylibNames
, mpiCheckPhaseHook
, static ? stdenv.hostPlatform.isStatic
, mpiSupport ? false
, withParmetis ? false
, withPtScotch ? mpiSupport
}:

assert withParmetis -> mpiSupport;
assert withPtScotch -> mpiSupport;

let
  scotch' = scotch.override { inherit withPtScotch; };
  profile = if mpiSupport then "debian.PAR" else "debian.SEQ";

  orderingsF = toString (
    [ "-Dmetis" "-Dpord" "-Dscotch" ]
    ++ lib.optional withParmetis "-Dparmetis"
    ++ lib.optional withPtScotch "-Dptscotch"
  );

  lmetis = toString (
    [ "-L${metis}/lib" "-lmetis" ]
    ++ lib.optionals withParmetis [ "-L${parmetis}/lib" "-lparmetis" ]
  );

  lscotch = toString (
    if withPtScotch then [
      "-L${scotch'}/lib"
      "-lptscotch"
      "-lptesmumps"
      "-lptscotcherr"
      "-lscotch"
      "-lscotcherr"
    ] else [
      "-L${scotch'}/lib"
      "-lesmumps"
      "-lscotch"
      "-lscotcherr"
    ]
  );

  libblasFlags = toString [
    "-L${blas}/lib"
    "-lblas"
  ];

  lapackFlags = toString [
    "-L${lapack}/lib"
    "-llapack"
  ];

  # Give MUMPS everything the examples may need through ScaLAPACK path.
  scalapFlags = toString (
    lib.optionals mpiSupport [
      "-L${scalapack}/lib"
      "-lscalapack"
      "-L${lapack}/lib"
      "-llapack"
      "-L${blas}/lib"
      "-lblas"
    ]
  );

  libparFlags = toString (
    lib.optionals mpiSupport [
      "-L${mpi}/lib"
      "-lmpi"
      "-lmpi_mpifh"
      "-lmpi_usempi_ignore_tkr"
      "-lmpi_usempif08"
      "-L${scalapack}/lib"
      "-lscalapack"
      "-L${lapack}/lib"
      "-llapack"
      "-L${blas}/lib"
      "-lblas"
    ]
  );
in
stdenv.mkDerivation (finalAttrs: {
  pname = "mumps";
  version = "5.8.2";

  __structuredAttrs = true;
  strictDeps = true;

  src = fetchzip {
    url = "https://mumps-solver.org/MUMPS_${finalAttrs.version}.tar.gz";
    hash = "sha256-AzCzNUd+NFP7Jat4cw1YpA9160cvW1zXLoLxstsbtHA=";
  };

  nativeBuildInputs =
    [ gfortran ]
    ++ lib.optional mpiSupport mpi
    ++ lib.optional stdenv.hostPlatform.isDarwin fixDarwinDylibNames;

  buildInputs =
    [ blas lapack metis scotch' ]
    ++ lib.optional mpiSupport scalapack
    ++ lib.optional withParmetis parmetis;

  configurePhase = ''
    runHook preConfigure

    cp Make.inc/Makefile.${profile} ./Makefile.inc

    cat >> Makefile.inc <<EOF
    CC = mpicc
    FC = mpifort
    FL = mpifort

    ISCOTCH = -I${lib.getDev scotch'}/include
    LMETIS = ${lmetis}
    LSCOTCH = ${lscotch}
    ORDERINGSF = ${orderingsF}

    LIBBLAS = ${libblasFlags}
    LAPACK = ${lapackFlags}
    SCALAP = ${scalapFlags}

    INCPAR =
    LIBPAR = ${libparFlags}

    OPTF = -O0 -g -fallow-argument-mismatch
    OPTC = -O0 -g
    OPTL = -O0 -g
    EOF

    runHook postConfigure
  '';

  enableParallelBuilding = true;

  makeFlags =
    lib.optionals stdenv.hostPlatform.isDarwin [
      "SONAME="
      "LIBEXT_SHARED=.dylib"
    ]
    ++ [
      (if static then "all" else "allshared")
    ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp -r include lib $out
    runHook postInstall
  '';

  doInstallCheck = false;
})