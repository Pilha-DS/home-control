#!/bin/bash
set -e
echo '{"username":"admin","password":"admin123"}' > /tmp/login.json
LOGIN=$(curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' --data @/tmp/login.json)
TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
echo "token_len=${#TOKEN}"
curl -sk "https://mqtt.omny.app.br/api/admin/esp-monitor" -H "Authorization: Bearer $TOKEN" | head -c 600
echo
