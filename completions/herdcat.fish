# fish completion for herdcat
set -l agents opencode claude codex grok kimi cursor copilot pi qwen agy

function __herdcat_setup
    set -l words (commandline -opc)
    test (count $words) -ge 2; and test "$words[2]" = setup
end

complete -c herdcat -f
complete -c herdcat -n 'not __herdcat_setup; and __fish_use_subcommand' -a setup -d 'Connect agents'
complete -c herdcat -n __herdcat_setup -a "$agents tmux kitty"
complete -c herdcat -n __herdcat_setup -l status -d 'Check integrations'
complete -c herdcat -n __herdcat_setup -l dry-run -d 'Preview changes'
complete -c herdcat -n __herdcat_setup -l remove -d 'Remove integrations'
complete -c herdcat -n __herdcat_setup -l yes -d 'Apply without confirmation'
complete -c herdcat -s h -l help -d 'Show help'
complete -c herdcat -n 'not __herdcat_setup' -s c -l config -r -F -d 'Configuration path'
complete -c herdcat -n 'not __herdcat_setup' -s w -l watch-config -d 'Watch configuration'
complete -c herdcat -n 'not __herdcat_setup' -s m -l monitor -r -d 'Override output'
complete -c herdcat -n 'not __herdcat_setup' -s t -l toggle -d 'Start or stop herdcat'
complete -c herdcat -n 'not __herdcat_setup' -l hide -d 'Hide overlays'
complete -c herdcat -n 'not __herdcat_setup' -l show -d 'Show overlays'
complete -c herdcat -n 'not __herdcat_setup' -l pause -d 'Pause input animation'
complete -c herdcat -n 'not __herdcat_setup' -l resume -d 'Resume animation'
complete -c herdcat -n 'not __herdcat_setup' -l focus -r -d 'Focus session terminal'
complete -c herdcat -n 'not __herdcat_setup' -l pane -r -d 'Report process and split'
complete -c herdcat -n 'not __herdcat_setup' -l tmux -d 'Refresh TMUX sessions'
complete -c herdcat -n 'not __herdcat_setup' -l state -r -a 'idle working waiting done error' -d 'Set manual state'
complete -c herdcat -n 'not __herdcat_setup' -l sessions -d 'List sessions'
complete -c herdcat -n 'not __herdcat_setup' -l reset-position -d 'Reset positions'
complete -c herdcat -n 'not __herdcat_setup' -l event -r -d 'Override hook event'
complete -c herdcat -n 'not __herdcat_setup' -l hook -r -a "$agents" -d 'Read lifecycle event'
complete -c herdcat -n 'not __herdcat_setup' -l reload -d 'Reload configuration'
complete -c herdcat -n 'not __herdcat_setup' -l status -d 'Query running herdcat'
complete -c herdcat -n 'not __herdcat_setup' -l check-config -d 'Validate configuration'
complete -c herdcat -n 'not __herdcat_setup' -l list-devices -d 'List input devices'
complete -c herdcat -n 'not __herdcat_setup' -l list-monitors -d 'List outputs'
complete -c herdcat -n 'not __herdcat_setup' -l doctor -d 'Check configuration and permissions'
complete -c herdcat -n 'not __herdcat_setup' -s v -l version -d 'Show version'
