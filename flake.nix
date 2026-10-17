{
  description = "Project Somnium — ESP32-S3 AI camera firmware";

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
          clang-tools
          nixfmt
          (python3.withPackages (
            pythonPackages: with pythonPackages; [
              pyserial
              pillow
            ]
          ))
        ];
      };
    };
}
