{ stdenv
, fetchFromGitHub
, gnumake
, openmpi
}:

stdenv.mkDerivation rec {
  pname = "gslib";
  version = "1.0.9";

  src = fetchFromGitHub {
    owner = "gslib";
    repo = "gslib";
    rev = "v${version}";
    sha256 = "1nwj75xdgbmfi4cdpjjrpmljravwgk4mrmpyaqdkgcvh1nxh41ki";
  };

  nativeBuildInputs = [
    gnumake
    openmpi
  ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    make clean || true
    make CC=mpicc
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/include $out/lib
    cp build/include/gslib/*.h $out/include/
    cp -a build/include/gslib $out/include/
    cp -a build/lib/. $out/lib/
    ln -s $out/lib/libgs.a $out/lib/libgslib.a

    runHook postInstall
  '';
}
