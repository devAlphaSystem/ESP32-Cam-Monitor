# ESP32 Camera Watchdog

Firmware para ESP32 que monitora várias câmeras IP por ONVIF, verifica se continuam entregando snapshots JPEG válidos e oferece um painel web local para diagnóstico, prévia, configurações, logs e ações de recuperação.

A versão atual é a `0.17.0`. O navegador não acessa a câmera diretamente: o ESP32 descobre os serviços ONVIF, autentica as requisições, valida o JPEG e encaminha a imagem ao painel sem expor a senha da câmera.

## Principais recursos

- cadastro automático das câmeras anunciadas por ONVIF WS-Discovery, sem limite fixo de quantidade;
- fallback opcional por varredura limitada da sub-rede;
- seleção automática de perfil ONVIF e obtenção de `GetSnapshotUri`;
- autenticação Basic, Digest e WS-Security UsernameToken;
- usuário e senha da câmera configuráveis pelo painel, com persistência na NVS;
- validação do tamanho e dos marcadores `FF D8`/`FF D9` do JPEG;
- seletor central de câmera e prévia JPEG de uma câmera por vez, com até 2 FPS configuráveis;
- health-check periódico com estados online, instável, recuperando e offline;
- recuperação por `SystemReboot`, com tolerância, cooldown e limite por janela;
- catálogo e acionamento de até 128 presets PTZ;
- alertas de incidente, recuperação e mudança de IP pela API do Resend;
- configurações persistentes na NVS;
- até 7.500 warnings e erros persistentes, identificados por câmera, em uma partição LittleFS dedicada;
- painel responsivo com diagnóstico, recursos, logs, exportação CSV e ações administrativas.

## Visão geral do funcionamento

1. O ESP32 conecta-se ao Wi-Fi e sincroniza o relógio por NTP.
2. Os cadastros e ajustes de cada câmera são carregados da NVS. A descoberta atualiza endpoints e cadastra novas câmeras automaticamente.
3. Cada câmera mantém seu catálogo ONVIF, perfil JPEG, credenciais, contadores, incidente e limitador de reinícios. Os presets PTZ são consultados quando a prévia é utilizada.
4. O agendador alterna as câmeras com verificação pendente. Cada verificação usa ONVIF, RTSP quando necessário e validação JPEG.
5. Falhas consecutivas podem iniciar um incidente, enviar alertas, redescobrir a câmera e solicitar uma reinicialização ONVIF controlada.
6. O painel e a API HTTP exibem o estado atual; configurações e destinos descobertos permanecem na NVS.

O acesso às câmeras é serializado para evitar concorrência entre prévia, health-check, descoberta, PTZ e recuperação. As verificações vencidas têm prioridade sobre a prévia e tarefas de manutenção. Os intervalos padrão continuam iguais; o intervalo efetivo pode aumentar se a soma dos tempos das sondas exceder a capacidade do dispositivo.

Não existe uma constante de quantidade máxima de câmeras. RAM, espaço livre na NVS e tempo de rede são os limites físicos: o firmware recusa novos cadastros com uma mensagem explícita se não conseguir manter uma reserva de memória para operar. Isso não equivale a capacidade ilimitada no ESP32.

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
| `CAMERA_ONVIF_USERNAME_VALUE`    | não                | usuário inicial, usado antes de salvar credenciais no painel      |
| `CAMERA_ONVIF_PASSWORD_VALUE`    | não                | senha inicial, usada antes de salvar credenciais no painel        |
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

Wi-Fi, senha administrativa e dados do Resend ficam exclusivamente em `include/secrets.h` e não podem ser alterados pelo painel. As definições ONVIF desse arquivo são valores iniciais para importar o destino anterior ou o IP sugerido. Novas câmeras descobertas começam com credenciais próprias vazias; configure cada uma pelo seletor. Depois de salvar, todos os acessos à câmera usam as credenciais da NVS, inclusive quando removidas explicitamente.

### Ajustes persistentes pelo painel

Abra **Configurações** no painel. A aba **Global**, acima de Identificação, contém hostname, atualização do painel, Wi-Fi, descoberta, varredura, repetição de e-mail e LED compartilhados. As demais abas pertencem à câmera selecionada. Os valores globais ficam no namespace `camcfg`; os cadastros individuais, em `cameras`, no esquema `6`. A atualização importa a configuração do esquema `5` da versão 0.16 como a primeira câmera, preservando suas credenciais e ajustes. A maioria é aplicada imediatamente; o timeout inicial do Wi-Fi passa a valer no próximo boot.

Em **Autenticação da câmera**, informe o usuário e escolha entre manter a senha atual, definir/substituir a senha ou remover usuário e senha. A senha salva nunca é devolvida pela API; o painel recebe apenas a indicação de que há uma senha configurada. O campo de nova senha pode ficar vazio ao substituir, para câmeras que usam usuário sem senha. Para acesso sem autenticação, remova ambas as credenciais. Espaços são preservados, os limites são 64 bytes para o usuário e 128 bytes para a senha em UTF-8, e caracteres de controle são rejeitados; o usuário também não pode conter dois-pontos.

A gravação usa a autorização administrativa já existente. Alterações nas credenciais renovam a sessão JPEG e o catálogo ONVIF, descartam evidências da sessão anterior e antecipam a próxima verificação. Isso altera apenas o acesso usado pelo watchdog, não a conta cadastrada na câmera.

| Grupo         | Principais ajustes                                                    | Padrões                                  |
| ------------- | --------------------------------------------------------------------- | ---------------------------------------- |
| Identificação | nome da câmera, filtro do perfil, porta RTSP                          | `Camera N - IP`, `snap`, `554`           |
| Interface     | intervalo mínimo e ciclo da prévia, atualização do painel             | `5 s`, `5 s`, `3 s`                      |
| Monitoramento | health-check, tolerância inicial, redescoberta, offline e recuperação | `15 s`, `90 s`, `2`, `3`, `2`            |
| Incidentes    | falhas JPEG para alerta e reboot                                      | `2`, `3`                                 |
| Recuperação   | reboot automático, tolerância, cooldown, janela e cota                | ativo, `60 s`, `5 min`, `1 h`, `3`       |
| Rede          | varredura, descoberta e repetição de buscas                           | ativa, `3,5 s`, `30 s`, `5 min`          |
| Notificações  | alertas, mudança de IP e nova tentativa de e-mail                     | ativos, ativa, `60 s`                    |
| Comunicação   | TCP, ONVIF/RTSP, varredura, JPEG e nova resolução                     | `900 ms`, `3 s`, `300 ms`, `5 s`, `15 s` |
| Alerta local  | LED e intervalo da piscada                                            | ativo no GPIO `2`, `1 s`                 |

O backend também aplica estas relações:

- o limite para declarar offline não pode ser menor que o limite de redescoberta;
- o reboot não pode acontecer antes do alerta de imagem;
- a janela de reboots deve ser igual ou maior que o cooldown;
- o ciclo-alvo da prévia deve ser igual ou maior que seu intervalo mínimo.

O painel permite reduzir a prévia a um ciclo de `0,5 s`, com intervalo mínimo de `0,25 s`, resultando em no máximo 2 FPS. O padrão de `5 s` reduz tráfego e contenção com o health-check.

Use **Restaurar padrões** para restaurar apenas o escopo aberto: Global ou câmera selecionada. As credenciais da câmera são preservadas. A remoção dessas credenciais exige a opção explícita na seção de autenticação. Cada cadastro persiste ID estável, UUID ONVIF quando anunciado, endpoint e ajustes. Os dados antigos de `camwatch` são consultados apenas durante a importação inicial.

### Ajustes avançados de fábrica

Edite `include/app_config.h` quando precisar mudar um comportamento que não aparece no painel:

| Constante                    | Finalidade                                                    |
| ---------------------------- | ------------------------------------------------------------- |
| `CAMERA_IP_HINT`             | sugere um IP inicial quando não existe destino salvo          |
| `CAMERA_DAEMON_PRESET_TOKEN` | identifica o preset destacado como ponto Daemon; padrão `100` |
| `ONVIF_PORT_CANDIDATES`      | portas testadas para o Device Service                         |
| `DEFAULT_ONVIF_PATH`         | caminho ONVIF usado nos testes de fallback                    |
| `CAMERA_ALERT_LED_PIN`       | GPIO fixo do LED de alerta; padrão `2`                        |
| `MAX_SNAPSHOT_SIZE_BYTES`    | tamanho máximo aceito para um JPEG; padrão `512 KiB`          |
| `RELEASE_BLUETOOTH_MEMORY`   | libera a memória Bluetooth não utilizada no boot              |

Os demais valores de `app_config.h` são os padrões e limites usados pela configuração persistente. Mudá-los exige recompilar e gravar o firmware. Valores já salvos na NVS podem prevalecer até que os padrões sejam restaurados ou migrados.

## Uso do painel

- **Visão geral:** contagem por estado e tabela de todas as câmeras, com idade da última verificação.
- **Câmera:** a antiga visão geral individual, com disponibilidade, sondas ONVIF/RTSP, último JPEG e atividade recente.
- **Prévia:** snapshots JPEG, tela cheia e seleção pesquisável de presets PTZ.
- **Diagnóstico:** estado completo em JSON, com opção de copiar.
- **Logs:** warnings e erros de todas as câmeras, identificação de origem, filtros locais e CSV.
- **Recursos:** heap, fragmentação estimada, PSRAM, flash, Wi-Fi, catálogo ONVIF, relógio e eventos anunciados.
- **Ações:** verificar, reiniciar e limpar limitador da câmera selecionada; redescobrir todas; reiniciar todas em sequência, respeitando seus limitadores; limpar logs globais e reiniciar ESP32.
- **Configurações:** valores persistentes e limites de segurança.

Ações mutáveis exigem o token de escrita da sessão e, quando configurada, a senha administrativa. O token é recriado a cada boot.

## API multicâmera

- `GET /api/cameras`: resumo global e lista de IDs estáveis.
- `GET /api/status?cameraId=1`: estado completo da câmera.
- `GET /api/camera/snapshot?cameraId=1`: prévia da câmera.
- `GET/POST /api/settings?scope=global`: ajustes compartilhados. No POST, envie `scope` no formulário.
- `GET/POST /api/settings?scope=camera&cameraId=1`: ajustes individuais. No POST, envie ambos no formulário.
- Ações individuais e PTZ exigem `cameraId` explícito; IDs ausentes ou desconhecidos são rejeitados.
- `POST /api/actions/rediscover` e `POST /api/actions/reboot-all-cameras` são globais.

POSTs continuam exigindo `writeToken` e a senha administrativa quando configurada. Não há uma câmera selecionada global no servidor: cada navegador envia o ID de sua própria seleção.

## Logs persistentes

A partição `logs` possui `960 KiB` e usa LittleFS. O arquivo circular armazena até 7.500 registros de 80 bytes; quando fica cheio, os registros mais antigos são sobrescritos. A folga restante é reservada para as operações copy-on-write do sistema de arquivos. As gravações comuns são agrupadas por até um segundo e eventos críticos forçam a persistência.

Somente níveis Warning e Error são gravados. Sucessos rotineiros e o desafio inicial de autenticação Digest não ocupam o histórico. O registro binário continua com 80 bytes, no esquema 3, incluindo o ID da câmera. Registros antigos continuam legíveis, mas não recebem uma identificação de câmera inventada. Se uma escrita for interrompida ou a ocupação do LittleFS ficar insegura, a partição exclusiva de logs é reconstruída automaticamente no próximo boot para evitar um ciclo de reinicialização.

O histórico inclui avisos e falhas de Wi-Fi, descoberta, ONVIF, RTSP, snapshots, verificações, incidentes, PTZ, e-mails e ações administrativas. Corpos SOAP, conteúdo do JPEG, senhas e tokens de autenticação não são gravados.

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
- confira as portas em `ONVIF_PORT_CANDIDATES`;
- use **Ações > Redescobrir câmeras** e consulte os logs.

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

Na atualização 0.17.0, a compilação e a gravação foram verificadas em um ESP32 Devkit V1 com uma câmera real. Foram exercitados importação da configuração anterior, ONVIF, JPEG e prévia, gravação isolada por escopo, rejeição de IDs inválidos, redescoberta sem duplicação, reinício coletivo com uma câmera, bloqueio por cooldown e persistência após reiniciar o ESP32. Os ajustes usados nos testes foram restaurados. O leitor independente de logs também foi compilado, sem substituir o firmware principal na placa.

A interface foi exercitada com quinze câmeras simuladas, incluindo seleção direta, navegação circular, formulário individual/global, envio de ação para a câmera 15 e disposição em tela estreita. Isso valida o painel, não a capacidade de monitorar quinze equipamentos físicos. A interação do diálogo nativo de descarte ficou bloqueada na ferramenta do navegador embutido; a preservação das alterações ao cancelar foi validada pelo teste automatizado.

O teste de regressão utiliza o JavaScript real embarcado e verifica também respostas de configuração fora de ordem:

```bash
node tests/camera_dashboard.test.cjs
```

Ainda é necessária uma bancada com várias câmeras reais para medir concorrência de rede, atrasos sob falhas simultâneas, consumo de memória por modelos distintos e cadastro automático simultâneo. O teste com uma câmera não comprova esses cenários.

1. confirme no monitor serial o Wi-Fi, o IP e a versão `0.17.0`;
2. abra o painel pelo IP e, se disponível, pelo endereço `.local`;
3. confirme a descoberta ONVIF e vários snapshots consecutivos;
4. salve uma configuração, reinicie e confira a persistência na NVS;
5. valide PTZ somente se a câmera anunciar o serviço e seus presets;
6. teste e-mails e recuperação automática apenas em ambiente controlado;
7. exporte os logs e confirme que continuam disponíveis após reiniciar o ESP32.
