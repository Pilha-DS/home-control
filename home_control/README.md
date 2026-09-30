# HOME Control

App Flutter para controlar os dispositivos ESP8266 do projeto **HOME** via MQTT (WSS).

## O que faz

- Conecta ao broker `wss://mosquito.omny.tec.br:443/`
- Publica comandos em `home/cmd`:
  ```json
  {"poke":1,"time":0,"device":"UUID","pin":4}
  ```
- Escuta `home/+/status`, `state`, `ack`, `ip` (descoberta automática)
- UI moderna em **cards sem bordas**, seções separadas por **cor de fundo**

## Como rodar

```bash
cd home_control
flutter pub get
flutter run
```

No Windows, se o `flutter` não estiver no PATH:

```powershell
& "$env:LOCALAPPDATA\flutter\bin\flutter.bat" pub get
& "$env:LOCALAPPDATA\flutter\bin\flutter.bat" run
```

## Uso

1. Abra a aba **Broker** se precisar alterar host/usuário/senha
2. Em **Devices**, adicione o UUID do ESP (ou aguarde a descoberta)
3. Em **Controle**, escolha o pino (D1–D8, D5 reservado) e Ligar/Desligar

## Telas

| Aba | Função |
|-----|--------|
| Controle | Ligar/desligar, pino, pulso em ms |
| Devices | Lista + cadastro de UUIDs |
| Broker | Configuração MQTT WSS |
