# ESP32 Camera Watchdog 0.9.0

Firmware para ESP32 que monitora uma câmera IP ONVIF/RTSP, oferece um painel web local, valida snapshots JPEG, retransmite vídeo H.264 para o navegador, controla PTZ e executa recuperação automática com limites de segurança.

A versão 0.9.0 torna o failover do Ao Vivo configurável e persistente, dá mais tempo para o perfil selecionado se recuperar antes de trocá-lo e interrompe a alternância contínua após uma varredura completa. O diagnóstico também passa a separar reboots manuais e automáticos, preservar as últimas oito recuperações aceitas com suas evidências e exigir uma janela pós-`PLAY` completa antes de classificar silêncio RTP como falha conclusiva.

## Funcionalidades

- descoberta da câmera por ONVIF WS-Discovery;
- varredura opcional da sub-rede quando a descoberta não encontra a câmera;
- catálogo dinâmico com `GetServices(true)` e validação dos endpoints anunciados;
- inventário de identificação, scopes, capacidades e tópicos de eventos ONVIF;
- seleção e failover de perfis JPEG;
- Prévia JPEG retransmitida sem manter a imagem inteira na RAM;
- Ao Vivo H.264 por RTSP/RTP sobre TCP, ESP32 e MSE/fMP4 no navegador;
- failover configurável e persistido dos perfis H.264 do Ao Vivo;
- proteção contra acúmulo de vídeo antigo no relay e no navegador;
- controle PTZ de pan, tilt e zoom na tela Ao Vivo;
- solicitação best-effort de quadro-chave com `SetSynchronizationPoint`;
- health-check com evidências independentes de JPEG e RTP;
- recuperação automática por `ONVIF SystemReboot`, com graça, cooldown e janela móvel;
- alertas por e-mail usando Resend;
- LED local de estado no GPIO 2;
- configurações persistidas na NVS;
- diagnóstico JSON detalhado de câmera, rede, memória, relay, relógio e recuperação.

## Arquitetura

```mermaid
flowchart LR
    CAMERA["Câmera IP<br>ONVIF + RTSP/H.264"]
    ESP["ESP32 Camera Watchdog"]
    PANEL["Painel web<br>porta 80"]
    PLAYER["Player MSE/fMP4<br>porta 81"]
    NTP["Servidores NTP"]
    EMAIL["Resend"]

    CAMERA -->|SOAP, JPEG e RTSP/RTP| ESP
    ESP --> PANEL
    ESP -->|RTP encapsulado em HTTP| PLAYER
    NTP -->|UTC de referência| ESP
    ESP -->|alertas opcionais| EMAIL
```

O relay integrado atende um navegador por vez. O canal de acesso à câmera é serializado para impedir que Prévia, health-check, PTZ e relay corrompam a mesma sessão de rede.

## Principais mudanças da 0.9.0

### Failover persistente do Ao Vivo

- o player reconecta o mesmo perfil durante 30 s antes de considerar a troca automática;
- a espera pode ser configurada entre 5 s e 300 s;
- a troca automática pode ser desativada sem impedir as reconexões do perfil selecionado;
- depois que todos os perfis H.264 falham, o player mantém o perfil atual e aguarda 5 min antes de repetir a varredura;
- o intervalo de repetição pode ser configurado entre 30 s e 3600 s;
- a troca automática temporária não sobrescreve a preferência de perfil escolhida manualmente;
- as três opções são validadas pelo firmware e persistidas na NVS.

### Canário RTP e recuperação

- o canário dispõe de até 3,5 s para concluir `DESCRIBE`, `SETUP` e `PLAY`;
- após um `PLAY` aceito, observa RTP por uma janela de 3 s;
- ausência de mídia só recebe `conclusiveSilence: true` depois que essa janela termina; timeout de handshake ou observação incompleta permanece inconclusivo;
- `handshakeLatencyMs`, `observedAfterPlayMs` e `conclusiveSilence` permitem distinguir lentidão de negociação de silêncio real após `PLAY`;
- mesmo após as contagens mínimas, o reboot exige ao menos 90 s contínuos de falha independente de mídia;
- a sonda RTSP bruta deixa de ser executada quando ONVIF já respondeu, reduzindo carga e atrasos sem remover o canário de mídia;
- o limite padrão foi reduzido para três reboots ONVIF aceitos por hora, mantendo cooldown de 5 min;
- contadores manuais e automáticos são separados, e `acceptedHistory` guarda as últimas oito recuperações aceitas com evidências de JPEG, canário, Wi-Fi e heap.

### Painel e persistência

- o gauge de sucesso considera somente verificações conclusivas: sucessos divididos por sucessos mais falhas;
- a tela Recursos agora possui 12 cards, incluindo `Canário RTP` e `Relay ao vivo`;
- o schema da NVS passa a ser 2 e migra automaticamente configurações válidas do schema 1;
- a revisão 3 dos defaults aplica o novo limite de três reboots por hora apenas quando o valor salvo ainda era o antigo padrão de 12, preservando valores personalizados.

### Proteções mantidas da 0.8.5

As proteções de backlog, drift e escrita do relay introduzidas na 0.8.5 permanecem ativas.

#### Latência do Ao Vivo

- o player inicia com aproximadamente 1,5 s de mídia acumulada;
- fragmentos MSE são agrupados em lotes limitados a 0,5 s, 12 entradas ou 512 KiB;
- uma fila JavaScript acima de 6 s gera `LiveBacklogError` e reconecta em uma sessão nova;
- o player nunca descarta P-frames ou fragmentos FU-A isoladamente para tentar alcançar o tempo real;
- o watchdog acompanha `video.currentTime`; oito segundos sem progresso acionam recuperação e, se necessário, reconexão;
- a interface mostra a latência estimada no navegador, incluindo mídia ainda não anexada ao `SourceBuffer`.

#### Proteção no relay

- cada registro enviado ao navegador possui deadline total de 1 s;
- a escrita usa socket não bloqueante, evitando que um cliente lento retenha indefinidamente o RTP da câmera;
- o relay compara o avanço do timestamp RTP com o tempo monotônico;
- atraso RTP acima de 3 s em duas janelas consecutivas encerra a sessão e força reconexão fresca;
- pacotes duplicados ou atrasados não alimentam o detector de drift;
- RTP v2, payload type, extensão, padding e H.264 são validados antes da retransmissão;
- NAL simples, STAP-A, STAP-B, FU-A e FU-B são aceitos; formatos não suportados são contabilizados como inválidos;
- abortos por latência têm telemetria própria e não contam como falha da câmera, incidente, LED ou gatilho de reboot.

#### Relógio da câmera

- o ESP32 registra quando recebeu uma sincronização SNTP nesta inicialização;
- `GetSystemDateAndTime` é executado periodicamente, fora do inventário ONVIF inicial;
- o desvio é calculado entre `UTCDateTime` da câmera e o ponto médio da consulta usando a referência SNTP do ESP32;
- alterações da referência SNTP durante a consulta invalidam a amostra;
- o painel mostra modo, timezone reportado, desvio e validade da referência;
- a verificação é somente leitura: o firmware não muda automaticamente NTP para Manual e não envia `SetSystemDateAndTime`;
- datas humanas no painel usam automaticamente o timezone do navegador; o JSON e o WS-Security continuam em UTC.

NTP fornece UTC, mas não determina o timezone geográfico da câmera. O timezone do OSD continua sendo configuração da própria câmera.

## Requisitos

### ESP32

- placa compatível com o ambiente PlatformIO `esp32dev`;
- flash de 4 MB usando `partitions.csv`;
- Wi-Fi de 2,4 GHz;
- PSRAM não é obrigatória.

### Câmera

- acesso pela mesma rede local do ESP32;
- serviço ONVIF Device Management;
- Media1 com perfis e `GetSnapshotUri` para a Prévia;
- RTSP H.264 para o Ao Vivo;
- RTP sobre RTSP/TCP para o relay integrado;
- serviço PTZ ONVIF para pan, tilt e zoom;
- `SystemReboot` apenas se a recuperação automática for utilizada.

O player integrado aceita H.264. H.265/HEVC e áudio não são reproduzidos.

## Configuração inicial

Crie o arquivo local de segredos a partir do exemplo:

```powershell
Copy-Item include\secrets.example.h include\secrets.h
```

Preencha `include/secrets.h`:

```cpp
#pragma once

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

Regras:

- não versione nem compartilhe `include/secrets.h`;
- credenciais ONVIF podem ficar vazias quando a câmera não exige autenticação;
- credenciais RTSP reutilizam ONVIF por padrão, mas podem ser diferentes;
- Resend é opcional; sem chave válida, somente o envio de e-mail fica indisponível;
- configure `DASHBOARD_ADMIN_PASSWORD_VALUE` para proteger ações sensíveis.

## Compilar, gravar e monitorar

O ambiente atual está em `platformio.ini`:

```ini
[env:esp32dev]
platform = platformio/espressif32@7.0.1
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 460800
board_build.partitions = partitions.csv
```

Comandos básicos:

```powershell
pio run
pio run --target upload
pio device monitor
```

Para selecionar explicitamente a COM5:

```powershell
pio run --target upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Também podem ser usadas as tarefas Build, Upload e Monitor da extensão PlatformIO.

## Acesso ao painel

Depois da conexão Wi-Fi, abra:

```text
http://camera-watchdog.local
```

Se o mDNS não estiver disponível, use o IP informado no monitor serial.

| Porta | Uso |
|---:|---|
| 80/TCP | painel e API HTTP |
| 81/TCP | relay HTTP/RTP do Ao Vivo |
| 3702/UDP | WS-Discovery ONVIF |
| 554/TCP | RTSP da câmera por padrão |

O player abre uma URL semelhante a:

```text
http://IP_DO_ESP32:81/stream?profile=1
```

## Módulos do painel

### Câmera

Apresenta estado global, disponibilidade, resultados das sondas, incidentes, última evidência de saúde, recuperação e diagnóstico bruto.

### Prévia

A Prévia executa um GET HTTP no snapshot da câmera e retransmite o JPEG conforme os bytes chegam:

- o perfil e a URI ONVIF são mantidos em cache e não são resolvidos a cada quadro;
- o JPEG completo não é armazenado na RAM;
- o buffer de retransmissão possui 2 KiB;
- o padrão é um quadro a cada 5 s, no máximo 0,2 FPS;
- a Prévia para ao sair da tela ou ocultar a aba;
- um quadro completamente entregue pode ser reutilizado como evidência do health-check;
- fechamento do navegador incrementa `clientAborts`, não `snapshot.errors`.

### Ao Vivo

Fluxo do vídeo:

1. o firmware obtém o URI RTSP do perfil H.264 selecionado;
2. abre `DESCRIBE`, `SETUP` interleaved e `PLAY`;
3. valida os pacotes RTP/H.264;
4. encapsula cada pacote em um registro HTTP binário;
5. o navegador reconstrói os access units e gera fMP4 para MSE;
6. watchdogs de progresso, fila e drift encerram sessões atrasadas.

Se nenhum IDR chegar nos primeiros 750 ms, o relay tenta `SetSynchronizationPoint`. A falha desse comando é best-effort e não encerra a sessão.

O player reconecta primeiro o perfil selecionado. Com a troca automática ativa, somente uma falha contínua além da espera configurada faz o player avançar para outro perfil H.264. Depois que todos os perfis forem testados, ele mantém o perfil atual até o intervalo de repetição vencer, em vez de alternar indefinidamente. Receber apenas SDP ou bitrate não é considerado sucesso suficiente.

Em Configurações > Ao Vivo é possível:

- ativar ou desativar a troca automática de perfil;
- definir a espera antes da troca entre 5 s e 300 s, com padrão de 30 s;
- definir a repetição de uma varredura esgotada entre 30 s e 3600 s, com padrão de 300 s.

Mesmo durante um failover automático, a preferência escolhida manualmente permanece salva para a próxima inicialização do player.

### Controle PTZ

Quando a câmera anuncia serviço PTZ:

- botões pressionados enviam `ContinuousMove`;
- soltar envia `Stop`;
- velocidade é ajustável entre 10% e 100%;
- a câmera recebe timeout de segurança de 2 s para o movimento;
- ocultar a aba ou sair do Ao Vivo também tenta enviar `Stop`;
- comandos exigem `writeToken` e, quando configurada, senha administrativa.

### Recursos

Mostra 12 cards: heap, maior bloco, mínimo histórico, PSRAM, flash, Wi-Fi, catálogo ONVIF, eventos anunciados, sincronização de quadro-chave, relógio da câmera, canário RTP e relay ao vivo. Os dois últimos resumem respectivamente o resultado das sondas independentes de mídia e a proporção de sessões encerradas por latência, incluindo prazos de escrita excedidos.

O card do relógio usa a referência SNTP do ESP32. Se o OSD da câmera e o relógio do computador divergirem, consulte `onvif.clock` antes de concluir que o vídeo está atrasado.

### Configurações e Ações

Configurações são validadas pelo firmware e persistidas na NVS. Ações manuais permitem executar health-check, redescobrir a câmera, solicitar reboot ONVIF, limpar o limitador e reiniciar o ESP32.

## Como o health-check decide o estado

O monitor combina várias evidências, em vez de depender apenas do snapshot:

1. novo RTP entregue pelo relay;
2. relay ativo ou período curto de graça após uma sessão;
3. JPEG completo entregue recentemente pela Prévia;
4. disponibilidade ONVIF e, quando ONVIF não responder, sonda RTSP bruta;
5. resolução e validação do snapshot JPEG;
6. canário `DESCRIBE`/`SETUP`/`PLAY` com primeiro RTP nos perfis H.264 compatíveis.

Quando ONVIF responde, a sonda RTSP bruta é pulada porque não acrescenta evidência de imagem e pode atrasar o ciclo. A mídia continua sendo verificada pelo relay, pela Prévia, pelo snapshot e pelo canário RTP.

O canário reserva até 3,5 s para o handshake RTSP e, após `PLAY`, observa a sessão por até 3 s. Silêncio só é conclusivo quando `conclusiveSilence` está ativo e a janela pós-`PLAY` foi cumprida; negociação lenta, timeout antes de `PLAY` ou observação incompleta permanecem inconclusivos.

Fontes positivas aparecem em `healthEvidence.source`:

- `relay_rtp`;
- `preview_jpeg`;
- `snapshot_jpeg`;
- `media_canary_rtp`.

Ciclos inconclusivos, mutex ocupado, autenticação inválida, HTTP 4xx ou transporte não suportado não comprovam falha de mídia e não autorizam reboot.

Incidentes possíveis:

- `snapshot_unavailable`: controles respondem, mas JPEG e canário de mídia falharam de forma conclusiva;
- `camera_offline`: a câmera não responde aos controles e não existe evidência recente de imagem.

O LED pisca por incidente confirmado, câmera offline, recuperação, perda de Wi-Fi ou outra condição global real. Uma reconexão de baixa latência do relay não é incidente.

## Recuperação automática

O comando utilizado é:

```text
ONVIF Device Management / SystemReboot
```

Um reboot automático exige, em conjunto:

- ONVIF ainda acessível;
- falha JPEG conclusiva;
- ausência de RTP recente no relay;
- varreduras completas sem RTP em todos os perfis H.264 compatíveis;
- limiar de falhas consecutivas;
- pelo menos 90 s contínuos de falha independente de mídia;
- fim da graça inicial e da graça pós-reboot;
- cooldown e cota da janela ainda disponíveis.

Regras de segurança:

- falha isolada do snapshot não autoriza reboot;
- somente comandos aceitos consomem a cota da janela móvel;
- o padrão permite no máximo três comandos aceitos por hora e um a cada 5 min;
- um reboot aceito zera sequências conclusivas e inicia graça silenciosa;
- autenticação, configuração inválida e recurso não suportado são inconclusivos para ação destrutiva;
- aborto por latência do Ao Vivo não incrementa `failedSessions` nem contadores do health-check.

O diagnóstico separa tentativas, aceitações e erros de origem `automatic` e `manual`. Cada comando aceito também entra em `recovery.acceptedHistory`, um histórico circular em RAM com as oito entradas mais recentes e a evidência disponível naquele momento: snapshot, canário, RSSI e heap. O histórico é reiniciado quando o ESP32 reinicia; ele não é gravado na NVS.

O ESP32 não corta a energia da câmera. Se ela perder toda a conectividade, `SystemReboot` não pode ser entregue; esse cenário exige relé, MOSFET ou tomada inteligente externa.

## Valores padrão

| Configuração | Padrão |
|---|---:|
| Intervalo do health-check | 15 s |
| Graça inicial | 90 s |
| Falhas para redescoberta | 2 |
| Falhas para declarar offline | 3 |
| Sucessos para confirmar recuperação | 2 |
| Falhas de vídeo para alerta | 2 |
| Falhas de vídeo para reboot | 3 |
| Graça após reboot | 60 s |
| Cooldown entre reboots | 5 min |
| Janela móvel de reboots | 1 h |
| Máximo de reboots aceitos na janela | 3 |
| Timeout de resposta dos serviços ONVIF | 3 s |
| Timeout HTTP do snapshot | 5 s |
| Retry de resolução do snapshot | 15 s |
| Ciclo da Prévia | 5 s, até 0,2 FPS |
| Atualização do painel | 3 s |
| Troca automática de perfil do Ao Vivo | ativa |
| Espera antes da troca de perfil | 30 s, faixa de 5–300 s |
| Repetição da varredura de perfis | 5 min, faixa de 30–3600 s |
| Evidência recente de RTP | 30 s |
| Reuso do canário RTP | 60 s |
| Timeout do handshake do canário RTP | 3,5 s |
| Janela pós-`PLAY` do canário RTP | 3 s |
| Duração mínima de falha de mídia para reboot | 90 s |
| Timeout de pacote do Ao Vivo | 12 s |
| Buffer inicial do player | 1,5 s |
| Limite da fila MSE | 6 s |
| Deadline de escrita do relay | 1 s |
| Drift RTP que força reconexão | 3 s em 2 janelas |
| Timeout de segurança PTZ | 2 s |
| Verificação periódica do relógio | 15 min |
| Retry de relógio inconclusivo | 60 s |

Esses valores podem ser alterados pelo painel quando pertencem a `RuntimeSettings`. A troca automática, sua espera e a repetição da varredura fazem parte das configurações persistidas. Limites internos do player, relay, canário RTP, PTZ e observação do relógio permanecem definidos no firmware.

## Configurações e NVS

- namespace: `camcfg`;
- chave principal: `runtime`;
- chave da revisão: `defaults_rev`;
- schema: `2`;
- revisão dos defaults: `3`.

Ao carregar um blob válido do schema 1, o firmware copia os campos existentes, preenche as novas opções de failover com os defaults da 0.9.0, valida o resultado e o salva no schema 2. A revisão 3 também converte o antigo padrão de 12 reboots por janela para o novo padrão de 3; qualquer limite personalizado diferente de 12 é preservado. As migrações anteriores dos intervalos da Prévia continuam disponíveis e valores personalizados permanecem intactos.

Restaurar padrões limpa a configuração persistida e reaplica os valores de `include/app_config.h`.

## API HTTP

| Método | Endpoint | Função |
|---|---|---|
| GET | `/` | painel web |
| GET | `/health` | resposta simples `ok` |
| GET | `/api/status` | diagnóstico completo |
| GET | `/api/camera/snapshot` | JPEG validado e retransmitido |
| POST | `/api/camera/ptz` | `ContinuousMove` ou `Stop` |
| GET | `/api/settings` | valores, limites e token temporário de escrita |
| POST | `/api/settings` | valida e grava configurações na NVS |
| POST | `/api/settings/reset` | restaura padrões |
| POST | `/api/actions/health-check` | agenda verificação manual |
| POST | `/api/actions/rediscover` | agenda redescoberta ONVIF |
| POST | `/api/actions/reboot-camera` | solicita `SystemReboot` |
| POST | `/api/actions/reset-reboot-limiter` | limpa cooldown e janela |
| POST | `/api/actions/reboot-esp32` | reinicia o ESP32 |
| GET | `/assets/live-player.js` | player JavaScript embarcado |

Operações de escrita exigem o `writeToken` fornecido por `/api/settings`. Quando `DASHBOARD_ADMIN_PASSWORD_VALUE` não está vazio, a senha administrativa também é obrigatória.

## Diagnóstico `/api/status`

Campos importantes:

- `firmwareVersion`, `state`, `lastEvent` e `uptimeSeconds`;
- `resources.heap`, `resources.psram`, `resources.flash` e `resources.chip.resetReason`;
- `wifi.connected`, `wifi.rssi`, IP e gateway;
- `onvif.services`, `onvif.capabilities`, `onvif.deviceInfo` e `onvif.events`;
- `onvif.clock.referenceSynchronized`, `healthy`, `driftMilliseconds`, `dateTimeType` e `reportedTimeZone`;
- `onvif.ptzControl.attempts`, `accepted`, `errors` e `lastResult`;
- `probes.rtspAttempted`, que fica `false` quando a resposta ONVIF torna desnecessária a sonda RTSP bruta;
- `snapshot.healthy`, `conclusive`, `served`, `clientAborts`, `cameraHttpRequests` e `healthEvidenceUses`;
- `healthEvidence.source` e `healthEvidence.ageSeconds`;
- `mediaCanary.playAccepted`, `mediaReceived`, `conclusiveSilence`, `handshakeLatencyMs`, `profilesTested`, `observedAfterPlayMs` e `failureSequenceAgeSeconds`;
- `live.relay.mediaActive`, `currentSessionPackets`, `currentSessionBytes` e `lastMediaPacketAgeSeconds`;
- `live.relay.currentRtpClockDriftMs`, `slowWriteCount`, `writeDeadlineExceeded` e `latencyAborts`;
- `live.relay.lastLatencyAbortReason`, `invalidRtpPackets`, `syncAccepted`, `syncFailed` e `syncSkipped`;
- `incident.kind`, `incident.durationSeconds` e estado do alerta;
- `recovery.graceActive`, `attemptsInWindow`, limites, `lastResult` e `lastEvidence`;
- `recovery.automatic`, `recovery.manual` e `recovery.acceptedHistory`, com até oito recuperações aceitas e suas evidências;
- `ui.enableLiveProfileFailover`, `liveProfileFailoverDelayMs` e `liveProfileSweepRetryIntervalMs`;
- contadores de verificações bem-sucedidas, falhas e inconclusivas.

`successfulChecks`, `failedChecks` e `inconclusiveChecks` continuam separados. O gauge do painel calcula a taxa somente sobre `successfulChecks + failedChecks`; verificações inconclusivas não reduzem artificialmente a porcentagem de sucesso.

Exemplo reduzido:

```json
{
  "firmwareVersion": "0.9.0",
  "state": "online",
  "probes": {
    "onvifHttp": true,
    "rtspAttempted": false
  },
  "mediaCanary": {
    "playAccepted": true,
    "mediaReceived": true,
    "conclusiveSilence": false,
    "handshakeLatencyMs": 640,
    "observedAfterPlayMs": 210
  },
  "onvif": {
    "clock": {
      "referenceSynchronized": true,
      "utcValid": true,
      "healthy": true,
      "driftMilliseconds": -620,
      "dateTimeType": "NTP",
      "reportedTimeZone": "BRT3"
    }
  },
  "snapshot": {
    "healthy": true,
    "clientAborts": 0
  },
  "live": {
    "relay": {
      "mediaActive": true,
      "currentRtpClockDriftMs": 140,
      "latencyAborts": 0
    }
  },
  "recovery": {
    "automatic": { "attempts": 0, "accepted": 0, "errors": 0 },
    "manual": { "attempts": 0, "accepted": 0, "errors": 0 },
    "acceptedHistory": []
  }
}
```

`driftMilliseconds` e `roundTripMs` são `null` quando a amostra do relógio é inconclusiva.

### Motivo do reset

| `resetReason` | Indicação |
|---|---|
| `power_on` | energização normal |
| `software` | reinicialização solicitada pelo firmware |
| `brownout` | queda ou instabilidade de alimentação |
| `task_watchdog` / `interrupt_watchdog` | tarefa ou interrupção bloqueada |
| `panic` | exceção ou falha de execução |

## Solução de problemas

### O OSD parece dezenas de segundos atrasado

1. consulte `onvif.clock.referenceSynchronized`;
2. confira `onvif.clock.driftMilliseconds`;
3. se a câmera estiver alinhada ao ESP32, sincronize o relógio do computador ou celular;
4. não use somente o relógio da barra de tarefas para medir latência do vídeo;
5. use os indicadores `Latência no navegador` e `Atraso no relay` da tela Ao Vivo.

### O Ao Vivo mostra bitrate, mas a imagem congela

1. confira FPS e latência no navegador;
2. consulte `pendingLatencySeconds`, `mseLatencySeconds` e `liveLatencySeconds` no callback do player durante depuração;
3. confira `currentRtpClockDriftMs`, `slowWriteCount` e `latencyAborts` no status;
4. confirme se o player reconecta quando a fila ultrapassa 6 s;
5. verifique `invalidRtpPackets` e `lastError`.

### O Ao Vivo entra em reconexão contínua

- verifique sinal Wi-Fi, `maxWriteDurationMs` e `writeDeadlineExceeded`;
- confirme que o navegador alcança a porta 81 diretamente;
- observe a mensagem do player: ele deve reconectar o mesmo perfil durante a espera configurada antes de trocar;
- em Configurações > Ao Vivo, aumente a espera entre 5 s e 300 s ou desative a troca automática para testar um único perfil;
- mantenha a repetição da varredura suficientemente longa; o padrão de 300 s impede o ciclo contínuo entre `mainStream` e `subStream`;
- teste manualmente outro perfil H.264 e confirme que essa preferência volta a ser usada ao reabrir o player;
- procure limite de sessões simultâneas na câmera, especialmente se o iCSee também estiver aberto;
- encerre outras conexões RTSP antes de concluir que o perfil está defeituoso.

### O canário aceita PLAY, mas não recebe RTP

- confira `mediaCanary.handshakeLatencyMs` para separar negociação RTSP lenta de silêncio após `PLAY`;
- confira `observedAfterPlayMs`; uma observação interrompida antes da janela não deve produzir falha conclusiva;
- só trate ausência de mídia como evidência destrutiva quando `conclusiveSilence` for `true`;
- compare `profileIndex`, `profilesTested` e `silentProfilesMask` para verificar se todos os perfis compatíveis foram realmente observados;
- se `probes.rtspAttempted` for `false` enquanto `probes.onvifHttp` for `true`, isso é esperado: o canário, e não a sonda RTSP bruta, valida a mídia.

### A Prévia não mostra imagem

- confira `snapshot.profile`, `targetResolved`, `status` e `detail`;
- verifique `busyResponses` para identificar contenção com o Ao Vivo;
- confira `cameraHttpRequests`, `previewFetchOperations` e `clientAborts`;
- aguarde o failover de perfil antes de reiniciar a câmera.

### A câmera não reinicia automaticamente

- confirme que ONVIF ainda responde;
- verifique credenciais e o resultado de `SystemReboot`;
- confira graça, cooldown, janela e cota;
- valide que todos os perfis H.264 falharam de forma conclusiva;
- confira `recovery.automatic`, `recovery.lastEvidence` e as entradas de `recovery.acceptedHistory`;
- se a câmera estiver totalmente fora da rede, use power-cycle externo.

### Reboots parecem ocorrer mais de dia ou em cenas detalhadas

Cenas iluminadas com grama, folhagem, chuva, ruído ou muito movimento podem aumentar a complexidade e o bitrate do H.264, mas essa correlação isolada não prova falha do encoder. Para separar a câmera, a rede e o ESP32:

1. compare o comportamento do `mainStream` e do `subStream` sem deixar a troca automática mascarar o perfil testado;
2. registre `snapshot.status`, `mediaCanary.handshakeLatencyMs`, `observedAfterPlayMs` e `conclusiveSilence`;
3. compare RSSI, heap e prazos de escrita do relay no mesmo horário;
4. após um reboot aceito, exporte `recovery.acceptedHistory` antes de reiniciar o ESP32;
5. repita a observação à noite e com a câmera apontada para uma cena menos detalhada, mantendo as demais configurações iguais.

### Suspeita de alimentação ou travamento do ESP32

- consulte `resources.chip.resetReason`;
- acompanhe `minimumFreeBytes` e `largestBlockBytes`;
- procure `brownout`, `watchdog` ou `panic`;
- verifique fonte, cabos, regulador e aterramento.

## Limitações atuais

- apenas H.264 é reproduzido no navegador;
- áudio não é retransmitido;
- somente um navegador usa o relay por vez;
- eventos ONVIF são inventariados por `GetEventProperties`, mas ainda não existe assinatura PullPoint em runtime;
- Media2 é catalogado, porém os fluxos atuais usam Media1;
- o firmware observa o relógio da câmera, mas não o corrige automaticamente;
- não existe power-cycle físico da câmera;
- o painel e o relay usam HTTP sem TLS.

## Segurança

O projeto foi projetado para uma LAN confiável. Não exponha as portas 80, 81 ou ONVIF diretamente à internet.

- painel e API não usam TLS;
- o relay da porta 81 não possui autenticação HTTP própria;
- o cliente HTTPS do Resend está configurado com `setInsecure()`;
- o token de escrita é entregue pelo endpoint local `/api/settings` e deve permanecer restrito à LAN confiável;
- credenciais permanecem compiladas em `include/secrets.h`;
- câmeras que aceitam comandos ONVIF sem autenticação devem ficar isoladas por firewall ou VLAN.

Use senha administrativa, rede segmentada e regras de firewall em instalações sensíveis.

## Estrutura do projeto

```text
ESP32/
├── src/
│   ├── main.cpp                  # estado, ONVIF, snapshot, API e recuperação
│   └── live_stream_relay.cpp     # RTSP/RTP e relay de baixa latência
├── include/
│   ├── app_config.h              # defaults e limites
│   ├── dashboard_page.h          # painel web embarcado
│   ├── live_catalog.h            # perfis ONVIF e status do relay
│   ├── live_player.js            # fonte JavaScript do player
│   ├── live_player_script.h      # cópia do player em PROGMEM
│   ├── live_stream_relay.h       # contratos públicos do relay
│   ├── secrets.example.h         # exemplo de configuração privada
│   └── secrets.h                 # configuração local ignorada pelo Git
├── partitions.csv
├── platformio.ini
├── LICENSE
└── README.md
```

`include/live_player.js` e o conteúdo JavaScript de `include/live_player_script.h` devem permanecer idênticos.

## Validação recomendada

1. compile o ambiente `esp32dev`;
2. grave o firmware e abra o monitor em 115200 baud;
3. confirme `firmwareVersion: 0.9.0` em `/api/status`;
4. confirme `schemaVersion: 2` em `/api/settings` e preserve uma configuração personalizada ao reiniciar o ESP32;
5. em uma instalação v0.8.5 existente, confirme a migração do schema 1 sem perda dos campos anteriores;
6. valide heap, maior bloco e motivo do reset;
7. confirme que Recursos mostra os 12 cards sem lacuna no layout de uma, duas e três colunas;
8. confirme catálogo ONVIF e perfis H.264/JPEG;
9. abra a Prévia e confirme JPEG válido a cada 5 s;
10. abra o Ao Vivo e confirme FPS, bitrate e latência limitada;
11. provoque uma falha do perfil e confirme que ele é reconectado por 30 s antes do failover;
12. desative a troca automática e confirme reconexão exclusiva do perfil escolhido, sem alteração da preferência manual;
13. esgote os perfis em teste controlado e confirme que a nova varredura só começa após o intervalo configurado;
14. salve valores-limite de 5 s/300 s e 30 s/3600 s, recarregue a página e confirme a persistência;
15. mantenha o vídeo aberto por pelo menos 30 minutos e confirme que o atraso não cresce continuamente;
16. confira `latencyAborts`, `writeDeadlineExceeded` e `invalidRtpPackets`;
17. confirme que ONVIF saudável produz `probes.rtspAttempted: false` sem impedir o canário RTP;
18. confirme que silêncio RTP só é conclusivo após a janela pós-`PLAY` e que o reboot continua bloqueado até `failureSequenceAgeSeconds` atingir 90 s;
19. valide pan, tilt, zoom e parada ao soltar;
20. oculte a aba durante movimento e confirme a parada de segurança;
21. confirme `onvif.clock.referenceSynchronized: true` e um drift saudável;
22. teste falha de snapshot mantendo RTSP disponível e confirme ausência de reboot falso;
23. valide graça, cooldown, limite de três aceites por hora e os contadores manual/automático antes de testar `SystemReboot` em ambiente controlado;
24. após um reboot aceito, confirme a evidência em `recovery.acceptedHistory` e que o histórico mantém no máximo oito entradas;
25. confirme que a taxa de sucesso do painel não inclui verificações inconclusivas no denominador;
26. confirme que abortos do navegador e reconexões por latência não acionam LED ou incidente.

## Licença

Distribuído sob a licença MIT. Consulte [LICENSE](LICENSE).
