# ESP32 Camera Watchdog

O Camera Watchdog roda em um ESP32 ligado à sua rede local e vigia câmeras IP com ONVIF. Ele verifica a câmera a cada poucos segundos, avisa por e-mail quando ela falha e reinicia a câmera sozinho quando ela trava. Tudo é acompanhado por um painel web servido pelo próprio ESP32.

## O que o firmware faz

- Encontra câmeras ONVIF na rede por WS-Discovery e guarda o cadastro na memória do ESP32.
- Testa ONVIF, RTSP e a captura de imagem JPEG de cada câmera e classifica o estado como online, instável, recuperando ou offline.
- Reinicia a câmera pelo comando ONVIF SystemReboot depois de falhas confirmadas. O intervalo mínimo entre reinícios e o limite por hora são configuráveis.
- Envia alertas por e-mail pelo serviço Resend quando a câmera cai, volta ou muda de IP.
- Mostra o vídeo ao vivo em H.264 no navegador, com até dois espectadores ao mesmo tempo.
- Comanda posições PTZ salvas na câmera.
- Guarda um histórico de até 7.500 avisos e erros em uma partição própria. O histórico sobrevive a reinícios e pode ser exportado em CSV.
- Abre uma rede Wi-Fi própria para configuração quando não tem Wi-Fi configurado ou perde a conexão por mais de 2 minutos.
- Pisca o LED azul da placa, no GPIO 2, enquanto a câmera não está online.

## O que você precisa

- Uma placa ESP32 com 4 MB de flash, como um ESP32 DevKit. Não precisa de PSRAM.
- Um cabo USB que transmita dados.
- Uma rede Wi-Fi de 2,4 GHz. O ESP32 não usa 5 GHz.
- Uma câmera IP com ONVIF ligado, na mesma rede.
- O PlatformIO, em um computador com Windows, Linux ou macOS. Para instalar, use a extensão PlatformIO IDE do Visual Studio Code ou o PlatformIO Core.

O e-mail, o horário por NTP e as mensagens com data exigem acesso à internet. O monitoramento, o painel e o vídeo funcionam só na rede local.

## Instalação do firmware principal

1. Baixe o projeto do GitHub e abra a pasta no Visual Studio Code, ou use um terminal dentro da pasta.

2. Copie `include/secrets.example.h` para `include/secrets.h`.

3. Preencha o `include/secrets.h`.

   | Campo | Para que serve |
   | --- | --- |
   | `WIFI_SSID` e `WIFI_PASSWORD` | Nome e senha do Wi-Fi. Pode ficar vazio, porque dá para configurar o Wi-Fi pelo celular depois. |
   | `CAMERA_ONVIF_USERNAME_VALUE` e `CAMERA_ONVIF_PASSWORD_VALUE` | Usuário e senha ONVIF da câmera, usados como padrão para as câmeras novas. Deixe vazio se a câmera não pede senha. |
   | `DASHBOARD_ADMIN_PASSWORD_VALUE` | Senha pedida no painel para salvar configurações e executar ações. Recomendamos definir uma. |
   | `RESEND_API_KEY`, `RESEND_FROM` e `ALERT_EMAIL` | Chave do Resend, remetente e destinatário dos alertas. A chave começa com `re_`. Sem ela, o painel funciona e só os e-mails ficam desligados. |

4. No `platformio.ini`, troque `upload_port = COM5` pela porta do seu ESP32, ou apague a linha para o PlatformIO detectar a porta.

5. Conecte o ESP32 por USB e grave o firmware.

   ```
   pio run -t upload
   ```

   No Visual Studio Code, o mesmo comando é o botão Upload da barra inferior do PlatformIO. Se a gravação falhar, segure o botão BOOT da placa enquanto o PlatformIO conecta e solte quando a gravação começar.

6. Abra o monitor serial para ver o endereço do painel.

   ```
   pio device monitor
   ```

   O ESP32 imprime o IP e o endereço `http://camera-watchdog.local/` assim que conecta ao Wi-Fi.

Para atualizar o firmware depois, rode `pio run -t upload` de novo. O script `scripts/preserve_nvs_upload.py` impede que a gravação apague as configurações salvas.

## Configurar o Wi-Fi pelo celular

Se o `secrets.h` estiver sem Wi-Fi, ou se a rede não responder por 2 minutos, o ESP32 abre a rede `Camera-Watchdog-XXXX`, sem senha. Os quatro caracteres finais mudam de placa para placa.

1. Conecte o celular a essa rede. Na maioria dos aparelhos a página de configuração abre sozinha. Se não abrir, acesse `http://192.168.4.1/`.
2. Escolha a sua rede na lista, ordenada por intensidade de sinal. Para uma rede oculta, toque em Digitar o nome da rede.
3. Digite a senha e toque em Testar e conectar.
4. O ESP32 testa a senha, o endereço IP e a qualidade do sinal. Só se o teste passar ele grava a rede.
5. Volte o celular para o seu Wi-Fi normal e abra `http://camera-watchdog.local/`.

A rede salva fica na memória do ESP32 e sobrevive a reinícios e a quedas de energia. Ela tem prioridade sobre a rede do `secrets.h`, que continua como reserva. Com o ESP32 já conectado, a mesma página abre em `http://camera-watchdog.local/wifi`, ou em Configurações, Global, Escolher outra rede Wi-Fi.

Para proteger a rede de configuração com senha, defina `WIFI_SETUP_AP_PASSWORD` em `include/app_config.h` com 8 a 63 caracteres. Sem senha, os celulares mostram o aviso de entrar na rede automaticamente, e a senha administrativa do painel continua exigida para gravar.

## Configurar o painel

Abra `http://camera-watchdog.local/`, ou o IP mostrado no monitor serial. A aba Visão geral lista todas as câmeras. Se você não vir nenhuma, aguarde a descoberta, que roda a cada 30 segundos por padrão, ou abra Ações e use Redescobrir câmeras.

Se a câmera não aparecer, confira se ela e o ESP32 estão na mesma sub-rede e se o ONVIF está ligado no aplicativo da câmera. Como alternativa, informe o IP em `CAMERA_IP_HINT`, no arquivo `include/app_config.h`, e grave o firmware de novo.

As abas do painel.

| Aba | O que mostra |
| --- | --- |
| Visão geral | Estado de todas as câmeras. |
| Câmera | Resultado dos testes de ONVIF, RTSP e imagem da câmera selecionada. |
| Ao vivo | Vídeo H.264 da câmera selecionada e as posições PTZ. |
| Prévia | Imagens JPEG, usadas para diagnóstico. |
| Ações | Verificar agora, reiniciar a câmera, redescobrir câmeras, zerar logs e zerar os dados do ESP32. |
| Recursos | Memória, flash e sinal Wi-Fi do ESP32. |
| Configurações | Todos os ajustes, em seções. |

As seções de Configurações.

- Global. Nome local do ESP32, tempos do Wi-Fi e da descoberta, e o link para trocar de rede Wi-Fi.
- Identificação. Nome da câmera, perfil ONVIF preferido para a imagem e porta RTSP.
- Autenticação da câmera. Usuário e senha da câmera, usados pelo watchdog. Não alteram a conta dentro da câmera. A senha salva nunca volta para o navegador.
- Monitoramento. Intervalo entre verificações e quantas falhas levam a cada estado.
- Recuperação. Reinício automático por ONVIF, tempo de espera, intervalo mínimo e limite por hora.
- Rede e notificações. Liga os alertas por e-mail e o aviso de troca de IP.
- Comunicação. Tempos limite de conexão e de resposta.
- Diagnóstico. O estado completo em JSON, com botão para copiar.
- Logs. O histórico persistente, com filtros e exportação em CSV.

Para começar, siga esta ordem.

1. Em Configurações, Autenticação da câmera, informe o usuário e a senha da câmera, se ela pedir.
2. Em Configurações, Rede e notificações, ligue os alertas por e-mail.
3. Em Ações, rode Verificar agora e confira a aba Câmera.

Os valores padrão são verificação a cada 15 segundos, tolerância de 90 segundos depois de ligar, três falhas para declarar a câmera offline e no máximo três reinícios por hora, com 5 minutos entre eles.

### Vídeo ao vivo

A aba Ao vivo usa o fluxo H.264 da câmera por RTSP. O ESP32 repassa os pacotes ao navegador pela porta 81 sem decodificar nada, e o navegador toca o vídeo.

- A qualidade Padrão usa o menor perfil H.264 da câmera com pelo menos 640 por 360 pixels. A qualidade Alta resolução usa o maior perfil e pode travar, porque a câmera envia esse fluxo em rajadas.
- O ESP32 abre uma sessão por vez, então dois espectadores só assistem à mesma câmera na mesma qualidade.
- Câmeras que enviam só H.265 não funcionam nessa aba.
- O vídeo para sozinho quando você sai da aba, e a câmera fica livre.

### Segurança

O painel usa HTTP, sem criptografia, e foi feito para a rede local. Não abra a porta 80 nem a 81 do roteador para a internet. Defina `DASHBOARD_ADMIN_PASSWORD_VALUE` para que qualquer pessoa na sua rede não consiga mudar ajustes nem reiniciar câmeras.

## Firmwares de apoio

A pasta `firmwares/` tem três firmwares que ajudam a testar a placa, limpar a memória e recuperar o histórico. Os instaladores são scripts do Windows, e cada pasta tem um `instalar.bat`.

| Pasta | O que faz |
| --- | --- |
| `minimal` | Apaga toda a flash e grava um firmware de teste, com uma página que mostra os dados da placa. |
| `zeroed` | Apaga toda a flash e não grava nada. |
| `log-reader` | Salva uma cópia dos logs, apaga o resto e grava um leitor de logs. |

### Antes de usar os instaladores

- Instale o PlatformIO Core. O instalador procura o `platformio.exe` em `%USERPROFILE%\.platformio\penv\Scripts`, depois no PATH. Para usar outro caminho, defina a variável de ambiente `ESP32_PLATFORMIO_EXE`.
- Crie o `include/secrets.h` como descrito acima. Os firmwares `minimal` e `log-reader` usam o Wi-Fi desse arquivo.
- Descubra a porta COM do ESP32 no Gerenciador de Dispositivos do Windows. Pode ser preciso instalar o driver USB da placa, como CP210x ou CH340.

Para executar, dê dois cliques no `instalar.bat` da pasta, informe a porta, como `COM3`, e confirme com S. Responder N cancela sem alterar a placa.

Os modos `minimal` e `zeroed` apagam tudo, inclusive o firmware principal, as configurações, a rede Wi-Fi salva e os logs. Use esses dois só quando quiser mesmo limpar a placa. Para recuperar os logs antes de limpar, use o `log-reader`.

### minimal

Compila e grava um firmware de diagnóstico. Ele conecta ao Wi-Fi do `secrets.h` e mostra a página `http://esp32-recovery.local/` com modelo do chip, memória, tamanho da flash, motivo do último reset, tempo ligado, endereço IP, sinal e MAC. A página se atualiza a cada 3 segundos, e o LED azul pisca a cada meio segundo. Serve para confirmar que a placa, a antena e o Wi-Fi funcionam.

### zeroed

Apaga a flash inteira, incluindo bootloader, configurações, NVS e logs. A placa fica sem firmware até você gravar outro. Serve para devolver uma placa ao estado de fábrica antes de reutilizá-la.

### log-reader

Recupera o histórico de logs de um watchdog sem perder nenhum registro. O instalador faz estas etapas.

1. Compila o leitor.
2. Salva uma cópia integral de 960 KiB da partição de logs na pasta `firmwares/log-reader/backups/` e confere o tamanho e o SHA-256.
3. Apaga tudo, menos a partição de logs.
4. Confere que a partição de logs continua idêntica ao backup.
5. Grava o leitor e confere os logs outra vez.

O processo para antes de apagar qualquer coisa se não conseguir confirmar o backup, o tamanho da flash ou a identidade da placa. A pasta `backups` não vai para o Git e pode conter nomes e endereços da sua rede, então trate-a como material sensível.

Depois da instalação, abra o leitor por um destes caminhos.

- Pelo Wi-Fi do `secrets.h`, em `http://esp32-log-reader.local/`.
- Pela rede `ESP32-LogReader-XXXXXX`, com a senha `Logs-XXXXXX`, onde os seis caracteres finais são os mesmos nos dois nomes. O endereço é `http://192.168.4.1/`.

O site filtra por nível, categoria, evento, resultado, Boot ID e Context ID. Ele exporta os registros válidos em CSV e JSONL, e exporta a partição inteira em RAW, mesmo se o sistema de arquivos estiver corrompido. Para voltar ao monitoramento, grave o firmware principal com `pio run -t upload` na pasta raiz.

## Solução de problemas

- O endereço `.local` não abre. Use o IP que o monitor serial mostra. No Windows, o endereço `.local` depende do serviço mDNS, que pode estar bloqueado por alguma VPN ou firewall.
- O ESP32 não conecta ao Wi-Fi. Confirme que a rede é de 2,4 GHz e que a senha está certa. Depois de 2 minutos a rede `Camera-Watchdog-XXXX` abre para você corrigir.
- A câmera não é encontrada. Veja a seção sobre configurar o painel, e confira se a câmera aceita ONVIF sem bloqueio de IP.
- O vídeo ao vivo engasga. Troque a qualidade para Padrão e feche outras abas que estejam assistindo à câmera.
- A gravação falha. Troque o cabo USB, confira a porta em `upload_port` e segure o botão BOOT durante a conexão.
- Esqueci a senha administrativa. Troque `DASHBOARD_ADMIN_PASSWORD_VALUE` no `secrets.h` e grave o firmware de novo.

## Licença

O projeto usa a licença MIT. O texto completo está no arquivo `LICENSE`.
