{
  description = "Linux-native archive browser and mod tool for GTA V";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "archon";
        version = "0.1.0";
        src = ./.;
        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
          pkgs.qt6.wrapQtAppsHook
          pkgs.pkg-config
        ];
        buildInputs = [
          pkgs.qt6.qtbase
          pkgs.qt6.qtsvg
          pkgs.zlib
          pkgs.openssl
          pkgs.libzip
        ];
        qtWrapperArgs = [
          "--suffix PATH : ${pkgs.lib.makeBinPath [ pkgs.protontricks ]}"
        ];
        doCheck = true;
        checkPhase = ''
          runHook preCheck
          QT_QPA_PLATFORM=offscreen ctest --output-on-failure
          runHook postCheck
        '';
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
        packages = [
          pkgs.xvfb-run
          pkgs.xdotool
          pkgs.imagemagick
          pkgs.clang
          pkgs.clang-tools
          pkgs.gdb
        ];
      };

      checks.${system}.default = self.packages.${system}.default;
    };
}
