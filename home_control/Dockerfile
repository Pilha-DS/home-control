# Build Flutter Web (API em mqtt.omny.app.br; UI em automacao.omny.app.br)
FROM ghcr.io/cirruslabs/flutter:stable AS build
WORKDIR /app
COPY pubspec.yaml pubspec.lock* ./
RUN flutter pub get
COPY . .
RUN flutter build web --release --base-href / --pwa-strategy=none \
    --dart-define=API_BASE_URL=https://mqtt.omny.app.br \
    --dart-define=BUILD_ID=$(date -u +%Y%m%d%H%M%S)

# Serve estático (sem proxy — backend é mqtt.omny.app.br)
FROM nginx:1.27-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/build/web /usr/share/nginx/html
EXPOSE 80
