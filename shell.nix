{ pkgs ? import ./nix/pkgs_pin.nix }:

let
  lib = pkgs.lib;
  custom = import ./nix/pkgs.nix { inherit pkgs; };
  inherit (custom) gslib nelderMead indicators mfem gmsh mumps parmetis;
  ftetwild = import ./nix/ftetwild-shell.nix { inherit pkgs; };

  python = pkgs.python3.withPackages (ps: with ps; [
    pip
    numpy
    scipy
    matplotlib
    pandas
    jupyterlab
    meshio
    h5py
  ]);

  runtimeLibs = lib.unique ([
    pkgs.openmpi
    pkgs.hypre
    pkgs.scalapack
    mumps
    pkgs.metis
    parmetis
    pkgs.hdf5
    pkgs.hdf5.dev
    pkgs."yaml-cpp"
    pkgs.vtk
    pkgs.paraview
    pkgs.gmsh
    pkgs.netgen
    pkgs.zlib
    pkgs.libGLU
    pkgs.libglvnd
    pkgs.mesa
    pkgs.glew
    pkgs.fontconfig
    pkgs.freetype
    pkgs.libpng
    pkgs.SDL2
    pkgs.glm
    pkgs.libdrm
    pkgs.libxkbcommon
    pkgs.xorg.libX11
    pkgs.xorg.libXext
    pkgs.xorg.libXrender
    pkgs.xorg.libSM
    pkgs.xorg.libXft
    pkgs.xorg.libXrandr
    pkgs.xorg.libxcb
    gslib
    mfem
  ] ++ ftetwild.runtimeLibs);
in
pkgs.mkShell {
  packages = lib.unique ([
    pkgs.cmake
    pkgs.ninja
    pkgs.gnumake
    pkgs.pkg-config
    pkgs.git
    pkgs.curl
    pkgs.wget
    pkgs.unzip
    pkgs.gcc
    pkgs.gfortran
    pkgs.gdb
    pkgs.swig
    pkgs.openmpi
    pkgs.hypre
    pkgs.scalapack
    mumps
    pkgs.metis
    parmetis
    pkgs.hdf5
    pkgs."yaml-cpp"
    pkgs.boost
    pkgs.eigen
    pkgs.cgal
    pkgs.vtk
    pkgs.paraview
    pkgs.netgen
    pkgs.ffmpeg
    pkgs.zlib
    pkgs.libGLU
    pkgs.libglvnd
    pkgs.mesa
    pkgs.glew
    pkgs.fontconfig
    pkgs.freetype
    pkgs.libpng
    pkgs.SDL2
    pkgs.glm
    pkgs.libdrm
    pkgs.libxkbcommon
    pkgs.xorg.xauth
    pkgs.xorg.xhost
    pkgs.xterm
    pkgs.nlohmann_json
    gmsh
    gslib
    mfem
    nelderMead
    indicators
    python
  ] ++ ftetwild.packages);

  shellHook = ''
    export PYTHONPATH="${gmsh}/${pkgs.python3.sitePackages}:''${PYTHONPATH:-}"

    export MFEM_ROOT="${mfem}"
    export MFEM_DIR="${mfem}/lib/cmake/mfem"
    export GSLIB_DIR="${gslib}"
    export NELDERMEAD_ROOT="${nelderMead}"
    export INDICATORS_ROOT="${indicators}"

    export HYPRE_ROOT="${pkgs.hypre}"
    export METIS_ROOT="${pkgs.metis}"
    export PARMETIS_ROOT="${parmetis}"
    export MUMPS_ROOT="${mumps}"
    export HDF5_ROOT="${pkgs.hdf5.dev}"
    export HDF5_DIR="${pkgs.hdf5.dev}/lib/cmake"
    export YAML_CPP_ROOT="${pkgs."yaml-cpp"}"
    export BOOST_ROOT="${pkgs.boost}"
    export Eigen3_ROOT="${pkgs.eigen}"

    export CC=mpicc
    export CXX=mpicxx
    export FC=mpifort
    export CMAKE_GENERATOR="Ninja"

    old_cmake_prefix_path="''${CMAKE_PREFIX_PATH:-}"
    export CMAKE_PREFIX_PATH="${mfem}:${gslib}:${gmsh}:${pkgs.hypre}:${pkgs.metis}:${parmetis}:${mumps}:${pkgs.scalapack}:${pkgs.hdf5.dev}:${pkgs."yaml-cpp"}:${pkgs.boost}:${pkgs.eigen}:${pkgs.cgal}:${pkgs.vtk}:${pkgs.paraview}:${pkgs.gmsh}:${pkgs.netgen}:${pkgs.zlib}"
    if [ -n "$old_cmake_prefix_path" ]; then
      export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:$old_cmake_prefix_path"
    fi

    if [ -z "''${VTK_DIR:-}" ] && [ -d "${pkgs.vtk}/lib/cmake/vtk" ]; then
      export VTK_DIR="${pkgs.vtk}/lib/cmake/vtk"
    elif [ -z "''${VTK_DIR:-}" ] && [ -d "${pkgs.vtk}/lib/cmake" ]; then
      export VTK_DIR="$(find ${pkgs.vtk}/lib/cmake -maxdepth 1 -mindepth 1 -type d -name 'vtk-*' | head -n 1)"
    fi

    old_ld_library_path="''${LD_LIBRARY_PATH:-}"
    export LD_LIBRARY_PATH="${lib.makeLibraryPath runtimeLibs}"
    if [ -n "$old_ld_library_path" ]; then
      export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$old_ld_library_path"
    fi

    export LIBRARY_PATH="${mfem}/lib:${gslib}/lib:''${LIBRARY_PATH:-}"
    export CPATH="${mfem}/include:${gslib}/include:${pkgs.hdf5.dev}/include:${pkgs.eigen}/include/eigen3:''${CPATH:-}"
    export CPLUS_INCLUDE_PATH="${mfem}/include:${gslib}/include:${pkgs.hdf5.dev}/include:${pkgs.eigen}/include/eigen3:''${CPLUS_INCLUDE_PATH:-}"

    ${ftetwild.shellHook}

    : "''${OMP_NUM_THREADS:=1}"
    : "''${MKL_NUM_THREADS:=1}"
    : "''${BLIS_NUM_THREADS:=1}"
    : "''${VECLIB_MAXIMUM_THREADS:=1}"
    : "''${NUMEXPR_NUM_THREADS:=1}"
    export OMP_NUM_THREADS MKL_NUM_THREADS BLIS_NUM_THREADS VECLIB_MAXIMUM_THREADS NUMEXPR_NUM_THREADS

    export VTK_DEFAULT_OPENGL_WINDOW="''${VTK_DEFAULT_OPENGL_WINDOW:-vtkEGLRenderWindow}"
    export VTK_DEFAULT_EGL_DEVICE_INDEX="''${VTK_DEFAULT_EGL_DEVICE_INDEX:-0}"
    export LIBGL_ALWAYS_SOFTWARE="''${LIBGL_ALWAYS_SOFTWARE:-1}"
    export MESA_LOADER_DRIVER_OVERRIDE="''${MESA_LOADER_DRIVER_OVERRIDE:-llvmpipe}"
    export GALLIUM_DRIVER="''${GALLIUM_DRIVER:-llvmpipe}"
  '';
}
