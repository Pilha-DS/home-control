#!/bin/bash
set -e
MP=$(kubectl get secret automacao-secrets -n automacao -o jsonpath='{.data.MYSQL_PASSWORD}' | base64 -d)
echo "==> users secrets"
kubectl exec -n automacao deploy/mysql -- mysql -uhome -p"$MP" home_control -e \
  "SELECT username, role, LENGTH(esp_secret_token) AS len, LEFT(esp_secret_token,4) AS pfx, RIGHT(esp_secret_token,4) AS sfx FROM users;"
echo "==> devices"
kubectl exec -n automacao deploy/mysql -- mysql -uhome -p"$MP" home_control -e \
  "SELECT uuid, user_id, name, active, LENGTH(device_token) AS tok_len FROM devices;"
echo "==> recent activity"
curl -sk -X POST https://mqtt.omny.app.br/api/auth/login -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}' > /tmp/a.json
TOKEN=$(sed -n 's/.*"token":"\([^"]*\)".*/\1/p' /tmp/a.json)
curl -sk "https://mqtt.omny.app.br/api/admin/esp-monitor?activityLimit=5" -H "Authorization: Bearer $TOKEN" | head -c 800
echo
