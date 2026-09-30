#!/usr/bin/env bash
set -e
ADMIN_PASS="$(kubectl get secret automacao-secrets -n automacao -o jsonpath='{.data.ADMIN_PASSWORD}' | base64 -d)"
echo "admin_pass_len=${#ADMIN_PASS}"
echo "---LOGIN---"
LOGIN="$(curl -s -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' -d "{\"username\":\"admin\",\"password\":\"$ADMIN_PASS\"}")"
echo "$LOGIN" | head -c 300
echo ""
TOKEN="$(echo "$LOGIN" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("token",""))')"
echo "token_len=${#TOKEN}"
echo "---MONITOR---"
curl -s "https://mqtt.omny.app.br/api/admin/esp-monitor?activityLimit=60" -H "Authorization: Bearer $TOKEN"
echo ""
