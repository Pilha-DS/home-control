#!/bin/bash
set -e
NS=automacao
BACKEND_IP=$(kubectl get svc backend -n "$NS" -o jsonpath='{.spec.clusterIP}')

echo "==> login backend interno ($BACKEND_IP)"
curl -s "http://${BACKEND_IP}:8080/api/auth/login" \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}'
echo

echo "==> health via ingress mqtt.omny.app.br"
curl -sk --resolve mqtt.omny.app.br:443:127.0.0.1 https://mqtt.omny.app.br/health
echo

echo "==> UI via ingress automacao.omny.app.br"
curl -sk --resolve automacao.omny.app.br:443:127.0.0.1 -o /dev/null -w "%{http_code}\n" https://automacao.omny.app.br/

echo "==> DNS"
dig +short automacao.omny.app.br mqtt.omny.app.br
