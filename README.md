# Sidequest

Estudo durante partidas de Valorant. Quando você morre, uma pergunta toma a tela (com uma prévia ao vivo do jogo no canto);
na fase de compra aparece só uma pílula ou pergunta rápida num canto. Erros voltam na mesma sessão e alimentam uma
repetição espaçada (Leitner) entre sessões.

Nativo (C++20, Win32, Direct2D/DirectWrite sobre DirectComposition). Nunca toca no processo do jogo: só janelas, DWM e,
na Etapa 2, a API local da Riot em leitura.

## Compilar

```powershell
.\build.ps1 -Test        # configura (Ninja + MSVC), compila e roda os testes
.\build\sidequest.exe    # lê config.json da pasta atual (ou o caminho passado como argumento)
```

Copie `config.example.json` para `config.json` e ajuste. Para testar sem o jogo, aponte `janela_alvo` para qualquer
janela (por título; a classe pode ficar vazia).

## Teclas (configuráveis)

| Tecla | Ação |
| --- | --- |
| (opcional) `morri`, `revivi`, `round_comecou`, `fim_de_partida` | simulação sem o jogo: ficam desligadas se não estiverem no config |
| 1–6, Enter | marcar / confirmar (hotkeys globais, só registradas com a tela aberta) |
| F5 | espiar o jogo por alguns segundos |
| F6 | agora não (fecha sem penalidade) |

Lacuna: Enter para digitar (único caso em que o app pega o foco), Enter confirma, e o foco volta ao jogo.
Saída: ícone da bandeja → Sair... (dupla confirmação; Alt+F4 é ignorado). Nada reabre o app sozinho.

## Estrutura

- `src/core` — sem Win32, testável: banco de questões, estado, Leitner, fila da sessão, comparação de lacunas.
- `src/platform` — hotkeys, localizar janela, DWM thumbnail, volume por aplicativo (WASAPI).
- `src/ui` — Direct2D/DirectWrite/DirectComposition: o cartão é desenhado uma vez; fade, zoom e barra de tempo rodam no compositor.
- `src/app` — máquina de estados (ocioso → morto → compra → relatório).
- `tools/wgc_probe.cpp` — teste de risco do Windows Graphics Capture (Etapa 4).
