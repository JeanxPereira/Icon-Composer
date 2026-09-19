#pragma once
// A BARRA DE RENDITIONS -- o `RenditionBar`/`AppIconGrid` do alvo, e o que
// neste editor mais faltava para ele se parecer com o original.
//
// A FORMA É MEDIDA, NÃO INVENTADA
// -----------------------------------------------------------------------------
// Tudo nesta barra sai de `Docs/Laudos/2026-09-19-renditions-e-mirroring.md` §2:
//
//   * `[BIN]` são SEIS renditions, `IconComposerFoundation.Rendition`, na ordem
//     de tag `lightColor(0) darkColor(1) lightTint(2) darkTint(3) lightClear(4)
//     darkClear(5)` -- que é exatamente `rb::Rendition` (FillResolve.h), então
//     este arquivo NÃO declara um sétimo enum e usa aquele.
//   * `[BIN]` `Rendition.displayGrouped` (`0x4202C`) monta três arrays de dois:
//     `[0,1]`, `[4,5]`, `[2,3]`. Isto é, na tela:
//         [ Default, Dark ] [ Clear Light, Clear Dark ] [ Tinted Light, Tinted Dark ]
//     **Clear vem ANTES de Tinted.** Não é a ordem do enum, e é a ordem da tela.
//   * `[BIN]` `Platform.validRenditions` (`0x3A248`): `Default` vale para toda
//     plataforma, as outras cinco só para plataforma <= 1 -- **watchOS oferece
//     só `Default`**. A grade não é 3x6.
//   * `[BIN]` `Rendition.sourceAppearance` (`0x415FC`), a tabela de bytes
//     `01 02 03 03 03 03`: as QUATRO tingidas leem a MESMA fatia `tinted` do
//     documento. É `rb::sourceAppearance`, que já está medido e testado.
//   * `[BIN]` `Appearance.defaultRendition` (`0x20D98`), a tabela
//     `00 00 01 04`: base->Default, light->Default, dark->Dark,
//     tinted->**Clear Light**.
//
// AS DUAS TABELAS NÃO SÃO INVERSAS, E ISSO É DO ALVO
// -----------------------------------------------------------------------------
// `sourceAppearance(Default) == light`, mas `defaultRendition(base)` e
// `defaultRendition(light)` são as DUAS `Default`. Então ir da barra para o
// combo e voltar não é identidade quando o combo está em `base` -- e essa
// assimetria é dos dois bytes lidos, não uma escolha deste arquivo.
//
// O QUE ESTE RENDERIZADOR SABE DESENHAR, E POR QUE ISSO DESABILITA ITENS
// -----------------------------------------------------------------------------
// Uma opção que entrega a imagem da vizinha é uma mentira na tela (a mesma
// regra que `Export.h` já aplica). Duas medições dizem quais renditions este
// motor sabe produzir, e as duas estão no laudo:
//
//  1. **Clear não é uma aparência, é o terceiro modo de render** (§2.5,
//     `RenderingMode.Contents = {color, tinted, clear}`). Os 15 campos de
//     `ICRRenderingParameters.ClearMode` NÃO foram lidos (§6), e o laudo diz
//     com todas as letras que isso "não autoriza implementar o modo Clear".
//     Este motor tem dois modos, não três.
//  2. **O eixo claro/escuro DO RENDER não existe aqui.** No alvo,
//     `ICRIconStyle.appearance` é `{light, dark}` e é OUTRO eixo, ortogonal ao
//     `appearance` do documento (§2.5). Neste editor o único eixo de aparência
//     que chega ao motor é o do documento, `icf::Context::appearance` -- uma
//     varredura de `Source/RenderBox/` não acha nenhum outro. Logo duas
//     renditions que caem no MESMO `icf::Context` são, aqui, o MESMO render.
//
// `renditionSupported` é a conjunção das duas, e é DERIVADA em vez de ser uma
// lista escrita à mão: uma rendition é oferecida quando não é Clear e quando
// nenhuma rendition ANTERIOR na ordem medida cai no mesmo `icf::Context`. O que
// sobra hoje é `Default`, `Dark` e `Tinted Light`; o par Clear e o `Tinted Dark`
// são desenhados CINZAS, no lugar medido deles, com o motivo no tooltip.
//
// `[DEC]` O dia em que alguém ler `ClearMode` e o dia em que o motor ganhar o
// eixo claro/escuro do render, esta função passa a devolver `true` sozinha para
// os itens correspondentes -- nenhuma lista precisa ser reescrita.
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/RenderBox/FillResolve.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ick {

// ─── O VOCABULÁRIO, TODO MEDIDO ──────────────────────────────────────────────

// `[BIN]` As seis, em ordem de tag (`Rendition.displayOrder` é `and x0, x0,
// #0xff ; ret` -- a ordem de exibição É o tag; a ordem da BARRA é outra, e é
// `renditionGroups`).
inline constexpr rb::Rendition kAllRenditions[6] = {
    rb::Rendition::LightColor, rb::Rendition::DarkColor,  rb::Rendition::LightTint,
    rb::Rendition::DarkTint,   rb::Rendition::LightClear, rb::Rendition::DarkClear,
};

// `[BIN]` `Rendition.displayName`, getter `0x41F5C`: o que a pessoa lê.
const char* renditionDisplayName(rb::Rendition r);
// `[BIN]` `Rendition.fileNameComponent`, getter `0x421BC`: o que o `ictool`
// aceita em `--rendition`, e a "tabela compacta de nomes" do Kit em `0x185438`.
const char* renditionFileNameComponent(rb::Rendition r);
// `[BIN]` As grafias de `Rendition.SerializedRepresentation`, `__cstring`
// `0x12a6e0`-`0x12a718`.
const char* renditionSerializedName(rb::Rendition r);

// `[BIN]` `Rendition.isTintedOrClear` (`0x362F8`) é `tag > 1`; o par Clear são
// os tags 4 e 5.
bool renditionIsClear(rb::Rendition r);

// `[BIN]` `Platform.validRenditions` (`0x3A248`): watchOS só tem `Default`.
//
// `[INF]` A PONTE ENTRE `Platform` E `icf::Idiom`, dita em vez de escondida. O
// alvo tem `Platform = iOS(0), macOS(1), watchOS(2)`; nós temos
// `icf::Idiom = Base, Square, IOS, MacOS, WatchOS`, que tem um caso a mais
// (`Square`, a família não estreitada) e um fallback (`Base`). A única metade
// que a medição decide é a de watchOS, e essa é exata. Os outros quatro idiomas
// caem no ramo "plataforma <= 1", isto é, todas as seis -- o que é leitura, não
// medição, porque `Square` e `Base` não existem do outro lado.
bool renditionValidFor(rb::Rendition r, icf::Idiom idiom);

// `[BIN]` `Appearance.defaultRendition` (`0x20D98`), a tabela `00 00 01 04`.
// PURA: devolve `Clear Light` para `tinted` mesmo que este motor não saiba
// desenhar Clear. Quem quer a que a barra pode MOSTRAR usa
// `renditionForAppearance`.
rb::Rendition defaultRenditionFor(icf::Appearance a);

// O `icf::Context` que este editor renderiza para uma rendition: a fatia do
// documento (`rb::sourceAppearance`, medida) no idioma que o canvas está
// mostrando. É a chave por onde as miniaturas são compartilhadas, e é por isso
// que as quatro tingidas caem numa só.
icf::Context renditionContext(rb::Rendition r, icf::Idiom idiom);

// Este motor sabe produzir uma imagem que SÓ esta rendition produz. Ver o
// cabeçalho: não-Clear, e nenhuma anterior na ordem medida com o mesmo
// contexto.
bool renditionSupported(rb::Rendition r);
// Por que não, numa frase, para o tooltip. Vazia quando é suportada.
std::string renditionUnsupportedReason(rb::Rendition r);

// A rendition que a barra marca para uma aparência do documento: a medida
// (`defaultRenditionFor`), e quando essa não é desenhável aqui, a primeira
// SUPORTADA que lê a mesma fatia. Para `tinted` isso troca o `Clear Light`
// medido por `Tinted Light` -- a única substituição por este motivo, e ela some
// no dia em que `ClearMode` for lido.
//
// O IDIOMA ENTRA AQUI, E ENTROU POR UM DEFEITO. Sem ele, em watchOS com
// aparência `dark` a resposta era `Dark`, que `renditionValidFor` exclui dessa
// plataforma: a barra desenhava UM item e não marcava NENHUM, quebrando o
// invariante que `test_e2e_renditions.cpp` escreve ("exatamente uma marcada:
// nenhuma deixaria a pessoa sem saber o que está na tela grande"). A resposta
// agora é sempre uma rendition que a plataforma TEM -- em watchOS, a única que
// ela tem. O preço disso está dito em `drawRenditions`: a fatia que o canvas
// mostra pode não ser a que a rendition marcada nomeia, e a barra diz isso na
// nota em vez de mostrar a imagem da vizinha.
rb::Rendition renditionForAppearance(icf::Appearance a, icf::Idiom idiom);

// A fatia do documento que um `appearance` do canvas lê. `[BIN]` `0x10AEF4`
// (FillResolve.cpp) põe `base` e `light` no MESMO braço, então as duas são uma
// chave só -- é o que faz a miniatura da rendition marcada e o render do canvas
// serem o mesmo pedido num documento em `base`.
icf::Appearance canvasSliceOf(icf::Appearance a);

// `[BIN]` `Rendition.displayGrouped` (`0x4202C`): três pares, `[0,1] [4,5]
// [2,3]`, filtrados por `renditionValidFor`. Em watchOS sobra um grupo de um.
struct RenditionGroup {
    rb::Rendition items[2]{};
    std::size_t count = 0;
};
std::vector<RenditionGroup> renditionGroups(icf::Idiom idiom);

// ─── DUAS FILAS NUM AGENDADOR SÓ ─────────────────────────────────────────────
//
// `JobScheduler` (OnyxPorts.h) é UMA raia e UM dispositivo: dois deles no mesmo
// `JobQueue` renderizariam em paralelo no mesmo `rb::Device`. E o canal do
// `RenderScheduler` é de um resultado por vez, então dois consumidores lendo o
// mesmo `poll()` roubariam o resultado um do outro -- o coordenador do canvas
// descarta o que não casa com a chave dele (RenderCoordinator.cpp), e a
// miniatura sumiria para sempre sem nada na tela dizendo por quê.
//
// Então o agendador real ganha um multiplexador: duas `RenderScheduler`
// virtuais, cada uma com o contrato de sempre (um pedido pendente, o mais novo
// descarta o anterior), e UM pedido de verdade em voo por vez. O multiplexador
// lembra de quem era o que está em voo e entrega o resultado só a ele.
//
// A PRIORIDADE É DO CANVAS, e não é cosmética: é o que a pessoa está olhando.
// Com as duas filas cheias, a do canvas sai primeiro. A miniatura espera, e o
// relógio dela conta essa espera (`RenditionThumb::pendingSeconds`), que é o
// que a barra mostra.
//
// O QUE A PRIORIDADE **NÃO** É, dito em vez de escondido: ela não interrompe um
// render em voo -- não há como, o `JobQueue` não cancela. Com a raia livre,
// quem pedir primeiro sai primeiro, e é por isso que `Source/app/Window.cpp`
// registra o painel do canvas ANTES do da barra. O pior caso que sobra é o
// canvas esperar UMA miniatura, que é um render de 128 px -- uma fração do de
// 512 que ele mesmo pede.
//
// ─── E UM TERCEIRO CONSUMIDOR, QUE NÃO É UMA RAIA: A EXPORTAÇÃO ──────────────
//
// `State::drainExport` (Source/app/Window.cpp) renderiza NA THREAD DO FRAME,
// chamando `rb::renderIcon` direto. Ela não pode virar uma raia deste
// multiplexador por uma razão escrita: o PNG sai dos FLOATS do render
// (`Export.h` -- "os floats do render vão DIRETO para `encodePng`"), e
// `RenderResult` (Ports.h) só carrega 8 bits. Passar por `RenderResult` daria
// hoje os mesmos bytes e amanhã uma igualdade que depende de coincidência.
//
// O DEFEITO QUE ISTO CONSERTA (revisão de 19/09, C1). `JobScheduler` roda
// `renderNow(device_, ...)` numa thread do `JobQueue`, sobre o MESMO
// `rb::Device` que a exportação usa. `rb::Device` tem UM `VkCommandPool` e UMA
// `VkQueue` e nenhum mutex, e os dois são objetos de sincronização EXTERNA pela
// especificação Vulkan: dois usos concorrentes são comportamento indefinido.
// Clicar Export com uma miniatura em voo alcançava exatamente isso. O
// comentário que estava ao lado de `State::device` afirmava o contrário -- "o
// `JobQueue` serializa a raia, então dois renders nunca dividem o dispositivo"
// --, e o `JobQueue` serializa a raia DELE, não a thread do frame.
//
// POR QUE UM ARRENDAMENTO E NÃO UM `std::mutex` EM VOLTA DO DISPOSITIVO. Um
// mutex faria a thread do frame ESPERAR pelo job, que segura o dispositivo por
// segundos -- a janela congelaria sem desenhar, que é a regressão de 15/09 que
// a T2 evitou de propósito ao drenar a fila um item por quadro. `tryLease` não
// espera: quando há render em voo ela devolve false, e o quadro segue
// normalmente (o app diz na barra por que a exportação está parada e tenta de
// novo no quadro seguinte).
//
// O ARRENDAMENTO É EXATO porque o mux é a única porta do dispositivo. Um
// resultado só chega a `real_.poll()` depois de o `Work` do job ter terminado
// (OnyxPorts.cpp: `done_` é escrito no callback `Done`, que roda na thread
// principal, DEPOIS do `Work`), então `inFlight_` falso quer dizer que nenhuma
// thread está no dispositivo. E enquanto o arrendamento está de pé, `pump()`
// não submete: os pedidos das duas raias esperam em `waiting_` e saem no
// `endLease`.
class SharedScheduler {
public:
    explicit SharedScheduler(RenderScheduler& real) : real_(real) {}
    SharedScheduler(const SharedScheduler&) = delete;
    SharedScheduler& operator=(const SharedScheduler&) = delete;

    RenderScheduler& canvasLane() { return canvas_; }
    RenderScheduler& thumbnailLane() { return thumbs_; }

    // "O dispositivo é meu até eu devolver." `false` quer dizer "há um render
    // em voo, tente no próximo quadro" -- nunca bloqueia. Recolhe antes de
    // responder o que já terminou, para não recusar por causa de um resultado
    // que só faltava ser colhido.
    bool tryLease();
    // Devolve o dispositivo e deixa sair o que ficou esperando.
    void endLease();
    bool leased() const { return leased_; }

private:
    // 0 = canvas (prioridade), 1 = miniaturas. Uma classe com função virtual
    // não é agregado, então a raia tem construtor em vez de inicialização por
    // membro.
    struct Lane : RenderScheduler {
        Lane(SharedScheduler* m, int i) : mux(m), id(i) {}
        SharedScheduler* mux = nullptr;
        int id = 0;
        void request(RenderRequest r) override { mux->request(id, std::move(r)); }
        std::optional<RenderResult> poll() override { return mux->poll(id); }
    };
    void request(int lane, RenderRequest r);
    std::optional<RenderResult> poll(int lane);
    // Recolhe de `real_` o que já terminou. Não submete nada -- é a metade que
    // `tryLease` pode rodar sem entregar o dispositivo a uma raia.
    void drain();
    void pump();

    RenderScheduler& real_;
    Lane canvas_{this, 0};
    Lane thumbs_{this, 1};
    std::optional<RenderRequest> waiting_[2];
    std::vector<RenderResult> ready_[2];
    std::optional<int> inFlight_;
    bool leased_ = false;
};

// ─── AS MINIATURAS ───────────────────────────────────────────────────────────
//
// UMA POR CONTEXTO, NÃO UMA POR RENDITION. As quatro tingidas caem no mesmo
// `icf::Context` (§2.6), então uma só entrada as serve -- e, como só uma delas
// é oferecida, na prática a barra pede três. Chavear por contexto é o que faz
// "as tingidas são o mesmo render aqui" ser estrutura em vez de coincidência.
struct RenditionThumb {
    icf::Context context;
    ImTextureID texture = ImTextureID_Invalid;
    std::uint32_t width = 0, height = 0;
    // A versão da Session que estes pixels respondem. Diferente de
    // `Session::version()` quer dizer "envelheceu, vai ser refeita".
    std::uint64_t version = 0;
    bool pending = false;
    // O mesmo contrato de `RenderView`: negativo até o primeiro render
    // terminar, e NÃO é zero. `pendingSeconds` conta a fila também, porque a
    // fila é parte do que a pessoa espera.
    double lastRenderSeconds = -1.0;
    double pendingSeconds = 0.0;
    std::string error;
};

// UM RENDER DE CADA VEZ, SOB DEMANDA. Nunca há dois pedidos de miniatura em
// voo: um documento pesado custa ~0,5 s a 512 px, e quatro de uma vez seriam
// dois segundos de janela parada -- que é a regressão que esta classe existe
// para não cometer. A barra diz quantas faltam e há quanto tempo a atual está
// rodando.
class RenditionThumbnails {
public:
    // `size` é o lado da miniatura em pixels. 128 e não 512: a barra desenha
    // 92 pt por item (`kTile`, PanelRenditions.cpp), e um render de 128 custa
    // uma fração do de 512.
    RenditionThumbnails(RenderScheduler& scheduler, TextureSink& sink, std::uint32_t size = 128);
    ~RenditionThumbnails();
    RenditionThumbnails(const RenditionThumbnails&) = delete;
    RenditionThumbnails& operator=(const RenditionThumbnails&) = delete;

    // `want` são os contextos que a barra vai desenhar, em ordem de
    // prioridade (o selecionado primeiro). Um contexto que sai da lista devolve
    // a textura dele: trocar de idioma não pode vazar uma textura por troca.
    void tick(Session& s, const std::vector<icf::Context>& want);
    const RenditionThumb* find(icf::Context ctx) const;
    std::uint32_t size() const { return size_; }
    // Quantas das pedidas ainda não respondem a versão atual do documento.
    std::size_t stale() const { return stale_; }

private:
    RenderScheduler& scheduler_;
    TextureSink& sink_;
    std::uint32_t size_;
    std::vector<RenditionThumb> thumbs_;
    // A chave do pedido em voo, do mesmo jeito que `RenderCoordinator::Key`:
    // um resultado só é O resultado quando responde a chave INTEIRA.
    bool inFlight_ = false;
    std::uint64_t flightVersion_ = 0;
    icf::Context flightContext_;
    std::chrono::steady_clock::time_point flightAt_{};
    std::size_t stale_ = 0;
};

// ─── O PAINEL ────────────────────────────────────────────────────────────────

// UM ITEM DA BARRA, COMO ELE FOI PARA A TELA -- e ONDE, pelo mesmo contrato de
// `RowInfo::rowAt` e `MenuItemInfo::at`: entre o pixel e a troca de contexto
// existem o retângulo do botão, o `BeginDisabled` em volta e a ordem em que
// tudo é submetido, e um teste que escrevesse `s.view.context.appearance` na
// mão provaria a Session e passaria com o botão desligado.
struct RenditionInfo {
    rb::Rendition rendition = rb::Rendition::LightColor;
    std::string label;        // `renditionDisplayName`
    bool enabled = true;      // false: cinza, com o motivo no tooltip
    bool selected = false;    // é a que o contexto do canvas está mostrando
    bool textured = false;    // havia miniatura desenhada neste quadro
    bool pending = false;     // um render desta miniatura está em voo
    // O centro do `RenditionTitleButton` -- o botão do NOME, que é onde se
    // clica. `{0,0}` nunca acontece num item desenhado.
    ImVec2 at{0.0f, 0.0f};
    // O centro da miniatura, que clica a mesma coisa. Gravado porque o alvo
    // deixa a imagem clicável também e um dos dois pode quebrar sozinho.
    ImVec2 imageAt{0.0f, 0.0f};
};

struct RenditionStats {
    std::size_t groups = 0;
    // Quantos itens em cada grupo, na ordem da tela. `{2,2,2}` fora do watchOS,
    // `{1}` nele.
    std::vector<std::size_t> groupSizes;
    // Um por item desenhado, da esquerda para a direita, grupo após grupo.
    std::vector<RenditionInfo> drawn;
    std::size_t disabled = 0;
    // A linha que diz o que está acontecendo -- quantas miniaturas faltam e há
    // quanto tempo a atual está rodando, ou o relógio da última. Vazia nunca:
    // um painel que renderiza e não diz nada é a regressão de 15/09.
    std::string note;
};

// `thumbs` pode ser nulo: sem janela não há `TextureSink`, e a barra ainda
// tem de desenhar (é o que a suíte exercita). Sem ele os itens saem sem
// miniatura, e a nota diz isso.
RenditionStats drawRenditions(Session& s, RenditionThumbnails* thumbs);

}  // namespace ick
