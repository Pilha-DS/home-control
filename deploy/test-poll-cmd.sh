#!/bin/bash
set -e
MP=$(kubectl get secret automacao-secrets -n automacao -o jsonpath='{.data.MYSQL_PASSWORD}' | base64 -d)
SECRET=$(kubectl exec -n automacao deploy/mysql -- mysql -uhome -p"$MP" home_control -N -e \
  "SELECT esp_secret_token FROM users WHERE username='venue';")
echo "secret_len=${#SECRET}"

echo "==> enqueue poke=1"
LOGIN=$(curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' \
  -d '{"username":"venue","password":"123456"}')
TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
curl -sk -X POST https://mqtt.omny.app.br/api/devices/genesis/command \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"poke":1,"timeMs":0}'
echo

echo "==> poll as ESP (should return cmd)"
curl -sk "https://mqtt.omny.app.br/api/esp/genesis/poll" \
  -H "X-User-Secret: $SECRET"
echo
echo "==> poll again (should be empty)"
curl -sk "https://mqtt.omny.app.br/api/esp/genesis/poll" \
  -H "X-User-Secret: $SECRET"
echo
