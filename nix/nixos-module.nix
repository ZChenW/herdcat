# NixOS module for herdcat
{
  config,
  lib,
  pkgs,
  ...
}:
with lib; let
  cfg = config.programs.herdcat;
in {
  imports = [./common.nix];
  config = lib.mkIf cfg.enable (let
    configFile = config._herdcat.configFile;
  in {
    environment.systemPackages = [
      cfg.package

      # Helper scripts
      # For starting `herdcat` using the config file defined with Nix
      (pkgs.writeScriptBin "herdcat-exec" ''
        #!${pkgs.bash}/bin/bash
        exec ${cfg.package}/bin/herdcat --config ${configFile}
      '')
    ];

    users.groups.input = {};
    security.wrappers.herdcat-input = {
      source = "${cfg.package}/lib/herdcat/herdcat-input";
      owner = "root";
      group = "input";
      setuid = false;
      setgid = true;
      permissions = "u+rx,g+rx,o+rx";
    };

    # SystemD service
    systemd.user.services.herdcat = mkIf cfg.autostart {
      enable = true;
      description = "herdcat overlay";
      wantedBy = ["graphical-session.target"];
      partOf = ["graphical-session.target"];
      after = ["graphical-session.target"];
      serviceConfig = {
        Type = "exec";
        ExecStart = "${cfg.package}/bin/herdcat --config ${configFile}";
        Restart = "on-failure";
        RestartSec = "5s";
      };
    };
  });
}
