#!/usr/bin/env python3
import asyncio
import ssl

import websockets


async def test_direct():
    try:
        async with websockets.connect(
            "ws://mosquitto.automacao.svc.cluster.local:9001/",
            subprotocols=["mqtt"],
        ) as ws:
            print("direct: ok")
    except Exception as e:
        print("direct:", e)


async def test_traefik_local():
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    try:
        async with websockets.connect(
            "wss://mqtt.omny.app.br/",
            subprotocols=["mqtt"],
            ssl=ctx,
            server_hostname="mqtt.omny.app.br",
        ) as ws:
            print("traefik-local: ok")
    except Exception as e:
        print("traefik-local:", e)


async def main():
    await test_direct()
    await test_traefik_local()


asyncio.run(main())
