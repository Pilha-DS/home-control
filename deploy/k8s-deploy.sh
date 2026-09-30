#!/usr/bin/env bash
# Deploy stack automacao no K3s (namespace automacao).
#
# Modo local (builda as imagens na propria maquina e importa no K3s):
#   ADMIN_PASSWORD='senha-forte' ./deploy/k8s-deploy.sh [TAG]
#
# Modo CI (imagens ja publicadas num registry; nao builda):
#   REGISTRY=ghcr.io/owner SKIP_BUILD=1 TAG="$GIT_SHA" ./deploy/k8s-deploy.sh
#
# Variaveis:
#   TAG         tag das imagens (default: timestamp)
#   REGISTRY    prefixo do registry (vazio = imagens locais importadas no K3s)
#   SKIP_BUILD  1 = pula build/push (usa imagens ja publicadas)
#   ADMIN_PASSWORD  obrigatorio na PRIMEIRA subida (cria o secret automacao-secrets)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NS=automacao
TAG="${TAG:-${1:-$(date +%Y%m%d%H%M%S)}}"
REGISTRY="${REGISTRY:-}"
SKIP_BUILD="${SKIP_BUILD:-0}"

# Remove barra final do REGISTRY, se houver.
REGISTRY="${REGISTRY%/}"
BACKEND_IMAGE="${REGISTRY:+${REGISTRY}/}automacao-backend:${TAG}"
WEB_IMAGE="${REGISTRY:+${REGISTRY}/}automacao-web:${TAG}"

echo "==> Deploy automacao tag=${TAG} registry='${REGISTRY:-local}'"

# ---------------------------------------------------------------------------
# Segredos
# ---------------------------------------------------------------------------
if ! kubectl get secret automacao-secrets -n "$NS" >/dev/null 2>&1; then
  echo "==> Criando secret automacao-secrets"
  : "${ADMIN_PASSWORD:?Defina ADMIN_PASSWORD (o padrao 'admin123' nao e mais aceito) antes do primeiro deploy}"
  JWT_SECRET="$(openssl rand -hex 32)"
  MYSQL_ROOT_PASSWORD="$(openssl rand -hex 16)"
  MYSQL_PASSWORD="$(openssl rand -hex 16)"

  kubectl create namespace "$NS" --dry-run=client -o yaml | kubectl apply -f -
  kubectl create secret generic automacao-secrets -n "$NS" \
    --from-literal=JWT_SECRET="$JWT_SECRET" \
    --from-literal=MYSQL_ROOT_PASSWORD="$MYSQL_ROOT_PASSWORD" \
    --from-literal=MYSQL_PASSWORD="$MYSQL_PASSWORD" \
    --from-literal=ADMIN_PASSWORD="$ADMIN_PASSWORD"

  echo "    JWT/MYSQL gerados automaticamente."
  echo "    ADMIN_PASSWORD salvo no secret — guarde-o em local seguro."
else
  # APP_STRICT_SECRETS=true no manifest: subir com 'admin123' causa CrashLoopBackOff.
  CURRENT_ADMIN="$(kubectl get secret automacao-secrets -n "$NS" \
    -o jsonpath='{.data.ADMIN_PASSWORD}' 2>/dev/null | base64 -d 2>/dev/null || true)"

  if [ -n "${ADMIN_PASSWORD:-}" ]; then
    kubectl patch secret automacao-secrets -n "$NS" \
      -p "{\"stringData\":{\"ADMIN_PASSWORD\":\"${ADMIN_PASSWORD}\"}}"
    echo "==> ADMIN_PASSWORD do secret atualizado"
  elif [ "$CURRENT_ADMIN" = "admin123" ]; then
    echo "ERRO: o secret ainda usa ADMIN_PASSWORD='admin123' e o backend roda com" >&2
    echo "      APP_STRICT_SECRETS=true — ele entraria em CrashLoopBackOff." >&2
    echo "      Rode novamente informando uma senha forte:" >&2
    echo "        ADMIN_PASSWORD='...' $0" >&2
    exit 1
  fi
fi

# ---------------------------------------------------------------------------
# Build / publicacao das imagens
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" = "1" ]; then
  echo "==> SKIP_BUILD=1: usando imagens ja publicadas (${BACKEND_IMAGE})"
else
  echo "==> Build imagens Docker"
  docker build -t "$BACKEND_IMAGE" "${ROOT}/backend"
  docker build -t "$WEB_IMAGE" "${ROOT}/home_control"

  if [ -n "$REGISTRY" ]; then
    echo "==> Push imagens para ${REGISTRY}"
    docker push "$BACKEND_IMAGE"
    docker push "$WEB_IMAGE"
  else
    echo "==> Import imagens no K3s"
    docker save "$BACKEND_IMAGE" | k3s ctr images import -
    docker save "$WEB_IMAGE" | k3s ctr images import -
  fi
fi

# ---------------------------------------------------------------------------
# Manifests
# ---------------------------------------------------------------------------
echo "==> Apply manifests"
kubectl apply -f "${ROOT}/k8s/namespace.yaml"
kubectl apply -f "${ROOT}/k8s/mysql.yaml"
kubectl apply -f "${ROOT}/k8s/mosquitto.yaml"
kubectl apply -f "${ROOT}/k8s/mqtt-certificate.yaml" 2>/dev/null || true

# Remove exposicao publica antiga do Mosquitto em mqtt.omny.app.br
kubectl delete ingressroute mqtt-ingressroute -n "$NS" --ignore-not-found
kubectl delete serverstransport mqtt-ws-transport -n "$NS" --ignore-not-found

sed "s|IMAGE_BACKEND|${BACKEND_IMAGE}|g" "${ROOT}/k8s/backend.yaml" | kubectl apply -f -
sed "s|IMAGE_WEB|${WEB_IMAGE}|g" "${ROOT}/k8s/web.yaml" | kubectl apply -f -

echo "==> Aguardando pods"
kubectl rollout status deployment/mysql -n "$NS" --timeout=300s || true
kubectl rollout status deployment/mosquitto -n "$NS" --timeout=120s
kubectl rollout status deployment/backend -n "$NS" --timeout=300s
kubectl rollout status deployment/web -n "$NS" --timeout=120s

echo ""
echo "Deploy concluido."
echo "  UI:      https://automacao.omny.app.br"
echo "  Backend: https://mqtt.omny.app.br  (/api /ws /health /api/esp)"
echo "  MQTT:    interno apenas (mosquitto:1883)"
echo ""
kubectl get ingress,pods,svc -n "$NS"
