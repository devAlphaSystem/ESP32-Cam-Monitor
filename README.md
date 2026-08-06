# ESP32 Camera Watchdog — firmware 0.4.0

Esta atualização corrige o falso estado **online** causado por portas TCP abertas sem vídeo. O watchdog agora baixa e valida periodicamente um JPEG real da câmera, diferencia falha de imagem de indisponibilidade total e pode solicitar uma reinicialização ONVIF automática.

## Arquivos para substituir

Copie os arquivos desta pasta sobre o projeto atual:

```text
src/main.cpp
include/app_config.h
include/dashboard_page.h
include/secrets.example.h
platformio.ini
```

Mantenha o seu arquivo `include/secrets.h`; não o substitua pelo exemplo, pois ele contém as suas credenciais de Wi-Fi e Resend.

O `platformio.ini` continua referenciando `partitions.csv`. Preserve o arquivo `partitions.csv` que já existe no seu projeto.

## Credenciais ONVIF

O firmware continua compilando mesmo que o seu `secrets.h` antigo não contenha credenciais da câmera. Para permitir snapshots autenticados e o comando `SystemReboot`, acrescente ao `include/secrets.h`:

```cpp
#define CAMERA_ONVIF_USERNAME_VALUE "admin"
#define CAMERA_ONVIF_PASSWORD_VALUE "senha-da-camera"
```

Use o usuário administrador configurado para ONVIF na câmera. Ele pode ser diferente da conta usada no aplicativo iCSee.

O cliente tenta, conforme a resposta da câmera:

- WS-Security UsernameToken com `PasswordDigest` nas mensagens SOAP;
- HTTP Basic;
- HTTP Digest com algoritmo MD5 e `qop=auth`.

As credenciais não são devolvidas pela API local nem exibidas no painel.

## Comportamento padrão

- Teste completo de ONVIF, RTSP e JPEG a cada 30 segundos.
- Um teste de imagem somente passa quando recebe um JPEG com início `FF D8`, final `FF D9` e tamanho mínimo válido.
- Uma porta TCP aberta, sozinha, não classifica mais a câmera como online.
- Após três falhas consecutivas de imagem, o firmware gera o alerta e tenta `ONVIF SystemReboot` quando o serviço ONVIF está respondendo e a falha aparenta ser operacional.
- Erros claros de autenticação, recurso não suportado ou configuração não disparam reboot automático.
- Depois de um reboot aceito, aguarda 90 segundos antes de considerar nova falha.
- Há cooldown de 15 minutos entre reinicializações.
- O limite é de três tentativas por janela de 24 horas de uptime do ESP32.
- A prévia web solicita um quadro aproximadamente a cada 2,5 segundos, em vez de pressionar continuamente o encoder da câmera.
- Quando um quadro falha, a imagem anterior é ocultada para não parecer que a prévia continua atualizada.

Todos esses valores podem ser alterados em `include/app_config.h`.

## Estados exibidos

- `Online`: um JPEG completo foi validado.
- `Instável`: a câmera está sendo confirmada, ou responde ao controle mas não entrega imagem.
- `Recuperando`: o comando ONVIF de reinicialização foi aceito e o ESP32 está aguardando o boot da câmera.
- `Offline`: imagem, ONVIF e RTSP permaneceram indisponíveis após as confirmações configuradas.

O painel agora mostra o resultado do último teste JPEG, bytes recebidos, latência, falhas consecutivas e histórico da recuperação automática.

## Compilar e enviar

No terminal do projeto:

```bash
pio run
pio run --target upload
pio device monitor
```

No VS Code também é possível usar os comandos **PlatformIO: Build**, **Upload** e **Monitor**.

As mensagens principais do monitor serial são:

```text
CAMERA OK: ... JPEG=... bytes
VIDEO FALHOU: ...
SystemReboot aceito
SystemReboot rejeitado
CAMERA SEM CONTROLE E SEM IMAGEM: ...
```

## Limitações

`SystemReboot` depende da implementação ONVIF da câmera e normalmente exige usuário administrador. Alguns firmwares baratos implementam consulta ONVIF, mas não implementam reinicialização remota.

Quando o serviço ONVIF também trava, nenhum comando de rede consegue recuperar o equipamento. Nesse cenário, o fallback efetivo continua sendo um power-cycle por relé ou MOSFET, com o ESP32 alimentado separadamente da câmera. Esse controle elétrico não foi incluído nesta versão porque depende da tensão, corrente, fonte e circuito usados na instalação.

## Validação realizada

Os arquivos foram verificados com análise sintática C++11 usando interfaces equivalentes às APIs do Arduino-ESP32, conferência das APIs usadas no core 2.0.17, verificação sintática do JavaScript do painel e checagem de integridade dos arquivos do pacote. A compilação final e o teste do comando ONVIF precisam ser feitos na sua placa e na sua câmera, pois dependem do firmware específico do equipamento.
