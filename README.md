# ESP32 Camera Watchdog

Firmware para ESP32 que monitora uma câmera IP por ONVIF, verifica se ela continua entregando snapshots JPEG válidos e oferece um painel web local para diagnóstico, prévia, configurações, logs e ações de recuperação.

A versão atual é a `0.14`. O navegador não acessa a câmera diretamente: o ESP32 descobre os serviços ONVIF, autentica as requisições, valida o JPEG e encaminha a imagem ao painel sem expor as credenciais da câmera.

## Principais recursos

- descoberta da câmera por ONVIF WS-Discovery;
- fallback opcional por varredura limitada da sub-rede;
- seleção automática de perfil ONVIF e obtenção de `GetSnapshotUri`;
- autenticação Basic, Digest e WS-Security UsernameToken;
- validação do tamanho e dos marcadores `FF D8`/`FF D9` do JPEG;
- prévia JPEG intermediada pelo ESP32, com até 2 FPS configuráveis;
- health-check periódico com estados online, instável, recuperando e offline;
- recuperação por `SystemReboot`, com tolerância, cooldown e limite por janela;
- catálogo e acionamento de até 128 presets PTZ;
- alertas de incidente, recuperação e mudança de IP pela API do Resend;
- configurações persistentes na NVS;
- até 7.500 logs circulares persistentes em uma partição LittleFS dedicada;
- painel responsivo com diagnóstico, recursos, logs, exportação CSV e ações administrativas.

## Visão geral do funcionamento

1. O ESP32 conecta-se ao Wi-Fi e sincroniza o relógio por NTP.
2. O último endpoint da câmera é carregado da NVS. Se não existir, o firmware usa o IP sugerido ou executa WS-Discovery.
3. O catálogo ONVIF, os perfis de mídia, a URI de snapshot e os presets PTZ são consultados.
4. A cada health-check, o firmware verifica ONVIF, usa RTSP como sonda auxiliar quando necessário e valida um JPEG.
5. Falhas consecutivas podem iniciar um incidente, enviar alertas, redescobrir a câmera e solicitar uma reinicialização ONVIF controlada.
6. O painel e a API HTTP exibem o estado atual; configurações e destinos descobertos permanecem na NVS.

O acesso à câmera é serializado para evitar concorrência entre prévia, health-check, descoberta, PTZ e recuperação.

## Requisitos

### Hardware

- placa ESP32 compatível com o perfil PlatformIO `esp32dev`;
- pelo menos 4 MB de flash, exigidos por `partitions.csv`;
- cabo USB com transferência de dados;
- rede Wi-Fi de 2,4 GHz acessível ao ESP32.

### Ambiente de desenvolvimento

- Visual Studio Code com a extensão PlatformIO IDE; ou
- PlatformIO Core disponível pelo comando `pio`.

O projeto fixa `platformio/espressif32@7.0.1`, usa o framework Arduino e não declara bibliotecas externas em `lib_deps`.

### Câmera

A câmera deve oferecer:

- ONVIF Device Service por HTTP;
- ONVIF Media1 com `GetProfiles` e `GetSnapshotUri`;
- snapshot JPEG por HTTP;
- credenciais com permissão de leitura, quando exigidas.

PTZ, serviço Events e `SystemReboot` são opcionais. Snapshots locais por HTTPS não são suportados pelo firmware atual.

## Instalação

### 1. Abra o projeto no PlatformIO

Abra a pasta raiz, onde estão `platformio.ini`, `partitions.csv`, `include/` e `src/`.

No VS Code, instale a extensão recomendada `platformio.platformio-ide` e use **PlatformIO: Open Project**. Pelo terminal, execute os comandos abaixo a partir desta pasta.

### 2. Crie o arquivo de segredos

Copie `include/secrets.example.h` para `include/secrets.h`. Este arquivo já está ignorado por `.gitignore` e não deve ser versionado.

Preencha os valores locais:

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

Campos:

| Campo                            | Obrigatório        | Uso                                                               |
| -------------------------------- | ------------------ | ----------------------------------------------------------------- |
| `WIFI_SSID`                      | sim                | nome da rede Wi-Fi de 2,4 GHz                                     |
| `WIFI_PASSWORD`                  | sim                | senha da rede                                                     |
| `CAMERA_ONVIF_USERNAME_VALUE`    | conforme a câmera  | usuário ONVIF e do snapshot                                       |
| `CAMERA_ONVIF_PASSWORD_VALUE`    | conforme a câmera  | senha ONVIF e do snapshot                                         |
| `DASHBOARD_ADMIN_PASSWORD_VALUE` | recomendado        | protege gravações e ações administrativas; vazio desativa a senha |
| `RESEND_API_KEY`                 | não                | chave da API; deixe vazia para não enviar e-mails                 |
| `RESEND_FROM`                    | apenas para e-mail | remetente aceito pela conta Resend                                |
| `ALERT_EMAIL`                    | apenas para e-mail | destinatário dos alertas                                          |

Alterações em `secrets.h` exigem nova compilação e gravação do firmware.

### 3. Ajuste a placa e a porta serial

O ambiente padrão de `platformio.ini` é:

```ini
[env:esp32dev]
platform = platformio/espressif32@7.0.1
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_port = COM5
upload_speed = 460800
board_build.partitions = partitions.csv
```

Troque `upload_port = COM5` pela porta usada no seu computador. A linha também pode ser removida para o PlatformIO tentar detectar a porta automaticamente. Se a placa não for um ESP32 Dev Module compatível, ajuste `board` somente depois de confirmar o modelo e a flash disponível.

### 4. Compile, grave e abra o monitor serial

```bash
pio run
pio run -t upload
pio device monitor
```

O monitor usa `115200` baud. Durante a inicialização ele informa versão, motivo do reset, conexão Wi-Fi, IP, resultado da descoberta e endereços do painel.

### 5. Acesse o painel

Depois da conexão ao Wi-Fi, abra:

```text
http://IP_DO_ESP32/
http://camera-watchdog.local/
```

O segundo endereço depende de suporte a mDNS na rede e no dispositivo cliente. Se `.local` não resolver, use o IP exibido no monitor serial.

## Configuração

Existem três grupos de configuração, com responsabilidades diferentes.

### Segredos compilados

Wi-Fi, credenciais ONVIF, senha administrativa e dados do Resend ficam exclusivamente em `include/secrets.h`. Eles não são enviados ao navegador nem podem ser alterados pelo painel.

### Ajustes persistentes pelo painel

Abra **Configurações** no painel. Ao salvar, os valores são validados e gravados no namespace `camcfg` da NVS, usando o schema `4`. A maioria é aplicada imediatamente; o timeout inicial do Wi-Fi passa a valer no próximo boot.

| Grupo         | Principais ajustes                                                    | Padrões                                                 |
| ------------- | --------------------------------------------------------------------- | ------------------------------------------------------- |
| Identificação | nome, hostname mDNS, filtro do perfil, porta RTSP                     | `Camera de seguranca`, `camera-watchdog`, `snap`, `554` |
| Interface     | intervalo mínimo e ciclo da prévia, atualização do painel             | `5 s`, `5 s`, `3 s`                                     |
| Monitoramento | health-check, tolerância inicial, redescoberta, offline e recuperação | `15 s`, `90 s`, `2`, `3`, `2`                           |
| Incidentes    | falhas JPEG para alerta e reboot                                      | `2`, `3`                                                |
| Recuperação   | reboot automático, tolerância, cooldown, janela e cota                | ativo, `60 s`, `5 min`, `1 h`, `3`                      |
| Rede          | varredura, descoberta e repetição de buscas                           | ativa, `3,5 s`, `30 s`, `5 min`                         |
| Notificações  | alertas, mudança de IP e nova tentativa de e-mail                     | ativos, ativa, `60 s`                                   |
| Comunicação   | TCP, ONVIF/RTSP, varredura, JPEG e nova resolução                     | `900 ms`, `3 s`, `300 ms`, `5 s`, `15 s`                |
| Alerta local  | LED e intervalo da piscada                                            | ativo no GPIO `2`, `1 s`                                |

O backend também aplica estas relações:

- o limite para declarar offline não pode ser menor que o limite de redescoberta;
- o reboot não pode acontecer antes do alerta de imagem;
- a janela de reboots deve ser igual ou maior que o cooldown;
- o ciclo-alvo da prévia deve ser igual ou maior que seu intervalo mínimo.

O painel permite reduzir a prévia a um ciclo de `0,5 s`, com intervalo mínimo de `0,25 s`, resultando em no máximo 2 FPS. O padrão de `5 s` reduz tráfego e contenção com o health-check.

Use **Restaurar padrões** para limpar apenas os ajustes operacionais e voltar aos valores de `include/app_config.h`. Segredos não são alterados. O endpoint de câmera descoberto é armazenado separadamente no namespace `camwatch` para reaproveitamento após reinicializações.

### Ajustes avançados de fábrica

Edite `include/app_config.h` quando precisar mudar um comportamento que não aparece no painel:

| Constante                    | Finalidade                                                     |
| ---------------------------- | -------------------------------------------------------------- |
| `CAMERA_MATCH`               | filtra respostas WS-Discovery pelo texto anunciado pela câmera |
| `CAMERA_IP_HINT`             | sugere um IP inicial quando não existe destino salvo           |
| `CAMERA_DAEMON_PRESET_TOKEN` | identifica o preset destacado como ponto Daemon; padrão `100`  |
| `ONVIF_PORT_CANDIDATES`      | portas testadas para o Device Service                          |
| `DEFAULT_ONVIF_PATH`         | caminho ONVIF usado nos testes de fallback                     |
| `CAMERA_ALERT_LED_PIN`       | GPIO fixo do LED de alerta; padrão `2`                         |
| `MAX_SNAPSHOT_SIZE_BYTES`    | tamanho máximo aceito para um JPEG; padrão `512 KiB`           |
| `RELEASE_BLUETOOTH_MEMORY`   | libera a memória Bluetooth não utilizada no boot               |

Os demais valores de `app_config.h` são os padrões e limites usados pela configuração persistente. Mudá-los exige recompilar e gravar o firmware. Valores já salvos na NVS podem prevalecer até que os padrões sejam restaurados ou migrados.

## Uso do painel

- **Câmera:** estado, disponibilidade, sondas ONVIF/RTSP, último JPEG e atividade recente.
- **Prévia:** snapshots JPEG, tela cheia e seleção pesquisável de presets PTZ.
- **Recursos:** heap, fragmentação estimada, PSRAM, flash, Wi-Fi, catálogo ONVIF, relógio e eventos anunciados.
- **Logs:** histórico paginado, filtros locais e exportação CSV completa.
- **Configurações:** valores persistentes e limites de segurança.
- **Ações:** health-check, redescoberta, reboot da câmera, limpeza do limitador, limpeza dos logs e reboot do ESP32.

Ações mutáveis exigem o token de escrita da sessão e, quando configurada, a senha administrativa. O token é recriado a cada boot.

## Logs persistentes

A partição `logs` possui `960 KiB` e usa LittleFS. O arquivo circular armazena até 7.500 registros de 80 bytes; quando fica cheio, os registros mais antigos são sobrescritos. A folga restante é reservada para as operações copy-on-write do sistema de arquivos. As gravações comuns são agrupadas por até um segundo e eventos críticos forçam a persistência.

Snapshots bem-sucedidos são amostrados no histórico no máximo uma vez por minuto; falhas continuam sendo registradas imediatamente. Se uma escrita for interrompida ou a ocupação do LittleFS ficar insegura, a partição exclusiva de logs é reconstruída automaticamente no próximo boot para evitar um ciclo de reinicialização.

O histórico inclui boot, Wi-Fi, descoberta, conexões TCP, ONVIF, RTSP, snapshots, health-checks, incidentes, configurações, PTZ, e-mails e ações administrativas. Corpos SOAP, conteúdo do JPEG, senhas e tokens de autenticação não são gravados.

Ao usar **Zerar logs**, o histórico anterior é removido e um novo marcador registra a limpeza e a quantidade excluída.

## Portas e conectividade

| Porta      | Direção             | Finalidade                     |
| ---------- | ------------------- | ------------------------------ |
| `80/TCP`   | entrada no ESP32    | painel e API HTTP              |
| `3702/UDP` | rede local          | ONVIF WS-Discovery multicast   |
| `5353/UDP` | rede local          | resolução mDNS de `*.local`    |
| `554/TCP`  | saída, configurável | sonda RTSP e apoio à varredura |
| `123/UDP`  | saída               | sincronização NTP              |
| `443/TCP`  | saída, opcional     | envio de e-mails pelo Resend   |

As portas ONVIF e do snapshot são obtidas da câmera. ESP32 e câmera devem estar na mesma rede alcançável; WS-Discovery e mDNS dependem de multicast permitido pela rede.

## Segurança

- mantenha `include/secrets.h` fora do controle de versão;
- defina `DASHBOARD_ADMIN_PASSWORD_VALUE` em redes com outros usuários;
- não exponha a porta `80` do ESP32 diretamente à internet;
- considere `/api/status`, `/api/logs` e a prévia como informações internas da rede;
- o painel usa HTTP sem TLS e as rotas de leitura não exigem login;
- o cliente HTTPS do Resend está configurado com `setInsecure()`, portanto não valida o certificado remoto no estado atual do código.

## Solução de problemas

### `secrets.h` não encontrado ao compilar

Crie `include/secrets.h` a partir de `include/secrets.example.h` e preencha ao menos o Wi-Fi.

### Não foi possível gravar a placa

- confirme a porta em `upload_port`;
- feche outros monitores seriais que estejam usando a porta;
- confira o cabo USB e o driver da interface serial;
- em algumas placas, mantenha `BOOT` pressionado ao iniciar a gravação.

### O ESP32 não conecta ao Wi-Fi

- confirme SSID e senha;
- use uma rede de 2,4 GHz;
- verifique no monitor serial o status e o IP obtido;
- aproxime a placa do ponto de acesso para descartar sinal fraco.

### A câmera não é localizada

- confirme que ESP32 e câmera estão na mesma rede;
- permita multicast UDP `3702`;
- preencha `CAMERA_IP_HINT` se o WS-Discovery não for confiável;
- ajuste `CAMERA_MATCH` quando houver várias câmeras;
- confira as portas em `ONVIF_PORT_CANDIDATES`;
- use **Ações > Redescobrir câmera** e consulte os logs.

### A prévia não mostra imagem

- confirme usuário e senha ONVIF;
- verifique se `GetSnapshotUri` retorna JPEG por HTTP, não HTTPS;
- teste outro filtro em **Perfil ONVIF preferido**;
- aumente o timeout do JPEG apenas se a câmera responder lentamente;
- consulte o detalhe do snapshot e as respostas HTTP no painel.

### O endereço `.local` não abre

Use o IP mostrado no monitor serial. Verifique também se mDNS/multicast está liberado entre o dispositivo cliente e o ESP32.

### Os e-mails não são enviados

- confirme que `RESEND_API_KEY` começa com `re_`;
- use remetente e destinatário aceitos pela conta Resend;
- confirme acesso do ESP32 à internet pela porta `443`;
- consulte **Câmera > Envio de e-mail** e os logs de notificações.

## Estrutura do projeto

```text
ESP32/
├── include/
│   ├── app_config.h           # padrões e limites de fábrica
│   ├── persistent_log.h       # tipos e API do log circular
│   ├── runtime_settings.h     # estrutura das configurações persistentes
│   ├── secrets.example.h      # modelo sem credenciais
│   └── secrets.h              # credenciais locais, ignorado pelo Git
├── src/
│   ├── main.cpp               # ponte para setup/loop do firmware
│   ├── firmware_internal.h    # estado e contratos internos compartilhados
│   ├── firmware_core.cpp      # estado global, NVS, segurança e e-mails
│   ├── camera_services.cpp    # ONVIF, snapshot, PTZ, RTSP e descoberta
│   ├── watchdog_app.cpp       # health-check, Wi-Fi e ciclo principal
│   ├── dashboard_server.cpp   # API HTTP e ações administrativas
│   ├── dashboard_page.cpp     # HTML, CSS e JavaScript embarcados
│   ├── persistent_log.cpp     # armazenamento LittleFS circular
│   ├── persistent_log_types.cpp
│   └── runtime_settings.cpp   # valores padrão em tempo de execução
├── partitions.csv             # NVS, firmware e partição dedicada de logs
└── platformio.ini             # ambiente PlatformIO
```

## Validação recomendada após a instalação

1. confirme no monitor serial o Wi-Fi, o IP e a versão `0.14.4`;
2. abra o painel pelo IP e, se disponível, pelo endereço `.local`;
3. confirme a descoberta ONVIF e vários snapshots consecutivos;
4. salve uma configuração, reinicie e confira a persistência na NVS;
5. valide PTZ somente se a câmera anunciar o serviço e seus presets;
6. teste e-mails e recuperação automática apenas em ambiente controlado;
7. exporte os logs e confirme que continuam disponíveis após reiniciar o ESP32.
