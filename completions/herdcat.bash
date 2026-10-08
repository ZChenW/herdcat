# bash completion for herdcat
_herdcat() {
  local agents='opencode claude codex grok kimi cursor copilot pi qwen agy'
  local current=${COMP_WORDS[COMP_CWORD]} previous=${COMP_WORDS[COMP_CWORD-1]}
  local options='-c --config -w --watch-config -m --monitor -t --toggle --hide --show --pause --resume --focus --pane --tmux --state --sessions --reset-position --event --hook --reload --status --check-config --list-devices --list-monitors --doctor -h --help -v --version'
  COMPREPLY=()
  if [[ ${COMP_WORDS[1]} == setup ]]; then
    COMPREPLY=( $(compgen -W "$agents tmux kitty --status --dry-run --remove --yes -h --help" -- "$current") )
    return
  fi
  case $previous in
    --hook) COMPREPLY=( $(compgen -W "$agents" -- "$current") ); return ;;
    --state) COMPREPLY=( $(compgen -W 'idle working waiting done error' -- "$current") ); return ;;
    -c|--config)
      COMPREPLY=( $(compgen -f -- "$current") )
      compopt -o filenames
      return ;;
    -m|--monitor|--event|--focus|--pane) return ;;
  esac
  # --pane takes two values; do not offer options for the split identifier.
  if (( COMP_CWORD > 2 )) && [[ ${COMP_WORDS[COMP_CWORD-2]} == --pane ]]; then
    return
  fi
  COMPREPLY=( $(compgen -W "setup $options" -- "$current") )
}
complete -F _herdcat herdcat
