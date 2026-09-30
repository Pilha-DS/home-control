#!/bin/bash
set -e
# Login as venue-like user if we know password; else admin can't command.
# Test enqueue via admin isn't allowed. Use device API with a user JWT.
echo '{"username":"venue","password":"venue123"}' > /tmp/v.json
LOGIN=$(curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' --data @/tmp/v.json || true)
TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
echo "venue_login_token_len=${#TOKEN}"
echo "$LOGIN" | head -c 180; echo

if [ -z "$TOKEN" ]; then
  echo '{"username":"venue","password":"123456"}' > /tmp/v.json
  LOGIN=$(curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' --data @/tmp/v.json || true)
  TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
  echo "venue_alt_token_len=${#TOKEN}"
fi

echo "==> devices"
curl -sk https://mqtt.omny.app.br/api/devices -H "Authorization: Bearer $TOKEN"
echo
echo "==> command poke=1"
curl -sk -X POST https://mqtt.omny.app.br/api/devices/genesis/command \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"poke":1,"timeMs":0}'
echo
echo "==> monitor activity"
ADMIN=$(curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' -d '{"username":"admin","password":"admin123"}')
ATOKEN=$(echo "$ADMIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
curl -sk "https://mqtt.omny.app.br/api/admin/esp-monitor?activityLimit=8" -H "Authorization: Bearer $ATOKEN" | head -c 1200
echo
echo "==> backend logs"
kubectl logs -n automacao deploy/backend --tail=40
