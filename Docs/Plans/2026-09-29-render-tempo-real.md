# Plano — o render deixa de ser a distância que se sente

*29/09/2026. Continua a T7 do plano de paridade, que levou o documento mais
pesado de 2,42 s para 1,68 s a 1024 px. Agora o objetivo é a sensação de
editar, e não mais o número de um render isolado.*

## Onde estamos (medido 29/09, Release, uma execução por número)

| documento | 256 px | 512 px | 1024 px |
|---|---|---|---|
| Jellify (1 camada) | 0,05 s | 0,09 s | 0,32 s |
| Apollo-Reborn (10 camadas de vidro, o mais pesado do corpus) | 0,22 s | 0,50 s | 1,76 s |

A decomposição do laudo de 15/09 (§1.1), refeita no Apollo a 1024 px: o que
**não é vidro** custa 0,72 s (42 %), a sombra 0,56 s (33 %), o campo 0,31 s
(18 %), o especular 0,09 s e a translucidez 0,03 s. O campo deixou de ser o
gargalo. A parte que nunca foi otimizada passou a ser a maior.

Dois fatos definem a estratégia:

1. **A UI refaz tudo a cada edição.** `renderIcon` não guarda nada de um
   quadro para o outro: relê e reinterpreta o SVG de cada camada, refaz a
   cobertura, o campo e a sombra. Mexer na opacidade de um grupo custa o mesmo
   que abrir o documento.
2. **Os 0,72 s de base nunca foram abertos.** A cobertura vai para a GPU e
   volta para a CPU a cada camada. Quanto disso é esse vaivém, ninguém mediu.

## Restrições globais

1. **Saída idêntica byte a byte**, como na T7: SHA-256 do PNG antes e depois,
   mais o gate de tolerância zero do viewport. É proibido trocar `double` por
   `float`, mexer no `superSample`, aproximar ou linkar Onyx em RenderBox.
   Passar a cadeia para a GPU fica fora desta frente. Só se discute depois, com
   os números desta frente, e exigiria trocar a igualdade exata por uma
   tolerância medida.
2. **Mede-se antes de otimizar**, e a medição é datada. O laudo de 15/09
   despachou uma task contra um número vencido.
3. **Detecção medida** para todo cache: um caso que reprova quando a chave do
   cache é mutilada (por exemplo, esquecer um campo), executado, não suposto.
4. A suíte inteira roda verde ao fim de cada task, com `IC_CORPUS_DIR`
   apontado para `References/corpus` e `ic_tests` construído pelo
   `--target`.

## Tasks

### R1 — Laudo de perfil *(só laudo, nenhum código commitado)*

Tempo por função dos 0,72 s de base e dos 0,56 s da sombra no Apollo a 1024
px, incluindo o custo de submeter e ler de volta a cobertura da GPU. A
instrumentação vive numa árvore descartável e não entra no repo.

Saída: `Docs/Laudos/2026-09-29-perfil-do-render.md`.

### R2 — Prévia progressiva

O canvas pede primeiro um render pequeno e troca pelo cheio quando ele fica
pronto. A imagem final não muda. O que o R1 medir decide o tamanho da prévia.

### R3 — Cache entre quadros

Guarda o que cada grupo produz, com a chave sendo o que entrou no cálculo.
Numa edição, só o grupo que mudou é refeito. O R1 decide em que camada
cachear (SVG interpretado, cobertura, campo, sombra).

*Fechada.* Quatro etapas, com a chave sendo o hash de todas as entradas. Uma
edição no Apollo a 512 px caiu de 0,40 s para 0,09–0,18 s, e a 1024 px de
1,35 s para 0,39–0,65 s. O gate pega 10 de 13 mutilações da chave, e as três
que não pega são redundantes (laudo de perfil §6).

### R4 — Os pontos quentes que o R1 apontar

Definida pelo laudo.

*R4a fechada* (`c38a469`): o `readBack` caiu de 0,39 s para 0,05 s, com staging
em memória com cache e half->float por F16C.
