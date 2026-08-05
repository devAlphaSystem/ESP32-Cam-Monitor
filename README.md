# ESP32 Camera Watchdog

Firmware para uma ESP32 clássica monitorar uma câmera Wi-Fi ONVIF/RTSP e enviar alertas diretamente pelo Resend.

## O que esta versão faz

- conecta a ESP32 ao Wi-Fi 2,4 GHz;
- tenta localizar a câmera por ONVIF WS-Discovery;
- mantém o último endpoint ONVIF salvo na NVS;
- verifica a porta/serviço ONVIF;
- verifica a porta/serviço RTSP;
- redescobre a câmera caso o IP mude;
- possui varredura opcional da sub-rede como fallback;
- classifica o estado como `online`, `degraded` ou `offline`;
- envia um e-mail quando a falha é confirmada;
- envia outro e-mail quando a câmera volta;
- pode avisar quando o IP da câmera muda;
- oferece uma prévia JPEG ao vivo sem enviar as credenciais da câmera ao navegador;
- pisca o LED integrado enquanto a câmera não está confirmada como online;
- oferece painel web local somente para consulta, organizado como SPA nas abas
  `Câmera` e `Recursos`;
- acompanha heap, firmware, flash, PSRAM, Wi-Fi e características do chip na
  aba `Recursos`.

Esta versão não reinicia a câmera por ONVIF. O `SystemReboot` exige autenticação compatível com o firmware específico da câmera e será uma evolução separada.

## 1. Configuração

Edite `include/secrets.h`:

```cpp
constexpr char WIFI_SSID[] = "SUA_REDE_2.4G";
constexpr char WIFI_PASSWORD[] = "SUA_SENHA";
constexpr char RESEND_API_KEY[] = "re_xxxxxxxxx";
constexpr char RESEND_FROM[] =
    "Camera Watchdog <alertas@notify.seudominio.com>";
constexpr char ALERT_EMAIL[] = "voce@exemplo.com";
```

No Resend, o domínio usado em `RESEND_FROM` precisa estar verificado.

Edite `include/app_config.h` quando necessário:

```cpp
constexpr char CAMERA_NAME[] = "Camera da garagem";
constexpr char CAMERA_MATCH[] = "";
constexpr char CAMERA_IP_HINT[] = "";
constexpr char CAMERA_SNAPSHOT_PROFILE_MATCH[] = "sub";
constexpr uint8_t CAMERA_ALERT_LED_PIN = 2;
constexpr unsigned long CAMERA_ALERT_LED_BLINK_INTERVAL_MS = 1000UL;
#define RELEASE_BLUETOOTH_MEMORY 1
```

`CAMERA_MATCH` pode conter parte do UUID, Scope ou XAddr exibido no monitor serial. Ele é útil quando existe mais de uma câmera ONVIF na rede.

O LED integrado pisca sem usar `delay()` enquanto o estado for `starting`,
`discovering`, `degraded` ou `offline`. Ele é apagado somente no estado
`online`. Depois de uma indisponibilidade confirmada, isso exige duas respostas
consecutivas da câmera, seguindo a mesma regra usada pelo alerta de recuperação.

Com `RELEASE_BLUETOOTH_MEMORY` ativo, o firmware devolve ao heap, antes de
inicializar o Wi-Fi, a memória reservada para Bluetooth Classic e BLE. Essa
liberação vale até o próximo reinício e impede o uso de Bluetooth durante o
mesmo ciclo de execução. Desative a opção se Bluetooth for necessário no
futuro. A quantidade efetivamente recuperada fica disponível no diagnóstico
JSON do painel em `bluetoothMemory`.

Na placa testada com Arduino-ESP32 2.0.17, o core já liberava 36.680 bytes do
controlador antes de `setup()`. A chamada completa liberou 40.692 bytes no
boot, portanto o ganho líquido medido com o watchdog em execução foi de cerca
de 4 KB. Esses valores podem mudar com a versão do core e sua configuração.

## 2. Compilar

No PowerShell, dentro da pasta do projeto:

```powershell
pio run
```

## 3. Gravar na ESP32

```powershell
pio run --target upload --upload-port COM5
```

Troque `COM5` quando o Windows atribuir outra porta.

## 4. Abrir o monitor serial

```powershell
pio device monitor --port COM5 --baud 115200
```

## 5. Abrir o painel

O monitor informa o endereço recebido por DHCP, por exemplo:

```text
Painel: http://192.168.1.15
Painel mDNS: http://camera-watchdog.local
```

O painel é uma SPA somente para consulta. As abas `Câmera` e `Recursos` ficam
disponíveis no cabeçalho e são alternadas sem recarregar a página.

Na aba `Câmera`, o ESP32 apresenta uma prévia ao vivo. Ele consulta os perfis de
mídia e o endereço de snapshot por ONVIF, mantém o endereço completo somente
na RAM e repassa cada JPEG ao navegador. Usuário e senha eventualmente
retornados pela câmera não são incluídos no HTML nem na API de status.

A aba `Recursos` acompanha o heap atual, o menor heap desde o boot, o maior
bloco alocável, uma estimativa de fragmentação, o tamanho do firmware, o espaço
livre na partição da aplicação, a flash física, a PSRAM e o Wi-Fi. Ela também
mostra modelo, revisão, núcleos e frequência do chip. O histórico de heap e
sinal Wi-Fi mantém até três minutos apenas na sessão atual do navegador e não
grava dados no ESP32.

`CAMERA_SNAPSHOT_PROFILE_MATCH` seleciona preferencialmente um perfil cujo
nome contenha o trecho configurado. O perfil secundário costuma reduzir o uso
de rede e a latência. Se não houver correspondência, o primeiro perfil ONVIF é
usado.

A prévia é atualizada quadro a quadro e é pausada quando a aba fica oculta. Ela
não decodifica H.264/H.265, não possui áudio e não substitui um player RTSP,
WebRTC ou um NVR.

## Como a falha é confirmada

A cada 10 segundos o firmware testa ONVIF e RTSP. Por padrão:

- após três falhas, tenta redescobrir a câmera;
- após seis falhas, considera a câmera offline;
- envia somente um alerta de falha;
- exige dois testes bem-sucedidos para declarar recuperação;
- envia somente um alerta de recuperação.

Os valores ficam em `include/app_config.h`.

## Descoberta e varredura

O WS-Discovery usa multicast ONVIF. Alguns repetidores bloqueiam multicast. Nesse caso, a varredura fallback procura uma máquina com:

1. porta RTSP 554 aberta;
2. uma das portas ONVIF configuradas aberta.

A varredura automática é limitada a 254 hosts e não roda continuamente. Ela pode levar dezenas de segundos.

## Resend e TLS

Para facilitar o protótipo, o código usa `WiFiClientSecure::setInsecure()`. O tráfego continua criptografado, mas o certificado do servidor não é validado. Não use essa configuração em um produto comercial.

## Observação sobre o firmware MicroDNS anterior

Este firmware não executa servidor DNS. Antes de gravá-lo, restaure o DNS do Windows caso o computador ainda esteja usando a ESP32 como DNS:

```powershell
Set-DnsClientServerAddress `
    -InterfaceAlias "Wi-Fi" `
    -ResetServerAddresses

Clear-DnsClientCache
```

## Diagnóstico inicial

Uma câmera saudável pode responder assim:

```text
CAMERA OK: 192.168.1.50 | ONVIF tcp=1 http=1 code=401 | RTSP tcp=1 protocolo=1 code=200
```

HTTP `401` no teste ONVIF não significa falha: significa que o serviço está acessível e exigiu autenticação.

## Limitações do MVP

- IPv4 apenas;
- ONVIF HTTPS local não é monitorado;
- não autentica no ONVIF;
- não decodifica nem valida o fluxo RTSP completo;
- a prévia depende de a câmera permitir `GetProfiles` e `GetSnapshotUri` por
  ONVIF com a autenticação atualmente suportada;
- a prévia JPEG não possui a fluidez de um stream WebRTC/HLS;
- não reinicia a câmera;
- a varredura TCP pode selecionar outro servidor RTSP se houver mais de um na rede;
- o painel não possui autenticação; qualquer pessoa na rede local que acesse o
  ESP32 também pode visualizar a prévia;
- a chave do Resend fica no firmware conforme a escolha deste protótipo pessoal.
