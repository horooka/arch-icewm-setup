set -euo pipefail

if [[ ${1:-} == "2" ]]; then
  export DEEPSEEK_API_KEY="$(pass show api/deepseek2)"
  shift
else
  export DEEPSEEK_API_KEY="$(pass show api/deepseek)"
fi

if [[ ${1:-} == "balance" ]]; then
  printf '%s' "["
  curl -fsS https://api.deepseek.com/user/balance \
    -H "Authorization: Bearer $DEEPSEEK_API_KEY" \
    -H "Accept: application/json" |
    jq -r '.balance_infos[]
      | (.topped_up_balance | tonumber) as $balance
      | ([range(0; [($balance * 2 | round), 40] | min)] | map("#") | join(""))
        + ([range(0; 40 - ([($balance * 2 | round), 40] | min))] | map("-") | join(""))
        + "]" + " (\($balance))"'
  exit
fi
if [[ ! -d .git ]]; then
  echo "Refusing to run outside a Git repository" >&2
  exit 1
fi

exec opencode "$@"
