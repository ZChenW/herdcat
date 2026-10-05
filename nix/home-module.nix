{
  lib,
  config,
  pkgs,
  ...
}: let
  cfg = config.programs.herdcat;
in {
  imports = [./common.nix];
  config = lib.mkIf cfg.enable (let
    configFile = config._herdcat.configFile;
  in {
    home.packages = [
      cfg.package

      # Helper scripts
      # For starting `herdcat` using the config file defined with Nix
      (pkgs.writeScriptBin "herdcat-exec" ''
        #!${pkgs.bash}/bin/bash
        exec ${cfg.package}/bin/herdcat --config ${configFile}
      '')
    ];

    # SystemD service
    systemd.user.services.herdcat = lib.mkIf cfg.autostart {
      Unit = {
        Description = "herdcat overlay";
        PartOf = ["graphical-session.target"];
        After = ["graphical-session.target"];
      };

      Install = {
        WantedBy = ["graphical-session.target"];
      };

      Service = {
        Type = "exec";
        ExecStart = "${cfg.package}/bin/herdcat --config ${configFile}";
        Restart = "on-failure";
        RestartSec = "5s";
      };
    };
  });
}
