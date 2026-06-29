# TODO: Replace with the actual path to your ameba-rtos SDK env.sh before building
# Example (Linux/macOS):
#   source /path/to/ameba-rtos/env.sh
source /path/to/ameba-rtos/env.sh

# Aliases (consistent with Windows env.ps1)
alias build.py='python build.py'
alias menuconfig.py='python menuconfig.py'
alias flash.py='python flash.py'
alias monitor.py='python monitor.py'

# bb dispatcher
bb() {
    ameba.py build
}
alias bm='ameba.py menuconfig'
alias bms='ameba.py menuconfig -s prj.conf'
alias bp='ameba.py build -p'

# ================= Terminal Title =================
# Get directory name using POSIX-compatible method (no realpath dependency)
_PRJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_PRJ_NAME="${_PRJ_DIR##*/}"

# Set title immediately
echo -ne "\033]0;${_PRJ_NAME}\007"

# Key: PS1 by default contains title escape (e.g. \[\e]0;\u@\h: \w\a\]),
# and PS1 recomputation overwrites the title after every command.
# Must modify PS1 itself; PROMPT_COMMAND is ineffective after this point.
if [ -n "$PS1" ]; then
    # Strip existing OSC title escape sequences from PS1 (matches \[\e]0;...\a\] pattern)
    _CLEAN_PS1="$(echo "$PS1" | sed -E 's|\\\[\\e\]0;.*\\a\\\]||g' 2>/dev/null)"
    _CLEAN_PS1="${_CLEAN_PS1:-$PS1}"
    # Re-add project name title
    export PS1="\[\e]0;${_PRJ_NAME}\a\]${_CLEAN_PS1}"
fi
unset _PRJ_DIR _PRJ_NAME _CLEAN_PS1