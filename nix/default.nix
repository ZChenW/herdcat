{
  lib,
  stdenv,
  pkg-config,
  wayland,
  freetype,
  fontconfig,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "herdcat";
  version = "0.2.0";
  src = ../.;

  # Build toolchain and dependencies
  # Protocol bindings are pre-generated and committed to git, so
  # wayland-scanner and wayland-protocols are only needed for `make protocols`.
  strictDeps = true;
  nativeBuildInputs = [pkg-config];
  buildInputs = [
    wayland
    freetype
    fontconfig
  ];

  makeFlags = ["release"];
  installPhase = ''
    runHook preInstall

    # Install binaries
    install -Dm755 build/herdcat $out/bin/${finalAttrs.meta.mainProgram}
    install -Dm755 scripts/find_input_devices.sh $out/bin/herdcat-find-devices
    
    # Install man page
    install -Dm644 man/herdcat.1 $out/share/man/man1/herdcat.1
    install -Dm644 herdcat.conf.example $out/share/herdcat/herdcat.conf.example

    runHook postInstall
  '';

  # Package information
  meta = {
    description = "Wayland desktop cat that holds up a sign for every coding agent session";
    homepage = "https://github.com/ZChenW/herdcat";
    license = lib.licenses.mit;
    maintainers = [];
    mainProgram = "herdcat";
    platforms = lib.platforms.linux;
  };
})
