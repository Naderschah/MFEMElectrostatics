{ stdenv
, fetchFromGitHub
, cmake
, ninja
, pkg-config
, openmpi
, hypre
, metis
, parmetis
, mumps
, zlib
, gslib
, gfortran
, scalapack
}:

stdenv.mkDerivation rec {
  pname = "mfem";
  version = "4.9";

  src = fetchFromGitHub {
    owner = "mfem";
    repo = "mfem";
    rev = "v${version}";
    sha256 = "0n3chkavzf0w0fdcj59s38kv3sbxsgg1g3ic4c458vkx24x83qfg";
  };

  postPatch = ''
    substituteInPlace CMakeLists.txt \
      --replace-fail \
        'if (MFEM_USE_CONDUIT OR' \
        'if (MFEM_USE_CONDUIT OR MFEM_USE_MUMPS OR'

    substituteInPlace CMakeLists.txt \
      --replace-fail 'set(MFEM_INSTALL_DIR ''${CMAKE_INSTALL_PREFIX})' 'set(MFEM_INSTALL_DIR ''${CMAKE_INSTALL_PREFIX})
  set_source_files_properties(fem/tmop/mult/mult3_limit.cpp PROPERTIES COMPILE_OPTIONS "-fno-openmp")'
  '';

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    openmpi
    gfortran
  ];

  buildInputs = [
    openmpi
    hypre
    metis
    parmetis
    mumps
    zlib
    gslib
    scalapack
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Debug"
    "-DCMAKE_C_COMPILER=mpicc"
    "-DCMAKE_CXX_COMPILER=mpicxx"
    "-DCMAKE_Fortran_COMPILER=mpifort"

    "-DMPI_C_COMPILER=mpicc"
    "-DMPI_CXX_COMPILER=mpicxx"
    "-DMPI_Fortran_COMPILER=mpifort"

    "-DXSDK_ENABLE_C=ON"

    "-DMFEM_ENABLE_TESTING=OFF"
    "-DMFEM_ENABLE_EXAMPLES=OFF"
    "-DMFEM_ENABLE_MINIAPPS=OFF"

    "-DMFEM_USE_MPI=ON"
    "-DMFEM_USE_OPENMP=OFF"

    "-DMFEM_USE_METIS=ON"
    "-DMETIS_DIR=${metis}"
    "-DParMETIS_DIR=${parmetis}"

    "-DMFEM_USE_GSLIB=ON"
    "-DGSLIB_DIR=${gslib}"

    "-DHYPRE_DIR=${hypre}"
    "-DZLIB_ROOT=${zlib}"

    "-DMFEM_USE_MUMPS=ON"
    "-DMUMPS_DIR=${mumps}"
    "-Dscalapack_DIR=${scalapack}"

    "-DMFEM_USE_SUPERLU=OFF"
    "-DMFEM_USE_PETSC=OFF"
    "-DMFEM_USE_SUNDIALS=OFF"
  ];

  enableParallelBuilding = true;
}