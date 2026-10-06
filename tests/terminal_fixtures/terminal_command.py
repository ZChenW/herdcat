"""Fixed replies for argv-only terminal jobs; never contacts a compositor."""
import json
import os
from pathlib import Path
import sys
import time


def run():
    tool = Path(sys.argv[0]).name
    mode = os.environ.get('TERMINAL_TEST_MODE', 'ok')
    args = sys.argv[1:]
    with open(os.environ['TERMINAL_TEST_LOG'], 'a') as stream:
        stream.write(json.dumps({'tool': tool, 'args': args, 'pid': os.getpid(),
                                 'socket': os.environ.get('WEZTERM_UNIX_SOCKET')}) + '\n')
    if mode == 'timeout':
        time.sleep(10)
    if mode == 'kitty-error' and tool == 'kitten':
        sys.exit(1)
    if mode == 'error':
        sys.exit(1)
    if mode == 'overflow':
        print('x' * 70000)
        return
    if mode == 'malformed':
        print('[{')
        return
    pid = int(os.environ['TERMINAL_TEST_PID'])
    if tool == 'tmux':
        if args[2] == 'display-message':
            print('test session')
        elif args[2] == 'list-clients' and mode != 'detached':
            print(f'{pid} other /dev/pts/2 999')
            print(f'{pid} test session /dev/pts/3 10')
            print(f'{pid} test session /dev/pts/4 20')
    elif tool == 'wezterm':
        if args[1] == 'list':
            print(json.dumps([{'window_id': 10, 'tab_id': 0, 'pane_id': 0,
                               'title': 'repo "a"'},
                              {'window_id': 10, 'tab_id': 1, 'pane_id': 1,
                               'title': 'other'}]))
        elif args[1] == 'list-clients':
            result = [{'focused_pane_id': 0}]
            if mode == 'ambiguous':
                result.append({'focused_pane_id': 1})
            print(json.dumps(result))
    elif tool == 'niri' and args == ['msg', '-j', 'windows']:
        print(json.dumps([{'id': 111, 'pid': pid, 'title': 'other'},
                          {'id': 222, 'pid': pid, 'title': '[1/2] repo "a"'}]))
