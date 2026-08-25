# Leitor forense de logs

Este firmware substitui o watchdog e preserva somente a particao `logs`. Ele monta o LittleFS como somente leitura, nunca formata a particao e disponibiliza os registros em um site local.

## Instalacao

Execute `instalar.bat`, informe a porta COM e confirme a operacao. O instalador:

1. compila o leitor;
2. salva uma copia RAW de 960 KiB em `backups/`;
3. confere o tamanho e o SHA-256 do backup;
4. apaga apenas `0x000000` ate `0x30FFFF`;
5. verifica que `0x310000` ate `0x3FFFFF` continua identico ao backup;
6. grava o leitor e repete a verificacao.

O processo para antes do apagamento se o backup, o layout, o tamanho da flash ou a identidade do ESP32 nao puderem ser confirmados.

## Acesso

- Rede Wi-Fi configurada: `http://esp32-log-reader.local/`
- Rede alternativa: `ESP32-LogReader-XXXXXX`
- Senha da rede alternativa: `Logs-XXXXXX`, usando o mesmo sufixo de seis caracteres
- Endereco na rede alternativa: `http://192.168.4.1/`

## Leitura e exportacao

O site permite buscar e filtrar por nivel, categoria, evento, resultado, Boot ID e Context ID. Os filtros sao aplicados sobre todos os registros validos antes da paginacao.

- CSV e JSONL exportam todos os registros validos em ordem cronologica, independentemente dos filtros atuais.
- RAW exporta os 960 KiB exatos da particao, incluindo metadados do LittleFS, registros invalidos e dados que o leitor nao conseguiu interpretar.
- Se o LittleFS estiver corrompido, o site continua ativo e mantem a exportacao RAW disponivel.

Os backups podem conter enderecos, nomes e detalhes operacionais. A pasta e ignorada pelo Git e deve ser tratada como material sensivel.
