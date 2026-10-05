{
  description = "herdcat - a Wayland desktop cat that holds up a sign for every coding agent session";

  # Dependencies
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  # What to do after fetching all dependencies
  outputs = inputs:
    inputs.flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = inputs.nixpkgs.legacyPackages.${system};
        herdcat = pkgs.callPackage ./nix/default.nix {};
      in {
        formatter = pkgs.alejandra;
        packages.default = herdcat;
        devShells.default = import ./nix/shell.nix {
          inherit pkgs;
        };
      }
    )
    // {
      nixosModules.default = import ./nix/nixos-module.nix;
      homeManagerModules.default = import ./nix/home-module.nix;
      homeModule.default = import ./nix/home-module.nix;
    };
}
