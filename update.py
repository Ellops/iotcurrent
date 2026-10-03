#!/usr/bin/env python3
import http.server
import socket
import socketserver
import threading
import time
import requests

# Configurações do servidor de firmware
HTTP_PORT = 8070
FIRMWARE_PATH = "build/iot_current_station.bin"
ESP_HOSTNAME = "http://esp8266_000.local"


def get_local_ip():
    """Descobre o IP da máquina local na rede Wi-Fi/Ethernet."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


def start_file_server():
    """Inicia um servidor HTTP em background para servir o arquivo .bin."""
    handler = http.server.SimpleHTTPRequestHandler
    httpd = socketserver.TCPServer(("", HTTP_PORT), handler)
    print(
        f"[HTTP] Servidor de firmware rodando na porta {HTTP_PORT}..."
    )
    httpd.serve_forever()


def main():
    # 1. Inicia o servidor HTTP em uma thread separada
    server_thread = threading.Thread(target=start_file_server, daemon=True)
    server_thread.start()

    my_ip = get_local_ip()
    firmware_url = f"http://{my_ip}:{HTTP_PORT}/{FIRMWARE_PATH}"
    print(f"[OTA] URL do firmware: {firmware_url}")

    # 2. Envia o comando para o ESP via mDNS (esp8266.local)
    target_endpoint = f"{ESP_HOSTNAME}/update"
    print(f"[OTA] Enviando comando de atualização para {target_endpoint}...")

    try:
        response = requests.post(target_endpoint, data=firmware_url, timeout=5)
        if response.status_code == 200:
            print(f"[SUCESSO] ESP respondeu: {response.text}")
            print("[OTA] Aguardando o ESP fazer o download e reinicializar...")
            time.sleep(15)
        else:
            print(
                f"[ERRO] Falha na requisição. Código: {response.status_code}"
            )
    except requests.exceptions.RequestException as e:
        print(f"[ERRO] Não foi possível conectar ao ESP via mDNS: {e}")


if __name__ == "__main__":
    main()