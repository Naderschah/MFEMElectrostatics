{ pkgs ? import ./nix/pkgs_pin.nix }:

let
  custom = import ./nix/pkgs.nix { inherit pkgs; };
in
custom // {
  shell = import ./shell.nix { inherit pkgs; };
}
