# Docker — subida rápida

Stack: **MySQL** + **backend Spring Boot** + **app Flutter (web/nginx)**.

O nginx do container `web` faz proxy de `/api` e `/ws` para o backend — o app usa a **mesma origem** (sem CORS).

## Pré-requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (Compose v2+)

## Subir tudo

```bash
cp .env.docker.example .env
docker compose up -d --build
```

Aguarde ~1–2 min na primeira vez (build do backend + Flutter web).

> Desenvolvimento: em vez de `-d --build` a cada mudança, use
> `docker compose watch` para rebuildar backend/web automaticamente.

| Serviço | URL |
|---|---|
| **App (Flutter web)** | http://localhost:8080 |
| **API direta (debug)** | http://localhost:8081 |
| **Health backend** | http://localhost:8081/health |

Login padrão (`.env`): usuário `admin`, senha `admin123` (altere no `.env`).

## Comandos úteis

```bash
docker compose ps
docker compose logs -f backend
docker compose logs -f web
docker compose down          # para containers
docker compose down -v       # para + apaga volume MySQL
```

## Variáveis (.env)

Veja `.env.docker.example`. Principais:

- `APP_PORT` — porta do app no host (padrão `8080`)
- `MYSQL_*` — credenciais do banco
- `JWT_SECRET`, `ADMIN_*` — segurança
- `MQTT_*` — broker MQTT do backend (padrão: broker remoto `mosquito.omny.tec.br`)

## MQTT

- O **backend** conecta ao broker por **TCP** (porta `1883` por padrão no `.env`).
- Os **ESP8266** (`ALPHA.ino`) usam **WSS na porta 443** — continuam apontando para o broker remoto, salvo se você reconfigurar o firmware.

### Broker local (opcional, só dev)

```bash
docker compose --profile local-mqtt up -d
```

No `.env`, aponte o backend para o Mosquitto local:

```env
MQTT_HOST=mosquitto
MQTT_PORT=1883
MQTT_SSL=false
```

Reinicie o backend: `docker compose up -d backend`.

## Desenvolvimento sem Docker

- Backend: JDK 17 + Maven → `cd backend && mvn spring-boot:run`
- App: Flutter → `cd home_control && flutter run -d chrome`
  - URL do backend: `http://localhost:8080` nas configurações do app

## Observabilidade

| Rota | Descrição |
|---|---|
| `/health` | status **agregado real** (503 se o banco cair) — usado pelo healthcheck do Docker |
| `/actuator/health` | health do Actuator; detalhes completos exigem JWT. Probes em `/actuator/health/liveness` e `/readiness` |
| `/actuator/metrics` | métricas da JVM/HTTP (requer JWT) |
| `/actuator/prometheus` | métricas em formato Prometheus (requer JWT) |

Toda requisição recebe/devolve o header `X-Request-Id` e o id aparece nos logs
(veja o `RequestIdFilter`), facilitando rastrear uma chamada ponta a ponta.

## Produção (checklist)

- Defina `JWT_SECRET` forte (≥32 chars) e `ADMIN_PASSWORD` ≠ `admin123`.
- Rotacione as credenciais MQTT (as do repositório estão expostas).
- `SPRINGDOC_ENABLED=false` desliga Swagger/OpenAPI.
- `CORS_ALLOWED_ORIGINS=https://seu-dominio` restringe o CORS (padrão `*` é só para dev).
- `APP_STRICT_SECRETS=true` (ou `SPRING_PROFILES_ACTIVE=prod`) faz o backend
  **falhar na subida** se os segredos padrão ainda estiverem em uso.
- `POST /api/auth/login` tem rate limit (padrão 10 tentativas/min por IP+usuário);
  ajuste com `LOGIN_MAX_ATTEMPTS` / `LOGIN_WINDOW_SECONDS`.

## Estrutura

```
docker-compose.yml          # orquestração
.env.docker.example         # template de variáveis
backend/Dockerfile          # Java 17 + Maven (multi-stage)
home_control/Dockerfile     # Flutter web + nginx
home_control/nginx.conf     # proxy /api e /ws
docker/mosquitto/           # config opcional (profile local-mqtt)
```
