{
  description = "nixalarm packaging example";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system:
          f {
            pkgs = import nixpkgs { inherit system; };
          });
    in {
      packages = forAllSystems ({ pkgs }: {
        default = pkgs.stdenv.mkDerivation {
          pname = "nixalarm";
          version = "0.1.0";
          src = ../..;

          nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];
          buildInputs = [ pkgs.SDL2 pkgs.ffmpeg ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
        };
      });

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.gcc
            pkgs.SDL2
            pkgs.ffmpeg
          ];
        };
      });
    };
}
