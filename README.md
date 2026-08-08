# ESP32 Camera Watchdog 0.7.0

Firmware para ESP32 que monitora uma câmera IP ONVIF/RTSP, oferece painel web local, valida snapshots JPEG, retransmite vídeo H.264 para o navegador e tenta recuperar automaticamente a câmera com `ONVIF SystemReboot`.

A versão `0.7.0` reformula o monitoramento e a recuperação. Ela separa falha de snapshot de indisponibilidade total de vídeo, evita sobrecarregar câmeras instáveis e substitui o limitador antigo de três reboots por dia por uma política compatível com recuperação contínua.

## Recursos

- descoberta ONVIF por WS-Discovery e varredura opcional da sub-rede;
- persistência da câmera e das configurações na NVS;
- sondas ONVIF, RTSP e JPEG com telemetria de latência;
- seleção automática de perfil JPEG e failover entre perfis;
- relay HTTP/RTP de vídeo H.264 para reprodução MSE/fMP4 no navegador;
- reconhecimento de pacotes RTP reais como evidência de vídeo ativo;
- recuperação automática por `ONVIF SystemReboot`;
- cooldown e limite de comandos de reboot aceitos;
- período de silêncio após reboot para a câmera inicializar sem novas sondas;
- painel responsivo com Câmera, Prévia, Ao Vivo, Recursos, Configurações e Ações;
- alertas e notificações de recuperação por Resend;
- LED local de estado no GPIO 2;
- diagnóstico JSON com heap, flash, Wi-Fi, câmera, relay, incidentes, contadores e motivo do último reset do ESP32.

## O que mudou na versão 0.7.0

- ciclo de saúde reduzido de 30 para 15 segundos;
- perfil de snapshot padrão alterado de `sub` para `snap`;
- perfis JPEG passam a ter prioridade real;
- o perfil H.264 de menor carga é escolhido separadamente para o vídeo ao vivo;
- falhas HTTP 500 acionam failover de perfil antes do reboot;
- espera de backoff não é mais contabilizada como falha JPEG;
- timeouts de `GetProfiles` e `GetSnapshotUri`, inclusive corpo HTTP 200 interrompido, passam a ser recuperáveis após repetição;
- a prévia do navegador não altera mais os contadores internos do monitor;
- sessão RTP ativa evita sondas e reboots paralelos;
- relay, monitor, prévia e ações usam exclusão mútua de I/O da câmera;
- somente comandos `SystemReboot` aceitos consomem a cota da janela;
- a graça pós-reboot não executa sondas nem incrementa falhas;
- falha de Wi-Fi durante o boot não impede a inicialização posterior do painel;
- migração dirigida atualiza os antigos valores padrão salvos na NVS;
- respostas ONVIF e Resend são lidas com limite de memória;
- o painel não injeta mais o corpo remoto do Resend via `innerHTML`;
- o diagnóstico informa `resources.chip.resetReason` e `resetReasonCode`;
- versão do firmware e cache do player atualizados para `0.7.0`.

## Requisitos

### ESP32

- placa compatível com o ambiente PlatformIO `esp32dev`;
- flash de 4 MB com a tabela de partições incluída;
- conexão Wi-Fi de 2,4 GHz;
- PSRAM não é obrigatória.

### Câmera

- câmera acessível na mesma rede local do ESP32;
- serviço ONVIF para descoberta, perfis e snapshot;
- RTSP H.264 para o player integrado;
- `SystemReboot` ONVIF se a recuperação automática for utilizada.

O relay integrado aceita H.264. Perfis H.265/HEVC não são reproduzidos por este player.

## Configuração inicial

Copie o arquivo de exemplo:

```powershell
Copy-Item include\secrets.example.h include\secrets.h
```

Edite somente `include/secrets.h` e preencha os valores necessários:

```cpp
constexpr char WIFI_SSID[] = "NOME_DA_REDE";
constexpr char WIFI_PASSWORD[] = "SENHA_DA_REDE";

#define CAMERA_ONVIF_USERNAME_VALUE "usuario"
#define CAMERA_ONVIF_PASSWORD_VALUE "senha"

#define CAMERA_RTSP_USERNAME_VALUE CAMERA_ONVIF_USERNAME_VALUE
#define CAMERA_RTSP_PASSWORD_VALUE CAMERA_ONVIF_PASSWORD_VALUE

#define DASHBOARD_ADMIN_PASSWORD_VALUE "senha-administrativa"

constexpr char RESEND_API_KEY[] = "";
constexpr char RESEND_FROM[] = "Camera Watchdog <watchdog@example.com>";
constexpr char ALERT_EMAIL[] = "recipient@example.com";
```

Não versione nem compartilhe `include/secrets.h`. O arquivo já está ignorado pelo Git.

As credenciais ONVIF podem ficar vazias quando a câmera não exige autenticação. As credenciais RTSP reutilizam as ONVIF por padrão, mas podem ser configuradas separadamente.

O Resend é opcional. Sem uma configuração válida, o monitor continua funcionando e apenas o envio de e-mails fica desativado.

## Compilar, gravar e monitorar

No terminal do projeto:

```powershell
pio run
pio run --target upload
pio device monitor
```

Também podem ser utilizadas as tarefas Build, Upload e Monitor da extensão PlatformIO.

Configuração atual do ambiente:

```ini
[env:esp32dev]
platform = platformio/espressif32@7.0.1
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 460800
```

## Acesso ao painel

Depois da conexão Wi-Fi, abra:

```text
http://camera-watchdog.local
```

Se o mDNS não estiver disponível, use o endereço IP exibido no monitor serial.

Portas utilizadas:

| Porta | Uso |
|---:|---|
| 80 | painel e API HTTP |
| 81 | relay HTTP/RTP para o player ao vivo |
| 3702/UDP | WS-Discovery ONVIF |
| 554/TCP | RTSP da câmera por padrão |

O endpoint consumido pelo player é semelhante a:

```text
http://IP_DO_ESP32:81/stream?profile=1
```

## Como o monitor decide o estado

A cada ciclo, o firmware executa esta sequência:

1. verifica Wi-Fi e o período de graça pós-reboot;
2. consulta o relay;
3. considera sucesso quando uma sessão ativa entregou RTP recentemente;
4. impede I/O concorrente entre relay, health-check, prévia e ações manuais;
5. verifica os protocolos ONVIF e RTSP;
6. resolve o perfil e o URI de snapshot quando necessário;
7. baixa e valida um JPEG completo;
8. atualiza contadores, incidente, alerta e recuperação.

Os incidentes são separados:

- `snapshot_unavailable`: a câmera responde, mas o monitor não recebeu um JPEG válido;
- `camera_offline`: a câmera não respondeu aos controles e também não entregou imagem.

Uma espera de retry, um catálogo ocupado ou uma resolução adiada é inconclusiva. Esses ciclos aparecem na telemetria, mas não incrementam a sequência de falhas JPEG nem autorizam reboot.

## Valores padrão de conectividade

| Configuração | Padrão |
|---|---:|
| Intervalo do health-check | 15 s |
| Graça inicial | 90 s |
| Falhas para redescoberta | 2 |
| Falhas para declarar offline | 3 |
| Sucessos para confirmar recuperação | 2 |
| Falhas JPEG para alerta | 2 |
| Falhas conclusivas para reboot | 3 |
| Graça silenciosa após reboot | 60 s |
| Cooldown entre reboots | 5 min |
| Janela de contabilização | 1 h |
| Comandos aceitos por janela | 12 |
| Intervalo entre redescobertas | 30 s |
| Timeout de resposta ONVIF/RTSP | 3 s |
| Timeout do JPEG | 5 s |
| Retry da resolução de snapshot | 15 s |
| Falhas antes de trocar o perfil | 2 |
| Ciclo padrão da prévia | 2 s, até 0,5 FPS |

O alerta normalmente ocorre após cerca de 30 segundos de falhas conclusivas. Dependendo do failover de perfil, o primeiro reboot pode ocorrer aproximadamente entre 45 e 60 segundos depois do início da falha, respeitando a graça inicial e o cooldown.

## Failover de snapshot

O firmware consulta os perfis ONVIF e aplica esta prioridade:

1. perfil JPEG cujo nome contém o filtro configurado;
2. perfil cujo nome contém o filtro configurado;
3. qualquer perfil JPEG;
4. perfil de menor resolução como fallback.

Com o padrão `snap`, uma câmera que publique `snapStream/JPEG` utilizará esse perfil antes de `subStream` ou `mainStream`.

Após o limiar de falhas, o token atual é temporariamente ignorado e um perfil alternativo é testado. O reboot somente fica elegível depois que o alternativo também falha ou quando o serviço de mídia ONVIF falha repetidamente.

## Recuperação automática

O firmware envia:

```text
ONVIF Device Management / SystemReboot
```

Regras importantes:

- somente respostas aceitas incrementam `attemptsInWindow`;
- tentativas rejeitadas aplicam cooldown para evitar martelamento;
- a cota não é apagada apenas porque a câmera voltou por alguns segundos;
- durante a graça pós-reboot não são realizadas sondas;
- RTP ativo impede reboot automático;
- autenticação, HTTP 4xx, recurso não suportado e configuração inválida não são tratados como falhas recuperáveis da câmera.

### Limitação física

O firmware não corta a energia da câmera. O GPIO 2 é utilizado somente pelo LED de alerta.

Se a câmera travar completamente e deixar de responder pela rede, o ESP32 não consegue entregar `SystemReboot`. Recuperação desse cenário exige hardware externo, como relé, MOSFET ou tomada inteligente, com GPIO, nível ativo e tempo de corte definidos para a instalação.

## Configurações e NVS

As configurações editadas no painel são validadas e gravadas no namespace NVS `camcfg`.

- esquema persistido: `1`;
- revisão dos defaults: `1`;
- chave principal: `runtime`;
- chave da revisão de defaults: `defaults_rev`.

Na primeira inicialização da versão 0.7.0, valores que ainda forem exatamente iguais aos antigos defaults são migrados. Valores personalizados diferentes dos defaults antigos são preservados.

O diagnóstico esperado depois da migração contém:

```json
{
  "firmwareVersion": "0.7.0",
  "settings": {
    "defaultsRevision": 1,
    "source": "nvs",
    "lastResult": "Configuracoes carregadas; novos padroes de conectividade aplicados"
  }
}
```

O botão Restaurar padrões limpa a configuração persistida e reaplica os valores de fábrica atuais.

## API HTTP

| Método | Endpoint | Função |
|---|---|---|
| GET | `/` | painel web |
| GET | `/health` | resposta simples `ok` |
| GET | `/api/status` | diagnóstico completo |
| GET | `/api/camera/snapshot` | snapshot JPEG validado |
| GET | `/api/settings` | configuração, limites e token temporário de escrita |
| POST | `/api/settings` | grava configurações na NVS |
| POST | `/api/settings/reset` | restaura padrões |
| POST | `/api/actions/health-check` | solicita verificação manual |
| POST | `/api/actions/rediscover` | solicita redescoberta ONVIF |
| POST | `/api/actions/reboot-camera` | solicita `SystemReboot` |
| POST | `/api/actions/reset-reboot-limiter` | limpa cooldown e janela |
| POST | `/api/actions/reboot-esp32` | reinicia o ESP32 |
| GET | `/assets/live-player.js` | player JavaScript embarcado |

Operações de escrita exigem o `writeToken` fornecido por `/api/settings`. Quando `DASHBOARD_ADMIN_PASSWORD_VALUE` está configurado, a senha administrativa também é obrigatória.

## Diagnóstico

Os campos mais úteis de `/api/status` são:

- `firmwareVersion` e `settings.defaultsRevision`;
- `resources.heap`, `resources.flash` e `resources.chip.resetReason`;
- `wifi.rssi`, IP e gateway;
- `probes.onvif*` e `probes.rtsp*`;
- `snapshot.conclusive`, `resolutionTransportFailure`, `profileFailoverPending`, `status` e `detail`;
- `live.relay.mediaActive`, `mediaRecentlyDelivered`, pacotes e bytes;
- `incident.kind` e duração;
- `recovery.attemptsInWindow`, `maxAttemptsInWindow`, graça e último resultado;
- contadores de sucesso, falha, inconclusivos e sequências atuais.

O motivo do último reset ajuda a separar problemas do ESP32:

| `resetReason` | Indicação |
|---|---|
| `power_on` | energização normal |
| `software` | reinicialização solicitada pelo firmware |
| `brownout` | queda ou instabilidade de alimentação |
| `task_watchdog` / `interrupt_watchdog` | tarefa ou interrupção bloqueada |
| `panic` | exceção ou falha de execução |

## Solução de problemas

### Snapshot retorna HTTP 500

1. confira `live.profiles` e confirme a existência de perfil JPEG;
2. verifique `snapshot.profile` e `profileFailoverPending`;
3. confirme se o vídeo ao vivo entrega pacotes RTP;
4. aguarde o teste do perfil alternativo antes de concluir que a câmera está offline;
5. verifique `recovery.lastResult` para saber se o reboot foi aceito, bloqueado por cooldown ou recusado.

### A câmera continua sem reiniciar

- confirme que o ONVIF ainda responde e aceita `SystemReboot`;
- confira credenciais e `recovery.credentialsConfigured`;
- confira cooldown, janela e número de comandos aceitos;
- se a câmera estiver totalmente fora da rede, utilize power-cycle externo.

### Suspeita de fonte ou falha do ESP32

- procure `brownout`, `watchdog` ou `panic` em `resources.chip.resetReason`;
- acompanhe `minimumFreeBytes` e `largestBlockBytes`;
- verifique estabilidade de 5 V/3,3 V, cabos, regulador e aterramento;
- compare o horário do reset do ESP32 com a queda da câmera.

## Segurança

Este projeto foi projetado para uma LAN confiável e não deve ser exposto diretamente à internet.

Limitações atuais:

- painel e API utilizam HTTP sem TLS;
- o relay da porta 81 não possui autenticação HTTP própria;
- a resposta do Resend ainda utiliza cliente TLS sem validação de certificado;
- o token de escrita é entregue ao navegador por `/api/settings`;
- credenciais da câmera devem permanecer somente em `include/secrets.h`.

Configure uma senha administrativa, restrinja a rede e utilize firewall/VLAN quando a câmera estiver em ambiente sensível.

## Estrutura do projeto

```text
ESP32/
├── src/
│   ├── main.cpp                  # estado, ONVIF, snapshot, API e recuperação
│   └── live_stream_relay.cpp     # cliente RTSP/RTP e relay para o navegador
├── include/
│   ├── app_config.h              # defaults e limites
│   ├── dashboard_page.h          # painel web embarcado
│   ├── live_catalog.h            # catálogo ONVIF e descritores de stream
│   ├── live_player.js            # fonte JavaScript do player
│   ├── live_player_script.h      # player embarcado em PROGMEM
│   ├── live_stream_relay.h       # tipos públicos do relay
│   ├── secrets.example.h         # modelo de configuração privada
│   └── secrets.h                 # configuração local ignorada pelo Git
├── partitions.csv
├── platformio.ini
├── LICENSE
└── README.md
```

Ao alterar `live_player.js`, mantenha o conteúdo correspondente em `live_player_script.h`, pois o firmware serve a versão embarcada no header.

## Validação recomendada antes de instalar

1. compile o ambiente `esp32dev`;
2. grave o firmware e abra o monitor serial em 115200 baud;
3. confirme `firmwareVersion: 0.7.0` em `/api/status`;
4. confirme a migração NVS em `settings.lastResult`;
5. abra a Prévia e o Ao Vivo separadamente;
6. valide que `mediaActive` fica verdadeiro enquanto há pacotes RTP;
7. simule falha do endpoint JPEG e observe o failover;
8. confirme que um reboot aceito inicia 60 segundos de graça silenciosa;
9. confira o motivo do reset do ESP32 após reinicializações inesperadas.

## Licença

Distribuído sob a licença MIT. Consulte [LICENSE](LICENSE).
