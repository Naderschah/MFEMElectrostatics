{ stdenvNoCC
, fetchFromGitHub
}:

stdenvNoCC.mkDerivation rec {
  pname = "nelder-mead";
  version = "unstable-dbb2ed0";

  src = fetchFromGitHub {
    owner = "YibaiMeng";
    repo = "nelder-mead";
    rev = "dbb2ed00a4aa9220b6728579705f738c35d3eeba";
    sha256 = "1zzn764sx5cm78s1ssfxi188q0r49khbpb99zhamvmigm2zv9s3r";
  };

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp nelder-mead.h $out/
    runHook postInstall
  '';
}
