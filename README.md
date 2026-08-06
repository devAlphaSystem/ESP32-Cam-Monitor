# ESP32 Camera Watchdog 0.5.0 — revisão da interface

Esta revisão mantém o firmware, o monitoramento, a recuperação automática, a API e o formato das configurações da versão 0.5.0. As alterações foram concentradas na organização visual do painel.

## Alterações desta revisão

O cabeçalho agora possui cinco páginas:

```text
Câmera | Prévia | Recursos | Configurações | Ações
```

Na página **Configurações**, os antigos cards simultâneos foram transformados em categorias internas:

```text
Identificação | Monitoramento | Recuperação | Rede e notificações | Comunicação | Alerta local
```

Cada categoria mostra somente um card por vez. Dentro dele, cada campo ocupa uma linha própria, evitando duas entradas lado a lado.

As **Ações administrativas** foram removidas da página Configurações e passaram a ocupar a página independente **Ações**, acessível pelo cabeçalho.

Também foram incluídos:

- navegação das categorias por clique e pelas teclas de seta, Home e End;
- rolagem horizontal das abas em telas estreitas;
- abertura automática da categoria que contém um campo inválido ao tentar salvar;
- compartilhamento temporário da senha administrativa entre Configurações e Ações durante a sessão;
- layout responsivo sem criar rolagem horizontal na página.

## O que não mudou

Esta revisão não altera:

- os valores padrão ou limites definidos em `app_config.h`;
- o esquema persistido na NVS;
- os endpoints HTTP;
- a lógica de monitoramento ONVIF, RTSP e JPEG;
- o `SystemReboot` automático;
- os limites de cooldown e tentativas;
- as credenciais e o formato de `secrets.h`;
- a versão informada pelo firmware, que continua sendo `0.5.0`.

## Atualização mínima

Para aplicar somente esta revisão visual em um projeto 0.5.0 já funcional, substitua apenas:

```text
include/dashboard_page.h
```

O ZIP também contém os demais arquivos da versão 0.5.0 para permitir reconstruir o projeto completo.

## Estrutura do pacote

```text
esp32-camera-watchdog-v0.5.0-settings-tabs/
├── src/
│   └── main.cpp
├── include/
│   ├── app_config.h
│   ├── dashboard_page.h
│   └── secrets.example.h
├── partitions.csv
├── platformio.ini
├── README.md
├── CHANGELOG.md
└── SHA256SUMS.txt
```

## Arquivo que deve ser preservado

Não substitua o seu arquivo real:

```text
include/secrets.h
```

Ele contém Wi-Fi, credenciais ONVIF, senha administrativa opcional e configurações do Resend. O pacote contém somente `secrets.example.h`.

## Compilar e enviar

No terminal do projeto:

```bash
pio run
pio run --target upload
pio device monitor
```

Também é possível usar os comandos **PlatformIO: Build**, **Upload** e **Monitor** no VS Code.

## Validações executadas

Antes do empacotamento foram executados:

- verificação sintática do JavaScript com Node.js;
- validação dos IDs, labels, referências ARIA e mapas de campos;
- teste funcional das páginas Configurações e Ações em navegador automatizado;
- teste da troca entre categorias e da abertura do painel com campo inválido;
- teste dos endpoints simulados de leitura e ação administrativa;
- teste responsivo em 390 px e em resolução desktop;
- verificação lexical C++11 do `dashboard_page.h` em `PROGMEM`;
- conferência de integridade do ZIP e hashes SHA-256.

A compilação final pelo PlatformIO deve ser executada no projeto local, pois depende do toolchain e das bibliotecas instaladas no computador.
