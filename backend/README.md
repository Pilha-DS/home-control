# Home Backend

Intermediário entre os dispositivos ESP8266 (`ALPHA.ino`) e o
app Flutter (`home_control`). Faz o controle de acesso por **login + JWT**.
No modo padrão (**Java gateway**), o ESP fala HTTP direto com o backend —
sem broker MQTT.

## Arquitetura

```
Flutter App ──(REST + WebSocket, JWT)──▶ Spring Boot ──(JPA)──▶ MySQL
ESP8266     ──(HTTP /api/esp/{uuid}, token)──▶ Spring Boot
```

Modo padrão (`APP_TRANSPORT=java`): **tudo passa pelo Java**. O ESP envia
telemetria em `POST /api/esp/{uuid}/events` e busca comandos em
`GET /api/esp/{uuid}/poll`. Não precisa de Mosquitto.

Modo legado (`APP_TRANSPORT=mqtt`): ESP via broker MQTT — só se reativar
explicitamente no `.env` e no firmware (`USE_JAVA_GATEWAY=0`).

## Requisitos

- Java 17+ (o Spring Boot 3 exige 17; o Java 8 instalado na máquina **não** serve).
- Maven 3.8+ (ou use o wrapper `mvnw` — gere com `mvn wrapper:wrapper`).
- MySQL 8+ (ou Postgres/H2, trocando apenas as variáveis de banco).

## Docker (recomendado)

Subida completa com MySQL + backend + app web:

```bash
cp .env.docker.example .env
docker compose up -d --build
```

App em http://localhost:8080 — detalhes em [DOCKER.md](../DOCKER.md) na raiz do projeto.

## Como rodar (manual)

1. Crie o banco:

```sql
CREATE DATABASE home_control CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

2. Configure as variáveis de ambiente (veja `.env.example`). No mínimo:

```bash
DB_URL=jdbc:mysql://localhost:3306/home_control?useSSL=false&serverTimezone=UTC&allowPublicKeyRetrieval=true
DB_USERNAME=root
DB_PASSWORD=root

JWT_SECRET=um-segredo-longo-e-aleatorio-de-pelo-menos-32-caracteres
ADMIN_USERNAME=admin
ADMIN_PASSWORD=troque-esta-senha

MQTT_HOST=mosquito.omny.tec.br
MQTT_PORT=443
MQTT_USER=cardoso
MQTT_PASS=C6m8n4d2d3
MQTT_PATH=/
MQTT_SSL=true
```

3. Compile e rode:

```bash
./mvnw spring-boot:run
```

> Use o wrapper `mvnw` (incluso) — não precisa ter Maven instalado, só o JDK 17.
> No Windows: `mvnw.cmd spring-boot:run`.

O Flyway cria as tabelas automaticamente na primeira subida. O admin inicial é
semeado a partir de `ADMIN_USERNAME`/`ADMIN_PASSWORD`.

## Variáveis de ambiente (todas externalizáveis)

| Variável | Padrão | Descrição |
|---|---|---|
| `SERVER_PORT` | `8080` | Porta HTTP |
| `DB_URL` | `jdbc:mysql://localhost:3306/home_control...` | URL do banco |
| `DB_USERNAME` | `root` | Usuário do banco |
| `DB_PASSWORD` | `root` | Senha do banco |
| `DB_DRIVER` | `com.mysql.cj.jdbc.Driver` | Driver JDBC (troque p/ Postgres/H2) |
| `JWT_SECRET` | (placeholder) | Segredo do JWT (obrigatório em produção) |
| `JWT_EXPIRATION_MS` | `86400000` | Validade do token (24h) |
| `ADMIN_USERNAME` | `admin` | Usuário admin semeado |
| `ADMIN_PASSWORD` | `admin123` | Senha do admin semeado |
| `ADMIN_SEED_ON_STARTUP` | `true` | Cria o admin se não existir |
| `MQTT_HOST`/`MQTT_PORT` | broker padrão | Endereço do broker |
| `MQTT_USER`/`MQTT_PASS` | conta de serviço | Credencial MQTT do backend |
| `MQTT_SSL` | `true` | Usa `ssl://` (senão `tcp://`) |
| `MQTT_URL` | (vazio) | Sobrescreve a URI completa (ex.: `tcp://host:1883`) |
| `MQTT_SUBSCRIBE_TOPICS` | (lista) | Tópicos de telemetria assinados |

> Para **Postgres**: `DB_URL=jdbc:postgresql://localhost:5432/home_control` e
> `DB_DRIVER=org.postgresql.Driver`. Para **H2 em arquivo**:
> `DB_URL=jdbc:h2:file:./data/home_control;MODE=MySQL` e `DB_DRIVER=org.h2.Driver`.
> Basta trocar as variáveis — o Flyway e o JPA se adaptam ao driver.

## Endpoints

Todos (exceto login e WebSocket) exigem `Authorization: Bearer <JWT>`.

| Método | Rota | Descrição |
|---|---|---|
| POST | `/api/auth/login` | Login → `{ token, username, role }` |
| POST | `/api/users` | Cadastrar usuário (só admin) |
| GET | `/api/devices` | Lista dispositivos do usuário |
| GET | `/api/devices/{uuid}` | Detalhe + estado em tempo real |
| POST | `/api/devices` | Provisiona dispositivo (gera token MQTT + token de app) |
| PUT | `/api/devices/{uuid}` | Atualiza nome/modelo |
| DELETE | `/api/devices/{uuid}` | Remove |
| POST | `/api/devices/{uuid}/command` | `{ poke, pin?, timeMs? }` → publica em `home/{user}/{uuid}/cmd` |
| POST | `/api/devices/{uuid}/config` | `{ adminPass, ... }` → publica em `home/{user}/{uuid}/config` |
| WS | `/ws/events?token=<JWT>` | Push de telemetria (envelope `{type, uuid, payload}`) |

O `username` do login é o `user_id` dos tópicos MQTT. O backend valida que o
usuário só acessa os próprios dispositivos (`device.user_id == JWT.sub`).

### Mapeamento MQTT → dispositivos

O ESP publica em `home/<user_id>/<uuid>/status|state|ip|ack|...` (e logs em
`home/logs/<user_id>/<uuid>/...`). Na primeira telemetria válida, o backend
**registra automaticamente** o UUID na conta do `user_id` do tópico (se o usuário
existir). Mensagens com `user_id` divergente do dono cadastrado são ignoradas.

O app lista dispositivos via `GET /api/devices` (JWT) e recebe atualizações em
tempo real via WebSocket, incluindo o evento `device/discovered` quando um ESP
novo aparece no broker.

## Provisionamento de um dispositivo

1. **Automático:** ligue o ESP com `USER_ID` igual ao `username` do login;
   na primeira publicação MQTT ele entra na lista do usuário.
2. **Manual:** no app (logado), adicione o dispositivo pelo `uuid`.
3. O backend gera:
   - `mqtt_username` / `mqtt_password` — credencial MQTT exclusiva do dispositivo;
   - `device_token` — token de aplicação validado nos comandos/config.
3. Configure o ESP com essas credenciais (via portal de setup ou pelo comando
   `config` remoto, usando `MQTT_USER`/`MQTT_PASS`). O `device_token` pode ser
   enviado uma vez no campo `DEVICE_TOKEN` (ou `TOKEN`) do `config` para ativar
   a validação no firmware.

## ACL do Mosquitto (recomendado)

Restrinja por ACL para que cada dispositivo só acesse os próprios tópicos e a
conta de serviço acesse tudo:

```text
# Conta de serviço do backend
user backend
topic readwrite home/#
topic readwrite home/logs/#

# Cada dispositivo (token próprio)
user dev_<uuid>
topic readwrite home/<user_id>/<uuid>/#
```

O app **não** tem credencial de broker — ele passa pelo backend.

## Testes

```bash
./mvnw test
```

## Observabilidade

- `/health` — status agregado real (inclui o banco); responde 503 se algo estiver fora.
- `/actuator/health` — Actuator (detalhes com JWT); probes em `/liveness` e `/readiness`.
- `/actuator/metrics` e `/actuator/prometheus` — métricas (exigem JWT).
- Header `X-Request-Id` por requisição, ecoado na resposta e presente no log.

## Produção

- `SPRINGDOC_ENABLED=false` desliga o Swagger.
- `CORS_ALLOWED_ORIGINS=https://seu-dominio` restringe o CORS.
- `APP_STRICT_SECRETS=true` (ou `SPRING_PROFILES_ACTIVE=prod`) falha a subida se
  `JWT_SECRET`/`ADMIN_PASSWORD` ainda forem os padrões.
- Login com rate limit (`LOGIN_MAX_ATTEMPTS`, `LOGIN_WINDOW_SECONDS`).
