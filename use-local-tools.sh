# Source from this directory:  source ./use-local-tools.sh
# Prepends ./bin so programs built from src/ (wlp4scan, wlp4parse, wlp4type, wlp4gen) are found first.
_demo_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export PATH="$_demo_root/bin:$PATH"
unset _demo_root
