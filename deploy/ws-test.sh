#!/bin/bash
set -e
pip install -q websockets
python /opt/automacao/deploy/ws-test.py
