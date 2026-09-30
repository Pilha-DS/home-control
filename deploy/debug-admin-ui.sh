#!/bin/bash
set -e
echo "==> images"
kubectl get deploy web backend -n automacao -o wide
echo "==> login via mqtt.omny.app.br (backend)"
echo '{"username":"admin","password":"admin123"}' > /tmp/login.json
LOGIN=$(curl -sk --resolve mqtt.omny.app.br:443:127.0.0.1 -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' --data @/tmp/login.json)
echo "$LOGIN" | head -c 200
echo
TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
echo "token_len=${#TOKEN}"
echo "==> GET /api/users via mqtt ingress"
curl -sk --resolve mqtt.omny.app.br:443:127.0.0.1 https://mqtt.omny.app.br/api/users -H "Authorization: Bearer $TOKEN"
echo
echo "==> GET /api/users via CF (mqtt.omny.app.br)"
curl -sk https://mqtt.omny.app.br/api/users -H "Authorization: Bearer $TOKEN"
echo
echo "==> UI automacao"
curl -sk -o /dev/null -w "automacao_http=%{http_code}\n" https://automacao.omny.app.br/
echo "==> backend logs"
kubectl logs -n automacao deploy/backend --tail=30
