{
  description = "Project Somnium — ESP32 edge client + laptop server";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell {
        packages = with pkgs; [
          platformio
          curl
          jq
          clang-tools
          nixfmt
          (python3.withPackages (
            pythonPackages: with pythonPackages; [
              pyserial
              pillow
              pygame
              fastapi
              uvicorn
              pyyaml
              python-multipart
            ]
          ))
        ];
      };
    };
}
