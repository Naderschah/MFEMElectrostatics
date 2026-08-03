{ stdenvNoCC
, fetchFromGitHub
}:

stdenvNoCC.mkDerivation rec {
  pname = "indicators";
  version = "unstable-3872f37";

  src = fetchFromGitHub {
    owner = "p-ranav";
    repo = "indicators";
    rev = "3872f37abd90d7557bac5f834bfb45bd6c75259a";
    sha256 = "0makhf266nh9qax7sslpnrbi9wp6xgnrmydcv6mblsd513ljvxfp";
  };

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp -a include $out/
    runHook postInstall
  '';
}
