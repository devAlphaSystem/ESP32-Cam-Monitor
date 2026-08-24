# ESP32 Camera Watchdog 0.13.0

Firmware para ESP32 que monitora uma câmera IP por ONVIF, valida snapshots JPEG e disponibiliza uma Prévia local no navegador. O watchdog também pode enviar alertas e executar uma recuperação controlada quando a câmera deixa de fornecer imagens válidas.

## Escopo da versão 0.13.0

A visualização da câmera é feita exclusivamente pela Prévia JPEG. O navegador nunca acessa a câmera diretamente: o ESP32 resolve o perfil ONVIF, autentica a solicitação, valida o JPEG e transfere o quadro ao painel.

O RTSP permanece somente como uma sonda básica de disponibilidade e como apoio opcional à descoberta da câmera. Ele não é usado para exibir imagens.

As comunicações externas e operações relevantes são gravadas em um histórico persistente circular com até 10.000 registros. Ao atingir o limite, cada novo registro sobrescreve o mais antigo.

## Principais recursos

- descoberta por ONVIF WS-Discovery;
- varredura opcional e limitada da sub-rede;
- seleção automática de perfil ONVIF para snapshot;
- descoberta do serviço ONVIF PTZ e dos presets gravados na câmera;
- acionamento autenticado de posições PTZ e do preset Daemon;
- preferência configurável por nome de perfil JPEG;
- Prévia JPEG intermediada pelo ESP32;
- autenticação Basic e Digest nas solicitações à câmera;
- validação dos marcadores, tipo e tamanho da imagem;
- reaproveitamento de um quadro recente da Prévia como evidência de saúde;
- health-check periódico com estados de degradação, recuperação e indisponibilidade;
- reinicialização ONVIF manual ou automática com tolerância, cooldown e limite por janela;
- alertas por e-mail através da API HTTPS do Resend;
- configurações persistentes e validadas na NVS;
- histórico persistente das conexões ONVIF, snapshot, RTSP, descoberta e varredura;
- registro de health-checks, incidentes, Wi-Fi, ações, alertas e reinicializações;
- painel responsivo com consulta paginada e exportação CSV completa dos logs.

## Arquitetura

```mermaid
flowchart LR
    CAMERA[Câmera IP ONVIF] -->|SOAP e snapshot JPEG| ESP[ESP32 Camera Watchdog]
    ESP -->|Prévia JPEG e painel HTTP| BROWSER[Navegador local]
    ESP -->|HTTPS opcional| RESEND[API Resend]
    ESP -->|Configurações| NVS[NVS]
    ESP -->|10.000 registros circulares| LOGS[LittleFS dedicado]
```

O acesso à câmera é serializado para evitar operações concorrentes entre Prévia, PTZ, health-check, descoberta e recuperação. As credenciais ONVIF permanecem no firmware e não são enviadas ao navegador.

## Requisitos

### Hardware e ambiente

- placa compatível com `esp32dev`;
- rede Wi-Fi de 2,4 GHz;
- PlatformIO;
- framework Arduino para ESP32;
- esquema de partições fornecido em `partitions.csv`.

### Câmera

- ONVIF Device Service;
- ONVIF Media1 com `GetProfiles` e `GetSnapshotUri`;
- endpoint HTTP que forneça snapshot JPEG;
- credenciais com permissão de consulta;
- `SystemReboot` disponível caso a recuperação por reinicialização seja utilizada.

## Configuração inicial

Crie o arquivo local de segredos a partir do modelo:

```text
include/secrets.example.h -> include/secrets.h
```

Preencha `include/secrets.h`:

```cpp
#pragma once

constexpr char WIFI_SSID[] = "NOME_DA_REDE";
constexpr char WIFI_PASSWORD[] = "SENHA_DA_REDE";

#define CAMERA_ONVIF_USERNAME_VALUE "usuario"
#define CAMERA_ONVIF_PASSWORD_VALUE "senha"

#define DASHBOARD_ADMIN_PASSWORD_VALUE "senha-administrativa"

constexpr char RESEND_API_KEY[] = "";
constexpr char RESEND_FROM[] = "Camera Watchdog <watchdog@example.com>";
constexpr char ALERT_EMAIL[] = "recipient@example.com";
```

Recomendações:

- mantenha `include/secrets.h` fora do controle de versão;
- não coloque credenciais em `main.cpp` ou `app_config.h`;
- configure uma senha administrativa se outras pessoas acessam a rede local;
- deixe `RESEND_API_KEY` vazio quando o envio de e-mails não for necessário.

Os filtros opcionais de fábrica ficam em `include/app_config.h`:

```cpp
constexpr char CAMERA_MATCH[] = "";
constexpr char CAMERA_IP_HINT[] = "";
constexpr char CAMERA_SNAPSHOT_PROFILE_MATCH[] = "snap";
```

## Compilar, gravar e monitorar

```bash
pio run
pio run -t upload
pio device monitor
```

O ambiente atual usa `platformio/espressif32@7.0.1`, placa `esp32dev` e framework Arduino. A porta de upload configurada em `platformio.ini` é `COM5` e pode precisar ser ajustada para o computador utilizado.

## Acessar o painel

Depois que o ESP32 se conectar à rede:

```text
http://IP_DO_ESP32/
http://camera-watchdog.local/
```

O nome usado pelo mDNS pode ser alterado em `Configurações > Identificação`,
sem adicionar `http://` ou `.local` ao valor. O padrão de fábrica permanece em
`include/app_config.h`:

```cpp
constexpr char LOCAL_HOSTNAME[] = "camera-watchdog";
```

O painel mostra o endereço mDNS quando o serviço é iniciado com sucesso e
mantém o endereço IP como alternativa. Ao salvar outro nome, o serviço mDNS é
reiniciado e o novo endereço passa a valer imediatamente.

| Porta | Finalidade |
|---|---|
| 80/TCP | painel e API HTTP do ESP32 |
| 3702/UDP | WS-Discovery ONVIF na rede local |
| 554/TCP | sonda RTSP e auxílio à varredura, configurável |
| 443/TCP | API do Resend |

As portas ONVIF e do snapshot são determinadas a partir da câmera encontrada.

## Painel

### Câmera

Apresenta estado geral, endereço encontrado, sondas ONVIF e RTSP, último JPEG, contadores e atividade recente.

### Prévia

O navegador solicita `GET /api/camera/snapshot`. Para cada quadro, o firmware:

1. resolve os perfis e a URI de snapshot quando necessário;
2. prioriza um perfil JPEG compatível com o filtro configurado;
3. autentica a solicitação diretamente na câmera;
4. limita o tamanho da resposta;
5. confirma os marcadores JPEG `FF D8` e `FF D9`;
6. transfere o quadro ao navegador;
7. registra bytes, latência, falhas e cancelamentos.

`feedMinIntervalMs` limita a frequência máxima e `feedTargetCycleMs` define o ciclo desejado. O padrão é um quadro a cada cinco segundos; o limite configurável chega a dois quadros por segundo.

### Recursos

Exibe heap, maior bloco alocável, menor heap desde o boot, flash, PSRAM, Wi-Fi, catálogo ONVIF, relógio da câmera e eventos anunciados.

### Logs

Exibe os registros persistentes em ordem decrescente, com filtros locais por nível, categoria, texto e modo de visualização. O modo `Diagnóstico` oculta apenas tentativas rotineiras de portas fechadas; `Todas as tentativas` e a exportação preservam o histórico integral. A listagem carrega 100 itens por vez, enquanto `Exportar CSV completo` transmite todos os registros diretamente da flash sem montar o arquivo inteiro na heap.

Cada registro ocupa 80 bytes e contém sequência, identificadores de boot e contexto, horário, uptime, categoria, evento, resultado, IP e porta de destino, status, duração, timeout, valor auxiliar, flags e detalhe limitado. Um mesmo `context_id` agrupa as tentativas e o resultado de cada health-check, prévia ou ação administrativa. A entrega da prévia aponta para o cliente do navegador e não é classificada como conexão iniciada contra a câmera. O firmware também registra a origem do destino da câmera, o contexto das configurações, respostas WS-Discovery e o motivo disponível para falhas de socket. Credenciais, tokens de autenticação, corpos SOAP, conteúdo de e-mail e bytes do JPEG não são gravados. Registros no formato anterior continuam legíveis, com os novos identificadores zerados.

O CSV mantém as colunas anteriores e acrescenta `boot_id`, `context_id`, `timestamp_utc`, `time_synchronized` e `timeout_ms` ao final de cada linha.

A partição `logs` possui 960 KiB. O arquivo circular é limitado a 10.000 registros, ou 800.000 bytes, deixando espaço para metadados e nivelamento de desgaste do LittleFS. As escritas normais são agrupadas por até um segundo; eventos críticos forçam a persistência imediata.

### Configurações

Permite ajustar identificação, perfil JPEG, monitoramento, recuperação, descoberta, notificações, timeouts, LED e ritmo do painel. O backend valida os limites e as relações entre campos antes de gravar a NVS.

### Ações

As posições PTZ detectadas ficam na aba Prévia, em uma lista pesquisável e rolável aberta por modal. O firmware mantém até 128 presets, portanto uma câmera com 50 posições não expande nem desorganiza a página. Os presets são atualizados automaticamente a cada minuto, e a Prévia é pausada durante a escolha e o comando para liberar o servidor HTTP e o canal da câmera.

A aba Ações permanece reservada ao health-check manual, redescoberta, reinicialização ONVIF da câmera, limpeza do limitador, exclusão do histórico de logs e reinicialização do ESP32. Ao zerar os logs, o histórico anterior é removido e um novo marcador de auditoria registra a limpeza e a quantidade excluída. Operações mutáveis exigem o token de escrita da sessão e, quando configurada, a senha administrativa.

Os nomes personalizados exibidos pelo iCSee podem permanecer apenas no aplicativo. Quando a câmera publica nomes numéricos, o painel usa `Posição <token>`. Nesta câmera XM, o token `100` foi confirmado como o ponto Daemon e está definido por `CAMERA_DAEMON_PRESET_TOKEN` em `include/app_config.h`.

## Health-check

O ciclo de monitoramento:

1. verifica Wi-Fi e tolerância inicial;
2. aceita um JPEG recente da Prévia como evidência válida;
3. tenta reservar o canal de acesso à câmera;
4. consulta ONVIF e, se necessário, executa a sonda RTSP básica;
5. resolve, baixa e valida um snapshot JPEG;
6. separa falhas conclusivas de indisponibilidades temporárias de transporte;
7. aplica os limites de redescoberta, alerta, recuperação e estado offline.

Fontes possíveis em `healthEvidence.source`:

- `snapshot_jpeg`;
- `preview_jpeg`;
- `none`.

Incidentes possíveis:

- `snapshot_unavailable`: a câmera responde, mas não entrega JPEG válido após o limite configurado;
- `camera_offline`: controles e imagem deixaram de responder;
- `none`: nenhum incidente ativo.

## Recuperação automática

A reinicialização automática da câmera exige:

- opção habilitada;
- término da tolerância inicial;
- quantidade configurada de falhas JPEG conclusivas;
- endpoint ONVIF compatível com `SystemReboot`;
- ausência de erro permanente de configuração;
- inexistência de outro perfil de snapshot ainda pendente;
- cooldown e cota da janela disponíveis.

Erros 4xx, operação não suportada, URI inválida e snapshot HTTPS local não suportado não disparam reinicialização automática.

As últimas oito recuperações aceitas mantêm em RAM a evidência do snapshot, RSSI e heap. Esse histórico reinicia junto com o ESP32.

## Valores padrão

| Configuração | Valor |
|---|---:|
| Endereço local | `camera-watchdog.local` |
| Health-check | 15 s |
| Tolerância inicial | 90 s |
| Falhas para redescoberta | 2 |
| Falhas para offline | 3 |
| Sucessos para recuperação | 2 |
| Falhas JPEG para alerta | 2 |
| Falhas JPEG para reinicialização | 3 |
| Tolerância após reinicialização | 60 s |
| Cooldown de reinicialização | 5 min |
| Janela de reinicializações | 1 h |
| Máximo de reinicializações por janela | 3 |
| Timeout do snapshot | 5 s |
| Nova resolução da URI de snapshot | 15 s |
| Falhas antes de tentar outro perfil | 2 |
| Ciclo da Prévia | 5 s |
| Atualização do painel | 3 s |
| Varredura automática | ativa |
| Alertas por e-mail | ativos |
| Limite de logs persistentes | 10.000 registros |

## Configurações persistentes

O firmware `0.13.0` usa o schema de configurações `4`:

- `GET /api/settings` lê valores, limites e token de escrita;
- `POST /api/settings` valida e salva alterações;
- `POST /api/settings/reset` apaga os valores atuais e restaura os padrões.

As configurações do schema `3` são migradas automaticamente, preservando os
valores existentes e adicionando `camera-watchdog` como hostname inicial.
Registros de outros layouts incompatíveis continuam sendo removidos e
substituídos pelos valores de fábrica.

## API HTTP

| Método | Rota | Finalidade |
|---|---|---|
| GET | `/` | painel embarcado |
| GET | `/health` | disponibilidade do ESP32 |
| GET | `/api/status` | diagnóstico completo |
| GET | `/api/logs?offset=0&limit=100` | página do histórico persistente |
| GET | `/api/logs/export` | exportação CSV completa em streaming |
| GET | `/api/camera/snapshot` | quadro JPEG da Prévia |
| GET | `/api/settings` | configurações e token de escrita |
| POST | `/api/settings` | salvar configurações |
| POST | `/api/settings/reset` | restaurar padrões |
| POST | `/api/actions/health-check` | executar verificação |
| POST | `/api/actions/rediscover` | redescobrir a câmera |
| POST | `/api/actions/goto-ptz-preset` | mover a câmera para um preset PTZ detectado |
| POST | `/api/actions/reboot-camera` | reiniciar a câmera por ONVIF |
| POST | `/api/actions/reset-reboot-limiter` | limpar o limitador |
| POST | `/api/actions/clear-logs` | zerar o histórico e preservar o marcador da limpeza |
| POST | `/api/actions/reboot-esp32` | reiniciar o ESP32 |

## Diagnóstico

Campos úteis de `GET /api/status`:

- `state`, `lastEvent` e `uptimeSeconds`;
- `wifi.hostname`, `wifi.localUrl` e `wifi.mdnsAvailable`;
- `camera.known`, `ip`, `onvifUrl` e `rtspPort`;
- `probes.onvif*` e `probes.rtsp*`;
- `snapshot.healthy`, `status`, `latencyMs`, `bytesRead`, `profile` e `detail`;
- `snapshot.previewFetchOperations`, `busyResponses`, `clientAborts` e `bytesTransferred`;
- `healthEvidence.source` e `ageSeconds`;
- `onvif.services`, `onvif.ptz`, `deviceInfo`, `clock` e `events`;
- `recovery.automatic`, `manual`, `acceptedHistory` e `lastResult`;
- `resources.heap`, `flash`, `psram` e `bluetoothMemory`;
- `counters` e `settings`.

## Solução de problemas

### A Prévia não apresenta imagem

- confira `snapshot.ready`, `status` e `detail` em `/api/status`;
- confirme que o perfil escolhido oferece snapshot JPEG;
- verifique usuário e senha ONVIF;
- ajuste `snapshotProfileMatch` para outro perfil;
- aumente o timeout apenas se a câmera responder lentamente;
- consulte `busyResponses` para identificar contenção temporária.

### O ESP32 não encontra a câmera

- confirme que ESP32 e câmera estão na mesma rede;
- verifique se multicast UDP 3702 está permitido;
- preencha `CAMERA_IP_HINT` quando o WS-Discovery não for confiável;
- confira as portas candidatas em `ONVIF_PORT_CANDIDATES`;
- consulte `lastDiscovery` e `lastSubnetScan` no diagnóstico.

### O endereço `.local` não abre

- confirme que o dispositivo usado para acessar o painel está na mesma rede do ESP32;
- verifique se a rede permite multicast mDNS entre os dispositivos;
- confirme no monitor serial a mensagem `mDNS: http://<nome-escolhido>.local/`;
- use o endereço IP exibido no monitor serial como alternativa.

### A câmera não reinicia automaticamente

Confira `recovery.autoRebootEnabled`, `credentialsConfigured`, `attemptsInWindow`, `maxAttemptsInWindow`, `snapshot.conclusive`, `profileFailoverPending` e `recovery.lastResult`.

### Memória ou alimentação instável

- acompanhe `resources.heap.minimumFreeBytes` e `largestBlockBytes`;
- confira `resources.chip.resetReason`;
- compare RSSI, heap e horário dos erros;
- use fonte e cabo adequados aos picos de corrente do ESP32.

## Limitações

- uma câmera monitorada por ESP32;
- snapshot HTTPS local da câmera não suportado;
- painel HTTP sem TLS;
- contadores operacionais e evidências recentes em RAM reiniciam no boot;
- registros anteriores ao primeiro sincronismo NTP usam uptime em vez de horário absoluto;
- varredura limitada a 254 hosts;
- Prévia dependente de `GetSnapshotUri` e de um JPEG válido.
- nomes personalizados do iCSee não são publicados por esta câmera via ONVIF;
- `GotoHomePosition` é aceito sem movimentar este modelo; o retorno Daemon usa o preset `100` confirmado no dispositivo.

## Segurança

- utilize o painel somente em uma rede local confiável;
- configure uma senha administrativa;
- não exponha a porta 80 diretamente à internet;
- mantenha `include/secrets.h` fora do repositório;
- trate `/api/status` como informação operacional sensível;
- limite o acesso ao ESP32 com regras de firewall quando necessário.

## Estrutura do projeto

```text
ESP32/
├── src/
│   ├── main.cpp
│   └── persistent_log.cpp
├── include/
│   ├── app_config.h
│   ├── dashboard_page.h
│   ├── persistent_log.h
│   ├── secrets.example.h
│   └── secrets.h
├── partitions.csv
├── platformio.ini
├── LICENSE
└── README.md
```

## Validação recomendada no dispositivo

1. confirme conexão Wi-Fi e endereço IP no monitor serial;
2. altere o endereço local, salve e abra o painel pelo novo nome mDNS;
3. confirme a descoberta ONVIF;
4. abra a Prévia e valide vários quadros consecutivos;
5. confira perfil, bytes e latência do JPEG;
6. salve configurações e confirme a persistência após reiniciar;
7. verifique que falhas inconclusivas não incrementam falhas JPEG conclusivas;
8. teste alertas e recuperação somente em ambiente controlado;
9. confirme cooldown e limite por janela;
10. acompanhe heap e RSSI durante uso prolongado.
11. confirme que o modal de posições PTZ aparece na aba Prévia, filtre a lista e teste o retorno ao ponto Daemon.
12. abra a aba Logs, gere tráfego de Prévia e health-check, confirme a ordem dos registros e exporte o CSV.
13. reinicie o ESP32 e confirme que os registros anteriores continuam disponíveis.

## Licença

MIT. Consulte `LICENSE`.
