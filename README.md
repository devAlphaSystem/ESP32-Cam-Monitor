# ESP32 Camera Watchdog — firmware 0.5.0

A versão 0.5.0 mantém o monitoramento ONVIF/RTSP/JPEG e a recuperação automática da versão 0.4.0, acrescentando uma aba **Configurações** com valores persistentes na NVS, validação de limites e ações administrativas.

## Arquivos para substituir

Copie estes arquivos sobre o projeto atual:

```text
src/main.cpp
include/app_config.h
include/dashboard_page.h
include/secrets.example.h
platformio.ini
```

Mantenha os arquivos abaixo do seu projeto:

```text
include/secrets.h
partitions.csv
```

O `secrets.h` contém credenciais reais e não deve ser substituído pelo arquivo de exemplo. O `partitions.csv` continua sendo referenciado pelo `platformio.ini`.

## Nova aba Configurações

O painel passa a ter quatro abas:

```text
Câmera | Prévia | Recursos | Configurações
```

A nova aba permite ajustar, sem recompilar o firmware:

- nome da câmera, perfil ONVIF preferido e porta RTSP;
- intervalo das verificações e quantidades de falhas/sucessos;
- recuperação automática por `ONVIF SystemReboot`;
- cooldown, janela e limite de reinicializações;
- descoberta ONVIF e varredura da sub-rede;
- ativação dos alertas e aviso de mudança de IP;
- timeouts TCP, ONVIF, RTSP e JPEG;
- intervalo da prévia, atualização do painel e LED local.

As credenciais de Wi-Fi, ONVIF e Resend continuam somente no `secrets.h` e nunca são retornadas pela API de configurações.

## Valores padrão principais

| Configuração | Padrão | Limite aceito |
|---|---:|---:|
| Verificação completa | 30 s | 10 s a 60 min |
| Tolerância após ligar | 45 s | 0 a 10 min |
| Falhas para redescobrir | 2 | 1 a 20 |
| Falhas para declarar offline | 3 | 1 a 20 |
| Sucessos para confirmar recuperação | 2 | 1 a 10 |
| Falhas JPEG para alerta | 3 | 1 a 20 |
| Falhas JPEG para reboot | 3 | 1 a 20 |
| Tolerância após reboot | 90 s | 30 s a 10 min |
| Cooldown entre reboots | 15 min | 5 min a 24 h |
| Janela de reboots | 24 h | 1 h a 7 dias |
| Máximo de reboots por janela | 3 | 1 a 20 |
| WS-Discovery | 3,5 s | 1 s a 15 s |
| Redescoberta | 60 s | 30 s a 60 min |
| Varredura da sub-rede | 5 min | 1 min a 24 h |
| Timeout TCP | 900 ms | 200 ms a 5 s |
| Resposta ONVIF/RTSP | 1,5 s | 500 ms a 10 s |
| Timeout JPEG | 7 s | 1 s a 30 s |
| Ciclo-alvo da prévia | 2,5 s | 1 s a 30 s |
| Atualização do painel | 3 s | 1 s a 30 s |

Com os padrões, uma indisponibilidade contínua de imagem é confirmada em aproximadamente 90 segundos. O painel recalcula essa estimativa enquanto os campos são editados.

## Regras de segurança entre configurações

Além dos limites individuais, o firmware rejeita a gravação inteira quando alguma combinação é incoerente:

- a quantidade de falhas para offline não pode ser menor que a quantidade usada para redescoberta;
- o reboot não pode ocorrer antes do alerta de imagem;
- a janela de reboots deve ser igual ou maior que o cooldown;
- o ciclo-alvo da prévia deve ser igual ou maior que o intervalo mínimo;
- textos com caracteres de controle ou acima do tamanho máximo são recusados.

A validação é feita novamente no ESP32. Alterar o HTML ou enviar uma requisição manual não ignora esses limites. Se um campo for inválido, nenhuma configuração é aplicada ou persistida.

## Persistência na NVS

As configurações são gravadas como um único bloco em um namespace separado chamado `camcfg`. O bloco contém:

- identificador do formato;
- versão do esquema;
- tamanho esperado;
- valores persistentes;
- checksum de integridade.

Quando o bloco estiver ausente, corrompido, incompatível ou fora dos limites, o firmware inicia com os valores de fábrica. O alvo ONVIF descoberto continua salvo separadamente no namespace usado pelas versões anteriores.

O botão **Restaurar padrões** apaga somente as configurações ajustáveis. Ele não apaga:

- Wi-Fi;
- credenciais ONVIF;
- chave do Resend;
- destinatário dos alertas;
- câmera ONVIF descoberta.

## Proteção das alterações

Toda operação de escrita exige um token aleatório gerado a cada boot. O navegador obtém esse token pela mesma origem do painel e o envia nas operações administrativas.

Também é possível exigir uma senha. Acrescente ao seu `include/secrets.h`:

```cpp
#define DASHBOARD_ADMIN_PASSWORD_VALUE "uma-senha-forte"
```

Quando a macro estiver vazia ou ausente, o firmware continua compatível com o `secrets.h` da versão 0.4.0 e não solicita senha. O token por boot continua obrigatório.

A senha administrativa:

- não é armazenada na NVS;
- não é devolvida pela API;
- não é salva pelo JavaScript no navegador;
- protege gravações, restauração de padrões e ações administrativas;
- não bloqueia as abas de monitoramento somente leitura.

O painel foi projetado para uso na rede local. Não o exponha diretamente à internet.

## Credenciais ONVIF

Para snapshots autenticados e `SystemReboot`, preserve estas macros no `include/secrets.h`:

```cpp
#define CAMERA_ONVIF_USERNAME_VALUE "admin"
#define CAMERA_ONVIF_PASSWORD_VALUE "senha-da-camera"
```

O cliente tenta, conforme a resposta da câmera:

- WS-Security UsernameToken com `PasswordDigest` nas mensagens SOAP;
- HTTP Basic;
- HTTP Digest com algoritmo MD5 e `qop=auth`.

## Ações administrativas

A aba Configurações oferece:

- **Verificar agora**: executa ONVIF, RTSP e validação de um JPEG;
- **Redescobrir câmera**: repete WS-Discovery e confirma o endpoint;
- **Reiniciar câmera**: envia `ONVIF SystemReboot`;
- **Limpar limitador**: zera cooldown e tentativas da janela atual;
- **Reiniciar ESP32**: reinicia somente o watchdog.

As ações são enfileiradas e executadas pelo `loop()` depois que a resposta HTTP é enviada. O reboot manual da câmera ignora apenas a opção “reboot automático ativado”; cooldown e limite por janela continuam sendo respeitados.

## Endpoints locais

```text
GET  /api/status
GET  /api/settings
POST /api/settings
POST /api/settings/reset
POST /api/actions/health-check
POST /api/actions/rediscover
POST /api/actions/reboot-camera
POST /api/actions/reset-reboot-limiter
POST /api/actions/reboot-esp32
GET  /api/camera/snapshot
GET  /health
```

Os endpoints de escrita aceitam `application/x-www-form-urlencoded` e exigem `writeToken`. Quando configurada, também exigem `adminPassword`.

## Aplicação das alterações

A maioria dos valores é aplicada imediatamente:

- nome, alertas, timeouts, limites, LED e ritmos do painel;
- mudança de perfil invalida o URI JPEG e força nova resolução;
- mudança da porta RTSP atualiza o alvo persistido da câmera;
- desativar e-mails cancela notificações pendentes;
- mudar o intervalo das verificações inicia uma nova espera completa.

O timeout inicial do Wi-Fi é usado no próximo boot, porque a conexão inicial já ocorreu quando a aba é aberta.

## Compilar e enviar

No terminal do projeto:

```bash
pio run
pio run --target upload
pio device monitor
```

Também é possível usar os comandos **PlatformIO: Build**, **Upload** e **Monitor** no VS Code.

## Uso de memória

A página HTML/CSS/JavaScript permanece em `PROGMEM` e é transmitida diretamente da flash. A estrutura persistente de configurações possui limite estático de 256 bytes. Strings maiores, como os JSONs das APIs, são criadas apenas durante a requisição e liberadas depois.

## Limitações

`SystemReboot` depende da implementação ONVIF da câmera. Alguns firmwares respondem a consultas de leitura, mas recusam ou não implementam reinicialização remota.

Quando o serviço ONVIF também trava, nenhum comando de rede consegue recuperar a câmera. Nesse caso, o fallback confiável continua sendo um power-cycle por relé ou MOSFET corretamente dimensionado, mantendo o ESP32 em alimentação separada.

## Validação realizada

Antes do empacotamento foram executados:

- análise sintática C++11 com stubs equivalentes às interfaces usadas do Arduino-ESP32 2.0.17;
- conferência dos métodos utilizados em `Preferences`, `WebServer` e `ESP`;
- verificação sintática do JavaScript com Node.js;
- checagem de IDs, rótulos, painéis e referências do DOM;
- conferência de integridade e hashes SHA-256 do pacote.

A compilação definitiva e o teste funcional precisam ser feitos na placa e na câmera, pois dependem do `partitions.csv`, do hardware e do firmware ONVIF específicos da instalação.
