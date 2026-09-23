set -euo pipefail

export DEEPSEEK_API_KEY="$(pass show api/deepseek)"
cd "${1:-.}"
if [[ ! -d .git && "$1" != "-f" ]]; then
  echo "Refusing to run outside a Git repository"
  exit 1
fi

exec opencode "$@"
