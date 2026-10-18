{
  description = "Project Somnium — ESP32 edge client + laptop server";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      # server/scripts/somnium_uploader.py imports only the stdlib (urllib).
      # No extra Python modules are required for the dev shell to run it.
      pythonEnv = pkgs.python3.withPackages (
        pythonPackages: with pythonPackages; [
          pyserial
          pillow
          pygame
          fastapi
          uvicorn
          pyyaml
          python-multipart
        ]
      );
      # settings.json output_dir "outputs" is resolved next to the server package:
      # server/outputs. Run from the repository root.
      uploader = pkgs.writeShellScript "somnium-uploader" ''
        set -euo pipefail
        if [[ ! -f server/scripts/somnium_uploader.py ]]; then
          echo "run nix run .#uploader from the Somnium repository root" >&2
          exit 1
        fi
        mkdir -p server/outputs
        exec ${pkgs.python3}/bin/python server/scripts/somnium_uploader.py watch server/outputs
      '';
    in
    {
      apps.${system}.uploader = {
        type = "app";
        program = "${uploader}";
        meta.description = "Watch server/outputs and upload Somnium shots to the gallery";
      };

      checks.${system}.uploader-help = pkgs.runCommand "somnium-uploader-help" { } ''
        ${pkgs.python3}/bin/python ${./server/scripts/somnium_uploader.py} --help > "$out"
      '';

      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          platformio
          curl
          jq
          clang-tools
          nixfmt
          pythonEnv
        ];
      };
    };
}
